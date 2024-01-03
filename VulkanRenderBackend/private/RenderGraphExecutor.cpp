#include "pch.h"
#include "RenderGraphExecutor.h"
#include "VulkanApplication.h"
#include "InterfaceTranslator.h"
#include "CommandList_Impl.h"
#include "VulkanPipelineObject.h"
#include "VulkanBarrierCollector.h"

namespace graphics_backend
{
	RenderGraphExecutor::RenderGraphExecutor(CVulkanApplication& owner) : BaseApplicationSubobject(owner)
		, m_RenderGraph(nullptr)
	{
	}

	void RenderGraphExecutor::Create(std::shared_ptr<CRenderGraph> inRenderGraph)
	{
		m_RenderGraph = inRenderGraph;
	}
	void RenderGraphExecutor::Compile(CTaskGraph* taskGraph)
	{
		if (CompileIssued())
			return;
		m_CompiledFrame = GetVulkanApplication().GetSubmitCounterContext().GetCurrentFrameID();

		auto creationTask = taskGraph->NewTask();
		creationTask->Name("Initialize RenderGraph");
		uint32_t nodeCount = m_RenderGraph->GetRenderNodeCount();
		creationTask->Functor([this, nodeCount]()
		{
			m_RenderPasses.clear();
			m_RenderPasses.reserve(nodeCount);
			for (uint32_t itr_node = 0; itr_node < nodeCount; ++itr_node)
			{
				auto& renderPass = m_RenderGraph->GetRenderPass(itr_node);
				m_RenderPasses.emplace_back(*this, *m_RenderGraph, renderPass);
			}
		});

		//分配ShaderBindingSets
		auto allocateShaderBindingSetsGraph = taskGraph->NewTaskGraph()
			->Name("Allocate Shader Binding Sets");
		AllocateShaderBindingSets(allocateShaderBindingSetsGraph);
		
		//编译每个RenderPass(RenderPass, PSO)
		auto compileGraph = taskGraph->NewTaskGraph()
			->Name("Compile RenderPasses Graph")
			->DependsOn(creationTask)
			->DependsOn(allocateShaderBindingSetsGraph);
		{
			for (uint32_t passId = 0; passId < nodeCount; ++passId)
			{
				compileGraph->NewTaskGraph()
					->Name("Compile RenderPass " + std::to_string(passId))
					->SetupFunctor([this, passId](CTaskGraph* thisGraph)
						{
							m_RenderPasses[passId].Compile(thisGraph);
						});
			}
		}
	}

	void RenderGraphExecutor::Run(CTaskGraph* taskGraph)
	{
		auto compileTaskGraph = taskGraph->NewTaskGraph()
			->Name("Compile Render Graph")
			->SetupFunctor([this](CTaskGraph* thisGraph)
			{
				Compile(thisGraph);
			});
		auto executionTaskGraph = taskGraph->NewTaskGraph()
			->Name("Execute Render Graph")
			->DependsOn(compileTaskGraph)
			->SetupFunctor([this](CTaskGraph* thisGraph)
			{
				Execute(thisGraph);
			});
	}

	bool RenderGraphExecutor::CompileDone() const
	{
		if (m_CompiledFrame == INVALID_FRAMEID)
			return false;
		auto& frameContext = GetFrameCountContext();
		return frameContext.GetCurrentFrameID() >= m_CompiledFrame;
	}

	bool RenderGraphExecutor::CompileIssued() const
	{
		return m_CompiledFrame != INVALID_FRAMEID;
	}

	void RenderGraphExecutor::CollectCommands(std::vector<vk::CommandBuffer>& inoutCommands) const
	{
		inoutCommands.resize(inoutCommands.size() + m_PendingGraphicsCommandBuffers.size());
		std::copy(m_PendingGraphicsCommandBuffers.begin()
			, m_PendingGraphicsCommandBuffers.end()
			, inoutCommands.end() - m_PendingGraphicsCommandBuffers.size());
	}

	InternalGPUTextures const& RenderGraphExecutor::GetLocalTexture(TIndex textureHandle) const
	{
		auto& internalTexture = m_RenderGraph->GetGPUTextureInternalData(textureHandle);
		int32_t allocationIndex = m_TextureAllocationIndex[textureHandle];
		return m_Images[internalTexture.GetDescID()][allocationIndex];
	}

	VulkanBufferHandle const& RenderGraphExecutor::GetLocalBuffer(TIndex bufferHandle) const
	{
		return m_GPUBufferObjects[m_GPUBufferOffset + bufferHandle];
	}

	VulkanBufferHandle const& RenderGraphExecutor::GetLocalConstantSetBuffer(TIndex constantSetHandle) const
	{
		return m_GPUBufferObjects[m_ConstantBufferOffset + constantSetHandle];
	}

	ShaderDescriptorSetHandle const& RenderGraphExecutor::GetLocalDescriptorSet(TIndex setHandle) const
	{
		return m_DescriptorSets[setHandle];
	}

