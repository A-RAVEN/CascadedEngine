#include <pch.h>
#include "GPUGraphExecutor.h"
#include <VulkanApplication.h>
#include <GPUGraphExecutor/ShaderBindingHolder.h>
#include <InterfaceTranslator.h>
#include <CommandList_Impl.h>
#include <GPUResources/VKGPUBuffer.h>
#include <GPUResources/VKGPUTexture.h>
#include <VulkanDebug.h>

namespace graphics_backend
{
	

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
			auto buffer = castl::static_shared_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
			if (buffer->GetUsage() == ResourceUsage::eDontCare)
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			else
			{
				return ResourceState(-1, buffer->GetUsage(), buffer->GetQueueFamily());
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
			auto image = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			if (image->GetUsage() == eDontCare)
			{
				return ResourceState(currentPassID, ResourceUsage::eDontCare, currentPass.GetQueueFamily());
			}
			else
			{
				return ResourceState(-1, image->GetUsage(), image->GetQueueFamily());
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

	CVertexInputDescriptor MakeVertexInputDescriptorsNew(castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::unordered_map<cacore::HashObj<VertexInputsDescriptor>, BufferHandle> const& boundVertexBuffers
		, castl::unordered_map<cacore::HashObj<VertexInputsDescriptor>, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;
		for (auto& attributeData : vertexAttributes)
		{
			bool attribBindingFound = false;
			for (auto boundPair : boundVertexBuffers)
			{
				auto& attribDesc = boundPair.first;
				auto& boundBuffer = boundPair.second;
				for (auto& attribute : attribDesc->attributes)
				{
					if ((attribute.semanticName == attributeData.m_SematicName) && (attribute.sematicIndex == attributeData.m_SematicIndex))
					{
						auto found = inoutBindingNameToIndex.find(attribDesc);
						if (found == inoutBindingNameToIndex.end())
						{
							uint32_t bindingID = inoutBindingNameToIndex.size();
							found = inoutBindingNameToIndex.insert(castl::make_pair(attribDesc, VertexAttributeBindingData{ bindingID , attribDesc->stride, {}, attribDesc->perInstance })).first;
						}
						else
						{
						}
						found->second.attributes.push_back(VertexAttribute{ attributeData.m_Location , attribute.offset, attribute.format });
						attribBindingFound = true;
						break;
					}
				}
				if (attribBindingFound)
				{
					break;
				}
			}
			CA_ASSERT(attribBindingFound, "Attribute With Semantic" + attributeData.m_SematicName + "Not Found");
		}
		for (auto pair : inoutBindingNameToIndex)
		{
			if (result.m_PrimitiveDescriptions.size() <= pair.second.bindingIndex)
			{
				result.m_PrimitiveDescriptions.resize(pair.second.bindingIndex + 1);
			}
			result.m_PrimitiveDescriptions[pair.second.bindingIndex] = castl::make_tuple(pair.second.stride, pair.second.attributes, pair.second.bInstance);
		}
		return result;
	}

	//TODO: Same Sematics May be found in multiple attribute data
	CVertexInputDescriptor MakeVertexInputDescriptors(castl::map<castl::string, VertexInputsDescriptor> const& vertexAttributeDescs
		, castl::vector<ShaderCompilerSlang::ShaderVertexAttributeData> const& vertexAttributes
		, InputAssemblyStates assemblyStates
		, castl::map<castl::string, BufferHandle> const& boundVertexBuffers
		, castl::unordered_map<castl::string, VertexAttributeBindingData>& inoutBindingNameToIndex)
	{
		CVertexInputDescriptor result;
		result.assemblyStates = assemblyStates;

		for (auto& attributeData : vertexAttributes)
		{
			bool attribBindingFound = false;
			for (auto boundPair : boundVertexBuffers)
			{
				auto& boundAttribDescName = boundPair.first;
				auto foundAttribDesc = vertexAttributeDescs.find(boundAttribDescName);
				CA_ASSERT(foundAttribDesc != vertexAttributeDescs.end(), "Attribute Descriptor Not Found");
				for (auto& attribute : foundAttribDesc->second.attributes)
				{
					if ((attribute.semanticName == attributeData.m_SematicName) && (attribute.sematicIndex == attributeData.m_SematicIndex))
					{
						auto found = inoutBindingNameToIndex.find(boundAttribDescName);
						if (found == inoutBindingNameToIndex.end())
						{
							uint32_t bindingID = inoutBindingNameToIndex.size();
							found = inoutBindingNameToIndex.insert(castl::make_pair(boundAttribDescName, VertexAttributeBindingData{ bindingID , foundAttribDesc->second.stride, {}, foundAttribDesc->second.perInstance })).first;
						}
						else
						{
						}
						found->second.attributes.push_back(VertexAttribute{ attributeData.m_Location , attribute.offset, attribute.format });
						attribBindingFound = true;
						break;
					}
				}
				if (attribBindingFound)
				{
					break;
				}
			}
			CA_ASSERT(attribBindingFound, "Attribute With Semantic" + attributeData.m_SematicName + "Not Found");
		}
		for (auto pair : inoutBindingNameToIndex)
		{
			if (result.m_PrimitiveDescriptions.size() <= pair.second.bindingIndex)
			{
				result.m_PrimitiveDescriptions.resize(pair.second.bindingIndex + 1);
			}
			result.m_PrimitiveDescriptions[pair.second.bindingIndex] = castl::make_tuple(pair.second.stride, pair.second.attributes, pair.second.bInstance);
		}

		return result;
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
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
			return !window->Invalid();
		}
		case ImageHandle::ImageType::External:
		{
			return handle.GetExternalManagedTexture() != nullptr;
		}
		}
		return m_Graph->GetImageManager().GetDescriptorIndex(handle.GetKey()) >= 0;
	}

