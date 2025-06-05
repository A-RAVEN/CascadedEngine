#include "GPUGraphExecutor.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <Utils/InterfaceTranslation.h>
#include <RenderBackend_D3D12.h>
#include <GPUGraph/GPUPipelineInstance.h>

namespace graphics_backend
{

	//static void ForeachRenderPassShaderStructs(RenderPass const& renderPass, castl::function<void(D3D2ShaderStruct const&)> callback)
	//{
	//	castl::deque<castl::shared_ptr<ShaderStruct>> shaderStructs;
	//	for (auto shaderStruct : renderPass.GetShaderStructs())
	//	{
	//		CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
	//		shaderStructs.push_back(shaderStruct.second);
	//	}
	//	auto& drawcallBatchs = renderPass.GetDrawCallBatches();
	//	for (auto& batch : drawcallBatchs)
	//	{
	//		for (auto shaderStruct : batch.shaderStructs)
	//		{
	//			CA_ASSERT_BREAK(shaderStruct.second != nullptr, "Shader Struct Is Null, Why!?");
	//			shaderStructs.push_back(shaderStruct.second);
	//		}
	//	}
	//	while (!shaderStructs.empty())
	//	{
	//		auto shaderStruct = shaderStructs.front();
	//		D3D2ShaderStruct* pStruct = static_cast<D3D2ShaderStruct*>(shaderStruct.get());
	//		CA_ASSERT_BREAK(pStruct != nullptr, "Shader Struct Is Null, Why!?");
	//		callback(*pStruct);
	//		shaderStructs.pop_front();
	//		for (auto& subArgPairs : pStruct->GetSubStructs())
	//		{
	//			for (auto subStruct : subArgPairs.second)
	//			{
	//				shaderStructs.push_back(subStruct);
	//			}
	//		}
	//	}
	//}


	//static void ForeachComputePassShaderStructs(ComputeBatch const& computePass, castl::function<void(D3D2ShaderStruct const&)> callback)
	//{
	//	castl::deque<castl::shared_ptr<D3D2ShaderStruct>> shaderStructs;
	//	for (auto& shaderStruct : computePass.shaderStructs)
	//	{
	//		shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
	//	}
	//	for (auto& dispatch : computePass.dispatchs)
	//	{
	//		for (auto& shaderStruct : dispatch.shaderStructs)
	//		{
	//			shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
	//		}
	//	}
	//	while (!shaderStructs.empty())
	//	{
	//		auto shaderStruct = shaderStructs.front();
	//		callback(*shaderStruct);
	//		shaderStructs.pop_front();
	//		for (auto& subArgPairs : shaderStruct->GetSubStructs())
	//		{
	//			for (auto subStruct : subArgPairs.second)
	//			{
	//				shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(subStruct));
	//			}
	//		}
	//	}
	//}



	D3D12GPUGraphExecutor::D3D12GPUGraphExecutor(RenderBackend_D3D12* app) : 
		D3D12SubobjectBase(app)
		, m_LocalResourceManager(app)
		, m_ConstantBufferManager(app)
		, m_DescriptorAllocatorSet(app)
		, m_ResourceGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		, m_SamplerGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
	{}



	struct PassRWState
	{