	void RenderGraphExecutor::Execute(CTaskGraph* taskGraph)
	{
		if (!CompileDone())
			return;

		uint32_t nodeCount = m_RenderGraph->GetRenderNodeCount();

		//处理每个RenderPass的ResourceBarrier
		//TODO 收集临时资源
		auto resolvingResourcesTask = taskGraph->NewTask()
			->Name("Resolve Resource Usages")
			->Functor([this, nodeCount]()
				{
					m_TextureHandleUsageStates.clear();
					m_BufferHandleUsageStates.clear();
					std::vector<TextureHandleLifetimeInfo> textureHandleLifetimes;
					textureHandleLifetimes.resize(m_RenderGraph->GetTextureHandleCount());
					std::fill(textureHandleLifetimes.begin(), textureHandleLifetimes.end(), TextureHandleLifetimeInfo{});
					for (uint32_t itr_node = 0; itr_node < nodeCount; ++itr_node)
					{
						m_RenderPasses[itr_node].ResolveTextureHandleUsages(m_TextureHandleUsageStates);
						m_RenderPasses[itr_node].ResolveBufferHandleUsages(m_BufferHandleUsageStates);
						m_RenderPasses[itr_node].UpdateTextureLifetimes(itr_node, textureHandleLifetimes);
					}

					uint32_t textureDescTypeCount = m_RenderGraph->GetTextureTypesDescriptorCount();

					std::vector<std::pair<std::vector<TIndex>, std::vector<TIndex>>> textureAllocationRecorder;
					//textures will be released at next node stage, so we have nodeCount + 1 stages
					textureAllocationRecorder.resize(nodeCount + 1);

					for (TIndex textureHandleID = 0; textureHandleID < textureHandleLifetimes.size(); ++textureHandleID)
					{
						TextureHandleLifetimeInfo& lifetimeInfo = textureHandleLifetimes[textureHandleID];
						auto const& handleInfo = m_RenderGraph->GetGPUTextureInternalData(textureHandleID);
						if (!handleInfo.IsExternalTexture())
						{
							textureAllocationRecorder[lifetimeInfo.beginNodeID].first.push_back(textureHandleID);
							textureAllocationRecorder[lifetimeInfo.endNodeID + 1].second.push_back(textureHandleID);
						}
					}

					m_TextureAllocationIndex.clear();
					m_TextureAllocationIndex.resize(textureHandleLifetimes.size());
					std::fill(m_TextureAllocationIndex.begin(), m_TextureAllocationIndex.end(), -1);

					std::vector<std::pair<int32_t, std::deque<int32_t>>> textureAllocationQueue;
					textureAllocationQueue.resize(textureDescTypeCount);

					auto allocateIndexFunc = [&textureAllocationQueue](TIndex descIndex)
						{
							int32_t result = -1;
							auto& allocationState = textureAllocationQueue[descIndex];
							if (allocationState.second.empty())
							{
								result = allocationState.first;
								++allocationState.first;
							}
							else
							{
								result = allocationState.second.front();
								allocationState.second.pop_front();
							}
							return result;
						};

					auto releaseIndexFunc = [&textureAllocationQueue](TIndex descIndex, int32_t index)
						{
							auto& allocationState = textureAllocationQueue[descIndex];
							allocationState.second.push_back(index);
						};


					for (auto& recorderPass : textureAllocationRecorder)
					{
						for (TIndex handleID : recorderPass.second)
						{
							auto const& handleInfo = m_RenderGraph->GetGPUTextureInternalData(handleID);
							releaseIndexFunc(handleInfo.GetDescID(), m_TextureAllocationIndex[handleID]);
						}

						for (TIndex handleID : recorderPass.first)
						{
							auto const& handleInfo = m_RenderGraph->GetGPUTextureInternalData(handleID);
							m_TextureAllocationIndex[handleID] = allocateIndexFunc(handleInfo.GetDescID());
						}
					}

					m_Images.resize(textureDescTypeCount);
					for (uint32_t itr_type = 0; itr_type < textureDescTypeCount; ++itr_type)
					{
						auto& desc = m_RenderGraph->GetTextureDescriptor(itr_type);
						auto& images = m_Images[itr_type];
						images.resize(textureAllocationQueue[itr_type].first);
						bool depthStencil = IsDepthStencilFormat(desc.format);
						bool depthOnly = IsDepthOnlyFormat(desc.format);
						bool hasStencil = depthStencil && !depthOnly;

						for (auto& image : images)
						{
							image.m_ImageObject = GetMemoryManager().AllocateImage(desc, EMemoryType::GPU, EMemoryLifetime::FrameBound);
							image.m_ImageView = GetVulkanApplication().CreateDefaultImageView(desc, image.m_ImageObject->GetImage(), depthOnly, hasStencil);
						}
					}
				});

		auto prepareGPUBuffersGraph = taskGraph->NewTaskGraph()
			->Name("Prepare GPU Buffers");
		AllocateGPUBuffers(prepareGPUBuffersGraph);

		auto writeShaderBindingSetsGraph = taskGraph->NewTaskGraph()
			->Name("Write Shader Binding Sets")
			->DependsOn(resolvingResourcesTask)
			->DependsOn(prepareGPUBuffersGraph);
		WriteShaderBindingSets(writeShaderBindingSetsGraph);

		//Prepare Per-RenderPass Framebuffer Objects and CommandBuffers
		auto recordCmdTaskGraph = taskGraph->NewTaskGraph()
			->DependsOn(writeShaderBindingSetsGraph)
			->Name("Record RenderPass Cmds Graph")
			->SetupFunctor([this, nodeCount](CTaskGraph* thisGraph)
			{
				for (uint32_t passId = 0; passId < nodeCount; ++passId)
				{
					auto setupFrameBufferTask = thisGraph->NewTask()
						->Functor([this, passId]()
							{
								m_RenderPasses[passId].SetupFrameBuffer();
							});
					auto recordCmdGraph = thisGraph->NewTaskGraph()
						->Name("Record RenderPass " + std::to_string(passId) + " Cmds Graph")
						->DependsOn(setupFrameBufferTask)
						->SetupFunctor([this, passId](CTaskGraph* thisGraph)
							{
								m_RenderPasses[passId].PrepareCommandBuffers(thisGraph);
							});
				}
			});

		//Collect All Generated Primary CommandBuffers Into A List
		auto collectCommandsTask = taskGraph->NewTask()
			->Name("Collect RenderPass Commands")
			->DependsOn(recordCmdTaskGraph)
			->Functor([this]()
			{
				uint32_t nodeCount = m_RenderGraph->GetRenderNodeCount();
				m_PendingGraphicsCommandBuffers.clear();
				for (uint32_t i = 0; i < nodeCount; ++i)
				{
					m_RenderPasses[i].AppendCommandBuffers(m_PendingGraphicsCommandBuffers);
				}
			});

		//Mark Final Usage Of Swapchain Images
		auto markWindowsTargetTask = taskGraph->NewTask()
			->Name("Mark Swapchain Target Usages")
			->DependsOn(resolvingResourcesTask)
			->Functor([this]()
				{
					auto& windowToIDs = m_RenderGraph->WindowHandleToTextureIndexMap();

					for (auto& pair : windowToIDs)
					{
						CWindowContext* windowTarget = static_cast<CWindowContext*>(pair.first);
						TIndex windowIndex = pair.second;
						auto state = m_TextureHandleUsageStates.find(windowIndex);
						if (state != m_TextureHandleUsageStates.end())
						{
							windowTarget->MarkUsages(state->second);
						}
					}
				});
	}

