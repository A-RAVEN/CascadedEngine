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

		
		//编译每个RenderPass(FrameBuffer, RenderPass, PSO)
		auto compileGraph = taskGraph->NewTaskGraph()
			->Name("Compile RenderPasses Graph")
			->DependsOn(creationTask);
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

		//处理每个RenderPass的Barrier
		auto resolvingTask = taskGraph->NewTask()
			->Name("Resolve Resource Usages")
			->DependsOn(compileGraph)
			->Functor([this, nodeCount]()
			{
				m_TextureHandleUsageStates.clear();
				for (uint32_t itr_node = 0; itr_node < nodeCount; ++itr_node)
				{
					m_RenderPasses[itr_node].ResolveTextureHandleUsages(m_TextureHandleUsageStates);
				}
			});
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

	void RenderGraphExecutor::Execute(CTaskGraph* taskGraph)
	{
		if (!CompileDone())
			return;

		auto recordCmdTaskGraph = taskGraph->NewTaskGraph()
			->Name("Record RenderPass Cmds Graph")
			->SetupFunctor([this](CTaskGraph* thisGraph)
			{
				uint32_t nodeCount = m_RenderGraph->GetRenderNodeCount();
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
				auto windowTarget = m_RenderGraph->GetTargetWindow<CWindowContext>();
				TIndex windowIndex = m_RenderGraph->WindowHandleToTextureIndex(windowTarget);
				auto state = m_TextureHandleUsageStates.find(windowIndex);
				if (state != m_TextureHandleUsageStates.end())
				{
					windowTarget->MarkUsages(state->second);
				}
			});
	}


	RenderPassExecutor::RenderPassExecutor(RenderGraphExecutor& owningExecutor, CRenderGraph const& renderGraph, CRenderpassBuilder const& renderpassBuilder) :
		m_OwningExecutor(owningExecutor)
		, m_RenderGraph(renderGraph)
		, m_RenderpassBuilder(renderpassBuilder)
	{
	}
	void RenderPassExecutor::ResolveTextureHandleUsages(std::unordered_map<TIndex, ResourceUsage>& textureHandleUsageStates)
	{
		auto& handles = m_RenderpassBuilder.GetTextureHandles();
		for (auto handleID : handles)
		{
			if (handleID != INVALID_INDEX)
			{
				ResourceUsage srcUsage = ResourceUsage::eDontCare;
				auto found = textureHandleUsageStates.find(handleID);
				if (found != textureHandleUsageStates.end())
				{
					srcUsage = found->second;
				}
				ResourceUsage dstUsage = ResourceUsage::eColorAttachmentOutput;
				textureHandleUsageStates[handleID] = dstUsage;
				if (srcUsage != dstUsage)
				{
					m_UsageBarriers.push_back(std::make_tuple(handleID, srcUsage, dstUsage));
				}
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
		CCommandList_Impl cmdListInterface{ cmd, m_RenderPassObject, subpassID, psoList };
		cmdListInterface.BindPipelineState(0);
		subpassData.commandFunction(cmdListInterface);
	}

	void RenderPassExecutor::PrepareDrawcallInterfaceSecondaryCommands(
		CTaskGraph* thisGraph
		, uint32_t subpassID
		, uint32_t width
		, uint32_t height
		, std::vector<vk::CommandBuffer>& cmdList
	)
	{
		auto drawcallInterface = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
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
					CCommandList_Impl cmdListInterface{ cmd, m_RenderPassObject, subpassID, psoList };
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
		auto drawcallInterface = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
		auto batchCount = drawcallInterface->GetBatchCount();
		auto& psoList = m_GraphicsPipelineObjects[subpassID];
		for (uint32_t batchID = 0; batchID < batchCount; ++batchID)
		{
			CCommandList_Impl cmdListInterface{ cmd, pRenderPass, subpassID, psoList };
		}
	}

	void RenderPassExecutor::ProcessAquireBarriers(vk::CommandBuffer cmd)
	{
		VulkanBarrierCollector textureBarrier{ m_OwningExecutor.GetFrameCountContext().GetGraphicsQueueFamily() };
		for (auto& usageData : m_UsageBarriers)
		{
			auto& textureInfo = m_RenderGraph.GetTextureHandleInternalInfo(std::get<0>(usageData));
			textureBarrier.PushImageBarrier(static_cast<CWindowContext*>(textureInfo.p_WindowsHandle.get())->GetCurrentFrameImage()
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
		auto& handles = m_RenderpassBuilder.GetTextureHandles();
		m_FrameBufferImageViews.clear();
		m_FrameBufferImageViews.reserve(handles.size());

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t layers = 0;
		bool firstHandle = true;
		for (TIndex handleIDS : handles)
		{
			TextureHandleInternalInfo const& textureInfo = m_RenderGraph.GetTextureHandleInternalInfo(handleIDS);
			TextureHandle textureHandle = m_RenderGraph.TextureHandleByIndex(handleIDS);
			GPUTextureDescriptor const& desc = textureHandle.GetDescriptor();

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

			if (textureInfo.p_WindowsHandle != nullptr)
			{
				m_FrameBufferImageViews.push_back(static_cast<CWindowContext*>(textureInfo.p_WindowsHandle.get())->GetCurrentFrameImageView());
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
			}
			
		}
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

				//也许从shader中提取layout信息更好
				std::set<vk::DescriptorSetLayout> layoutSet;
				auto& bindingSets = subpassData.shaderBindingList.m_ShaderBindingSets;
				auto& descPoolCache = gpuObjectManager.GetShaderDescriptorPoolCache();
				for (auto itrSet : bindingSets)
				{
					std::shared_ptr<ShaderBindingSet_Impl> pSet = std::static_pointer_cast<ShaderBindingSet_Impl>(itrSet);
					auto& layoutInfo = pSet->GetMetadata()->GetLayoutInfo();
					auto shaderDescriptorSetAllocator = descPoolCache.GetOrCreate(layoutInfo).lock();
					layoutSet.insert(shaderDescriptorSetAllocator->GetDescriptorSetLayout());
				}
				std::vector<vk::DescriptorSetLayout> layouts;
				layouts.resize(layoutSet.size());
				std::copy(layoutSet.begin(), layoutSet.end(), layouts.begin());
				layoutSet.clear();

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
		auto drawcallInterface = m_RenderpassBuilder.GetSubpassData_MeshInterface(subpassID);
		size_t psoCount = drawcallInterface->GetGraphicsPipelineStatesCount();
		m_GraphicsPipelineObjects[subpassID].resize(psoCount);
		thisGraph->NewTaskParallelFor()
			->Name("Compile PSOs for Batch Interface Subpass " + std::to_string(subpassID))
			->JobCount(psoCount)
			->Functor([this, subpassID, drawcallInterface](uint32_t psoID)
				{
					GPUObjectManager& gpuObjectManager = m_OwningExecutor.GetGPUObjectManager();
					auto& descPoolCache = gpuObjectManager.GetShaderDescriptorPoolCache();
					auto& psoData = drawcallInterface->GetGraphicsPipelineStatesData(psoID);
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

					CPipelineObjectDescriptor pipelineDesc{
					pipelineStates
					, vertexInputs
					, ShaderStateDescriptor{vertModule, fragModule}
					, layouts
					, m_RenderPassObject
					, subpassID };
					m_GraphicsPipelineObjects[subpassID][psoID] = gpuObjectManager.GetPipelineCache().GetOrCreate(pipelineDesc).lock();
				});
	}
}
