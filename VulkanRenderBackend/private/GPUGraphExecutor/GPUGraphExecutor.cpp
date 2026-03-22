#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>
#include <GPUGraphExecutor/ShaderBindingHolder.h>
#include <InterfaceTranslator.h>
#include <CommandList_Impl.h>
#include <GPUResources/VKGPUBuffer.h>
#include <GPUResources/VKGPUTexture.h>
#include <VulkanDebug.h>
#include <ShaderStruct/VKShaderStruct.h>

namespace graphics_backend
{
	constexpr int PREPARE_PASS_ID = -1;
	constexpr int INVALID_PASS_ID = -2;

	//if source state and dst state have different queue family and source usage is not DontCare, a release barrier is required
	bool NeedReleaseBarrier(ResourceState const& srcState, ResourceState const& dstState)
	{
		return (srcState.queueFamily != dstState.queueFamily) && (srcState.usage != ResourceUsage::eDontCare);
	}

	ResourceState GetHandleInitializeUsage(BufferHandle const& handle, int currentPassID, PassInfoBase& currentPass)
	{
		switch (handle.GetType())
		{
		case BufferHandle::BufferType::Internal:
		{
			return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
		}
		case BufferHandle::BufferType::External:
		{
			auto buffer = castl::static_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
			if (buffer->GetUsage() == ResourceUsage::eDontCare)
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			else
			{
				return ResourceState(INVALID_PASS_ID, buffer->GetUsage(), buffer->GetQueueFamily());
			}
		}
		default:
			break;
		}
	}