	void RenderGraphExecutor::AllocateGPUBuffers(CTaskGraph* taskGrap)
	{
		uint32_t bufferCount = m_RenderGraph->GetGPUBufferHandleCount();
		uint32_t constantBufferCount = m_RenderGraph->GetConstantSetCount();
		uint32_t totalBufferCount = bufferCount + constantBufferCount;
		m_ConstantBufferOffset = 0;
		m_GPUBufferOffset = constantBufferCount;
		if(totalBufferCount == 0)
			return;
		auto initializeTask = taskGrap->NewTask()
			->Name("Initialize Internal GPU Buffers")
			->Functor([this, totalBufferCount]()
				{
					m_GPUBufferObjects.resize(totalBufferCount);
				});
		taskGrap->NewTaskParallelFor()
			->Name("Allocate Constant Buffers")
			->DependsOn(initializeTask)
			->JobCount(constantBufferCount)
			->Functor([this](TIndex handleID)
				{
					auto threadContext = GetVulkanApplication().AquireThreadContextPtr();
					IShaderConstantSetData const& constantsData = m_RenderGraph->GetConstantSetData(handleID);
					auto& shaderConstantDesc = m_RenderGraph->GetShaderConstantDesc(constantsData.GetDescID());
					auto subAllocator = GetVulkanApplication().GetShaderConstantSetAllocators().GetOrCreate(shaderConstantDesc).lock();
					auto& metaData = subAllocator->GetMetadata();

					std::vector<uint32_t> arrangedData;
					arrangedData.resize(metaData.GetTotalSize());
					auto& positionsMap = metaData.GetArithmeticValuePositions();

					std::vector<std::tuple<std::string, uint32_t, uint32_t>> cachedDataList;
					constantsData.PopulateCachedData(cachedDataList);

					size_t bufferSize = metaData.GetTotalSize() * sizeof(uint32_t);
					auto srcBufferHandle = GetMemoryManager().AllocateFrameBoundTransferStagingBuffer(bufferSize);

					for (auto& cachedData : cachedDataList)
					{
						auto targetPos = positionsMap.find(std::get<0>(cachedData));
						if (targetPos != positionsMap.end())
						{
							memcpy(static_cast<uint8_t*>(srcBufferHandle->GetMappedPointer()) + targetPos->second.first * sizeof(uint32_t)
								, static_cast<uint8_t const*>(constantsData.GetUploadingDataPtr()) + std::get<1>(cachedData), std::get<2>(cachedData));
						}	
					}

					auto bufferHandle = GetMemoryManager().AllocateBuffer(
						EMemoryType::GPU
						, EMemoryLifetime::Persistent
						, bufferSize
						, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);

					VulkanBarrierCollector barrierCollector{ GetFrameCountContext().GetGraphicsQueueFamily() };
					//Do We Need A Sync Barrier Here?
					barrierCollector.PushBufferBarrier(bufferHandle->GetBuffer(), ResourceUsage::eDontCare, ResourceUsage::eTransferDest);
					auto cmdBuffer = threadContext->GetCurrentFramePool().AllocateMiscCommandBuffer("Upload Template Shader Constants Buffer");
					barrierCollector.ExecuteBarrier(cmdBuffer);
					cmdBuffer.copyBuffer(srcBufferHandle->GetBuffer(), bufferHandle->GetBuffer(), vk::BufferCopy(0, 0, bufferSize));
					cmdBuffer.end();
					m_GPUBufferObjects[m_ConstantBufferOffset + handleID] = std::move(bufferHandle);
				});
		taskGrap->NewTaskParallelFor()
			->Name("Allocate GPU Buffers")
			->DependsOn(initializeTask)
			->JobCount(bufferCount)
			->Functor([this](TIndex handleID)
			{
				auto threadContext = GetVulkanApplication().AquireThreadContextPtr();
				IGPUBufferInternalData const& bufferData = m_RenderGraph->GetGPUBufferInternalData(handleID);
				GPUBufferDescriptor const& bufferDesc = m_RenderGraph->GetGPUBufferDescriptor(handleID);
				size_t bufferSize = std::min(bufferDesc.count * bufferDesc.stride, bufferData.GetSizeInBytes());
				if (bufferSize == 0)
					return;
				auto srcBufferHandle = GetMemoryManager().AllocateFrameBoundTransferStagingBuffer(bufferSize);
				auto bufferHandle = GetVulkanApplication().GetMemoryManager().AllocateBuffer(EMemoryType::GPU
					, EMemoryLifetime::FrameBound
					, bufferSize
					, EBufferUsageFlagsTranslate(bufferDesc.usageFlags));
				memcpy(srcBufferHandle->GetMappedPointer(), bufferData.GetPointer(), bufferSize);
				VulkanBarrierCollector barrierCollector{ GetFrameCountContext().GetGraphicsQueueFamily() };
				//Do We Need A Sync Barrier Here?
				barrierCollector.PushBufferBarrier(bufferHandle->GetBuffer(), ResourceUsage::eDontCare, ResourceUsage::eTransferDest);
				auto cmdBuffer = threadContext->GetCurrentFramePool().AllocateMiscCommandBuffer("Upload Template GPU Buffer");
				barrierCollector.ExecuteBarrier(cmdBuffer);
				cmdBuffer.copyBuffer(srcBufferHandle->GetBuffer(), bufferHandle->GetBuffer(), vk::BufferCopy(0, 0, bufferSize));
				cmdBuffer.end();
				m_GPUBufferObjects[m_GPUBufferOffset + handleID] = std::move(bufferHandle);
			});
	}

	void RenderGraphExecutor::AllocateShaderBindingSets(CTaskGraph* taskGrap)
	{
		uint32_t setCount = m_RenderGraph->GetBindingSetDataCount();
		if(setCount == 0)
			return;
		auto initializeTask = taskGrap->NewTask()
			->Name("Initialize Descriptor Sets")
			->Functor([this, setCount]()
			{
				m_DescriptorSets.resize(setCount);
			});
		taskGrap->NewTaskParallelFor()
			->Name("Allocate Shader Binding Sets")
			->DependsOn(initializeTask)
			->JobCount(setCount)
			->Functor([this](uint32_t setID)
			{
				IShaderBindingSetData* bindingSetData = m_RenderGraph->GetBindingSetData(setID);
				TIndex bindingSetDescID = bindingSetData->GetBindingSetDescIndex();
				auto& bindingSetDesc = m_RenderGraph->GetShaderBindingSetDesc(bindingSetDescID);
				//TODO: Improve This
				ShaderDescriptorSetLayoutInfo layoutInfo{ bindingSetDesc };
				auto& descPoolCache = GetVulkanApplication().GetGPUObjectManager().GetShaderDescriptorPoolCache();
				auto allocator = descPoolCache.GetOrCreate(layoutInfo).lock();
				m_DescriptorSets[setID] = std::move(allocator->AllocateSet());
			});
	}