	void GPUGraphExecutor::PrepareGraph(thread_management::CTaskGraph* taskGraph)
	{
		auto allocResources = taskGraph->NewTaskGraph()
			->Name("Prepare GPU Resource")
			->SetupFunctor([this](auto graph)
				{
					//Alloc Image & Buffer Resources
					graph->NewTask()
						->Name("Alloc Buffer Resources")
						->Functor([this]()
							{
								PrepareGraphLocalBufferResources();
							});
					graph->NewTask()
						->Name("Alloc Image Resources")
						->Functor([this]()
							{
								PrepareGraphLocalImageResources();
							});

					graph->NewTask()
						->Name("Initialize Passes")
						->Functor([this]()
							{
								InitializePasses();
							});
				});

		//auto waitBackbuffers = taskGraph->NewTaskGraph()
		//	->Name("Wait Backbuffers")
		//	->DependsOn(allocResources)
		//	->SetupFunctor([this](auto graph)
		//		{
		//			WaitBackbuffers();
		//		});

		auto prepareGPUObjects = taskGraph->NewTaskGraph()
			->Name("Prepare GPUObjects")
			->DependsOn(allocResources)
			->SetupFunctor([this](auto graph)
				{
					auto prepareRasterizePSO = graph->NewTaskGraph()
						->Name("Prepare PSO & FrameBuffer & RenderPass")
						->SetupFunctor([this](auto rstPSOGraph)
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
			->SetupFunctor([this](auto graph)
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
			->SetupFunctor([this](auto graph)
				{
					//Record CommandBuffers
					RecordGraph(graph);
				});

		auto recordAndSubmit = taskGraph->NewTaskGraph()
			->Name("Submit Commands")
			->DependsOn(recordGraphs)//执行指令录制完毕
			->DependsOn(writeShaderArgs)//着色器参数写入完毕
			//->DependsOn(waitBackbuffers)//backbuffer等待完毕
			->SetupFunctor([this](auto graph)
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
		m_Passes.resize(rasterizePassCount);
		m_ComputePasses.resize(computePassCount);
		m_TransferPasses.resize(transferPassCount);
	}


	void GPUGraphExecutor::PrepareResources()
	{
		auto& imageManager = m_Graph->GetImageManager();
		auto& bufferManager = m_Graph->GetBufferManager();
		m_BufferManager.ResetAllocator();
		m_ImageManager.ResetAllocator();

		{
			CPUTIMER_SCOPE("Stat Rasterize Resources");
			auto& renderPasses = m_Graph->GetRenderPasses();
			for (auto& renderPass : renderPasses)
			{
				//Rendertargets
				auto& imageHandles = renderPass.GetAttachments();
				for (auto& img : imageHandles)
				{
					if (img.GetType() == ImageHandle::ImageType::Internal)
					{
						m_ImageManager.AllocResourceIndex(img.GetKey(), imageManager.GetDescriptorIndex(img.GetKey()));
					}
					else if (img.GetType() == ImageHandle::ImageType::Backbuffer)
					{
						castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(img.GetWindowHandle());
						window->WaitCurrentFrameBufferIndex();
					}
				}

				//Shader Args
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto argList : renderPass.GetPipelineStates().shaderArgLists)
				{
					shaderArgLists.push_back(argList.second.get());
				}
				auto& drawcallBatchs = renderPass.GetDrawCallBatches();
				for (auto& batch : drawcallBatchs)
				{
					for (auto argList : batch.pipelineStateDesc.shaderArgLists)
					{
						shaderArgLists.push_back(argList.second.get());
					}
					{
						//Index Buffer
						auto& indesBuffer = batch.m_BoundIndexBuffer;
						if (indesBuffer.GetType() == BufferHandle::BufferType::Internal)
						{
							m_BufferManager.AllocResourceIndex(indesBuffer.GetKey(), bufferManager.GetDescriptorIndex(indesBuffer.GetKey()));
						}
						//Vertex Buffers
						for (auto& vertexBufferPair : batch.m_BoundVertexBuffers)
						{
							auto& vertexBuffer = vertexBufferPair.second;
							if (vertexBuffer.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocResourceIndex(vertexBuffer.GetKey(), bufferManager.GetDescriptorIndex(vertexBuffer.GetKey()));
							}
						}
					}

				}
				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
					for (auto& imagePair : shaderArgs.GetImageList())
					{
						auto& imgs = imagePair.second;
						for (auto& img : imgs)
						{
							auto& imgHandle = img.first;
							if (imgHandle.GetType() == ImageHandle::ImageType::Internal)
							{
								m_ImageManager.AllocResourceIndex(imgHandle.GetKey(), imageManager.GetDescriptorIndex(imgHandle.GetKey()));
							}
							else if (imgHandle.GetType() == ImageHandle::ImageType::Backbuffer)
							{
								castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
							}
						}
					}
					for (auto& bufferPair : shaderArgs.GetBufferList())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
							}
						}
					}
				}

				m_ImageManager.NextPass();
				m_BufferManager.NextPass();
			}
		}

		{
			CPUTIMER_SCOPE("Stat Compute Resources");
			auto& computePasses = m_Graph->GetComputePasses();
			for (auto& computePass : computePasses)
			{
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto& arg : computePass.shaderArgLists)
				{
					shaderArgLists.push_back(arg.second.get());
				}
				for (auto& dispatch : computePass.dispatchs)
				{
					for (auto& arg : dispatch.shaderArgLists)
					{
						shaderArgLists.push_back(arg.second.get());
					}
				}

				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
					for (auto& imagePair : shaderArgs.GetImageList())
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
								castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
							}
						}
					}
					for (auto& bufferPair : shaderArgs.GetBufferList())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocPersistantResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
							}
						}
					}
				}
			}
		}
		
		{
			CPUTIMER_SCOPE("Allocate GPU Resources");
			m_ImageManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetImageManager());
			m_BufferManager.AllocateResources(GetVulkanApplication(), m_FrameBoundResourceManager, m_Graph->GetBufferManager());
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
					if (img.GetType() == ImageHandle::ImageType::Internal)
					{
						CPUTIMER_SCOPE("Stat Attachment Tmp Image");
						m_ImageManager.AllocResourceIndex(img.GetKey(), imageManager.GetDescriptorIndex(img.GetKey()));
					}
					else if (img.GetType() == ImageHandle::ImageType::Backbuffer)
					{
						CPUTIMER_SCOPE("Stat Attachment FrameBuffer Image");
						castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(img.GetWindowHandle());
						window->WaitCurrentFrameBufferIndex();
						m_WaitingWindows.insert(window);
					}
				}

				//Shader Args
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto argList : renderPass.GetPipelineStates().shaderArgLists)
				{
					shaderArgLists.push_back(argList.second.get());
				}
				auto& drawcallBatchs = renderPass.GetDrawCallBatches();
				for (auto& batch : drawcallBatchs)
				{
					for (auto argList : batch.pipelineStateDesc.shaderArgLists)
					{
						shaderArgLists.push_back(argList.second.get());
					}
				}
				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
					for (auto& imagePair : shaderArgs.GetImageList())
					{
						auto& imgs = imagePair.second;
						for (auto& img : imgs)
						{
							CPUTIMER_SCOPE("Stat Shader Args Image");
							auto& imgHandle = img.first;
							if (imgHandle.GetType() == ImageHandle::ImageType::Internal)
							{
								m_ImageManager.AllocResourceIndex(imgHandle.GetKey(), imageManager.GetDescriptorIndex(imgHandle.GetKey()));
							}
							else if (imgHandle.GetType() == ImageHandle::ImageType::Backbuffer)
							{
								castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
								m_WaitingWindows.insert(window);
							}
						}
					}
				}

				m_ImageManager.NextPass();
			}
		}

		{
			CPUTIMER_SCOPE("Stat Compute Image Resources");
			auto& computePasses = m_Graph->GetComputePasses();
			for (auto& computePass : computePasses)
			{
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto& arg : computePass.shaderArgLists)
				{
					shaderArgLists.push_back(arg.second.get());
				}
				for (auto& dispatch : computePass.dispatchs)
				{
					for (auto& arg : dispatch.shaderArgLists)
					{
						shaderArgLists.push_back(arg.second.get());
					}
				}

				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
					for (auto& imagePair : shaderArgs.GetImageList())
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
								castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(imgHandle.GetWindowHandle());
								window->WaitCurrentFrameBufferIndex();
								m_WaitingWindows.insert(window);
							}
						}
					}
				}
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
				//Shader Args
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto argList : renderPass.GetPipelineStates().shaderArgLists)
				{
					shaderArgLists.push_back(argList.second.get());
				}
				auto& drawcallBatchs = renderPass.GetDrawCallBatches();
				for (auto& batch : drawcallBatchs)
				{
					for (auto argList : batch.pipelineStateDesc.shaderArgLists)
					{
						shaderArgLists.push_back(argList.second.get());
					}
					{
						//Index Buffer
						auto& indesBuffer = batch.m_BoundIndexBuffer;
						if (indesBuffer.GetType() == BufferHandle::BufferType::Internal)
						{
							m_BufferManager.AllocResourceIndex(indesBuffer.GetKey(), bufferManager.GetDescriptorIndex(indesBuffer.GetKey()));
						}
						//Vertex Buffers
						for (auto& vertexBufferPair : batch.m_BoundVertexBuffers)
						{
							auto& vertexBuffer = vertexBufferPair.second;
							if (vertexBuffer.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocResourceIndex(vertexBuffer.GetKey(), bufferManager.GetDescriptorIndex(vertexBuffer.GetKey()));
							}
						}
					}

				}
				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
	
					for (auto& bufferPair : shaderArgs.GetBufferList())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
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
				castl::deque<ShaderArgList const*> shaderArgLists;
				for (auto& arg : computePass.shaderArgLists)
				{
					shaderArgLists.push_back(arg.second.get());
				}
				for (auto& dispatch : computePass.dispatchs)
				{
					for (auto& arg : dispatch.shaderArgLists)
					{
						shaderArgLists.push_back(arg.second.get());
					}
				}

				while (!shaderArgLists.empty())
				{
					ShaderArgList const& shaderArgs = *shaderArgLists.front();
					shaderArgLists.pop_front();
					for (auto& subArgPairs : shaderArgs.GetSubArgList())
					{
						shaderArgLists.push_back(subArgPairs.second.get());
					}
					
					for (auto& bufferPair : shaderArgs.GetBufferList())
					{
						auto& bufs = bufferPair.second;
						for (auto& buf : bufs)
						{
							if (buf.GetType() == BufferHandle::BufferType::Internal)
							{
								m_BufferManager.AllocPersistantResourceIndex(buf.GetKey(), bufferManager.GetDescriptorIndex(buf.GetKey()));
							}
						}
					}
				}
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
			VKResultCheck(GetDevice().waitForFences(fences, true, castl::numeric_limits<uint64_t>::max()));
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
			castl::shared_ptr<VKGPUTexture> texture = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->EnsureImageView(view);
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
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
			castl::shared_ptr<VKGPUTexture> texture = castl::static_shared_pointer_cast<VKGPUTexture>(handle.GetExternalManagedTexture());
			return texture->GetImage().image;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			castl::shared_ptr<CWindowContext> window = castl::static_shared_pointer_cast<CWindowContext>(handle.GetWindowHandle());
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
			castl::shared_ptr<VKGPUBuffer> buffer = castl::static_shared_pointer_cast<VKGPUBuffer>(handle.GetExternalManagedBuffer());
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
		castl::vector<uint32_t> passToBatchID;
		passToBatchID.resize(graphStages.size());
		m_CommandBufferBatchList.clear();
		m_CommandBufferBatchList.push_back(CommandBatchRange::Create(GetBasePassInfo(0)->m_BarrierCollector.GetQueueFamily(), 0));
		auto lastBatch = &m_CommandBufferBatchList.back();
		for (uint32_t passID = 0; passID < graphStages.size(); ++passID)
		{
			auto pass = GetBasePassInfo(passID);
			uint32_t startCommandID = m_FinalCommandBuffers.size();
			//先执行准备资源的命令
			for (vk::CommandBuffer prepareCmd : pass->m_PrepareShaderArgCommands)
			{
				m_FinalCommandBuffers.push_back(prepareCmd);
			}
			for (vk::CommandBuffer cmd : pass->m_CommandBuffers)
			{
				m_FinalCommandBuffers.push_back(cmd);
			}
			uint32_t lastCommandID = m_FinalCommandBuffers.size() - 1;
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
		if (found != inoutResourceUsageFlagCache.end())
		{
			return found->second;
		}
		return defaultState;
	};

	void GPUGraphExecutor::PrepareVertexBuffersBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, DrawCallBatch const& batch
		, GPUPassBatchInfo const& batchInfo
		, uint32_t passID
	)
	{
		if (batch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
		{
			ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
			UpdateBufferDependency(passID, batch.m_BoundIndexBuffer, usageFlags, inoutBufferUsageFlagCache);
		}
		for (auto bindingPair : batchInfo.m_VertexAttributeBindings)
		{
			auto foundBuffer = batch.m_BoundVertexBuffers.find(bindingPair.first);
			if (foundBuffer != batch.m_BoundVertexBuffers.end())
			{
				ResourceUsageFlags usageFlags = ResourceUsage::eVertexAttribute;
				UpdateBufferDependency(passID, foundBuffer->second, usageFlags, inoutBufferUsageFlagCache);
			}
		}
	}

	//VulkanBarrierCollector& GPUGraphExecutor::GetBarrierCollector(uint32_t passID)
	//{
	//	auto& graphStages = m_Graph->GetGraphStages();
	//	auto& passIndices = m_Graph->GetPassIndices();
	//	auto stage = graphStages[passID];
	//	uint32_t realPassIndex = passIndices[passID];
	//	switch (stage)
	//	{
	//	case GPUGraph::EGraphStageType::eRenderPass:
	//		return m_Passes[realPassIndex].m_BarrierCollector;
	//	case GPUGraph::EGraphStageType::eTransferPass:
	//		return m_TransferPasses[realPassIndex].m_BarrierCollector;
	//	}
	//}

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
		CA_ASSERT(dstInfo != nullptr, "Invalid Dest Pass ID");

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
		CA_ASSERT(dstInfo != nullptr, "Invalid Dest Pass ID");

		auto pDesc = GetTextureHandleDescriptor(imageHandle);

		auto usageStates = GetResourceUsage(inoutImageUsageFlagCache, image, GetHandleInitializeUsage(imageHandle, destPassID, *dstInfo));
		auto newUsageState = MakeNewResourceState(destPassID, dstInfo->GetQueueFamily(), newUsageFlags);

		if (usageStates.usage != newUsageState.usage)
		{
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

	void GPUGraphExecutor::PrepareShaderArgsResourceBarriers(VulkanBarrierCollector& inoutBarrierCollector
		, castl::unordered_map<vk::Image, ResourceState>& inoutResourceUsageFlagCache
		, castl::unordered_map<vk::Buffer, ResourceState>& inoutBufferUsageFlagCache
		, ShaderArgList const* shaderArgList
		, uint32_t passID)
	{
		castl::deque<ShaderArgList const*> shaderArgLists = { shaderArgList };
		while (!shaderArgLists.empty())
		{
			ShaderArgList const& shaderArgs = *shaderArgLists.front();
			shaderArgLists.pop_front();
			for (auto& subArgPairs : shaderArgs.GetSubArgList())
			{
				shaderArgLists.push_back(subArgPairs.second.get());
			}
			for (auto& imagePair : shaderArgs.GetImageList())
			{
				auto& imgs = imagePair.second;
				for (auto& img : imgs)
				{
					auto& imgHandle = img.first;
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					UpdateImageDependency(passID, imgHandle, usageFlags, inoutResourceUsageFlagCache);
				}
			}
			for (auto& bufferPair : shaderArgs.GetBufferList())
			{
				auto& bufs = bufferPair.second;
				for (auto& buf : bufs)
				{
					ResourceUsageFlags usageFlags = ResourceUsage::eVertexRead | ResourceUsage::eFragmentRead;
					UpdateBufferDependency(passID, buf, usageFlags, inoutBufferUsageFlagCache);
				}
			}
		}
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

	void GPUGraphExecutor::PrepareFrameBufferAndPSOs(thread_management::CTaskGraph* taskGraph)
	{
		//FBO, RenderPass And PSO For Rasterization Passes
		auto& renderPasses = m_Graph->GetRenderPasses();
		CA_ASSERT(renderPasses.size() == m_Passes.size(), "Rasterize Pass Data Count InCompatible!!");
		for (uint32_t passID = 0; passID < renderPasses.size(); ++passID)
		{
			taskGraph->NewTaskGraph()
				->Name("Prepare Render Pass And FBO")
				->SetupFunctor([this, passID, &renderPasses](auto setupGraph)
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
									passInfo.m_ClearValues.resize(attachments.size());
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
										passInfo.m_ClearValues[i] = AttachmentClearValueTranslate(
											attachmentConfig.clearValue
											, pDesc->format);
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
					
					passInfo.m_Batches.resize(drawcallBatchs.size());

					auto& passLevelPsoDesc = renderPass.GetPipelineStates();

					setupGraph->NewTaskParallelFor()
						->Name("Prepare Batch PSOs")
						->DependsOn(createRenderPassTask)
						->JobCount(drawcallBatchs.size())
						->Functor([&](auto batchID)
							{
								auto& batch = drawcallBatchs[batchID];
								{
									CPUTIMER_SCOPE("Get Or Create PSO");
									GPUPassBatchInfo& newBatchInfo = passInfo.m_Batches[batchID];

									auto& batchLevelPsoDesc = batch.pipelineStateDesc;
									auto resolvedPSODesc = PipelineDescData::CombindDescData(passLevelPsoDesc, batchLevelPsoDesc);
									auto vertShader = GetGPUObjectManager().GetShaderModuleCache().GetOrCreate(resolvedPSODesc.m_ShaderSet->GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType::eSpirV, ECompileShaderType::eVert));
									auto fragShader = GetGPUObjectManager().GetShaderModuleCache().GetOrCreate(resolvedPSODesc.m_ShaderSet->GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType::eSpirV, ECompileShaderType::eFrag));

									//Shader Binding Holder
									//Dont Need To Make Instance here, We Only Need Descriptor Set Layouts
									newBatchInfo.m_ShaderBindingInstance.InitShaderBindingLayouts(GetVulkanApplication(), resolvedPSODesc.m_ShaderSet->GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType::eSpirV));
									newBatchInfo.m_ShaderBindingInstance.InitShaderBindingSets(m_FrameBoundResourceManager);

									//auto& vertexInputBindings = resolvedPSODesc.m_VertexInputBindings;
									auto& vertexAttributes = resolvedPSODesc.m_ShaderSet->GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType::eSpirV).m_VertexAttributes;
									CVertexInputDescriptor vertexInputDesc = MakeVertexInputDescriptorsNew(
										vertexAttributes
										, resolvedPSODesc.m_InputAssemblyStates.Get()
										, batch.m_BoundVertexBuffers
										, newBatchInfo.m_VertexAttributeBindings);

									CPipelineObjectDescriptor psoDescObj;
									psoDescObj.vertexInputs = vertexInputDesc;
									psoDescObj.pso = resolvedPSODesc.m_PipelineStates.Get();
									psoDescObj.shaderState = { vertShader, fragShader };
									psoDescObj.renderPassObject = passInfo.m_RenderPassObject;
									psoDescObj.descriptorSetLayouts = newBatchInfo.m_ShaderBindingInstance.m_DescriptorSetsLayouts;

									newBatchInfo.m_PSO = GetGPUObjectManager().GetPipelineCache().GetOrCreate(psoDescObj);
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
			for (auto& dispatch : computePass.dispatchs)
			{
				GPUComputePassInfo::ComputeDispatchInfo newDispatchInfo{};
				//Dont Need To Make Instance here, We Only Need Descriptor Set Layouts
				newDispatchInfo.m_ShaderBindingInstance.InitShaderBindingLayouts(GetVulkanApplication()
					, dispatch.shader->GetShaderReflectionData(ShaderCompilerSlang::EShaderTargetType::eSpirV));
				newDispatchInfo.m_ShaderBindingInstance.InitShaderBindingSets(m_FrameBoundResourceManager);

				auto comp = GetGPUObjectManager()
					.GetShaderModuleCache()
					.GetOrCreate(dispatch.shader
						->GetShaderSourceInfo(ShaderCompilerSlang::EShaderTargetType::eSpirV
							, ECompileShaderType::eComp
							, dispatch.kernelName));
				ComputePipelineDescriptor pipelineDesc{};
				pipelineDesc.computeShader = comp;
				pipelineDesc.descriptorSetLayouts = newDispatchInfo.m_ShaderBindingInstance.m_DescriptorSetsLayouts;
				newDispatchInfo.m_ComputePipeline = GetGPUObjectManager()
					.GetComputePipelineCache().GetOrCreate(pipelineDesc);

				newComputePass.m_DispatchInfos.push_back(newDispatchInfo);
			}
			//m_ComputePasses.push_back(newComputePass);
		}
	}



	void GPUGraphExecutor::WriteDescriptorSets(thread_management::CTaskGraph* taskGraph)
	{
		////Write Descriptors Of Render Passes
		{
			auto& renderPasses = m_Graph->GetRenderPasses();
			CA_ASSERT(renderPasses.size() == m_Passes.size(), "InCompatible Render Pass Sizes");
			for (size_t passID = 0; passID < renderPasses.size(); ++passID)
			{
				taskGraph->NewTaskGraph()
					->Name("Write Rasterize Pass Descriptors")
					->SetupFunctor([this, passID, &renderPasses](auto passGraph)
					{
						auto& renderPass = renderPasses[passID];
						auto& drawcallBatchs = renderPass.GetDrawCallBatches();
						auto& renderPassData = m_Passes[passID];
						auto& drawcallBatchData = renderPassData.m_Batches;
						CA_ASSERT(drawcallBatchs.size() == drawcallBatchData.size(), "InCompatible Render Pass Batch Sizes");
						auto& passLevelPsoDesc = renderPass.GetPipelineStates();
						renderPassData.m_PrepareShaderArgCommands.resize(drawcallBatchs.size());

						passGraph->NewTaskParallelFor()
							->Name("Rasterize Batch ShaderArgs")
							->JobCount(drawcallBatchs.size())
							->Functor([&](uint32_t batchID)
							{
								{
									CPUTIMER_SCOPE("Rasterize Batch ShaderArgs");
									auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
									vk::CommandBuffer writeConstantsCommand = cmdPool->AllocCommand(QueueType::eGraphics, "Write Descriptors");
									auto& batch = drawcallBatchs[batchID];
									GPUPassBatchInfo& newBatchInfo = drawcallBatchData[batchID];
									auto& batchLevelPsoDesc = batch.pipelineStateDesc;
									auto resolvedPSODesc = PipelineDescData::CombindDescData(passLevelPsoDesc, batchLevelPsoDesc);
									newBatchInfo.m_ShaderBindingInstance.FillShaderData(GetVulkanApplication(), *this, m_FrameBoundResourceManager, writeConstantsCommand, resolvedPSODesc.shaderArgLists);
									writeConstantsCommand.end();
									renderPassData.m_PrepareShaderArgCommands[batchID] = writeConstantsCommand;
								}
							});

						//for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
						//{
						//	CPUTIMER_SCOPE("Rasterize Batch ShaderArgs");
						//	auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
						//	vk::CommandBuffer writeConstantsCommand = cmdPool->AllocCommand(QueueType::eGraphics, "Write Descriptors");
						//	auto& batch = drawcallBatchs[batchID];
						//	GPUPassBatchInfo& newBatchInfo = drawcallBatchData[batchID];
						//	auto& batchLevelPsoDesc = batch.pipelineStateDesc;
						//	auto resolvedPSODesc = PipelineDescData::CombindDescData(passLevelPsoDesc, batchLevelPsoDesc);
						//	newBatchInfo.m_ShaderBindingInstance.FillShaderData(GetVulkanApplication(), *this, m_FrameBoundResourceManager, writeConstantsCommand, resolvedPSODesc.shaderArgLists);
						//	writeConstantsCommand.end();
						//	renderPassData.m_PrepareShaderArgCommands[batchID] = writeConstantsCommand;
						//}
					});
			}
		}
		////Write Descriptors Of Compute Passes
		{
			auto& computePasses = m_Graph->GetComputePasses();
			CA_ASSERT(computePasses.size() == m_ComputePasses.size(), "InCompatible Compute Pass Sizes");

			for (size_t passID = 0; passID < computePasses.size(); ++passID)
			{
				taskGraph->NewTask()
					->Name("Write Compute Pass Descriptors")
					->Functor([this, passID, &computePasses]()
					{
						castl::vector <castl::pair<castl::string, castl::shared_ptr<ShaderArgList>>> shaderArgs;
						auto& computePass = computePasses[passID];
						auto& computePassData = m_ComputePasses[passID];
						auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
						vk::CommandBuffer writeConstantsCommand = cmdPool->AllocCommand(QueueType::eCompute, "Write Descriptors");
						shaderArgs.resize(computePass.shaderArgLists.size());
						castl::copy(computePass.shaderArgLists.begin(), computePass.shaderArgLists.end(), shaderArgs.begin());
						for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
						{
							CPUTIMER_SCOPE("Compute Dispatch ShaderArgs");
							auto& dispatchData = computePass.dispatchs[dispatchID];
							auto& dispatchData1 = computePassData.m_DispatchInfos[dispatchID];
							shaderArgs.resize(computePass.shaderArgLists.size() + dispatchData.shaderArgLists.size());
							for (size_t copyID = 0; copyID < dispatchData.shaderArgLists.size(); ++copyID)
							{
								shaderArgs[computePass.shaderArgLists.size() + copyID] = dispatchData.shaderArgLists[copyID];
							}
							dispatchData1.m_ShaderBindingInstance.FillShaderData(GetVulkanApplication(), *this, m_FrameBoundResourceManager, writeConstantsCommand, shaderArgs);
						}
						writeConstantsCommand.end();
						computePassData.m_PrepareShaderArgCommands.push_back(writeConstantsCommand);
					});
			}
		}
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
						PrepareShaderBindingResourceBarriers(renderPassData.m_BarrierCollector
							, imageUsageFlagCache
							, bufferUsageFlagCache
							, batchData.m_ShaderBindingInstance
							, passID);

						for (auto& bufferSet : batchData.m_ShaderBindingInstance.m_UniformBuffers)
						{
							for (auto& bufferObject : bufferSet.second)
							{
								renderPassData.m_BarrierCollector.PushBufferBarrier(bufferObject.buffer, ResourceUsage::eTransferDest, ResourceUsage::eFragmentRead | ResourceUsage::eVertexRead);
							}
						}
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

					//TODO: 重写这个函数
					//PrepareShaderArgsResourceBarriers(computePassData.m_BarrierCollector, imageUsageFlagCache, bufferUsageFlagCache, batch.shaderArgs.get(), passID);
					PrepareShaderBindingResourceBarriers(computePassData.m_BarrierCollector
						, imageUsageFlagCache
						, bufferUsageFlagCache
						, dispatchData1.m_ShaderBindingInstance
						, passID);

					for (auto& bufferSet : dispatchData1.m_ShaderBindingInstance.m_UniformBuffers)
					{
						for (auto& bufferObject : bufferSet.second)
						{
							computePassData.m_BarrierCollector.PushBufferBarrier(bufferObject.buffer, ResourceUsage::eTransferDest, ResourceUsage::eComputeRead);
						}
					}
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

	void GPUGraphExecutor::RecordGraph(thread_management::CTaskGraph* taskGraph)
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
				->SetupFunctor([&](auto extResourceGraph)
					{
						for (auto& pair : m_ExternalResourceReleasingBarriers.queueFamilyToBarrierCollector)
						{
							extResourceGraph->NewTask()
								->Name("Prepare External Resource Release Barriers")
								->Functor([&]()
								{
									auto& releaser = pair.second;
									auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
									vk::CommandBuffer externalResourceReleaseBarriers = cmdPool->AllocCommand(pair.first, "Data Transfer");
									releaser.barrierCollector.ExecuteReleaseBarrier(externalResourceReleaseBarriers);
									externalResourceReleaseBarriers.end();
									releaser.commandBuffer = externalResourceReleaseBarriers;
								});
						}
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
					auto& passData = m_Passes[realPassID];

					auto& drawcallBatchs = renderPass.GetDrawCallBatches();
					auto& batchDatas = passData.m_Batches;
					CA_ASSERT(drawcallBatchs.size() == batchDatas.size(), "Batch Count Mismatch");

					auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
					vk::CommandBuffer renderPassCommandBuffer = cmdPool->AllocCommand(QueueType::eGraphics, "Render Pass");

					passData.m_BarrierCollector.ExecuteBarrier(renderPassCommandBuffer);

					if (passData.ValidPassData())
					{
						renderPassCommandBuffer.beginRenderPass(
							vk::RenderPassBeginInfo{
								passData.m_RenderPassObject->GetRenderPass()
								, passData.m_FrameBufferObject->GetFramebuffer()
								, vk::Rect2D{{0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }}
								, passData.m_ClearValues
							}
						, vk::SubpassContents::eInline);

						renderPassCommandBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, (float)passData.m_FrameBufferObject->GetWidth(), (float)passData.m_FrameBufferObject->GetHeight(), 0.0f, 1.0f) });
						renderPassCommandBuffer.setScissor(0, { vk::Rect2D({0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }) });

						CommandList_Impl commandList{ renderPassCommandBuffer };

						for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
						{
							auto& batchData = batchDatas[batchID];
							auto& drawcallBatch = drawcallBatchs[batchID];

							renderPassCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipeline());

							renderPassCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipelineLayout(), 0, batchData.m_ShaderBindingInstance.m_DescriptorSets, {});

							if (drawcallBatch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
							{
								auto buffer = GetBufferHandleBufferObject(drawcallBatch.m_BoundIndexBuffer);
								renderPassCommandBuffer.bindIndexBuffer(buffer, drawcallBatch.m_IndexBufferOffset, EIndexBufferTypeTranslate(drawcallBatch.m_IndexBufferType));
							}

							for (auto& attributePair : batchData.m_VertexAttributeBindings)
							{
								auto foundVertexBuffer = drawcallBatch.m_BoundVertexBuffers.find(attributePair.first);
								if (foundVertexBuffer != drawcallBatch.m_BoundVertexBuffers.end())
								{
									auto buffer = GetBufferHandleBufferObject(foundVertexBuffer->second);
									renderPassCommandBuffer.bindVertexBuffers(attributePair.second.bindingIndex, { buffer }, { 0 });
								}
							}

							for (auto& drawFunc : drawcallBatch.m_DrawCommands)
							{
								drawFunc(commandList);
							}
						}
						renderPassCommandBuffer.endRenderPass();
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
						computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipeline());
						computeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipelineLayout(), 0, dispatchData1.m_ShaderBindingInstance.m_DescriptorSets, {});
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
								auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(uploadRef.dataSize, EBufferUsage::eDataSrc, "Staging Buffer " + bufferHandle.GetName());
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
		//for (uint32_t passID = 0; passID < graphStages.size(); ++passID)
		//{
		//	GPUGraph::EGraphStageType stage = graphStages[passID];
		//	uint32_t realPassID = passIndices[passID];
		//	switch (stage)
		//	{
		//	case GPUGraph::EGraphStageType::eRenderPass:
		//	{
		//		auto& renderPass = renderPasses[realPassID];
		//		auto& passData = m_Passes[realPassID];

		//		auto& drawcallBatchs = renderPass.GetDrawCallBatches();
		//		auto& batchDatas = passData.m_Batches;
		//		CA_ASSERT(drawcallBatchs.size() == batchDatas.size(), "Batch Count Mismatch");

		//		auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
		//		vk::CommandBuffer renderPassCommandBuffer = cmdPool->AllocCommand(QueueType::eGraphics, "Render Pass");

		//		passData.m_BarrierCollector.ExecuteBarrier(renderPassCommandBuffer);

		//		if (passData.ValidPassData())
		//		{
		//			renderPassCommandBuffer.beginRenderPass(
		//				vk::RenderPassBeginInfo{
		//					passData.m_RenderPassObject->GetRenderPass()
		//					, passData.m_FrameBufferObject->GetFramebuffer()
		//					, vk::Rect2D{{0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }}
		//					, passData.m_ClearValues
		//				}
		//			, vk::SubpassContents::eInline);

		//			renderPassCommandBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, (float)passData.m_FrameBufferObject->GetWidth(), (float)passData.m_FrameBufferObject->GetHeight(), 0.0f, 1.0f) });
		//			renderPassCommandBuffer.setScissor(0, { vk::Rect2D({0, 0}, { passData.m_FrameBufferObject->GetWidth(), passData.m_FrameBufferObject->GetHeight() }) });

		//			CommandList_Impl commandList{ renderPassCommandBuffer };

		//			for (uint32_t batchID = 0; batchID < drawcallBatchs.size(); ++batchID)
		//			{
		//				auto& batchData = batchDatas[batchID];
		//				auto& drawcallBatch = drawcallBatchs[batchID];

		//				renderPassCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipeline());

		//				renderPassCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, batchData.m_PSO->GetPipelineLayout(), 0, batchData.m_ShaderBindingInstance.m_DescriptorSets, {});

		//				if (drawcallBatch.m_BoundIndexBuffer.GetType() != BufferHandle::BufferType::Invalid)
		//				{
		//					auto buffer = GetBufferHandleBufferObject(drawcallBatch.m_BoundIndexBuffer);
		//					renderPassCommandBuffer.bindIndexBuffer(buffer, drawcallBatch.m_IndexBufferOffset, EIndexBufferTypeTranslate(drawcallBatch.m_IndexBufferType));
		//				}

		//				for (auto& attributePair : batchData.m_VertexAttributeBindings)
		//				{
		//					auto foundVertexBuffer = drawcallBatch.m_BoundVertexBuffers.find(attributePair.first);
		//					if (foundVertexBuffer != drawcallBatch.m_BoundVertexBuffers.end())
		//					{
		//						auto buffer = GetBufferHandleBufferObject(foundVertexBuffer->second);
		//						renderPassCommandBuffer.bindVertexBuffers(attributePair.second.bindingIndex, { buffer }, { 0 });
		//					}
		//				}

		//				for (auto& drawFunc : drawcallBatch.m_DrawCommands)
		//				{
		//					drawFunc(commandList);
		//				}
		//			}
		//			renderPassCommandBuffer.endRenderPass();
		//		}

		//		passData.m_BarrierCollector.ExecuteReleaseBarrier(renderPassCommandBuffer);
		//		renderPassCommandBuffer.end();
		//		passData.m_CommandBuffers.push_back(renderPassCommandBuffer);
		//		break;
		//	}
		//	case GPUGraph::EGraphStageType::eComputePass:
		//	{
		//		auto& computePass = computePasses[realPassID];
		//		auto& computePassData = m_ComputePasses[realPassID];
		//		auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
		//		vk::CommandBuffer computeCommandBuffer = cmdPool->AllocCommand(QueueType::eCompute, "Compute Pass");
		//		computePassData.m_BarrierCollector.ExecuteBarrier(computeCommandBuffer);
		//		for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
		//		{
		//			auto& dispatchData = computePass.dispatchs[dispatchID];
		//			auto& dispatchData1 = computePassData.m_DispatchInfos[dispatchID];
		//			computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipeline());
		//			computeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, dispatchData1.m_ComputePipeline->GetPipelineLayout(), 0, dispatchData1.m_ShaderBindingInstance.m_DescriptorSets, {});
		//			computeCommandBuffer.dispatch(dispatchData.x, dispatchData.y, dispatchData.z);
		//		}
		//		computePassData.m_BarrierCollector.ExecuteReleaseBarrier(computeCommandBuffer);
		//		computeCommandBuffer.end();
		//		computePassData.m_CommandBuffers.push_back(computeCommandBuffer);
		//		break;
		//	}
		//	case GPUGraph::EGraphStageType::eTransferPass:
		//	{
		//		auto& transfersInfo = dataTransfers[realPassID];
		//		GPUTransferInfo& transfersData = m_TransferPasses[realPassID];

		//		auto cmdPool = m_FrameBoundResourceManager->commandBufferThreadPool.AquireCommandBufferPool();
		//		vk::CommandBuffer dataTransferCommandBuffer = cmdPool->AllocCommand(QueueType::eTransfer, "Data Transfer");
		//		transfersData.m_BarrierCollector.ExecuteBarrier(dataTransferCommandBuffer);

		//		for (auto& bufferUpload : transfersInfo.m_BufferDataUploads)
		//		{
		//			auto [bufferHandle, uploadRef] = bufferUpload;
		//			if (bufferHandle.GetType() != BufferHandle::BufferType::Invalid)
		//			{
		//				auto buffer = GetBufferHandleBufferObject(bufferHandle);
		//				if (buffer != vk::Buffer{ nullptr })
		//				{
		//					auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(uploadRef.dataSize, EBufferUsage::eDataSrc, "Staging Buffer " + bufferHandle.GetName());
		//					{
		//						auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
		//						memcpy(mappedSrcBuffer.mappedMemory, uploadData.GetPtr(uploadRef.dataIndex), uploadRef.dataSize);
		//					}
		//					dataTransferCommandBuffer.copyBuffer(srcBuffer.buffer, buffer, vk::BufferCopy(0, uploadRef.dstOffset, uploadRef.dataSize));
		//				}
		//			}
		//		}

		//		for (auto& imageUpload : transfersInfo.m_ImageDataUploads)
		//		{
		//			auto [imageHandle, uploadRef] = imageUpload;
		//			if (ValidImageHandle(imageHandle))
		//			{
		//				auto image = GetTextureHandleImageObject(imageHandle);
		//				auto pDesc = GetTextureHandleDescriptor(imageHandle);
		//				if (image != vk::Image{ nullptr })
		//				{
		//					auto srcBuffer = m_FrameBoundResourceManager->CreateStagingBuffer(uploadRef.dataSize, EBufferUsage::eDataSrc);
		//					{
		//						auto mappedSrcBuffer = m_FrameBoundResourceManager->memoryManager.ScopedMapMemory(srcBuffer.allocation);
		//						memcpy(mappedSrcBuffer.mappedMemory, uploadData.GetPtr(uploadRef.dataIndex), uploadRef.dataSize);
		//					}

		//					//TODO: offset is not used here for now
		//					std::array<vk::BufferImageCopy, 1> bufferImageCopy = { GPUTextureDescriptorToBufferImageCopy(*pDesc) };
		//					dataTransferCommandBuffer.copyBufferToImage(srcBuffer.buffer
		//						, image
		//						, vk::ImageLayout::eTransferDstOptimal
		//						, bufferImageCopy);
		//				}
		//			}
		//		}

		//		transfersData.m_BarrierCollector.ExecuteReleaseBarrier(dataTransferCommandBuffer);
		//		dataTransferCommandBuffer.end();
		//		transfersData.m_CommandBuffers.push_back(dataTransferCommandBuffer);
		//		break;
		//	}
		//	}
		//}
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