	ResourceState GetHandleInitializeUsage(ImageHandle const& handle, int currentPassID, PassInfoBase& currentPass)
	{
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::Internal:
		{
			return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
		}
		case ImageHandle::ImageType::External:
		{
			auto image = castl::static_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			if (image->GetUsage() == eDontCare)
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			else
			{
				return ResourceState(INVALID_PASS_ID, image->GetUsage(), image->GetQueueFamily());
			}
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			//TODO: 可能会有不丢弃backbuffer内容的需求
			return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
		}
		default:
			break;
		}
	}

	ResourceState MakeNewResourceState(int passID, uint32_t queueFamilyIndex, ResourceUsageFlags usage)
	{
		return ResourceState(passID, usage, queueFamilyIndex);
	}

	void MakeVertexInputDescriptorsNew(
		castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, castl::unordered_map<cacore::NameHash, cacore::HashObj<VertexInputsDescriptor>> const& boundVertexBuffers
		, castl::vector<cacore::NameHash>& inoutBindingNameToIndex
		, castl::vector<VKVertexAttributeBindingData>& outVertexAttributes)
	{
		auto findBoundVertexDescriptorWithSematics = [&](cacore::NameHash const& sematicName
			, uint32_t sematicIndex
			, VertexInputsDescriptor& outVertexInputsDesc
			, VertexAttribute& outAttribute
			, cacore::NameHash& outName)
		{
			for (auto boundPair : boundVertexBuffers)
			{
				auto& attribDesc = boundPair.second;
				for (auto& attribute : attribDesc->attributes)
				{
					if ((attribute.semanticName == sematicName) && (attribute.sematicIndex == sematicIndex))
					{
						outAttribute = attribute;
						outName = boundPair.first;
						outVertexInputsDesc = attribDesc.Get();
						return true;
					}
				}
			}
			return false;
		};
		castl::unordered_map<cacore::NameHash, VKVertexAttributeBindingData> nameToBindings;
		uint32_t bindingIndex = 0;
		for (auto& attributeData : vertexAttributes)
		{
			bool attribBindingFound = false;
			VertexInputsDescriptor foundDesc{};
			VertexAttribute foundAttribute{};
			cacore::NameHash foundName;
			if (findBoundVertexDescriptorWithSematics(attributeData.m_SematicName
				, attributeData.m_SematicIndex
				, foundDesc, foundAttribute, foundName))
			{
				auto found = nameToBindings.find(foundName);
				if (found == nameToBindings.end())
				{
					uint32_t bindingID = nameToBindings.size();
					found = nameToBindings.insert(castl::make_pair(foundName, VKVertexAttributeBindingData{ foundDesc.perInstance, bindingID, foundDesc.stride, {} })).first;
				}
				found->second.attributes.push_back(VkVertexAttribute{ foundAttribute.offset, VertexInputFormatToVkFormat(foundAttribute.format), attributeData.m_Location });
			}
			else
			{
				CA_LOG_ERR_BREAK("Vertex Attribute Not Bound For Sematics:{}[{}]", attributeData.m_SematicName, attributeData.m_SematicIndex);
			}
		}
		for (auto pair : nameToBindings)
		{
			outVertexAttributes.push_back(pair.second);
			inoutBindingNameToIndex.push_back(pair.first);
		}
	}

	ShaderBindingInstance& GPUGraphExecutor::SelectShaderBindingInstance(cacore::HashObj<GPUShaderBindingKey>const& shaderBindingKey)
	{
		CA_ASSERT_BREAK(shaderBindingKey.Valid(), "Invalid Shader Binding Key");
		ShaderBindingInstance* result = m_PrepareShaderBindingConstantsPass.m_ShaderBindingInstances.try_get(shaderBindingKey);
		CA_ASSERT_BREAK(result != nullptr, "Shader Binding Instance Not Found");
		return *result;
	}

	bool GPUGraphExecutor::ValidImageHandle(ImageHandle const& handle)
	{
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::Invalid:
		{
			return false;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return !window->Invalid();
		}
		case ImageHandle::ImageType::External:
		{
			return handle.GetExternalManagedTexture() != nullptr;
		}
		}
		return m_Graph->GetImageManager().GetDescriptorIndex(handle.GetKey()) >= 0;
	}

	void GPUGraphExecutor::PrepareGraph(thread_management::TaskScheduler* taskGraph)
	{
		auto allocResources = taskGraph->NewTaskGraph()
			->Name("Prepare GPU Resource")
			->Func([this](auto scheduler)
				{
					//Alloc Image & Buffer Resources
					scheduler->NewTask()
						->Name("Alloc Buffer Resources")
						->Functor([this]()
							{
								PrepareGraphLocalBufferResources();
							});
					scheduler->NewTask()
						->Name("Alloc Image Resources")
						->Functor([this]()
							{
								PrepareGraphLocalImageResources();
							});

					scheduler->NewTask()
						->Name("Initialize Passes")
						->Functor([this]()
							{
								InitializePasses();
							});
				});

		auto prepareGPUObjects = taskGraph->NewTaskGraph()
			->Name("Prepare GPUObjects")
			->DependsOn(allocResources)
			->Func([this](auto graph)
				{
					auto prepareRasterizePSO = graph->NewTaskGraph()
						->Name("Prepare PSO & FrameBuffer & RenderPass")
						->Func([this](auto rstPSOGraph)
							{
								//Prepare PSO & FrameBuffer & RenderPass
								PrepareFrameBufferAndPSOs(rstPSOGraph);
							});

					auto prepareComputePSO = graph->NewTask()
						->Name("Prepare Compute PSOs")
						->Functor([this]()
							{
								//Prepare PSO For Compute Passes
								PrepareComputePSOs();
							});

		
				});

		auto writeShaderArgs = taskGraph->NewTaskGraph()
			->Name("Write Descriptor Sets")
			->DependsOn(prepareGPUObjects)
			->Func([this](auto graph)
				{
					//Write DescriptorSets
					WriteDescriptorSets(graph);
				});

		auto prepareResourceBarriers = taskGraph->NewTask()
			->Name("Prepare Resource Barriers")
			->DependsOn(writeShaderArgs)
			->Functor([this]()
				{
					//Prepare Resource Barriers
					PrepareResourceBarriers();
				});

		auto recordGraphs = taskGraph->NewTaskGraph()
			->Name("Record Execution Commands")
			->DependsOn(prepareResourceBarriers)
			->Func([this](auto graph)
				{
					//Record CommandBuffers
					RecordGraph(graph);
				});

		auto recordAndSubmit = taskGraph->NewTaskGraph()
			->Name("Submit Commands")
			->DependsOn(recordGraphs)//执行指令录制完毕
			->DependsOn(writeShaderArgs)//着色器参数写入完毕
			//->DependsOn(waitBackbuffers)//backbuffer等待完毕
			->Func([this](auto graph)
				{
					WaitBackbuffers();
					//Scan Command Batches
					ScanCommandBatchs();
					//Submit
					Submit();
					//Sync Final Usages
					SyncExternalResources();
				});
	}

	//初始化Pass数组
	void GPUGraphExecutor::InitializePasses()
	{
		auto& graphStages = m_Graph->GetGraphStages();
		auto& passIndices = m_Graph->GetPassIndices();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& computePasses = m_Graph->GetComputePasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();
		m_Passes.clear();
		m_ComputePasses.clear();
		m_TransferPasses.clear();
		CA_ASSERT(passIndices.size() == graphStages.size(), "Pass Indices Not Equal");
		uint32_t rasterizePassCount = 0;
		uint32_t computePassCount = 0;
		uint32_t transferPassCount = 0;
		for (uint32_t passID = 0; passID < passIndices.size(); ++passID)
		{
			auto stage = graphStages[passID];
			uint32_t realPassIndex = passIndices[passID];
			switch (stage)
			{
			case GPUGraph::EGraphStageType::eRenderPass:
				++rasterizePassCount;
				break;
			case GPUGraph::EGraphStageType::eComputePass:
				++computePassCount;
				break;
			case GPUGraph::EGraphStageType::eTransferPass:
				++transferPassCount;
				break;
			}
		}
		//RenderPasses
		m_Passes.resize(rasterizePassCount);
		//auto& renderPasses = m_Graph->GetRenderPasses();
		for (uint32_t renderPassID = 0; renderPassID < m_Passes.size(); ++renderPassID)
		{
			auto& passInfo = m_Passes[renderPassID];
			auto& renderPass = renderPasses[renderPassID];
			auto& drawCallBatches = renderPass.GetDrawCallBatches();
			passInfo.m_Batches.resize(drawCallBatches.size());
			for (uint32_t batchID = 0; batchID < drawCallBatches.size(); ++batchID)
			{
				auto& batch = drawCallBatches[batchID];
				auto& batchInfo = passInfo.m_Batches[batchID];
				batchInfo.m_DrawCalls.resize(batch.m_DrawCalls.size());
			}
		}
		//ComputePasses
		m_ComputePasses.resize(computePassCount);
		//auto& computePasses = m_Graph->GetComputePasses();
		for (uint32_t computePassID = 0; computePassID < m_ComputePasses.size(); ++computePassID)
		{
			auto& passInfo = m_ComputePasses[computePassID];
			auto& computePass = computePasses[computePassID];
			passInfo.m_DispatchInfos.resize(computePass.dispatchs.size());
		}

		m_TransferPasses.resize(transferPassCount);
	}

	static void ForeachRenderPassShaderStructs(RenderPass const& renderPass, castl::function<void(VKShaderStruct const&)> callback)
	{
		castl::deque<castl::shared_ptr<ShaderStruct>> shaderStructs;
		for (auto shaderStruct : renderPass.GetShaderStructs())
		{
			CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
			shaderStructs.push_back(shaderStruct.second);
		}
		auto& drawcallBatchs = renderPass.GetDrawCallBatches();
		for (auto& batch : drawcallBatchs)
		{
			for (auto shaderStruct : batch.shaderStructs)
			{
				CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
				shaderStructs.push_back(shaderStruct.second);
			}
		}
		while (!shaderStructs.empty())
		{
			auto shaderStruct = shaderStructs.front();
			VKShaderStruct* pStruct = static_cast<VKShaderStruct*>(shaderStruct.get());
			CA_ASSERT_BREAK(pStruct != nullptr, "Shader Struct Is Null, Why!?");
			callback(*pStruct);
			shaderStructs.pop_front();
			for (auto& subArgPairs : pStruct->GetSubStructs())
			{
				for (auto subStruct : subArgPairs.second)
				{
					shaderStructs.push_back(subStruct);
				}
			}
		}
	}

	static void ForeachComputePassShaderStructs(ComputeBatch const& computePass, castl::function<void(VKShaderStruct const&)> callback)
	{
		castl::deque<castl::shared_ptr<VKShaderStruct>> shaderStructs;
		for (auto& shaderStruct : computePass.shaderStructs)
		{
			shaderStructs.push_back(castl::static_pointer_cast<VKShaderStruct>(shaderStruct.second));
		}
		for (auto& dispatch : computePass.dispatchs)
		{
			for (auto& shaderStruct : dispatch.shaderStructs)
			{
				shaderStructs.push_back(castl::static_pointer_cast<VKShaderStruct>(shaderStruct.second));
			}
		}
		while (!shaderStructs.empty())
		{
			auto shaderStruct = shaderStructs.front();
			callback(*shaderStruct);
			shaderStructs.pop_front();
			for (auto& subArgPairs : shaderStruct->GetSubStructs())
			{
				for (auto subStruct : subArgPairs.second)
				{
					shaderStructs.push_back(castl::static_pointer_cast<VKShaderStruct>(subStruct));
				}
			}
		}
	}


	void GPUGraphExecutor::PrepareGraphLocalImageResources()
	{
		auto& imageManager = m_Graph->GetImageManager();
		m_ImageManager.ResetAllocator();
		{
			CPUTIMER_SCOPE("Stat Rasterize Image Resources");
			auto& renderPasses = m_Graph->GetRenderPasses();
			for (auto& renderPass : renderPasses)
			{
				//Rendertargets
				auto& imageHandles = renderPass.GetAttachments();
				for (auto& img : imageHandles)
				{
					if (img.IsIntternal())
					{
						CPUTIMER_SCOPE("Stat Attachment Tmp Image");
						m_ImageManager.AllocResourceIndex(img.GetKey(), imageManager.GetDescriptorIndex(img.GetKey()));
					}
					else if (img.GetType() == ImageHandle::ImageType::Backbuffer)
					{
						CPUTIMER_SCOPE("Stat Attachment FrameBuffer Image");
						castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(img.GetWindowHandle());
						window->WaitCurrentFrameBufferIndex();
						m_WaitingWindows.insert(window);
					}
				}

				//Shader Args
				ForeachRenderPassShaderStructs(renderPass, [&](VKShaderStruct const& shaderStruct)
				{
					for (auto& imagePair : shaderStruct.GetImageHandles())
					{
						auto& imgs = imagePair.second;
						for (auto& img : imgs)
						{
							CPUTIMER_SCOPE("Stat Shader Args Image");
							auto& imgHandle = img.first;
							if (imgHandle.IsIntternal())
							{
								m_ImageManager.AllocResourceIndex(imgHandle.GetKey(), imageManager.GetDescriptorIndex(imgHandle.GetKey()));
							}
							else if (imgHandle.GetType() == ImageHandle::ImageType::Backbuffer)
							{
								castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
								m_WaitingWindows.insert(window);
							}
						}
					}
				});

				m_ImageManager.NextPass();
			}
		}

		{
			CPUTIMER_SCOPE("Stat Compute Image Resources");
			auto& computePasses = m_Graph->GetComputePasses();
			for (auto& computePass : computePasses)
			{
				ForeachComputePassShaderStructs(computePass, [&](VKShaderStruct const& shaderStruct)
				{
					for (auto& imagePair : shaderStruct.GetImageHandles())
					{
						auto& imgs = imagePair.second;
						for (auto& img : imgs)
						{
							auto& imgHandle = img.first;
							if (imgHandle.GetType() == ImageHandle::ImageType::Internal)
							{
								m_ImageManager.AllocPersistantResourceIndex(imgHandle.GetKey(), imageManager.GetDescriptorIndex(imgHandle.GetKey()));
							}
							else if (imgHandle.GetType() == ImageHandle::ImageType::Backbuffer)
							{
								castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
								m_WaitingWindows.insert(window);
							}
						}
					}
				});
			}
		}

		{
			CPUTIMER_SCOPE("Allocate GraphLocal GPU Image Resources");
			m_ImageManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetImageManager());
		}

	}

	void GPUGraphExecutor::PrepareGraphLocalBufferResources()
	{
		auto& bufferManager = m_Graph->GetBufferManager();
		m_BufferManager.ResetAllocator();
		{
			CPUTIMER_SCOPE("Stat Rasterize Buffer Resources");
			auto& renderPasses = m_Graph->GetRenderPasses();
			for (auto& renderPass : renderPasses)
			{

				ForeachRenderPassShaderStructs(renderPass, [&](VKShaderStruct const& shaderStruct)
				{
					for (auto& bufferPair : shaderStruct.GetBufferHandles())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.IsIntternal())
							{
								m_BufferManager.AllocResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
							}
						}
					}
				});

				auto& drawcallBatchs = renderPass.GetDrawCallBatches();
				for (auto& batch : drawcallBatchs)
				{

					for (auto& drawCall : batch.m_DrawCalls)
					{
						//Index Buffer
						if(drawCall.GetDrawInfo().drawIndexed)
						{
							auto indexBufferHandle = drawCall.GetIndexBuffer();
							//indexBufferHandle = indexBufferHandle.Select(batch.m_IndexBufferData);
							CA_ASSERT_BREAK(indexBufferHandle.indexBufferHandle.IsValid(), "Invalid Index Buffer");
							if (indexBufferHandle.indexBufferHandle.IsIntternal())
							{
								m_BufferManager.AllocResourceIndex(indexBufferHandle.indexBufferHandle.GetKey(), bufferManager.GetDescriptorIndex(indexBufferHandle.indexBufferHandle.GetKey()));
							}
						}

						for (auto& vertexBufferPair : drawCall.GetVertexBuffers())
						{
							auto& vertexBuffer = vertexBufferPair.second;
							if (vertexBuffer.IsIntternal())
							{
								m_BufferManager.AllocResourceIndex(vertexBuffer.GetKey(), bufferManager.GetDescriptorIndex(vertexBuffer.GetKey()));
							}
						}
					}
					
				}

				m_BufferManager.NextPass();
			}
		}

		{
			CPUTIMER_SCOPE("Stat Compute Buffer Resources");
			auto& computePasses = m_Graph->GetComputePasses();
			for (auto& computePass : computePasses)
			{
				ForeachComputePassShaderStructs(computePass, [&](VKShaderStruct const& shaderStruct)
				{
					for (auto& bufferPair : shaderStruct.GetBufferHandles())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.IsIntternal())
							{
								m_BufferManager.AllocPersistantResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
							}
						}
					}
				});
			}
		}

		{
			CPUTIMER_SCOPE("Allocate GraphLocal GPU Buffer Resources");
			m_BufferManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetBufferManager());
		}

	}

	void GPUGraphExecutor::WaitBackbuffers()
	{
		CPUTIMER_SCOPE("Wait For Backbuffers");
		if (!m_WaitingWindows.empty())
		{
			castl::vector<vk::Fence> fences;
			fences.reserve(m_WaitingWindows.size());
			for (auto& pWindow : m_WaitingWindows)
			{
				fences.push_back(pWindow->GetSwapchainContext().GetWaitDoneFence());
			}
			VK_RESULT_CHECK(GetDevice().waitForFences(fences, true, castl::numeric_limits<uint64_t>::max()));
			GetDevice().resetFences(fences);
			m_WaitingWindows.clear();
		}
	}


	GPUTextureDescriptor const* GPUGraphExecutor::GetTextureHandleDescriptor(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			return m_Graph->GetImageManager().GetDescriptor(handle.GetKey());
		}
		case ImageHandle::ImageType::External:
		{
			return &handle.GetExternalManagedTexture()->GetDescriptor();
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			return &handle.GetWindowHandle()->GetBackbufferDescriptor();
		}
		}
		return nullptr;
	}

	PassInfoBase* GPUGraphExecutor::GetBasePassInfo(int passID)
	{
		if (passID == PREPARE_PASS_ID)
		{
			return &m_PrepareShaderBindingConstantsPass;
		}
		if (passID >= 0)
		{
			auto& graphStages = m_Graph->GetGraphStages();
			auto& passIndices = m_Graph->GetPassIndices();
			auto stage = graphStages[passID];
			uint32_t realPassIndex = passIndices[passID];
			switch (stage)
			{
			case GPUGraph::EGraphStageType::eRenderPass:
				return &m_Passes[realPassIndex];
			case GPUGraph::EGraphStageType::eComputePass:
				return &m_ComputePasses[realPassIndex];
			case GPUGraph::EGraphStageType::eTransferPass:
				return &m_TransferPasses[realPassIndex];
			}
		}
		return nullptr;
	}

	vk::ImageView GPUGraphExecutor::GetTextureHandleImageView(ImageHandle const& handle, GPUTextureView const& view) const
	{
		auto& imageManager = m_Graph->GetImageManager();
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			auto& image = m_ImageManager.GetImageObject(handle.GetKey());
			auto& desc = *imageManager.GetDescriptor(handle.GetKey());
			return m_FrameBoundResourceManager->resourceObjectManager.EnsureImageView(image.image, desc, view);
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<VKGPUTexture> texture = castl::static_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->EnsureImageView(view);
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->EnsureCurrentFrameImageView(view);
		}
		}
		CA_LOG_ERR("Invalid Image Handle For Getting Image View");
		return {};
	}

	vk::Image GPUGraphExecutor::GetTextureHandleImageObject(ImageHandle const& handle) const
	{
		auto imageType = handle.GetType();
		switch (imageType)
		{
		case ImageHandle::ImageType::Internal:
		{
			return m_ImageManager.GetImageObject(handle.GetKey()).image;
		}
		case ImageHandle::ImageType::External:
		{
			castl::shared_ptr<VKGPUTexture> texture = castl::static_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->GetImage().image;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return window->GetCurrentFrameImage();
		}
		}
		CA_LOG_ERR("Invalid Image Handle For Getting Image");
		return {};
	}

	vk::Buffer GPUGraphExecutor::GetBufferHandleBufferObject(BufferHandle const& handle) const
	{
		auto bufferType = handle.GetType();
		switch (bufferType)
		{
		case BufferHandle::BufferType::Internal:
		{
			return m_BufferManager.GetBufferObject(handle.GetKey()).buffer;
		}
		case BufferHandle::BufferType::External:
		{
			castl::shared_ptr<VKGPUBuffer> buffer = castl::static_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
			return buffer->GetBuffer().buffer;
		}
		}
		return {};
	}

	void GPUGraphExecutor::ScanCommandBatchs()
	{

		for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
		{
			uint32_t queueFamilyIndex = pair.first;
			auto& releaser = pair.second;
			releaser.signalSemaphore = m_FrameBoundResourceManager->semaphorePool.AllocSemaphore();
		}

		auto& graphStages = m_Graph->GetGraphStages();
		castl::unordered_map<int32_t, int32_t> passToBatchID;
		m_CommandBufferBatchList.clear();
		m_CommandBufferBatchList.push_back(CommandBatchRange::Create(GetBasePassInfo(PREPARE_PASS_ID)->m_BarrierCollector.GetQueueFamily(), 0));
		auto lastBatch = &m_CommandBufferBatchList.back();

		auto collectPassCommands = [&](int32_t passID)
		{
			auto pass = GetBasePassInfo(passID);
			if (pass->m_CommandBuffers.empty())
				return;

			uint32_t startCommandID = m_FinalCommandBuffers.size();
			for (vk::CommandBuffer cmd : pass->m_CommandBuffers)
			{
				m_FinalCommandBuffers.push_back(cmd);
			}
			int32_t lastCommandID = (int32_t)m_FinalCommandBuffers.size() - 1;
			uint32_t queueFamilyID = pass->m_BarrierCollector.GetQueueFamily();

			if (lastBatch->queueFamilyIndex != queueFamilyID)
			{
				m_CommandBufferBatchList.push_back(CommandBatchRange::Create(pass->m_BarrierCollector.GetQueueFamily(), startCommandID));
				lastBatch = &m_CommandBufferBatchList.back();
			}
			lastBatch->lastCommand = castl::max(lastBatch->lastCommand, lastCommandID);

			lastBatch->hasSuccessor = lastBatch->hasSuccessor || (pass->m_SuccessorPasses.size() > 0);
			for (uint32_t predPassID : pass->m_PredecessorPasses)
			{
				lastBatch->waitingBatch.insert(passToBatchID[predPassID]);
			}
			for (uint32_t queueReleaserID : pass->m_WaitingQueueFamilies)
			{
				lastBatch->waitingQueueFamilyReleaser.insert(queueReleaserID);
			}
			passToBatchID[passID] = m_CommandBufferBatchList.size() - 1;
		};

		collectPassCommands(PREPARE_PASS_ID);

		for (uint32_t passID = 0; passID < graphStages.size(); ++passID)
		{
			collectPassCommands(passID);
		}

		for (auto& batch : m_CommandBufferBatchList)
		{
			batch.signalSemaphore = m_FrameBoundResourceManager->semaphorePool.AllocSemaphore();
			if (!batch.hasSuccessor)
			{
				m_FrameBoundResourceManager->AddLeafSempahores(batch.signalSemaphore);
			}
			batch.waitSemaphores.reserve(batch.waitingBatch.size());
			for (uint32_t waitingBatchID : batch.waitingBatch)
			{
				batch.waitSemaphores.push_back(m_CommandBufferBatchList[waitingBatchID].signalSemaphore);
				batch.waitStages.push_back(vk::PipelineStageFlagBits::eAllCommands);
			}
			for (uint32_t queueFamilyReleaserID : batch.waitingQueueFamilyReleaser)
			{
				auto& releaser = m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector[queueFamilyReleaserID];
				batch.waitSemaphores.push_back(releaser.signalSemaphore);
				batch.waitStages.push_back(vk::PipelineStageFlagBits::eAllCommands);
			}
		}
	}

	void GPUGraphExecutor::Submit()
	{
		for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
		{
			uint32_t queueFamilyIndex = pair.first;
			auto& releaser = pair.second;
			GetQueueContext().SubmitCommands(queueFamilyIndex
				, 0
				, releaser.commandBuffer
				, {}
				, {}
				, {}
			, releaser.signalSemaphore);
		}
		for (auto& batch : m_CommandBufferBatchList)
		{
			vk::CommandBuffer* pCommand = &m_FinalCommandBuffers[batch.firstCommand];
			uint32_t count = batch.lastCommand - batch.firstCommand + 1;
			vk::ArrayProxyNoTemporaries<vk::CommandBuffer> proxy = { count, pCommand };
			GetQueueContext().SubmitCommands(batch.queueFamilyIndex
				, 0
				, proxy
				, {}
				, batch.waitSemaphores
				, batch.waitStages
				, batch.signalSemaphore);
		}
	}

	void GPUGraphExecutor::SyncExternalResources()
	{
		for (auto pair : m_ExternBufferFinalUsageStates)
		{
			auto& bufferHandle = pair.first;
			auto& finalUsage = pair.second;
			auto buffer = castl::static_pointer_cast<VKGPUBuffer>(bufferHandle.GetExternalManagedBuffer());
			buffer->SetUsage(finalUsage.usage);
			buffer->SetQueueFamily(finalUsage.queueFamily);
		}

		for (auto pair : m_ExternImageFinalUsageStates)
		{
			auto& imageHandle = pair.first;
			auto& finalUsage = pair.second;
			switch (imageHandle.GetType())
			{
			case ImageHandle::ImageType::External:
			{
				auto image = castl::static_pointer_cast<VKGPUTexture>(imageHandle.GetExternalManagedTexture());
				image->SetUsage(finalUsage.usage);
				image->SetQueueFamily(finalUsage.queueFamily);
			}
			break;
			case ImageHandle::ImageType::Backbuffer:
			{
				auto window = castl::static_pointer_cast<CWindowContext>(imageHandle.GetWindowHandle());
				window->GetSwapchainContext().MarkUsages(finalUsage.usage, finalUsage.queueFamily);
			}
			break;
			}
		}
	}

	void GPUGraphExecutor::UpdateExternalBufferUsage(PassInfoBase* passInfo, BufferHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState)
	{
		if (handle.GetType() == BufferHandle::BufferType::External)
		{
			auto found = m_ExternBufferFinalUsageStates.find(handle);
			if (found == m_ExternBufferFinalUsageStates.end())
			{
				//Different Queue Needs Release barrier
				if (NeedReleaseBarrier(initUsageState, newUsageState))
				{
					auto& releaser = m_ExternalResourceReleasingBarriers.GetQueueFamilyReleaser(GetVulkanApplication(), initUsageState.queueFamily);
					releaser.barrierCollector.PushBufferReleaseBarrier(newUsageState.queueFamily, GetBufferHandleBufferObject(handle), initUsageState.usage, newUsageState.usage);
					passInfo->m_WaitingQueueFamilies.insert(initUsageState.queueFamily);
				}
			}
			m_ExternBufferFinalUsageStates[handle] = newUsageState;
		}
	}

	void GPUGraphExecutor::UpdateExternalImageUsage(PassInfoBase* passInfo, ImageHandle const& handle, ResourceState const& initUsageState, ResourceState const& newUsageState)
	{
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::External:
		case ImageHandle::ImageType::Backbuffer:
		{
			auto found = m_ExternImageFinalUsageStates.find(handle);
			if (found == m_ExternImageFinalUsageStates.end())
			{
				//Different Queue Needs Release barrier
				if (NeedReleaseBarrier(initUsageState, newUsageState))
				{
					auto& releaser = m_ExternalResourceReleasingBarriers.GetQueueFamilyReleaser(GetVulkanApplication(), initUsageState.queueFamily);
					auto pDesc = GetTextureHandleDescriptor(handle);
					releaser.barrierCollector.PushImageReleaseBarrier(newUsageState.queueFamily, GetTextureHandleImageObject(handle), pDesc->format, initUsageState.usage, newUsageState.usage);
					passInfo->m_WaitingQueueFamilies.insert(initUsageState.queueFamily);
				}
			}
			m_ExternImageFinalUsageStates[handle] = newUsageState;
			break;
		}
		}
	}

	template<typename T>
	void UpdateResourceUsageFlags(castl::unordered_map<T, ResourceState>& inoutResourceUsageFlagCache
		, T resource, ResourceState const& resourceState)
	{
		inoutResourceUsageFlagCache[resource] = resourceState;
	};

	template<typename T>
	ResourceState GetResourceUsage(castl::unordered_map<T, ResourceState>& inoutResourceUsageFlagCache
		, T resource
		, ResourceState const defaultState)
	{
		auto found = inoutResourceUsageFlagCache.find(resource);
		if (found == inoutResourceUsageFlagCache.end())
		{
			found = inoutResourceUsageFlagCache.insert(castl::make_pair(resource, defaultState)).first;
		}
		return found->second;
	};

	void GPUGraphExecutor::PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, DrawCallBatch const& batch
		, GPUPassBatchInfo const& batchInfo
		, uint32_t passID
	)
	{
		for (auto& drawcall : batch.m_DrawCalls)
		{
			auto& indexBufferData = drawcall.GetIndexBuffer();
			if (drawcall.GetDrawInfo().drawIndexed)
			{
				CA_ASSERT_BREAK(indexBufferData.indexBufferHandle.IsValid(), "Invalid Index Buffer");
				UpdateBufferDependency(passID, indexBufferData.indexBufferHandle, ResourceUsage::eVertexAttribute, inoutBufferUsageFlagCache);
			}
			for (auto bindingPair : drawcall.GetVertexBuffers())
			{
				auto vertexBuffer = bindingPair.second;
				UpdateBufferDependency(passID, vertexBuffer, ResourceUsage::eVertexAttribute, inoutBufferUsageFlagCache);
			}
		}
	}

	void GPUGraphExecutor::UpdateUniformBufferDepenedency(int32_t destPassID, vk::Buffer uniformBuffer, ResourceUsageFlags newUsageFlags, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache)
	{
		if (uniformBuffer == vk::Buffer{ nullptr })
			return;

		auto dstInfo = GetBasePassInfo(destPassID);
		CA_ASSERT_BREAK(dstInfo != nullptr, "Invalid Dest Pass ID");

		
		auto usageStates = GetResourceUsage(inoutBufferUsageFlagCache
			, uniformBuffer
			, ResourceState(destPassID, ResourceUsage::eTransferDest, dstInfo->GetQueueFamily()));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);
		CA_ASSERT(newUsageState.usage != ResourceUsage::eDontCare, "why dst usage is dont care?");
		if (usageStates.usage != newUsageState.usage)
		{
			auto sourceInfo = GetBasePassInfo(usageStates.passID);
			if (sourceInfo != nullptr)
			{
				if (NeedReleaseBarrier(usageStates, newUsageState))
				{
					sourceInfo->m_BarrierCollector.PushBufferReleaseBarrier(newUsageState.queueFamily, uniformBuffer, usageStates.usage, newUsageState.usage);
					sourceInfo->m_SuccessorPasses.insert(newUsageState.passID);
					dstInfo->m_PredecessorPasses.insert(usageStates.passID);
				}
			}
			dstInfo->m_BarrierCollector.PushBufferAquireBarrier(usageStates.queueFamily, uniformBuffer, usageStates.usage, newUsageState.usage);
			UpdateResourceUsageFlags(inoutBufferUsageFlagCache, uniformBuffer, newUsageState);
		}
	}

	void GPUGraphExecutor::UpdateBufferDependency(
		uint32_t destPassID
		, BufferHandle const& bufferHandle
		, ResourceUsageFlags newUsageFlags
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache)
	{
		if (bufferHandle.GetType() == BufferHandle::BufferType::Invalid)
		{
			return;
		}
		auto buffer = GetBufferHandleBufferObject(bufferHandle);
		if (buffer == vk::Buffer{ nullptr })
			return;

		auto dstInfo = GetBasePassInfo(destPassID);
		CA_ASSERT_BREAK(dstInfo != nullptr, "Invalid Dest Pass ID");

		auto usageStates = GetResourceUsage(inoutBufferUsageFlagCache, buffer, GetHandleInitializeUsage(bufferHandle, destPassID, *dstInfo));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);
		CA_ASSERT(newUsageState.usage != ResourceUsage::eDontCare, "why dst usage is dont care?");
		if (usageStates.usage != newUsageState.usage)
		{
			auto sourceInfo = GetBasePassInfo(usageStates.passID);
			if (sourceInfo != nullptr)
			{
				if (NeedReleaseBarrier(usageStates, newUsageState))
				{
					sourceInfo->m_BarrierCollector.PushBufferReleaseBarrier(newUsageState.queueFamily, buffer, usageStates.usage, newUsageState.usage);
					sourceInfo->m_SuccessorPasses.insert(newUsageState.passID);
					dstInfo->m_PredecessorPasses.insert(usageStates.passID);
				}
			}
			dstInfo->m_BarrierCollector.PushBufferAquireBarrier(usageStates.queueFamily, buffer, usageStates.usage, newUsageState.usage);
			UpdateResourceUsageFlags(inoutBufferUsageFlagCache, buffer, newUsageState);
			UpdateExternalBufferUsage(dstInfo, bufferHandle, usageStates, newUsageState);
		}
	}

	void GPUGraphExecutor::UpdateImageDependency(uint32_t destPassID, ImageHandle const& imageHandle
		, ResourceUsageFlags newUsageFlags
		, castl::unordered_map<vk::Image, ResourceState>& inoutImageUsageFlagCache)
	{
		if (!ValidImageHandle(imageHandle))
		{
			return;
		}
		auto image = GetTextureHandleImageObject(imageHandle);
		if (image == vk::Image{ nullptr })
			return;

		auto dstInfo = GetBasePassInfo(destPassID);
		CA_ASSERT_BREAK(dstInfo != nullptr, "Invalid Dest Pass ID");

		auto pDesc = GetTextureHandleDescriptor(imageHandle);

		auto usageStates = GetResourceUsage(inoutImageUsageFlagCache, image, GetHandleInitializeUsage(imageHandle, destPassID, *dstInfo));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);

		if (usageStates.usage != newUsageState.usage)
		{
			CA_ASSERT_BREAK(usageStates.passID != PREPARE_PASS_ID, "Image Dependency Shall Not Found In PreparePass");
			auto sourceInfo = GetBasePassInfo(usageStates.passID);
			if (sourceInfo != nullptr)
			{
				if (NeedReleaseBarrier(usageStates, newUsageState))
				{
					sourceInfo->m_BarrierCollector.PushImageReleaseBarrier(newUsageState.queueFamily, image, pDesc->format, usageStates.usage, newUsageState.usage);
					sourceInfo->m_SuccessorPasses.insert(newUsageState.passID);
					dstInfo->m_PredecessorPasses.insert(usageStates.passID);
				}
			}
			dstInfo->m_BarrierCollector.PushImageAquireBarrier(usageStates.queueFamily, image, pDesc->format, usageStates.usage, newUsageState.usage);
			UpdateResourceUsageFlags(inoutImageUsageFlagCache, image, newUsageState);
			UpdateExternalImageUsage(dstInfo, imageHandle, usageStates, newUsageState);
		}
	}

	GPUGraphExecutor::GPUGraphExecutor(CVulkanApplication& application) : VKAppSubObjectBaseNoCopy(application)
	{
	}

	void GPUGraphExecutor::Initialize(castl::shared_ptr<GPUGraph> const& gpuGraph, FrameBoundResourcePool* frameBoundResourceManager)
	{
		m_Graph = gpuGraph;
		m_FrameBoundResourceManager = frameBoundResourceManager;
	}

	void GPUGraphExecutor::Release()
	{
		m_Graph.reset();
		//Rasterize Passes
		m_Passes.clear();
		//Compute Passes
		m_ComputePasses.clear();
		//Transfer Passes
		m_TransferPasses.clear();
		//Manager
		m_ImageManager.ReleaseAll();
		m_BufferManager.ReleaseAll();

		//Final Handle State
		m_ExternBufferFinalUsageStates.clear();
		m_ExternImageFinalUsageStates.clear();
		m_ExternalResourceReleasingBarriers.Release();
		//Command Buffers
		m_FinalCommandBuffers.clear();
		m_CommandBufferBatchList.clear();
	}

	void GPUGraphExecutor::PrepareShaderBindingResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Image, ResourceState>& inoutImageUsageFlagCache
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, ShaderBindingInstance const& shaderBindingInstance
		, uint32_t passID)
	{
		auto basePassInfo = GetBasePassInfo(passID);
		ResourceUsageFlags readFlags;
		ResourceUsageFlags writeFlags;
		switch (basePassInfo->GetStageType())
		{
		case GPUGraph::EGraphStageType::eRenderPass:
		{
			readFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
			writeFlags = ResourceUsage::eVertexWrite | ResourceUsage::eFragmentWrite;
			break;
		}
		case GPUGraph::EGraphStageType::eComputePass:
		{
			readFlags = ResourceUsage::eComputeRead;
			writeFlags = ResourceUsage::eComputeWrite;
			break;
		}
		default:
		{
			CA_LOG_ERR("Invalid Pass Type For Shader Binding Resource Barriers");
			readFlags = ResourceUsage::eComputeRead;
			writeFlags = ResourceUsage::eComputeWrite;
			break;
		}
		}
		for (auto& descInstances : shaderBindingInstance.GetDescriptorSetInstances())
		{
			for (auto& uniformBufferBindings : descInstances.m_BoundUniformBuffers)
			{
				for (vk::Buffer uniformBuffer : uniformBufferBindings.m_UniformBuffers)
				{
					UpdateUniformBufferDepenedency(passID, uniformBuffer, readFlags, inoutBufferUsageFlagCache);
				}
			}
		}
		for (auto bufferHandlePairs : shaderBindingInstance.m_BufferHandles)
		{
			switch (bufferHandlePairs.second)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
			{
				UpdateBufferDependency(passID, bufferHandlePairs.first, readFlags, inoutBufferUsageFlagCache);
				break;
			}
			case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			{
				UpdateBufferDependency(passID, bufferHandlePairs.first, writeFlags, inoutBufferUsageFlagCache);
				break;
			}
			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
			{
				UpdateBufferDependency(passID, bufferHandlePairs.first, readFlags | writeFlags, inoutBufferUsageFlagCache);
				break;
			}
			}
		}
		for (auto imageHandlePairs : shaderBindingInstance.m_ImageHandles)
		{
			switch (imageHandlePairs.second)
			{
			case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
			{
				UpdateImageDependency(passID, imageHandlePairs.first, readFlags, inoutImageUsageFlagCache);
				break;
			}
			case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			{
				UpdateImageDependency(passID, imageHandlePairs.first, writeFlags, inoutImageUsageFlagCache);
				break;
			}
			case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
			{
				UpdateImageDependency(passID, imageHandlePairs.first, readFlags | writeFlags, inoutImageUsageFlagCache);
				break;
			}
			}
		}
	}

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs(thread_management::TaskScheduler* taskGraph)
	{
		//FBO, RenderPass And PSO For Rasterization Passes
		auto& renderPasses = m_Graph->GetRenderPasses();
		CA_ASSERT(renderPasses.size() == m_Passes.size(), "Rasterize Pass Data Count InCompatible!!");
		for (uint32_t passID = 0; passID < renderPasses.size(); ++passID)
		{
			taskGraph->NewTaskGraph()
				->Name("Prepare Render Pass And FBO")
				->Func([this, passID, &renderPasses](auto setupGraph)
				{
					auto& renderPass = renderPasses[passID];
					auto& attachments = renderPass.GetAttachments();
					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					GPUPassInfo& passInfo = m_Passes[passID];

					auto createRenderPassTask = setupGraph->NewTask()
						->Name("Prepare Render Pass Object")
						->Functor([&]()
							{
								//RenderPass Object
								{
									//passInfo.m_ClearValues.resize(attachments.size());
									RenderPassDescriptor renderPassDesc{};
									renderPassDesc.renderPassInfo.attachmentInfos.resize(attachments.size());
									renderPassDesc.renderPassInfo.subpassInfos.resize(1);

									for (size_t i = 0; i < attachments.size(); ++i)
									{
										auto& attachmentConfig = renderPass.GetAttachmentConfig(i);
										auto& attachment = attachments[i];
										auto& attachmentInfo = renderPassDesc.renderPassInfo.attachmentInfos[i];
										auto pDesc = GetTextureHandleDescriptor(attachment);
										attachmentInfo.format = pDesc->format;
										attachmentInfo.multiSampleCount = pDesc->samples;
										//TODO DO A Attachment Wise Version
										attachmentInfo.loadOp = attachmentConfig.loadOp;
										attachmentInfo.storeOp = attachmentConfig.storeOp;
										attachmentInfo.stencilLoadOp = attachmentConfig.loadOp;
										attachmentInfo.stencilStoreOp = attachmentConfig.storeOp;
									}

									{
										auto& subpass = renderPassDesc.renderPassInfo.subpassInfos[0];
										subpass.colorAttachmentIDs.reserve(attachments.size());
										for (uint32_t attachmentID = 0; attachmentID < attachments.size(); ++attachmentID)
										{
											if (attachmentID != renderPass.GetDepthAttachmentIndex())
											{
												subpass.colorAttachmentIDs.push_back(attachmentID);
											}
										}
										subpass.depthAttachmentID = renderPass.GetDepthAttachmentIndex();
									}
									passInfo.m_RenderPassObject = GetGPUObjectManager().GetRenderPassCache().GetOrCreate(renderPassDesc);
								}
							});
					
					setupGraph->NewTask()
						->Name("Prepare FrameBuffer Object")
						->DependsOn(createRenderPassTask)
						->Functor([&]()
						{
							//Frame Buffer Object
							{
								auto attachmentType = attachments[0].GetType();

								FramebufferDescriptor frameBufferDesc{};
								auto pDesc = GetTextureHandleDescriptor(attachments[0]);
								CA_ASSERT(pDesc != nullptr, "Invalid texture descriptor");
								frameBufferDesc.height = pDesc->height;
								frameBufferDesc.width = pDesc->width;
								frameBufferDesc.layers = 1;
								frameBufferDesc.renderImageViews.resize(attachments.size());

								bool anyInvalidFrameBuffer = false;
								for (size_t i = 0; i < attachments.size(); ++i)
								{
									auto& attachment = attachments[i];
									if (ValidImageHandle(attachment))
									{
										auto pDesc = GetTextureHandleDescriptor(attachment);
										CA_ASSERT(pDesc != nullptr, "Invalid texture descriptor");
										frameBufferDesc.renderImageViews[i] = GetTextureHandleImageView(attachment, GPUTextureView::CreateDefaultForRenderTarget(pDesc->format));
										frameBufferDesc.renderpassObject = passInfo.m_RenderPassObject;
									}
									else
									{
										anyInvalidFrameBuffer = true;
										break;
									}
								}

								if (anyInvalidFrameBuffer)
								{
									passInfo.m_FrameBufferObject = nullptr;
								}
								else
								{
									passInfo.m_FrameBufferObject = m_FrameBoundResourceManager->framebufferObjectCache.GetOrCreate(frameBufferDesc);
								}
							}
						});
					
					auto& passLevelPsoDesc = renderPass.GetPipelineStates();

					CA_ASSERT_BREAK(drawcallBatchs.size() == passInfo.m_Batches.size(), "DrawCall Size Incompatible!!!");

					setupGraph->NewTaskParallelFor()
						->Name("Prepare Batch PSOs")
						->DependsOn(createRenderPassTask)
						->JobCount(drawcallBatchs.size())
						->Functor([&](auto batchID)
							{
								auto& batch = drawcallBatchs[batchID];
								{
									GPUPassBatchInfo& newBatchInfo = passInfo.m_Batches[batchID];

									auto& batchLevelPsoDesc = batch.pipelineStateDesc;
									auto resolvedPSODesc = PipelineDescData::CombindDescData(passLevelPsoDesc, batchLevelPsoDesc);

									ShaderSetData shaderSet = GetVulkanApplication().GetShaderCodes(resolvedPSODesc.m_ShaderInfo);

									GPUShaderBindingKey shaderBindingKey = {};
									shaderBindingKey.m_ShaderInfo = resolvedPSODesc.m_ShaderInfo;
									shaderBindingKey.m_ShaderStructStack.push_back(&renderPass.GetShaderStructs());
									shaderBindingKey.m_ShaderStructStack.push_back(&batch.shaderStructs);
									newBatchInfo.m_ShaderBindingKey = shaderBindingKey;

				/*					auto& shaderBindingInstance = SelectShaderBindingInstance(newBatchInfo.m_ShaderBindingKey);
									shaderBindingInstance.InitShaderBindingLayoutsNew(GetVulkanApplication(), *shaderSet.reflectionData, resolvedPSODesc.m_ShaderInfo.path);
									shaderBindingInstance.InitShaderBindingSetsNew(m_FrameBoundResourceManager);*/

									auto& shaderBindingInstance = m_PrepareShaderBindingConstantsPass.m_ShaderBindingInstances.get_or_create(newBatchInfo.m_ShaderBindingKey, [&](auto& bindingKey)
									{
										ShaderBindingInstance newBindingInstance{};
										newBindingInstance.InitShaderBindingLayoutsNew(GetVulkanApplication(), *shaderSet.reflectionData, resolvedPSODesc.m_ShaderInfo.path);
										newBindingInstance.InitShaderBindingSetsNew(m_FrameBoundResourceManager);
										return newBindingInstance;
									})->second;

									auto& vertexAttributes = shaderSet.reflectionData->m_VertexAttributes;
		
									CPipelineObjectDescriptor psoDescObj;

									MakeVertexInputDescriptorsNew(
										vertexAttributes
										, batch.m_VertexInputDescs
										, newBatchInfo.m_VertexStreamNames
										, newBatchInfo.m_VertexStreamBindings);

									psoDescObj.vertexBindingData = newBatchInfo.m_VertexStreamBindings;
									psoDescObj.assemblyStates = resolvedPSODesc.m_InputAssemblyStates.Get();
									psoDescObj.pso = resolvedPSODesc.m_PipelineStates.Get();
									psoDescObj.shaderState = { shaderSet.vertexShader, shaderSet.fragmentShader };
									psoDescObj.renderPassObject = passInfo.m_RenderPassObject;
									psoDescObj.descriptorSetLayouts = shaderBindingInstance.m_DescriptorSetsLayouts;

									newBatchInfo.m_PSO = GetGPUObjectManager().GetPipelineCache().GetOrCreate(psoDescObj);
									CA_ASSERT_BREAK(newBatchInfo.m_PSO != nullptr, "Invalid PSO");

									CA_ASSERT_BREAK(batch.m_DrawCalls.size() == newBatchInfo.m_DrawCalls.size(), "DrawCall Size Incompatible!!!");
									for (uint32_t drawCallID = 0; drawCallID < batch.m_DrawCalls.size(); ++drawCallID)
									{
										auto& drawcall = batch.m_DrawCalls[drawCallID];
										auto& drawcallData = newBatchInfo.m_DrawCalls[drawCallID];
										auto& bufferMap = drawcall.GetVertexBuffers();
										for (auto& name : newBatchInfo.m_VertexStreamNames)
										{
											auto found = bufferMap.find(name);
											CA_ASSERT_BREAK(found != bufferMap.end(), "Invalid Vertex Buffer Name");
											drawcallData.m_VertexBufferBindings.push_back(found->second);
										}
									}
								}
							});
				});
		}
	}

	void GPUGraphExecutor::PrepareComputePSOs()
	{
		auto& computePasses = m_Graph->GetComputePasses();
		CA_ASSERT(computePasses.size() == m_ComputePasses.size(), "Compute Pass Data Count InCompatible!!");
		for (uint32_t passID = 0; passID < computePasses.size(); ++passID)
		{
			auto& computePass = computePasses[passID];
			GPUComputePassInfo& newComputePass = m_ComputePasses[passID];
			for (uint32_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				GPUComputePassInfo::ComputeDispatchInfo newDispatchInfo = newComputePass.m_DispatchInfos[dispatchID];

				ShaderSetData shaderSet = GetVulkanApplication().GetShaderCodes(dispatch.m_ShaderInfo);

				//auto comp = GetGPUObjectManager()
				//	.GetShaderModuleCache()
				//	.GetOrCreate(shaderSet.computeShader);

				GPUShaderBindingKey shaderBindingKey = {};
				shaderBindingKey.m_ShaderInfo = dispatch.m_ShaderInfo;
				shaderBindingKey.m_ShaderStructStack.push_back(&computePass.shaderStructs);
				shaderBindingKey.m_ShaderStructStack.push_back(&dispatch.shaderStructs);
				newDispatchInfo.m_ShaderBindingKey = shaderBindingKey;

				auto& shaderBindingInstance = m_PrepareShaderBindingConstantsPass.m_ShaderBindingInstances.get_or_create(newDispatchInfo.m_ShaderBindingKey, [&](auto& bindingKey)
				{
					ShaderBindingInstance newBindingInstance{};
					newBindingInstance.InitShaderBindingLayoutsNew(GetVulkanApplication(), *shaderSet.reflectionData, dispatch.m_ShaderInfo.path);
					newBindingInstance.InitShaderBindingSetsNew(m_FrameBoundResourceManager);
					return newBindingInstance;
				})->second;

				ComputePipelineDescriptor pipelineDesc{};
				pipelineDesc.computeShader = shaderSet.computeShader;
				pipelineDesc.descriptorSetLayouts = shaderBindingInstance.m_DescriptorSetsLayouts;
				newDispatchInfo.m_ComputePipeline = GetGPUObjectManager()
					.GetComputePipelineCache().GetOrCreate(pipelineDesc);

				newComputePass.m_DispatchInfos.push_back(newDispatchInfo);
			}
		}
	}



	void GPUGraphExecutor::WriteDescriptorSets(thread_management::TaskScheduler* taskGraph)
	{
		//Do Shader Binding Writes
		taskGraph->NewTaskGraph()
			->Name(CANAME("Write All Descriptors"))
			->Func([this](thread_management::TaskScheduler* scheduler)
			{
				auto& shaderBindingInstance = m_PrepareShaderBindingConstantsPass.m_ShaderBindingInstances;
				auto& barrierCollector = m_PrepareShaderBindingConstantsPass.m_BarrierCollector;
				auto& commandBuffers = m_PrepareShaderBindingConstantsPass.m_CommandBuffers;
				commandBuffers.resize(shaderBindingInstance.size());

				uint32_t shaderBindingIndex = 0;
				shaderBindingInstance.for_each([&](auto& shaderKey, auto& shaderBindingInst)
				{
					scheduler->NewTask()
						->Name(CANAME("Write Shader Binding Descriptors"))
						->Functor([
							this
							, shaderBindingIndex
							, &shaderKey
							, &shaderBindingInst
							, &commandBuffers]()
					{
						auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
						vk::CommandBuffer writeConstantsCommand = cmdPool->AllocCommand(QueueType::eTransfer, "Write Descriptors");
						shaderBindingInst.FillShaderData(GetVulkanApplication()
							, *this
							, m_FrameBoundResourceManager
							, writeConstantsCommand
							, shaderKey->m_ShaderStructStack);
						writeConstantsCommand.end();
						commandBuffers[shaderBindingIndex] = writeConstantsCommand;
					});
					++shaderBindingIndex;
				});
			});
	}

	void GPUGraphExecutor::PrepareResourceBarriers()
	{
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& computePasses = m_Graph->GetComputePasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();
		auto& passIndices = m_Graph->GetPassIndices();

		castl::unordered_map<vk::Image, ResourceState> imageUsageFlagCache;
		castl::unordered_map<vk::Buffer, ResourceState> bufferUsageFlagCache;

		//TODO Add Constant Buffer Resource States Here
		m_PrepareShaderBindingConstantsPass.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetTransferPipelineStageMask(), GetQueueContext().GetTransferQueueFamily());
		m_PrepareShaderBindingConstantsPass.m_ShaderBindingInstances.for_each([&](auto& shaderKey, auto& shaderBindingInstance)
		{
			for (auto& descInstances : shaderBindingInstance.GetDescriptorSetInstances())
			{
				for (auto& uniformBufferBindings : descInstances.m_BoundUniformBuffers)
				{
					for (vk::Buffer uniformBuffer : uniformBufferBindings.m_UniformBuffers)
					{
						UpdateUniformBufferDepenedency(PREPARE_PASS_ID, uniformBuffer, ResourceUsage::eTransferDest, bufferUsageFlagCache);
					}
				}
			}
		});

		uint32_t currentRenderPassIndex = 0;
		uint32_t currentComputePassIndex = 0;
		uint32_t currentTransferPassIndex = 0;

		uint32_t passID = 0;
		for (auto stage : graphStages)
		{
			uint32_t realPassID = passIndices[passID];
			switch (stage)
			{
			case GPUGraph::EGraphStageType::eRenderPass:
			{
				CA_ASSERT(realPassID == currentRenderPassIndex, "Render Pass Index Mismatch");
				++currentRenderPassIndex;
				auto& renderPass = renderPasses[realPassID];
				auto& renderPassData = m_Passes[realPassID];
				auto& attachments = renderPass.GetAttachments();
				auto& drawcallBatchs = renderPass.GetDrawCallBatches();
				//Barriers
				{
					renderPassData.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetGraphicsPipelineStageMask(), GetQueueContext().GetGraphicsQueueFamily());

					for (size_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
					{
						auto& batch = drawcallBatchs[batchID];
						auto& batchData = renderPassData.m_Batches[batchID];
						PrepareVertexBuffersBarriers(renderPassData.m_BarrierCollector, bufferUsageFlagCache, batch, batchData, passID);

						auto& shaderBindingInstance = SelectShaderBindingInstance(batchData.m_ShaderBindingKey);
						PrepareShaderBindingResourceBarriers(renderPassData.m_BarrierCollector
							, imageUsageFlagCache
							, bufferUsageFlagCache
							, shaderBindingInstance
							, passID);
					}
					for (size_t i = 0; i < attachments.size(); ++i)
					{
						auto& attachment = attachments[i];
						ResourceUsageFlags usageFlags = i == renderPass.GetDepthAttachmentIndex() ? ResourceUsage::eDepthStencilAttachment : ResourceUsage::eColorAttachmentOutput;
						UpdateImageDependency(passID, attachment, usageFlags, imageUsageFlagCache);
					}
				}
				break;
			}
			case GPUGraph::EGraphStageType::eComputePass:
			{
				CA_ASSERT(realPassID == currentComputePassIndex, "Compute Pass Index Mismatch");
				++currentComputePassIndex;
				auto& computePass = computePasses[realPassID];
				auto& computePassData = m_ComputePasses[realPassID];
				computePassData.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetComputePipelineStageMask(), GetQueueContext().GetComputeQueueFamily());
				for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
				{
					auto& dispatchData = computePass.dispatchs[dispatchID];
					auto& dispatchData1 = computePassData.m_DispatchInfos[dispatchID];

					auto& shaderBindingInstance = SelectShaderBindingInstance(dispatchData1.m_ShaderBindingKey);
					PrepareShaderBindingResourceBarriers(computePassData.m_BarrierCollector
						, imageUsageFlagCache
						, bufferUsageFlagCache
						, shaderBindingInstance
						, passID);
				}
				break;
			}
			case GPUGraph::EGraphStageType::eTransferPass:
			{
				CA_ASSERT(realPassID == currentTransferPassIndex, "Render Pass Index Mismatch");
				++currentTransferPassIndex;
				GPUTransferInfo& transfersData = m_TransferPasses[realPassID];
				transfersData.m_BarrierCollector.SetCurrentQueueFamilyIndex(GetQueueContext().GetTransferPipelineStageMask(), GetQueueContext().GetTransferQueueFamily());
				auto& transfersInfo = dataTransfers[realPassID];

				for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
				{

					auto [bufferHandle, uploadRef] = bufferUpload;
					if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
					{
						ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
						UpdateBufferDependency(passID, bufferHandle, usageFlags, bufferUsageFlagCache);
					}
				}
				for (auto& imageUpload : transfersInfo.m_ImageDataUploads)
				{
					auto [imageHandle, uploadRef] = imageUpload;
					ResourceUsageFlags usageFlags = ResourceUsage::eTransferDest;
					UpdateImageDependency(passID, imageHandle, usageFlags, imageUsageFlagCache);
				}
				break;
			}
			}
			++passID;
		}

	}

	void GPUGraphExecutor::RecordGraph(thread_management::TaskScheduler* taskGraph)
	{
		CA_ASSERT(m_Passes.size() == m_Graph->GetRenderPasses().size(), "Render Passe Count Mismatch");
		CA_ASSERT(m_TransferPasses.size() == m_Graph->GetDataTransfers().size(), "Transfer Pass Count Mismatch");
		auto& graphStages = m_Graph->GetGraphStages();
		auto& renderPasses = m_Graph->GetRenderPasses();
		auto& computePasses = m_Graph->GetComputePasses();
		auto& dataTransfers = m_Graph->GetDataTransfers();
		auto& passIndices = m_Graph->GetPassIndices();
		auto& uploadData = m_Graph->GetUploadDataHolder();

		//InitialTransferPasses
		{
			taskGraph->NewTaskGraph()
				->Name("Prepare External Resource Release Barriers")
				->Func([&](auto extResourceGraph)
					{
						for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
						{
							extResourceGraph->NewTask()
								->Name("Prepare External Resource Release Barriers")
								->Functor([&]()
								{
									auto& releaser = pair.second;
									auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
									vk::CommandBuffer externalResourceReleaseBarriers = cmdPool->AllocCommand(pair.first, "Extern Resource Barriers");
									releaser.barrierCollector.ExecuteReleaseBarrier(externalResourceReleaseBarriers);
									externalResourceReleaseBarriers.end();
									releaser.commandBuffer = externalResourceReleaseBarriers;
								});
						}
					});
		}

		//Shader Uniform Buffer Release Barriers
		{
			taskGraph->NewTaskGraph()
				->Name("Prepare Uniform Buffer Release Barriers")
				->Func([&](auto extResourceGraph)
				{
					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer releaseUniformCmd = cmdPool->AllocCommand(QueueType::eTransfer, "Release Uniform Buffer Barriers");
					m_PrepareShaderBindingConstantsPass.m_BarrierCollector.ExecuteBarrier(releaseUniformCmd);
					m_PrepareShaderBindingConstantsPass.m_BarrierCollector.ExecuteReleaseBarrier(releaseUniformCmd);
					releaseUniformCmd.end();
					m_PrepareShaderBindingConstantsPass.m_CommandBuffers.push_back(releaseUniformCmd);
				});
		}

		taskGraph->NewTaskParallelFor()
			->Name("Record Pass Commands")
			->JobCount(graphStages.size())
			->Functor([&](uint32_t passID)
			{
				GPUGraph::EGraphStageType stage = graphStages[passID];
				uint32_t realPassID = passIndices[passID];
				switch (stage)
				{
				case GPUGraph::EGraphStageType::eRenderPass:
				{
					auto& renderPass = renderPasses[realPassID];
					auto& attachments = renderPass.GetAttachments();
					auto& passData = m_Passes[realPassID];

					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					auto& batchDatas = passData.m_Batches;
					CA_ASSERT(drawcallBatchs.size() == batchDatas.size(), "Batch Count Mismatch");

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer renderPassCommandBuffer = cmdPool->AllocCommand(QueueType::eGraphics, "Render Pass");

					passData.m_BarrierCollector.ExecuteBarrier(renderPassCommandBuffer);

					

					if (passData.ValidPassData())
					{
						castl::vector<vk::ClearValue> clearValues;
						clearValues.resize(attachments.size());
						for (size_t i = 0; i < attachments.size(); ++i)
						{
							auto& attachmentConfig = renderPass.GetAttachmentConfig(i);
							auto& attachment = attachments[i];
							auto pDesc = GetTextureHandleDescriptor(attachment);
							clearValues[i] = AttachmentClearValueTranslate(
								attachmentConfig.clearValue
								, pDesc->format);
						}

						if (renderPass.GetName().Valid())
						{
							vk::DebugUtilsLabelEXT m_DebugLabelInfo = { renderPass.GetName().c_str() };
							renderPassCommandBuffer.beginDebugUtilsLabelEXT(m_DebugLabelInfo);
						}

						renderPassCommandBuffer.beginRenderPass(
							vk::RenderPassBeginInfo{
								passData.m_RenderPassObject->GetRenderPass()
								, passData.m_FrameBufferObject->GetFramebuffer()
								, vk::Rect2D{{0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }}
								, clearValues
							}
						, vk::SubpassContents::eInline);

						ViewRectData defaultViewRect{0, 0,passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight()};

						for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
						{
							auto& batchData = batchDatas[batchID];
							auto& drawcallBatch = drawcallBatchs[batchID];

							auto& shaderBindingInstance = SelectShaderBindingInstance(batchData.m_ShaderBindingKey);

							renderPassCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipeline());
							renderPassCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics
								, batchData.m_PSO->GetPipelineLayout(), 0
								, shaderBindingInstance.m_DescriptorSets, {});

							CA_ASSERT_BREAK(batchData.m_DrawCalls.size() == drawcallBatch.m_DrawCalls.size(), "Draw Call Count Mismatch");
							for (uint32_t drawCallID = 0; drawCallID < batchData.m_DrawCalls.size(); ++drawCallID)
							{
								auto& graphDrawcall = drawcallBatch.m_DrawCalls[drawCallID];
								auto& drawcall = batchData.m_DrawCalls[drawCallID];
								auto& drawCallInfo = graphDrawcall.GetDrawInfo();

								ViewRectData viewRect = graphDrawcall.GetViewPort().Valid() ? graphDrawcall.GetViewPort().Get() : defaultViewRect;
								ViewRectData scissorRect = graphDrawcall.GetScissor().Valid() ? graphDrawcall.GetScissor().Get() : defaultViewRect;

								renderPassCommandBuffer.setViewport(0, { vk::Viewport(viewRect.x, viewRect.y, viewRect.width, viewRect.height, 0.0f, 1.0f)});
								renderPassCommandBuffer.setScissor(0, { vk::Rect2D({scissorRect.x, scissorRect.y}, { (uint32_t)scissorRect.width, (uint32_t)scissorRect.height }) });

								CA_ASSERT_BREAK(batchData.m_VertexStreamBindings.size() == drawcall.m_VertexBufferBindings.size(), "InCompatible Vertex Buffer Binding Count");
								for (uint32_t vertexBindID = 0; vertexBindID < batchData.m_VertexStreamBindings.size(); ++vertexBindID)
								{
									auto& bindingInfo = batchData.m_VertexStreamBindings[vertexBindID];
									auto bindBuffer = drawcall.m_VertexBufferBindings[vertexBindID];
									auto buffer = GetBufferHandleBufferObject(bindBuffer);
									renderPassCommandBuffer.bindVertexBuffers(bindingInfo.bindingIndex, { buffer }, { 0 });
								}

								//Check Bind Index Buffer
								if (drawCallInfo.drawIndexed)
								{
									auto indexBufferHandle = graphDrawcall.GetIndexBuffer();
									//indexBufferHandle = indexBufferHandle.Select(drawcallBatch.m_IndexBufferData);
									CA_ASSERT_BREAK(indexBufferHandle.indexBufferHandle.IsValid(), "Invalid Index Buffer");
									auto indexBuffer = GetBufferHandleBufferObject(indexBufferHandle.indexBufferHandle);
									renderPassCommandBuffer.bindIndexBuffer(indexBuffer, indexBufferHandle.indexBufferOffset, EIndexBufferTypeTranslate(indexBufferHandle.indexBufferType));
									renderPassCommandBuffer.drawIndexed(drawCallInfo.indexCount, drawCallInfo.instanceCount, drawCallInfo.indexOffset, drawCallInfo.vertexOffset, drawCallInfo.firstInstanceID);
								}
								else
								{
									renderPassCommandBuffer.draw(drawCallInfo.vertexCount, drawCallInfo.instanceCount, drawCallInfo.vertexOffset, drawCallInfo.firstInstanceID);
								}
							}
						}
						renderPassCommandBuffer.endRenderPass();
						if (renderPass.GetName().Valid())
						{
							renderPassCommandBuffer.endDebugUtilsLabelEXT();
						}
					}

					passData.m_BarrierCollector.ExecuteReleaseBarrier(renderPassCommandBuffer);
					renderPassCommandBuffer.end();
					passData.m_CommandBuffers.push_back(renderPassCommandBuffer);
					break;
				}
				case GPUGraph::EGraphStageType::eComputePass:
				{
					auto& computePass = computePasses[realPassID];
					auto& computePassData = m_ComputePasses[realPassID];
					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer computeCommandBuffer = cmdPool->AllocCommand(QueueType::eCompute, "Compute Pass");
					computePassData.m_BarrierCollector.ExecuteBarrier(computeCommandBuffer);
					for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
					{
						auto& dispatchData = computePass.dispatchs[dispatchID];
						auto& dispatchData1 = computePassData.m_DispatchInfos[dispatchID];
						auto& shaderBindingInstance = SelectShaderBindingInstance(dispatchData1.m_ShaderBindingKey);
						computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipeline());
						computeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipelineLayout(), 0, shaderBindingInstance.m_DescriptorSets, {});
						computeCommandBuffer.dispatch(dispatchData.x, dispatchData.y, dispatchData.z);
					}
					computePassData.m_BarrierCollector.ExecuteReleaseBarrier(computeCommandBuffer);
					computeCommandBuffer.end();
					computePassData.m_CommandBuffers.push_back(computeCommandBuffer);
					break;
				}
				case GPUGraph::EGraphStageType::eTransferPass:
				{
					auto& transfersInfo = dataTransfers[realPassID];
					GPUTransferInfo& transfersData = m_TransferPasses[realPassID];

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer dataTransferCommandBuffer = cmdPool->AllocCommand(QueueType::eTransfer, "Data Transfer");
					transfersData.m_BarrierCollector.ExecuteBarrier(dataTransferCommandBuffer);

					for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
					{
						auto [bufferHandle, uploadRef] = bufferUpload;
						if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
						{
							auto buffer = GetBufferHandleBufferObject(bufferHandle);
							if (buffer != vk::Buffer{ nullptr })
							{
								auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(uploadRef.dataSize, EBufferUsage::eDataSrc, cacore::format("Staging Buffer {}", bufferHandle.GetName()));
								{
									auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
									memcpy(mappedSrcBuffer.mappedMemory, uploadData.GetPtr(uploadRef.dataIndex), uploadRef.dataSize);
								}
								dataTransferCommandBuffer.copyBuffer(srcBuffer.buffer, buffer, vk::BufferCopy(0, uploadRef.dstOffset, uploadRef.dataSize));
							}
						}
					}

					for (auto& imageUpload : transfersInfo.m_ImageDataUploads)
					{
						auto [imageHandle, uploadRef] = imageUpload;
						if (ValidImageHandle(imageHandle))
						{
							auto image = GetTextureHandleImageObject(imageHandle);
							auto pDesc = GetTextureHandleDescriptor(imageHandle);
							if (image != vk::Image{ nullptr })
							{
								auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(uploadRef.dataSize, EBufferUsage::eDataSrc);
								{
									auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
									memcpy(mappedSrcBuffer.mappedMemory, uploadData.GetPtr(uploadRef.dataIndex), uploadRef.dataSize);
								}

								//TODO: offset is not used here for now
								std::array<vk::BufferImageCopy, 1> bufferImageCopy = { GPUTextureDescriptorToBufferImageCopy(*pDesc) };
								dataTransferCommandBuffer.copyBufferToImage(srcBuffer.buffer
									, image
									, vk::ImageLayout::eTransferDstOptimal
									, bufferImageCopy);
							}
						}
					}

					transfersData.m_BarrierCollector.ExecuteReleaseBarrier(dataTransferCommandBuffer);
					dataTransferCommandBuffer.end();
					transfersData.m_CommandBuffers.push_back(dataTransferCommandBuffer);
					break;
				}
				}

			});
	}
	void BufferSubAllocator::Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUBufferDescriptor const& descriptor)
	{
		m_Buffers.clear();
		m_Buffers.reserve(passAllocationCount);
		for (int i = 0; i < passAllocationCount; ++i)
		{
			auto bufferObj = pResourcePool->CreateBufferWithMemory(descriptor);
			m_Buffers.push_back(castl::move(bufferObj));
		}
	}
	void ImageSubAllocator::Allocate(CVulkanApplication& app, FrameBoundResourcePool* pResourcePool, GPUTextureDescriptor const& descriptor)
	{
		m_Images.clear();
		m_Images.reserve(passAllocationCount);
		for (int i = 0; i < passAllocationCount; ++i)
		{
			auto imgObj = pResourcePool->CreateImageWithMemory(descriptor);
			m_Images.push_back(castl::move(imgObj));
		}
	}
	GPUGraphExecutor::ExternalResourceReleaser& GPUGraphExecutor::ExternalResourceReleasingBarriers::GetQueueFamilyReleaser(CVulkanApplication& app, uint32_t queueFamily)
	{
		auto found = queueFamilyToBarrierCollector.find(queueFamily);
		if (found == queueFamilyToBarrierCollector.end())
		{
			ExternalResourceReleaser newReleaser{};
			newReleaser.barrierCollector = VulkanBarrierCollector{ app.GetQueueContext().QueueFamilyIndexToPipelineStageMask(queueFamily)
				, queueFamily };
			newReleaser.commandBuffer = { nullptr };
			newReleaser.signalSemaphore = { nullptr };
			found = queueFamilyToBarrierCollector.insert(castl::make_pair(queueFamily, newReleaser)).first;
		}
		return found->second;
	}
}