	void RenderGraphExecutor::WriteShaderBindingSets(CTaskGraph* taskGrap)
	{
		uint32_t setCount = m_RenderGraph->GetBindingSetDataCount();
		taskGrap->NewTaskParallelFor()
			->Name("Allocate Shader Binding Sets")
			->JobCount(setCount)
			->Functor([this](uint32_t setID)
				{
					IShaderBindingSetData* bindingSetData = m_RenderGraph->GetBindingSetData(setID);
					TIndex bindingSetDescID = bindingSetData->GetBindingSetDescIndex();
					auto& constantSets = bindingSetData->GetExternalConstantSets();
					auto& internalConstantSets = bindingSetData->GetInternalConstantSets();
					auto& externalTextures = bindingSetData->GetExternalTextures();
					auto& internalTextures = bindingSetData->GetInternalTextures();
					auto& externalSamplers = bindingSetData->GetExternalSamplers();
					auto& bindingSetDesc = m_RenderGraph->GetShaderBindingSetDesc(bindingSetDescID);

					auto& descriptorSetObject = m_DescriptorSets[setID];
					//太绕了
					auto pAllocator = GetVulkanApplication().GetShaderBindingSetAllocators().GetOrCreate(bindingSetDesc).lock();
					auto& metaData = pAllocator->GetMetadata();

					vk::DescriptorSet targetSet = descriptorSetObject->GetDescriptorSet();
					uint32_t writeCount = constantSets.size() + externalTextures.size() + internalTextures.size() + externalSamplers.size();
					std::vector<vk::WriteDescriptorSet> descriptorWrites;
					if (writeCount > 0)
					{
						descriptorWrites.reserve(writeCount);
						auto& nameToIndex = metaData.GetCBufferNameToBindingIndex();
						for (auto itr = constantSets.begin(); itr != constantSets.end(); ++itr)
						{
							auto& name = itr->first;
							auto set = std::static_pointer_cast<ShaderConstantSet_Impl>(itr->second);
							uint32_t bindingIndex = metaData.CBufferNameToBindingIndex(name);
							if (bindingIndex != std::numeric_limits<uint32_t>::max())
							{
								vk::DescriptorBufferInfo bufferInfo{ set->GetBufferObject()->GetBuffer(), 0, VK_WHOLE_SIZE };
								vk::WriteDescriptorSet writeSet{ targetSet
									, bindingIndex
									, 0
									, 1
									, vk::DescriptorType::eUniformBuffer
									, nullptr
									, &bufferInfo
									, nullptr
								};
								descriptorWrites.push_back(writeSet);
							}
						}

						for (auto itr = internalConstantSets.begin(); itr != internalConstantSets.end(); ++itr)
						{
							auto& name = itr->first;
							auto& setHandle = itr->second;
							uint32_t bindingIndex = metaData.CBufferNameToBindingIndex(name);
							if (bindingIndex != std::numeric_limits<uint32_t>::max())
							{
								vk::DescriptorBufferInfo bufferInfo{ GetLocalConstantSetBuffer(setHandle.GetHandleIndex())->GetBuffer(), 0, VK_WHOLE_SIZE};
								vk::WriteDescriptorSet writeSet{ targetSet
									, bindingIndex
									, 0
									, 1
									, vk::DescriptorType::eUniformBuffer
									, nullptr
									, &bufferInfo
									, nullptr
								};
								descriptorWrites.push_back(writeSet);
							}
						}

						for (auto itr = externalTextures.begin(); itr != externalTextures.end(); ++itr)
						{
							auto& name = itr->first;
							auto texture = std::static_pointer_cast<GPUTexture_Impl>(itr->second);
							uint32_t bindingIndex = metaData.TextureNameToBindingIndex(name);
							if (bindingIndex != std::numeric_limits<uint32_t>::max())
							{
								if (!texture->UploadingDone())
									return;
								vk::DescriptorImageInfo imageInfo{ {}
									, texture->GetDefaultImageView()
									, vk::ImageLayout::eShaderReadOnlyOptimal };

								vk::WriteDescriptorSet writeSet{ targetSet
									, bindingIndex
									, 0
									, 1
									, vk::DescriptorType::eSampledImage
									, &imageInfo
									, nullptr
									, nullptr
								};
								descriptorWrites.push_back(writeSet);
							}
						}

						for (auto itr = internalTextures.begin(); itr != internalTextures.end(); ++itr)
						{
							auto& name = itr->first;
							auto& textureHandle = itr->second;
							auto& internalTexture = GetLocalTexture(textureHandle.GetHandleIndex());
							
							uint32_t bindingIndex = metaData.TextureNameToBindingIndex(name);
							if (bindingIndex != std::numeric_limits<uint32_t>::max())
							{
								vk::DescriptorImageInfo imageInfo{ {}
									, internalTexture.m_ImageView
									, vk::ImageLayout::eShaderReadOnlyOptimal };

								vk::WriteDescriptorSet writeSet{ targetSet
									, bindingIndex
									, 0
									, 1
									, vk::DescriptorType::eSampledImage
									, &imageInfo
									, nullptr
									, nullptr
								};
								descriptorWrites.push_back(writeSet);
							}
						}

						for (auto itr = externalSamplers.begin(); itr != externalSamplers.end(); ++itr)
						{
							auto& name = itr->first;
							auto sampler = std::static_pointer_cast<TextureSampler_Impl>(itr->second);
							uint32_t bindingIndex = metaData.SamplerNameToBindingIndex(name);
							if (bindingIndex != std::numeric_limits<uint32_t>::max())
							{
								vk::DescriptorImageInfo samplerInfo{ sampler->GetSampler()
									, {}
									, vk::ImageLayout::eShaderReadOnlyOptimal };

								vk::WriteDescriptorSet writeSet{ targetSet
									, bindingIndex
									, 0
									, 1
									, vk::DescriptorType::eSampler
									, &samplerInfo
									, nullptr
									, nullptr
								};
								descriptorWrites.push_back(writeSet);
							}
						}
					}
					if (descriptorWrites.size() > 0)
					{
						GetDevice().updateDescriptorSets(descriptorWrites, {});
					}
				});
	}


	RenderPassExecutor::RenderPassExecutor(RenderGraphExecutor& owningExecutor, CRenderGraph const& renderGraph, CRenderpassBuilder const& renderpassBuilder) :
		m_OwningExecutor(owningExecutor)
		, m_RenderGraph(renderGraph)
		, m_RenderpassBuilder(renderpassBuilder)
	{
	}
	void RenderPassExecutor::ResolveTextureHandleUsages(std::unordered_map<TIndex, ResourceUsageFlags>& textureHandleUsageStates)
	{
		auto& renderPassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		auto& handles = m_RenderpassBuilder.GetAttachmentTextureHandles();
		m_ImageUsageBarriers.reserve(handles.size());

		std::vector<ResourceUsageFlags> renderPassInitializeUsages;
		renderPassInitializeUsages.resize(handles.size());
		std::fill(renderPassInitializeUsages.begin(), renderPassInitializeUsages.end(), ResourceUsage::eDontCare);

		auto checkSetInitializeUsage = [&renderPassInitializeUsages](uint32_t attachmentID, ResourceUsageFlags newUsage)
			{
				if (attachmentID != INVALID_ATTACHMENT_INDEX
					&& renderPassInitializeUsages[attachmentID] == ResourceUsage::eDontCare)
				{
					CA_ASSERT(attachmentID >= 0 && attachmentID < renderPassInitializeUsages.size(), "Invalid AttachmentID");
					renderPassInitializeUsages[attachmentID] = newUsage;
				}
			};

		for (uint32_t subpassID = 0; subpassID < renderPassInfo.subpassInfos.size(); ++subpassID)
		{
			auto& subpassInfo = renderPassInfo.subpassInfos[subpassID];
			for (uint32_t colorAttachmentID : subpassInfo.colorAttachmentIDs)
			{
				checkSetInitializeUsage(colorAttachmentID, ResourceUsage::eColorAttachmentOutput);
			}
			for (uint32_t colorAttachmentID : subpassInfo.pixelInputAttachmentIDs)
			{
				checkSetInitializeUsage(colorAttachmentID, ResourceUsage::eFragmentRead);
			}
			checkSetInitializeUsage(subpassInfo.depthAttachmentID
							, subpassInfo.depthAttachmentReadOnly ? ResourceUsage::eDepthStencilReadonly : ResourceUsage::eDepthStencilAttachment);
		}

		for(uint32_t attachmentID = 0; attachmentID < handles.size(); ++attachmentID)
		{
			uint32_t handleID = handles[attachmentID];
			if (handleID != INVALID_INDEX)
			{
				ResourceUsageFlags srcUsage = ResourceUsage::eDontCare;
				auto found = textureHandleUsageStates.find(handleID);
				if (found != textureHandleUsageStates.end())
				{
					srcUsage = found->second;
				}
				ResourceUsageFlags dstUsage = renderPassInitializeUsages[attachmentID];
				textureHandleUsageStates[handleID] = dstUsage;
				if (srcUsage != dstUsage)
				{
					m_ImageUsageBarriers.push_back(std::make_tuple(handleID, srcUsage, dstUsage));
				}
			}
		}

		//Handle Texture Barriers In Descriptor Sets
		for (uint32_t subpassID = 0; subpassID < renderPassInfo.subpassInfos.size(); ++subpassID)
		{
			switch (m_RenderpassBuilder.GetSubpassType(subpassID))
			{
			case ESubpassType::eSimpleDraw:
			{
				auto& subpassData = m_RenderpassBuilder.GetSubpassData_SimpleDrawcall(subpassID);
				auto& shaderBindingSetHandles = subpassData.shaderBindingList.m_BindingSetHandles;
				for (auto& bindingSetHandle : shaderBindingSetHandles)
				{
					//TIndex bindingSetIndex = bindingSetHandle.GetHandleIndex();
					IShaderBindingSetData const* data = m_RenderGraph.GetBindingSetData(bindingSetHandle.GetHandleIndex());
					auto& internalTextureHandles = data->GetInternalTextures();
					for (auto itr = internalTextureHandles.begin(); itr != internalTextureHandles.end(); ++itr)
					{

						ResourceUsageFlags srcUsage = ResourceUsage::eDontCare;
						TIndex handleID = itr->second.GetHandleIndex();
						auto found = textureHandleUsageStates.find(handleID);
						if (found != textureHandleUsageStates.end())
						{
							srcUsage = found->second;
						}
						ResourceUsageFlags dstUsage = ResourceUsage::eFragmentRead | ResourceUsage::eVertexRead;
						if (srcUsage != dstUsage)
						{
							textureHandleUsageStates[handleID] = dstUsage;
							m_ImageUsageBarriers.push_back(std::make_tuple(handleID, srcUsage, dstUsage));
						}
					}
				}
				break;
			}
			case ESubpassType::eMeshInterface:
				break;
			case ESubpassType::eBatchDrawInterface:
				break;
			}
		}
	}