		castl::unordered_map<ImageHandle, ResourceState> imageRWStates;
		castl::unordered_map<BufferHandle, ResourceState> bufferRWStates;
		bool depends(PassRWState const& other) const
		{
			for(auto& pair : imageRWStates)
			{
				auto&&[img, rwState] = pair;
				auto found = other.imageRWStates.find(img);
				if(found != other.imageRWStates.end())
				{
					if(rwState.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
						&& found->second.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly)
					{
					}
					else
					{
						return true;
					}
				}
			}
			for(auto& pair : bufferRWStates)
			{
				auto&&[buf, rwState] = pair;
				auto found = other.bufferRWStates.find(buf);
				if(found != other.bufferRWStates.end())
				{
					if(rwState.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
						&& found->second.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly)
					{
					}
					else
					{
						return true;
					}
				}
			}
			return false;
		}
		void Append(PassRWState const& other)
		{
			for (auto pair : other.imageRWStates)
			{
				imageRWStates.insert(pair);
			}
			for (auto pair : other.bufferRWStates)
			{
				bufferRWStates.insert(pair);
			}
		}
		//bool isWriting(ImageHandle const& image) const
		//{
		//	auto found = imageRWStates.find(image);
		//	if(found != imageRWStates.end())
		//	{
		//		return found->second.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
		//			|| found->second.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
		//	}
		//	return false;
		//}
		//bool isWriting(BufferHandle const& buffer) const
		//{
		//	auto found = bufferRWStates.find(buffer);
		//	if(found != bufferRWStates.end())
		//	{
		//		return found->second == ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
		//			|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
		//	}
		//	return false;
		//}
		//bool isReading(ImageHandle const& image) const
		//{
		//	auto found = imageRWStates.find(image);
		//	if(found != imageRWStates.end())
		//	{
		//		return found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
		//			|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
		//	}
		//	return false;
		//}
		//bool isReading(BufferHandle const& buffer) const
		//{
		//	auto found = bufferRWStates.find(buffer);
		//	if(found != bufferRWStates.end())
		//	{
		//		return found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
		//			|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
		//	}
		//	return false;
		//}
		//bool isReadingOrWriting(ImageHandle const& image) const
		//{
		//	return imageRWStates.contains(image);
		//}
		//bool isReadingOrWriting(BufferHandle const& buffer) const
		//{
		//	return bufferRWStates.contains(buffer);
		//}
		void SetImageRWState(ImageHandle const& image
			, EShaderTypeFlags stages
			, EResourceUsageFlags usages
			, ShaderCompilerSlang::EShaderResourceAccess access)
		{
			ResourceState newResourceState{
				access,
				stages,
				usages
			};

			auto found = imageRWStates.find(image);
			if(found != imageRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.Compatible(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				imageRWStates.insert(castl::make_pair(image, newResourceState));
			}
		}
		void SetBufferRWState(BufferHandle const& buffer
			, EShaderTypeFlags stages
			, EResourceUsageFlags usages
			, ShaderCompilerSlang::EShaderResourceAccess access)
		{

			ResourceState newResourceState{
				access,
				stages,
				usages
			};

			auto found = bufferRWStates.find(buffer);
			if(found != bufferRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.Compatible(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				bufferRWStates.insert(castl::make_pair(buffer, newResourceState));
			}
		}
	};

	class PassDependency
	{
	public:
		PassRWState const& rwState;
		uint32_t passID;
		GPUGraph::EGraphStageType passType;
		uint32_t predecessorCount;
		castl::vector<PassDependency*> successors;

		PassDependency(PassRWState const& rwState, uint32_t passID, GPUGraph::EGraphStageType passType)
			: rwState(rwState), passID(passID), passType(passType), predecessorCount(0) {}

		bool DepsFree() const
		{
			return predecessorCount == 0;
		}

		void RemoveSelfDeps()
		{
			for (PassDependency* dep : successors)
			{
				dep->predecessorCount--;
			}
		}

		void CheckAddSuccessor(PassDependency* successor)
		{
			if(rwState.depends(successor->rwState))
			{
				successors.push_back(successor);
				successor->predecessorCount++;
			}
		}
	};

	void Prepare(GPUGraph const& owningGraph,
		//D3D12GPUGraphExecutor& executor,
		RenderBackend_D3D12* pApp,
		D3D12GraphLocalResourceManager& resourceManager,
		GPUConstantBufferManager& constantBufferManager,
		ShaderResourceInstanceDic& shaderResourceInstances,
		castl::vector<PassRWState>& passRWStates,
		castl::vector<PassRWState>& computePassRWStates,
		castl::vector<PassRWState>& transferPassRWStates
	)
	{
		//auto pApp = executor.GetApp();
		//auto& resourceManager = executor.GetLocalResourceManager();
		//auto& constantBufferManager = executor.GetConstantBufferManager();
		//auto& shaderResourceInstances = executor.GetShaderResourceInstances();
		auto registerBufferToLocalResourceManager = [&](BufferHandle const& buffer)
		{
			auto descriptor = owningGraph.GetBufferManager().GetDescriptor(buffer.GetKey());
			CA_ASSERT_BREAK(descriptor != nullptr, "Buffer {} Not Registered", buffer.GetName());
			resourceManager.AddBuffer(buffer, *descriptor);
		};
		//将一个pass中所有资源的读写状态注册进PassRWState中，包括CBuffer
		auto addPassShaderInstancesResourcesRWStates = [&](PassRWState& passRWState,
			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> const& passLocalBindingInstances)
		{
			for(auto pair : passLocalBindingInstances)
			{
				auto resourceBindingInstance = pair.second;
				resourceBindingInstance->IterateResourceUsages(
				[&](GPUResourceBindingInstance::ImageBindingElement const& imageInfo)
				{
					for (auto& img : imageInfo.bindings)
					{
						passRWState.SetImageRWState(img.image, imageInfo.usingStages, imageInfo.resourceUsages, imageInfo.bindingInfo.accessType);
					}
				},
				[&](GPUResourceBindingInstance::BufferBindingElement const& bufferInfo)
				{
					for (auto& buf : bufferInfo.bindings)
					{
						passRWState.SetBufferRWState(buf, bufferInfo.usingStages, bufferInfo.resourceUsages, bufferInfo.bindingInfo.accessType);
					}
				},
				[&](GPUResourceBindingInstance::CBufferBindingElement const& cbufferInfo)
				{
					passRWState.SetBufferRWState(cbufferInfo.cbufferHandle
						, cbufferInfo.usingStages
						, EResourceUsage::eShaderResource
						, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
				});
			}
		};
		//遍历所有Pass的shader，对每种shader和shaderstruct组合创建GPUResourceBindingInstance
		//shaderstruct中的constant资源会注册进m_ConstantBufferManager
		passRWStates.resize(owningGraph.GetRenderPasses().size());
		for (size_t passID = 0; passID < owningGraph.GetRenderPasses().size(); ++passID)
		{
			auto& renderPass = owningGraph.GetRenderPasses()[passID];
			auto& passRWState = passRWStates[passID];
			//将renderPass的attachment注册进m_LocalResourceManager中
			for (auto& attachment : renderPass.GetAttachments())
			{
				auto descriptor = owningGraph.GetImageManager().GetDescriptor(attachment.GetKey());
				CA_ASSERT_BREAK(descriptor != nullptr, "Image {} Not Registered", attachment.GetName());
				resourceManager.AddTexture(attachment, *descriptor, GPUTextureView::CreateDefaultForRenderTarget(descriptor->format));
				passRWState.SetImageRWState(attachment, EShaderTypeMask::eNone, EResourceUsage::eRenderTarget, ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
			}

			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for(auto& drawcallBatchs : renderPass.GetDrawCallBatches())
			{
				for (auto& drawcall : drawcallBatchs.m_DrawCalls)
				{
					//将drawcall的index和vertexbuffer注册进m_LocalResourceManager中
					if (drawcall.GetDrawInfo().drawIndexed)
					{
						auto& indesxBuffer = drawcall.GetIndexBuffer().indexBufferHandle;
						registerBufferToLocalResourceManager(indesxBuffer);
						passRWState.SetBufferRWState(indesxBuffer, EShaderTypeMask::eNone, EResourceUsage::eVertexInput, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						registerBufferToLocalResourceManager(vertBuf.second);
						passRWState.SetBufferRWState(vertBuf.second, EShaderTypeMask::eNone, EResourceUsage::eVertexInput, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
				}
				auto pipelineData = PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), drawcallBatchs.pipelineStateDesc);
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&drawcallBatchs.shaderStructs);
				shaderStructs.push_back(&renderPass.GetShaderStructs());
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, pipelineData.m_ShaderInfo, {shaderStructs});
				castl::shared_ptr<GPUResourceBindingInstance> resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
				{
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance, constantBufferManager);
				})->second;

				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(passRWState, passLocalBindingInstances);
		}
		computePassRWStates.resize(owningGraph.GetComputePasses().size());
		for(size_t passID = 0; passID < owningGraph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = owningGraph.GetComputePasses()[passID];
			auto& passRWState = computePassRWStates[passID];
			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for (auto& dispatch : computePass.dispatchs)
			{
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&dispatch.shaderStructs);
				shaderStructs.push_back(&computePass.shaderStructs);
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, dispatch.m_ShaderInfo, { shaderStructs });
				auto resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
				{
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance, constantBufferManager);
				})->second;
				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(passRWState, passLocalBindingInstances);
		}
		transferPassRWStates.resize(owningGraph.GetDataTransfers().size());
		for (size_t passID = 0; passID < owningGraph.GetDataTransfers().size(); ++passID)
		{
			auto& transferPass = owningGraph.GetDataTransfers()[passID];
			auto& passRWState = transferPassRWStates[passID];
			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for (auto& bufferWrites : transferPass.m_BufferDataUploads)
			{
				registerBufferToLocalResourceManager(bufferWrites.first);
				passRWState.SetBufferRWState(bufferWrites.first, EShaderTypeMask::eNone, EResourceUsage::eCopy, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly);
			}
			for (auto& imgWrites : transferPass.m_ImageDataUploads)
			{
				auto descriptor = owningGraph.GetImageManager().GetDescriptor(imgWrites.first.GetKey());
				CA_ASSERT_BREAK(descriptor != nullptr, "Image {} Not Registered", imgWrites.first.GetName());
				resourceManager.AddTexture(imgWrites.first, *descriptor, GPUTextureView::CreateDefaultForRenderTarget(descriptor->format));
				passRWState.SetImageRWState(imgWrites.first, EShaderTypeMask::eNone, EResourceUsage::eCopy, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly);
			}
		}