	void RenderPassExecutor::ResolveBufferHandleUsages(std::unordered_map<TIndex, ResourceUsageFlags>& bufferHandleUsageStates)
	{
		auto& renderPassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		for (uint32_t subpassID = 0; subpassID < renderPassInfo.subpassInfos.size(); ++subpassID)
		{
			switch (m_RenderpassBuilder.GetSubpassType(subpassID))
			{
			case ESubpassType::eSimpleDraw:
			{
				auto& subpassData = m_RenderpassBuilder.GetSubpassData_SimpleDrawcall(subpassID);
				auto& shaderBindingSetHandles = subpassData.shaderBindingList.m_BindingSetHandles;
				for (auto& bindingSetHandle : shaderBindingSetHandles)
				{
					IShaderBindingSetData const* data = m_RenderGraph.GetBindingSetData(bindingSetHandle.GetHandleIndex());
					auto& internalBufferHandles = data->GetInternalGPUBuffers();
					for (auto itr = internalBufferHandles.begin(); itr != internalBufferHandles.end(); ++itr)
					{
						//TODO: 初始化Resource Usage
						ResourceUsageFlags srcUsage = ResourceUsage::eTransferDest;
						TIndex handleID = itr->second.GetHandleIndex();
						auto found = bufferHandleUsageStates.find(handleID);
						if (found != bufferHandleUsageStates.end())
						{
							srcUsage = found->second;
						}
						ResourceUsageFlags dstUsage = ResourceUsage::eFragmentRead | ResourceUsage::eVertexRead;
						if (srcUsage != dstUsage)
						{
							bufferHandleUsageStates[handleID] = dstUsage;
							m_BufferUsageBarriers.push_back(std::make_tuple(handleID, srcUsage, dstUsage));
						}
					}
				}
				break;
			}
			case ESubpassType::eMeshInterface:
				break;
			case ESubpassType::eBatchDrawInterface:
				break;
			}
		}
	}

	void RenderPassExecutor::UpdateTextureLifetimes(uint32_t nodeIndex, std::vector<TextureHandleLifetimeInfo>& textureLifetimes)
	{
		auto& renderPassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		auto& handles = m_RenderpassBuilder.GetAttachmentTextureHandles();
		for (uint32_t attachmentID = 0; attachmentID < handles.size(); ++attachmentID)
		{
			uint32_t handleID = handles[attachmentID];
			if (handleID != INVALID_INDEX)
			{
				ResourceUsage srcUsage = ResourceUsage::eDontCare;
				textureLifetimes[handleID].Touch(nodeIndex);
			}
		}
	}
	void RenderPassExecutor::Compile(CTaskGraph* taskGraph)
	{
		auto compileRenderPassTask = taskGraph->NewTask()
			->Name("Compile RenderPass")
			->Functor([this]()
				{
					CompileRenderPass();
				});
		taskGraph->NewTaskGraph()
			->Name("Compile PSOs")
			->DependsOn(compileRenderPassTask)
			->SetupFunctor([this](CTaskGraph* thisGraph)
				{
					CompilePSOs(thisGraph);
				});
	}

	void RenderPassExecutor::ExecuteSubpass_SimpleDraw(
		uint32_t subpassID
		, uint32_t width
		, uint32_t height
		, vk::CommandBuffer cmd)
	{
		GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
		auto& renderPassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		RenderPassDescriptor rpDesc{ renderPassInfo };
		auto& subpassData = m_RenderpassBuilder.GetSubpassData_SimpleDrawcall(subpassID);
		cmd.setViewport(0
			, {
				vk::Viewport{0.0f, 0.0f
				, static_cast<float>(width)
				, static_cast<float>(height)
				, 0.0f, 1.0f}
			}
		);
		cmd.setScissor(0
			, {
				vk::Rect2D{
					{0, 0}
					, { width, height }
				}
			}
		);
		auto& psoList = m_GraphicsPipelineObjects[subpassID];
		CCommandList_Impl cmdListInterface{ cmd, &m_OwningExecutor, m_RenderPassObject, subpassID, psoList };
		cmdListInterface.BindPipelineState(0);
		subpassData.commandFunction(cmdListInterface);
	}

	void RenderPassExecutor::PrepareBatchDrawInterfaceSecondaryCommands(
		CTaskGraph* thisGraph
		, uint32_t subpassID
		, uint32_t width
		, uint32_t height
		, std::vector<vk::CommandBuffer>& cmdList
	)
	{

	}

	void RenderPassExecutor::PrepareDrawcallInterfaceSecondaryCommands(
		CTaskGraph* thisGraph
		, uint32_t subpassID
		, uint32_t width
		, uint32_t height
		, std::vector<vk::CommandBuffer>& cmdList
	)
	{
		auto& subpassData = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
		auto drawcallInterface = subpassData.meshInterface;
		auto batchCount = drawcallInterface->GetBatchCount();
		cmdList.resize(batchCount);

		thisGraph->NewTaskParallelFor()
			->Name("Prepare Secondary Cmds for Subpass " + std::to_string(subpassID))
			->JobCount(batchCount)
			->Functor([this, drawcallInterface, subpassID, width, height, &cmdList](uint32_t batchID)
				{
					auto threadContext = m_OwningExecutor.GetVulkanApplication().AquireThreadContextPtr();
					
					vk::CommandBufferInheritanceInfo commandBufferInheritanceInfo{
						m_RenderPassObject->GetRenderPass()
						, 0
						, m_FrameBufferObject->GetFramebuffer() };

					vk::CommandBufferBeginInfo secondaryBeginInfo(
						vk::CommandBufferUsageFlagBits::eOneTimeSubmit
						| vk::CommandBufferUsageFlagBits::eRenderPassContinue,
						&commandBufferInheritanceInfo);

					auto& commandListPool = threadContext->GetCurrentFramePool();
					vk::CommandBuffer cmd = commandListPool.AllocateSecondaryCommandBuffer("Render Pass Secondary Command Buffer");
					cmdList[batchID] = cmd;
					cmd.begin(secondaryBeginInfo);
					cmd.setViewport(0
						, {
							vk::Viewport{0.0f, 0.0f
							, static_cast<float>(width)
							, static_cast<float>(height)
							, 0.0f, 1.0f}
						}
					);
					cmd.setScissor(0
						, {
							vk::Rect2D{
								{0, 0}
								, { width, height }
							}
						}
					);
					auto& psoList = m_GraphicsPipelineObjects[subpassID];
					CCommandList_Impl cmdListInterface{ cmd, &m_OwningExecutor, m_RenderPassObject, subpassID, psoList };
					drawcallInterface->DrawBatch(batchID, cmdListInterface);
					cmd.end();
				});
	}

	void RenderPassExecutor::ExecuteSubpass_MeshInterface(uint32_t subpassID
		, uint32_t width
		, uint32_t height
		, vk::CommandBuffer cmd
		, CVulkanFrameBoundCommandBufferPool& cmdPool)
	{
		GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
		auto& renderPassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		RenderPassDescriptor rpDesc{ renderPassInfo };
		auto pRenderPass = gpuObjectManager.GetRenderPassCache().GetOrCreate(rpDesc).lock();
		auto& subpassData = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
		auto drawcallInterface = subpassData.meshInterface;
		auto batchCount = drawcallInterface->GetBatchCount();
		auto& psoList = m_GraphicsPipelineObjects[subpassID];
		for (uint32_t batchID = 0; batchID < batchCount; ++batchID)
		{
			CCommandList_Impl cmdListInterface{ cmd, &m_OwningExecutor, pRenderPass, subpassID, psoList };
		}
	}

	void RenderPassExecutor::ProcessAquireBarriers(vk::CommandBuffer cmd)
	{
		VulkanBarrierCollector textureBarrier{ m_OwningExecutor.GetFrameCountContext().GetGraphicsQueueFamily() };
		for (auto& usageData : m_ImageUsageBarriers)
		{
			TIndex handleIDS = std::get<0>(usageData);
			auto& textureInfo = m_RenderGraph.GetGPUTextureInternalData(handleIDS);
			auto& descriptor = m_RenderGraph.GetTextureDescriptor(textureInfo.GetDescID());

			vk::Image image;
			if (textureInfo.GetWindowsHandle() != nullptr)
			{
				image = static_cast<CWindowContext*>(textureInfo.GetWindowsHandle())->GetCurrentFrameImage();
			}
			else
			{
				auto& textureHandleInternal = m_OwningExecutor.GetLocalTexture(handleIDS);
				image = textureHandleInternal.m_ImageObject->GetImage();
			}

			textureBarrier.PushImageBarrier(image, descriptor.format
				, std::get<1>(usageData)
				, std::get<2>(usageData));
		}
		for (auto& usageData : m_BufferUsageBarriers)
		{
			TIndex gpuBufferHandleID = std::get<0>(usageData);
			auto& bufferInfo = m_RenderGraph.GetGPUBufferInternalData(gpuBufferHandleID);
			auto& descriptor = m_RenderGraph.GetGPUBufferDescriptor(gpuBufferHandleID);
			vk::Buffer buffer = m_OwningExecutor.GetLocalBuffer(gpuBufferHandleID)->GetBuffer();

			textureBarrier.PushBufferBarrier(buffer
				, std::get<1>(usageData)
				, std::get<2>(usageData));
		}
		textureBarrier.ExecuteBarrier(cmd);
	}