		//将m_ConstantBufferManager中的constant buffer资源注册到m_LocalResourceManager中
		constantBufferManager.BuildResources(resourceManager);
		//将m_ShaderResourceInstances中的资源注册到m_LocalResourceManager中(不包括CBuffer)
		shaderResourceInstances.for_each([&](ShaderResourceSet const& resourceSet
			, castl::shared_ptr<GPUResourceBindingInstance>& resourceInstance)
		{
			resourceInstance->BuildResources(owningGraph, resourceManager);
		});
	}

	struct GPUExecutionBatch
	{
		std::vector<uint32_t> rasterPassRefs;
		std::vector<uint32_t> computePassRefs;
		std::vector<uint32_t> transferPassRefs;
		PassRWState batchRWStates;
	};

	using ResourceUsageRange = castl::range<uint32_t>;

	void BuildDependencyFreeBatchs(GPUGraph const& owningGraph,
		//D3D12GPUGraphExecutor& executor,
		castl::vector<PassRWState> const& rasterPassRWStates,
		castl::vector<PassRWState> const& computePassRWStates,
		castl::vector<PassRWState> const& transferPassRWStates,
		castl::vector<GPUExecutionBatch>& outExecutionBatchs
	)
	{
		using EGraphStageType = GPUGraph::EGraphStageType;
		auto& stages = owningGraph.GetGraphStages();
		CA_ASSERT_BREAK(stages.size() == (rasterPassRWStates.size() + computePassRWStates.size() + transferPassRWStates.size()), "Graph Nodes Size Not Equal");
		castl::vector<PassDependency> passDeps;
		passDeps.reserve(stages.size());
		castl::list<PassDependency*> pendingDependencies;
		for (size_t stageID = 0; stageID < owningGraph.GetGraphStages().size(); ++stageID)
		{
			auto stage = owningGraph.GetGraphStages()[stageID];
			auto passID = owningGraph.GetPassIndices()[stageID];
			switch (stage)
			{
			case EGraphStageType::eRenderPass:
				passDeps.emplace_back(rasterPassRWStates[passID], passID, stage);
				break;
			case EGraphStageType::eComputePass:
				passDeps.emplace_back(computePassRWStates[passID], passID, stage);
				break;
			case EGraphStageType::eTransferPass:
				passDeps.emplace_back(transferPassRWStates[passID], passID, stage);
				break;
			}
			pendingDependencies.push_back(&passDeps.back());
		}

		if (owningGraph.GetGraphStages().size() < 2)
			return;
		for (size_t prevPass = 0; prevPass < owningGraph.GetGraphStages().size() - 1; ++prevPass)
		{
			for (size_t latterPass = prevPass + 1; latterPass < owningGraph.GetGraphStages().size(); ++latterPass)
			{
				passDeps[prevPass].CheckAddSuccessor(&passDeps[latterPass]);
			}
		}
		while (!pendingDependencies.empty())
		{
			GPUExecutionBatch& newPass = outExecutionBatchs.emplace_back();
			std::vector<PassDependency*> passFreeDeps;
			for (PassDependency* dep : pendingDependencies)
			{
				if (dep->DepsFree())
				{
					passFreeDeps.push_back(dep);
					//add to newPass
					newPass.batchRWStates.Append(dep->rwState);
					switch (dep->passType)
					{
					case EGraphStageType::eRenderPass:
						newPass.rasterPassRefs.push_back(dep->passID);
						break;
					case EGraphStageType::eComputePass:
						newPass.computePassRefs.push_back(dep->passID);
						break;
					case EGraphStageType::eTransferPass:
						newPass.transferPassRefs.push_back(dep->passID);
						break;
					}

				}
			}
			for (PassDependency* dep : passFreeDeps)
			{
				dep->RemoveSelfDeps();
			}
		}
	}

	void BuildResourceUsageRanges(castl::unordered_map<ImageHandle, ResourceUsageRange>& imageRanges,
		castl::unordered_map<BufferHandle, ResourceUsageRange>& bufferRanges,
		castl::vector<GPUExecutionBatch> const& executionBatchs
	)
	{
		for (uint32_t batchID = 0; batchID < executionBatchs.size(); ++batchID)
		{
			auto& batch = executionBatchs[batchID];
			auto& rwStates = batch.batchRWStates;
			for (auto& imageRWState : rwStates.imageRWStates)
			{
				imageRanges[imageRWState.first].encapsule(batchID);
			}
			for (auto& bufferRWState : rwStates.bufferRWStates)
			{
				bufferRanges[bufferRWState.first].encapsule(batchID);
			}
		}
	}

	struct RenderPassGPUData
	{
		struct DrawCallBatchGPUData
		{
			GPUPipelineInstance const* pipelineInstances;
		};
		castl::vector<DrawCallBatchGPUData> drawcallBatchs;
	};

	void BuildPipelineStates(RenderBackend_D3D12* app
		, GPUGraph const& owningGraph
		, D3D12GraphLocalResourceManager const& resourceManager
		, castl::vector<RenderPassGPUData>& outRasterPassObjects)
	{
		auto& renderPasses = owningGraph.GetRenderPasses();
		outRasterPassObjects.reserve(renderPasses.size());
		for (auto& rasterPass : renderPasses)
		{
			RenderPassGPUData gpuData;

			auto& attachments = rasterPass.GetAttachments();
			for (auto& batch : rasterPass.GetDrawCallBatches())
			{
				RenderPassGPUData::DrawCallBatchGPUData batchData;

				PipelineDescData batchLevelDescData = PipelineDescData::CombindDescData(
					rasterPass.GetPipelineStates()
					, batch.GetPipelineStates());
				GPUPipelineStateKey pipelineStateKey;
				pipelineStateKey.Init(app
					, batchLevelDescData
					, attachments
					, rasterPass.GetDepthAttachmentIndex()
					, batch
					, resourceManager);

				batchData.pipelineInstances = app->GetRasterPipelineManager().GetPipelineState(pipelineStateKey);
				gpuData.drawcallBatchs.push_back(batchData);
			}
			outRasterPassObjects.push_back(gpuData);
		}
	}

	void D3D12GPUGraphExecutor::Init(GPUGraph const& owningGraph)
	{
		//收集当前图中所有的资源
		//并注册到m_LocalResourceManager中
		//整理每个pass的读写状态
		castl::vector<PassRWState> passRWStates;
		castl::vector<PassRWState> computePassRWStates;
		castl::vector<PassRWState> transferPassRWStates;
		Prepare(owningGraph
			, GetApp()
			, m_LocalResourceManager
			, m_ConstantBufferManager
			, m_ShaderResourceInstances
			, passRWStates, computePassRWStates, transferPassRWStates);
		//整理出多个连续的无依赖batch
		castl::vector<GPUExecutionBatch> executionBatchs;
		BuildDependencyFreeBatchs(owningGraph
			, passRWStates
			, computePassRWStates
			, transferPassRWStates
			, executionBatchs
		);

		//统计资源的生命周期
		castl::unordered_map<ImageHandle, ResourceUsageRange> imageLifeTimes;
		castl::unordered_map<BufferHandle, ResourceUsageRange> bufferLifeTimes;
		BuildResourceUsageRanges(imageLifeTimes, bufferLifeTimes, executionBatchs);

		//依据资源的生命周期实际分配资源
		m_LocalResourceManager.AllocateAliasedResources(executionBatchs.size(), imageLifeTimes, bufferLifeTimes);
		//为资源创建 descriptor
		m_LocalResourceManager.PrepareResourceDescriptors(m_DescriptorAllocatorSet);

		m_ShaderResourceInstances.for_each([&](auto& shaderSet, castl::shared_ptr<GPUResourceBindingInstance>& pInstance)->void
		{
			pInstance->BuildDescriptors(m_LocalResourceManager, m_ResourceGPUHeap, m_SamplerGPUHeap);
		});

		//创建PipelineBarriers

		
		//创建PSO:TODO 并行化
		castl::vector<RenderPassGPUData> rasterPassGPUDataList;
		BuildPipelineStates(GetApp()
			, owningGraph
			, m_LocalResourceManager
			, rasterPassGPUDataList);



	}
}