	void RenderPassExecutor::PrepareCommandBuffers(CTaskGraph* thisGraph)
	{
		auto secondaryCmdTaskGraph = thisGraph->NewTaskGraph()
			->Name("Prepare RenderPass Secondary Cmds")
			->SetupFunctor([this](CTaskGraph* taskGraph)
				{
					auto& renderpassInfo = m_RenderpassBuilder.GetRenderPassInfo();
					for (uint32_t subpassId = 0; subpassId < renderpassInfo.subpassInfos.size(); ++subpassId)
					{
						if (m_RenderpassBuilder.GetSubpassType(subpassId) == ESubpassType::eMeshInterface)
						{
							m_PendingSecondaryCommandBuffers.push_back({});
							auto& cmdBufferList = m_PendingSecondaryCommandBuffers.back();
							PrepareDrawcallInterfaceSecondaryCommands(
								taskGraph
								, subpassId
								, m_FrameBufferObject->GetWidth()
								, m_FrameBufferObject->GetHeight()
								, cmdBufferList);
						}
						else if (m_RenderpassBuilder.GetSubpassType(subpassId) == ESubpassType::eBatchDrawInterface)
						{
							m_PendingSecondaryCommandBuffers.push_back({});
							auto& cmdBufferList = m_PendingSecondaryCommandBuffers.back();
							PrepareBatchDrawInterfaceSecondaryCommands(
								taskGraph
								, subpassId
								, m_FrameBufferObject->GetWidth()
								, m_FrameBufferObject->GetHeight()
								, cmdBufferList);
						}
					}
				});

		auto primaryCmdTask = thisGraph->NewTask()
			->Name("Prepare RenderPass Primary Cmd")
			->DependsOn(secondaryCmdTaskGraph)
			->Functor([this]()
				{
					auto threadContext = m_OwningExecutor.GetVulkanApplication().AquireThreadContextPtr();
					auto& commandListPool = threadContext->GetCurrentFramePool();
					m_PendingGraphicsCommandBuffers.clear();
					vk::CommandBuffer cmd = commandListPool.AllocateOnetimeCommandBuffer("Render Pass Command Buffer");
					ProcessAquireBarriers(cmd);
					auto& renderpassInfo = m_RenderpassBuilder.GetRenderPassInfo();
					

					uint32_t meshIndexCommandListID = 0;
					for (uint32_t subpassId = 0; subpassId < renderpassInfo.subpassInfos.size(); ++subpassId)
					{
						ESubpassType subpasType = m_RenderpassBuilder.GetSubpassType(subpassId);
						vk::SubpassContents subpassContents = vk::SubpassContents::eInline;
						switch (subpasType)
						{
						case ESubpassType::eSimpleDraw:
							subpassContents = vk::SubpassContents::eInline;
							break;
						case ESubpassType::eMeshInterface:
						case ESubpassType::eBatchDrawInterface:
							subpassContents = vk::SubpassContents::eSecondaryCommandBuffers;
							break;
						}
						if (subpassId == 0)
						{
							cmd.beginRenderPass(
								vk::RenderPassBeginInfo{
								m_RenderPassObject->GetRenderPass()
									, m_FrameBufferObject->GetFramebuffer()
									, vk::Rect2D{{0, 0}
								, { m_FrameBufferObject->GetWidth()
									, m_FrameBufferObject->GetHeight() }}
								, m_ClearValues }
							, subpassContents);
						}
						else
						{
							cmd.nextSubpass(subpassContents);
						}
						switch (subpasType)
						{
						case ESubpassType::eSimpleDraw:
							ExecuteSubpass_SimpleDraw(
								subpassId
								, m_FrameBufferObject->GetWidth()
								, m_FrameBufferObject->GetHeight()
								, cmd);
							break;
						case ESubpassType::eMeshInterface:
						case ESubpassType::eBatchDrawInterface:
							{
								auto& cmdList = m_PendingSecondaryCommandBuffers[meshIndexCommandListID];
								++meshIndexCommandListID;
								cmd.executeCommands(cmdList);
								break;
							}
						}
					}
					cmd.endRenderPass();
					cmd.end();
					m_PendingGraphicsCommandBuffers.push_back(cmd);
				});
	}
	void RenderPassExecutor::AppendCommandBuffers(std::vector<vk::CommandBuffer>& outCommandBuffers)
	{
		outCommandBuffers.resize(outCommandBuffers.size() + m_PendingGraphicsCommandBuffers.size());
		std::copy(m_PendingGraphicsCommandBuffers.begin(), m_PendingGraphicsCommandBuffers.end(), outCommandBuffers.end() - m_PendingGraphicsCommandBuffers.size());
	}
	void RenderPassExecutor::SetupFrameBuffer()
	{
		GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
		auto& handles = m_RenderpassBuilder.GetAttachmentTextureHandles();
		m_FrameBufferImageViews.clear();
		m_FrameBufferImageViews.reserve(handles.size());

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t layers = 0;
		bool firstHandle = true;
		for (TIndex handleIDS : handles)
		{
			auto const& textureInfo = m_RenderGraph.GetGPUTextureInternalData(handleIDS);
			GPUTextureDescriptor const& desc = m_RenderGraph.GetTextureDescriptor(handleIDS);

			if (firstHandle)
			{
				firstHandle = false;
				width = desc.width;
				height = desc.height;
				layers = desc.layers;
			}
			else
			{
				CA_ASSERT((width == desc.width && height == desc.height && layers == desc.layers)
					, "RenderPassExecutor::Compile() : All texture handles must have same width, height and layers");
			}

			if (textureInfo.GetWindowsHandle() != nullptr)
			{
				m_FrameBufferImageViews.push_back(static_cast<CWindowContext*>(textureInfo.GetWindowsHandle())->GetCurrentFrameImageView());
			}
			else
			{
				auto& textureHandleInternal = m_OwningExecutor.GetLocalTexture(handleIDS);
				m_FrameBufferImageViews.push_back(textureHandleInternal.m_ImageView);
			}
		}

		FramebufferDescriptor framebufferDesc{
			m_FrameBufferImageViews
			, m_RenderPassObject
			, width, height, layers
		};
		m_FrameBufferObject = gpuObjectManager.GetFramebufferCache().GetOrCreate(framebufferDesc).lock();
	}
	void RenderPassExecutor::CompileRenderPass()
	{
		auto& renderpassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		RenderPassDescriptor rpDesc{ renderpassInfo };
		GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
		m_RenderPassObject = gpuObjectManager.GetRenderPassCache().GetOrCreate(rpDesc).lock();

		m_ClearValues.resize(renderpassInfo.attachmentInfos.size());
		for (uint32_t attachmentID = 0; attachmentID < renderpassInfo.attachmentInfos.size(); ++attachmentID)
		{
			auto& attachmentInfo = renderpassInfo.attachmentInfos[attachmentID];
			m_ClearValues[attachmentID] = AttachmentClearValueTranslate(
				attachmentInfo.clearValue
				, attachmentInfo.format);
		}
	}

	void RenderPassExecutor::CompilePSOs(CTaskGraph* thisGraph)
	{
		auto& renderpassInfo = m_RenderpassBuilder.GetRenderPassInfo();
		uint32_t subpassCount = renderpassInfo.subpassInfos.size();
		m_GraphicsPipelineObjects.resize(subpassCount);
		for (uint32_t subpassID = 0; subpassID < subpassCount; ++subpassID)
		{
			auto subpassType = m_RenderpassBuilder.GetSubpassType(subpassID);
			switch (subpassType)
			{
				case ESubpassType::eSimpleDraw:
				{
					CompilePSOs_SubpassSimpleDrawCall(subpassID, thisGraph);
					break;
				}
				case ESubpassType::eMeshInterface:
					CompilePSOs_SubpassMeshInterface(subpassID, thisGraph);
					break;
				case ESubpassType::eBatchDrawInterface:
					CompilePSOs_BatchDrawInterface(subpassID, thisGraph);
					break;
			}
			
		}
	}

	void CollectDescriptorSetLayouts(
		ShaderBindingList const& shaderBindingList
		, ShaderDescriptorSetAllocatorPool& descPoolCache
		, CRenderGraph const& renderGraph
		, std::vector<vk::DescriptorSetLayout>& inoutLayouts)
	{
		//也许从shader中提取layout信息更好
		std::set<vk::DescriptorSetLayout> layoutSet;
		auto& bindingSets = shaderBindingList.m_ShaderBindingSets;
		for (auto itrSet : bindingSets)
		{
			std::shared_ptr<ShaderBindingSet_Impl> pSet = std::static_pointer_cast<ShaderBindingSet_Impl>(itrSet);
			auto& layoutInfo = pSet->GetMetadata()->GetLayoutInfo();
			auto shaderDescriptorSetAllocator = descPoolCache.GetOrCreate(layoutInfo).lock();
			layoutSet.insert(shaderDescriptorSetAllocator->GetDescriptorSetLayout());
		}
		auto& bindingSetHandle = shaderBindingList.m_BindingSetHandles;
		for (auto& itrHandle : bindingSetHandle)
		{
			auto& desc = renderGraph.GetShaderBindingSetDesc(itrHandle.GetHandleIndex());
			//太绕了
			ShaderDescriptorSetLayoutInfo layoutInfo{ desc };
			auto shaderDescriptorSetAllocator = descPoolCache.GetOrCreate(layoutInfo).lock();
			layoutSet.insert(shaderDescriptorSetAllocator->GetDescriptorSetLayout());
		}

		size_t originalSize = inoutLayouts.size();
		inoutLayouts.resize(originalSize + layoutSet.size());
		std::copy(layoutSet.begin(), layoutSet.end(), inoutLayouts.begin() + originalSize);
		layoutSet.clear();
	}

	void RenderPassExecutor::CompilePSOs_SubpassSimpleDrawCall(uint32_t subpassID, CTaskGraph* thisGraph)
	{
		thisGraph->NewTask()
			->Name("Compile PSO for SimpleDraw Subpass " + std::to_string(subpassID))
			->Functor([this, subpassID]()
			{
				GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
				auto& subpassData = m_RenderpassBuilder.GetSubpassData_SimpleDrawcall(subpassID);
				//shaders
				auto vertModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ subpassData.shaderSet.vert }).lock();
				auto fragModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ subpassData.shaderSet.frag }).lock();

				std::vector<vk::DescriptorSetLayout> layouts;
				CollectDescriptorSetLayouts(
					subpassData.shaderBindingList
					, gpuObjectManager.GetShaderDescriptorPoolCache()
					, m_RenderGraph
					, layouts);

				CPipelineObjectDescriptor pipelineDesc{
				subpassData.pipelineStateObject
				, subpassData.vertexInputDescriptor
				, ShaderStateDescriptor{vertModule, fragModule}
				, layouts
				, m_RenderPassObject
				, subpassID };
				m_GraphicsPipelineObjects[subpassID].push_back(gpuObjectManager.GetPipelineCache().GetOrCreate(pipelineDesc).lock());
			});
	}
	void RenderPassExecutor::CompilePSOs_SubpassMeshInterface(uint32_t subpassID, CTaskGraph* thisGraph)
	{
		auto& subpassData = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
		size_t psoCount = subpassData.meshInterface->GetGraphicsPipelineStatesCount();
		m_GraphicsPipelineObjects[subpassID].resize(psoCount);
		thisGraph->NewTaskParallelFor()
			->Name("Compile PSOs for Batch Interface Subpass " + std::to_string(subpassID))
			->JobCount(psoCount)
			->Functor([this, subpassID, &subpassData](uint32_t psoID)
				{
					GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
					auto& descPoolCache = gpuObjectManager.GetShaderDescriptorPoolCache();
					auto& psoData = subpassData.meshInterface->GetGraphicsPipelineStatesData(psoID);
					auto& pipelineStates = psoData.pipelineStateObject;
					auto& vertexInputs = psoData.vertexInputDescriptor;
					auto& shaderSet = psoData.shaderSet;
					auto& shaderBindings = psoData.shaderBindingDescriptors;

					//shaders
					auto vertModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ shaderSet.vert }).lock();
					auto fragModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ shaderSet.frag }).lock();

					//shaderBindings
					std::vector<vk::DescriptorSetLayout> layouts;
					layouts.reserve(shaderBindings.size());

					for (auto& bindingDesc : shaderBindings)
					{
						auto layoutInfo = ShaderDescriptorSetLayoutInfo{ bindingDesc };
						auto shaderDescriptorSetAllocator = descPoolCache.GetOrCreate(layoutInfo).lock();
						layouts.push_back(shaderDescriptorSetAllocator->GetDescriptorSetLayout());
					}

					
					CollectDescriptorSetLayouts(
						subpassData.shaderBindingList
						, gpuObjectManager.GetShaderDescriptorPoolCache()
						, m_RenderGraph
						, layouts);

					CPipelineObjectDescriptor pipelineDesc{
					pipelineStates
					, vertexInputs
					, ShaderStateDescriptor{vertModule, fragModule}
					, layouts
					, m_RenderPassObject
					, subpassID
					};
					m_GraphicsPipelineObjects[subpassID][psoID] = gpuObjectManager.GetPipelineCache().GetOrCreate(pipelineDesc).lock();
				});
	}
	void RenderPassExecutor::CompilePSOs_BatchDrawInterface(uint32_t subpassID, CTaskGraph* thisGraph)
	{
		m_BatchManagers.emplace_back();
		uint32_t batchID = m_BatchManagers.size() - 1;
		CA_ASSERT(batchID == m_RenderpassBuilder.GetSubpassDataIndex(subpassID), "Batch ID Not Match!");
		auto collectorTask = thisGraph->NewTaskGraph()
			->Name("Prepare Batch")
			->SetupFunctor([this, subpassID, batchID](auto pGraph)
				{
					auto& subpassData = m_RenderpassBuilder.GetSubpassData_BatchDrawInterface(subpassID);
					auto& batchManager = m_BatchManagers[batchID];
					subpassData.batchInterface->OnRegisterGraphicsPipelineStates(batchManager);
					m_GraphicsPipelineObjects[subpassID].resize(batchManager.GetPSOCount());

					pGraph->NewTaskParallelFor()
						->Name("Prepare PSOs for Batch Interface Subpass " + std::to_string(subpassID))
						->JobCount(batchManager.GetPSOCount())
						->Functor([this, subpassID, batchID](uint32_t psoID)
							{
								auto& batchManager = m_BatchManagers[batchID];
								auto& subpassData = m_RenderpassBuilder.GetSubpassData_BatchDrawInterface(subpassID);
								GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
								auto& descPoolCache = gpuObjectManager.GetShaderDescriptorPoolCache();

								auto& psoData = *batchManager.GetPSO(psoID);
								auto& pipelineStates = psoData.pipelineStateObject;
								auto& vertexInputs = psoData.vertexInputDescriptor;
								auto& shaderSet = psoData.shaderSet;
								auto& shaderBindings = psoData.shaderBindingDescriptors;

								//shaders
								auto vertModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ shaderSet.vert }).lock();
								auto fragModule = gpuObjectManager.GetShaderModuleCache().GetOrCreate({ shaderSet.frag }).lock();

								//shaderBindings
								std::vector<vk::DescriptorSetLayout> layouts;
								layouts.reserve(shaderBindings.size());

								for (auto& bindingDesc : shaderBindings)
								{
									auto layoutInfo = ShaderDescriptorSetLayoutInfo{ bindingDesc };
									auto shaderDescriptorSetAllocator = descPoolCache.GetOrCreate(layoutInfo).lock();
									layouts.push_back(shaderDescriptorSetAllocator->GetDescriptorSetLayout());
								}

								CollectDescriptorSetLayouts(
									subpassData.shaderBindingList
									, gpuObjectManager.GetShaderDescriptorPoolCache()
									, m_RenderGraph
									, layouts);

								CPipelineObjectDescriptor pipelineDesc{
								pipelineStates
								, vertexInputs
								, ShaderStateDescriptor{vertModule, fragModule}
								, layouts
								, m_RenderPassObject
								, subpassID
								};
								m_GraphicsPipelineObjects[subpassID][psoID] = gpuObjectManager.GetPipelineCache().GetOrCreate(pipelineDesc).lock();
							});
				});
	}
}
