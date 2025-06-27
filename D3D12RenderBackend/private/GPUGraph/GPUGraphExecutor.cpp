#include "GPUGraphExecutor.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <Utils/InterfaceTranslation.h>
#include <RenderBackend_D3D12.h>
#include <GPUGraph/GPUPipelineInstance.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>
#include <D3D12Debug.h>

namespace graphics_backend
{

	void CollectInputAssemblyBindings(VertexInputBindingData const& bindingData
		, DrawCallBatch::VertexInputDescMap const& vertexBufferDescs
		, castl::unordered_map<cacore::NameHash, BufferHandle> const& boundVertexBuffers
		, castl::vector<castl::pair<VertexInputsDescriptor, BufferHandle>>& outVertexBufferBindings
	)
	{
		outVertexBufferBindings.resize(bindingData.inoutBindingNameToIndex.size());
		for (int bindingIndex = 0; bindingIndex < bindingData.inoutBindingNameToIndex.size(); ++bindingIndex)
		{
			cacore::NameHash const& vertexBufferDescName = bindingData.inoutBindingNameToIndex[bindingIndex];
			auto foundDesc = vertexBufferDescs.find(vertexBufferDescName);
			auto foundBuffer = boundVertexBuffers.find(vertexBufferDescName);
			if (foundDesc != vertexBufferDescs.end() && foundBuffer != boundVertexBuffers.end())
			{
				outVertexBufferBindings[bindingIndex] = castl::make_pair(foundDesc->second.Get(), foundBuffer->second);
			}
		}
	}


	struct RenderPassGPUData
	{
		struct DrawCallGPUData
		{
			castl::vector<castl::pair<VertexInputsDescriptor, BufferHandle>> inputAssemblyBindingBuffers;
		};

		struct DrawCallBatchGPUData
		{
			//TODO: Set Topology Here
			D3D12_PRIMITIVE_TOPOLOGY topology;
			GPUPipelineInstance const* pipelineInstances;
			GPUResourceBindingInstance const* pResourceBindingInstance;
			castl::vector<DrawCallGPUData> drawcalls;
		};
		castl::vector<DrawCallBatchGPUData> drawcallBatchs;
	};

	struct ComputePassGPUData
	{
		struct DispatchGPUData
		{
			GPUComputePipelineInstance const* pipelineInstances;
			GPUResourceBindingInstance const* pResourceBindingInstance;
		};
		castl::vector<DispatchGPUData> dispatchs;
	};




	D3D12GPUGraphExecutor::D3D12GPUGraphExecutor(RenderBackend_D3D12* app) :
		D3D12SubobjectBase(app)
		, m_LocalResourceManager(app)
		, m_ConstantBufferManager(app)
		, m_DescriptorAllocatorSet(app)
		, m_ResourceGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		, m_SamplerGPUHeap(app, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
		, m_CommandListManager(app)
		, m_StagingMemoryManager(app)
	{
	}



	struct PassRWState
	{
		castl::unordered_map<ImageHandle, ResourceState> imageRWStates;
		castl::unordered_map<BufferHandle, ResourceState> bufferRWStates;
		castl::unordered_set<D3D2ShaderStruct const*> cBufferUsageStates;
		bool depends(PassRWState const& other) const
		{
			for (auto& pair : imageRWStates)
			{
				auto&& [img, rwState] = pair;
				auto found = other.imageRWStates.find(img);
				if (found != other.imageRWStates.end())
				{
					if (rwState.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
						&& found->second.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly)
					{
					}
					else
					{
						return true;
					}
				}
			}
			for (auto& pair : bufferRWStates)
			{
				auto&& [buf, rwState] = pair;
				auto found = other.bufferRWStates.find(buf);
				if (found != other.bufferRWStates.end())
				{
					if (rwState.resourceAccess == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
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
			for (D3D2ShaderStruct const* pstruct : other.cBufferUsageStates)
			{
				cBufferUsageStates.insert(pstruct);
			}
		}
		
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
			if (found != imageRWStates.end())
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
			if (found != bufferRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.Compatible(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				bufferRWStates.insert(castl::make_pair(buffer, newResourceState));
			}
		}
		void SetCBufferUsageState(D3D2ShaderStruct const* pCBufferStruct)
		{
			cBufferUsageStates.insert(pCBufferStruct);
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
			: rwState(rwState), passID(passID), passType(passType), predecessorCount(0) {
		}

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
			if (rwState.depends(successor->rwState))
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
		castl::vector<PassRWState>& transferPassRWStates,
		castl::vector<RenderPassGPUData>& rasterGPUData,
		castl::vector<ComputePassGPUData>& computeGPUData
	)
	{
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
			for (auto pair : passLocalBindingInstances)
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
					passRWState.SetCBufferUsageState(cbufferInfo.pCBufferStruct);
					/*passRWState.SetBufferRWState(cbufferInfo.cbufferHandle
						, cbufferInfo.usingStages
						, EResourceUsage::eConstantBuffer
						, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);*/
				});
			}
		};
		//遍历所有Pass的shader，对每种shader和shaderstruct组合创建GPUResourceBindingInstance
		//shaderstruct中的constant资源会注册进m_ConstantBufferManager
		CA_ASSERT_BREAK(passRWStates.size() == owningGraph.GetRenderPasses().size(), "Raster Pass RW State Size Incompatible");
		for (size_t passID = 0; passID < owningGraph.GetRenderPasses().size(); ++passID)
		{
			auto& renderPass = owningGraph.GetRenderPasses()[passID];
			auto& passRWState = passRWStates[passID];
			auto& rasterPassGPUData = rasterGPUData[passID];
			//将renderPass的attachment注册进m_LocalResourceManager中
			{
				int attachmentID = 0;
				for (auto& attachment : renderPass.GetAttachments())
				{
					auto descriptor = owningGraph.GetImageManager().GetDescriptor(attachment.GetKey());
					CA_ASSERT_BREAK(descriptor != nullptr, "Image {} Not Registered", attachment.GetName());
					resourceManager.AddTexture(attachment, *descriptor, GPUTextureView::CreateDefaultForRenderTarget(descriptor->format));
					if (attachmentID == renderPass.GetDepthAttachmentIndex())
					{
						passRWState.SetImageRWState(attachment, EShaderTypeMask::eNone, EResourceUsage::eDepthStencilTarget, ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
					}
					else
					{
						passRWState.SetImageRWState(attachment, EShaderTypeMask::eNone, EResourceUsage::eRenderTarget, ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
					}
					++attachmentID;
				}
			}


			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			auto& drawcalBatchs = renderPass.GetDrawCallBatches();

			for(size_t batchID = 0; batchID < drawcalBatchs.size(); ++batchID)
			{
				auto& drawcallBatch = drawcalBatchs[batchID];
				auto& batchGPUData = rasterPassGPUData.drawcallBatchs[batchID];
				for (auto& drawcall : drawcallBatch.m_DrawCalls)
				{
					//将drawcall的index和vertexbuffer注册进m_LocalResourceManager中
					if (drawcall.GetDrawInfo().drawIndexed)
					{
						auto& indesxBuffer = drawcall.GetIndexBuffer().indexBufferHandle;
						registerBufferToLocalResourceManager(indesxBuffer);
						passRWState.SetBufferRWState(indesxBuffer, EShaderTypeMask::eNone, EResourceUsage::eIndexInput, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						registerBufferToLocalResourceManager(vertBuf.second);
						passRWState.SetBufferRWState(vertBuf.second, EShaderTypeMask::eNone, EResourceUsage::eVertexInput, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
				}
				auto pipelineData = PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), drawcallBatch.pipelineStateDesc);
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&drawcallBatch.shaderStructs);
				shaderStructs.push_back(&renderPass.GetShaderStructs());
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, pipelineData.m_ShaderInfo, { shaderStructs });
				castl::shared_ptr<GPUResourceBindingInstance> resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
				{
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance, constantBufferManager);
				})->second;

				batchGPUData.pResourceBindingInstance = resourceBindingInstance.get();

				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(passRWState, passLocalBindingInstances);
		}
		CA_ASSERT_BREAK(computePassRWStates.size() == owningGraph.GetComputePasses().size(), "Compute Pass RW State Size Incompatible");
		for (size_t passID = 0; passID < owningGraph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = owningGraph.GetComputePasses()[passID];
			auto& passRWState = computePassRWStates[passID];
			auto& computePassGPUData = computeGPUData[passID];
			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				auto& dispatchGPUData = computePassGPUData.dispatchs[dispatchID];
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&dispatch.shaderStructs);
				shaderStructs.push_back(&computePass.shaderStructs);
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, dispatch.m_ShaderInfo, { shaderStructs });
				auto resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
				{
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance, constantBufferManager);
				})->second;
				dispatchGPUData.pResourceBindingInstance = resourceBindingInstance.get();
				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(passRWState, passLocalBindingInstances);
		}
		CA_ASSERT_BREAK(transferPassRWStates.size() == owningGraph.GetDataTransfers().size(), "Transfer Pass RW State Size Incompatible");
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

	struct RenderStateBarrier
	{
		ResourceState before;
		ResourceState after;
	};

	struct CBufferInitializeBarriers
	{
		castl::vector<castl::pair<BufferHandle, D3D2ShaderStruct const*>> cbufferData;
		void AddCBuffer(BufferHandle const& bufferHandle, D3D2ShaderStruct const* shaderStruct)
		{
			cbufferData.push_back(castl::make_pair(bufferHandle, shaderStruct));
		}
	};

	struct RenderStateBarriers
	{
		castl::vector<castl::pair<ImageHandle, D3D12_TEXTURE_BARRIER>> imageBarriers;
		castl::vector<castl::pair<BufferHandle, D3D12_BUFFER_BARRIER>> bufferBarriers;
		void AddImageBarrier(ImageHandle const& handle, D3D12_TEXTURE_BARRIER const& barrier)
		{
			imageBarriers.push_back(castl::make_pair(handle, barrier));
		}
		void AddBufferBarrier(BufferHandle const& handle, D3D12_BUFFER_BARRIER const& barrier)
		{
			bufferBarriers.push_back(castl::make_pair(handle, barrier));
		}
	};

	struct GPUExecutionBatch
	{
		std::vector<uint32_t> rasterPassRefs;
		std::vector<uint32_t> computePassRefs;
		std::vector<uint32_t> transferPassRefs;
		RenderStateBarriers aquireBarriers;
		RenderStateBarriers releaseBarriers;
		CBufferInitializeBarriers cbufferBarriers;
		PassRWState batchRWStates;

		void CbufferInitializeBarriers(ID3D12GraphicsCommandList7* pCommandList
			, D3D12GraphLocalResourceManager& resourceManager
			, castl::vector<D3D12_BUFFER_BARRIER>& beforeBarriers
			, castl::vector<D3D12_BUFFER_BARRIER>& afterBarriers) const
		{
			//CBuffer Barriers
			for (auto& cbufferData : cbufferBarriers.cbufferData)
			{
				BufferHandle const& bufferHandle = cbufferData.first;
				BufferResourceAllocationInfo const* info = resourceManager.GetBufferResource(bufferHandle);
				ID3D12Resource* targetBuffer = info->gpuResource.GetResource();

				D3D12_BUFFER_BARRIER beforeBarrier{};
				beforeBarrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
				beforeBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
				beforeBarrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
				beforeBarrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
				beforeBarrier.pResource = targetBuffer;
				beforeBarrier.Offset = 0;
				beforeBarrier.Size = ULLONG_MAX;
				beforeBarriers.push_back(beforeBarrier);

				D3D12_BUFFER_BARRIER afterBarrier{};
				afterBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
				afterBarrier.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
				afterBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
				afterBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
				afterBarrier.pResource = targetBuffer;
				afterBarrier.Offset = 0;
				afterBarrier.Size = ULLONG_MAX;
				afterBarriers.push_back(afterBarrier);
			}
		}

		void CBufferCopyInitialize(ID3D12GraphicsCommandList7* pCommandList
			, LinearMemoryManager& linearMemoryManager
			, D3D12GraphLocalResourceManager& resourceManager) const
		{
			//CBuffer Barriers
			for (auto& cbufferData : cbufferBarriers.cbufferData)
			{
				BufferHandle const& bufferHandle = cbufferData.first;
				BufferResourceAllocationInfo const* info = resourceManager.GetBufferResource(bufferHandle);
				ID3D12Resource* targetBuffer = info->gpuResource.GetResource();

				D3D2ShaderStruct const* pStruct = cbufferData.second;
				auto& uniformBufferData = pStruct->GetSelfUniformBuffer();
				ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(uniformBufferData.size());
				UINT8* mappedStagingData;
				stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedStagingData));
				memcpy(mappedStagingData, uniformBufferData.data(), uniformBufferData.size());
				stagingBuffer->Unmap(0, nullptr);

				pCommandList->CopyBufferRegion(targetBuffer, 0, stagingBuffer, 0, uniformBufferData.size());
			}
		}


		void ExecuteAquireBarriers(ID3D12GraphicsCommandList7* pCommandList
			, LinearMemoryManager& linearMemoryManager
			, D3D12GraphLocalResourceManager& resourceManager) const
		{
			castl::vector<D3D12_BARRIER_GROUP> barrierGroups;
			castl::vector<D3D12_TEXTURE_BARRIER> imageBarriers;
			for (auto& aquireImageBarriers : aquireBarriers.imageBarriers)
			{
				imageBarriers.push_back(aquireImageBarriers.second);
			}
			if (!imageBarriers.empty())
			{
				CD3DX12_BARRIER_GROUP imageBarrierGroup(imageBarriers.size(), imageBarriers.data());
				barrierGroups.push_back(imageBarrierGroup);
			}
			castl::vector<D3D12_BUFFER_BARRIER> bufferBarriers;

#pragma region CBuffer Barriers
			//CBuffer Barriers
			if (!cbufferBarriers.cbufferData.empty())
			{
				castl::vector<D3D12_BUFFER_BARRIER> cbufferAquireBarriers;
				CbufferInitializeBarriers(pCommandList, resourceManager, cbufferAquireBarriers, bufferBarriers);
				CD3DX12_BARRIER_GROUP cbufferAquireBarrierGroup(cbufferAquireBarriers.size(), cbufferAquireBarriers.data());
				pCommandList->Barrier(1, &cbufferAquireBarrierGroup);
				CBufferCopyInitialize(pCommandList, linearMemoryManager, resourceManager);
			}
#pragma endregion
			
			for (auto& aquireBufferBarriers : aquireBarriers.bufferBarriers)
			{
				bufferBarriers.push_back(aquireBufferBarriers.second);
			}
			if (!bufferBarriers.empty())
			{
				CD3DX12_BARRIER_GROUP bufferBarrierGroup(bufferBarriers.size(), bufferBarriers.data());
				barrierGroups.push_back(bufferBarrierGroup);
			}
			if (!barrierGroups.empty())
			{
				pCommandList->Barrier(barrierGroups.size(), barrierGroups.data());
			}
		}

		void ExecuteReleaseBarriers(ID3D12GraphicsCommandList7* pCommandList) const
		{
			castl::vector<D3D12_BARRIER_GROUP> barrierGroups;
			castl::vector<D3D12_TEXTURE_BARRIER> imageBarriers;
			for (auto& aquireImageBarriers : releaseBarriers.imageBarriers)
			{
				imageBarriers.push_back(aquireImageBarriers.second);
			}
			if (!imageBarriers.empty())
			{
				CD3DX12_BARRIER_GROUP imageBarrierGroup(imageBarriers.size(), imageBarriers.data());
				barrierGroups.push_back(imageBarrierGroup);
			}
			castl::vector<D3D12_BUFFER_BARRIER> bufferBarriers;
			
			for (auto& aquireBufferBarriers : releaseBarriers.bufferBarriers)
			{
				bufferBarriers.push_back(aquireBufferBarriers.second);
			}
			if (!bufferBarriers.empty())
			{
				CD3DX12_BARRIER_GROUP bufferBarrierGroup(bufferBarriers.size(), bufferBarriers.data());
				barrierGroups.push_back(bufferBarrierGroup);
			}
			if (!barrierGroups.empty())
			{
				pCommandList->Barrier(barrierGroups.size(), barrierGroups.data());
			}
		}

	};



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
			auto depItr = pendingDependencies.begin();
			while(depItr != pendingDependencies.end())
			{
				PassDependency* dep = *depItr;
				if (dep->DepsFree())
				{
					depItr = pendingDependencies.erase(depItr);
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
				else
				{
					++depItr;
				}
			}
			for (PassDependency* dep : passFreeDeps)
			{
				dep->RemoveSelfDeps();
			}
		}
	}

	void BuildResourceUsageRanges(castl::unordered_map<ImageHandle, ResourceUsageRangeData>& imageRanges,
		castl::unordered_map<BufferHandle, ResourceUsageRangeData>& bufferRanges,
		castl::unordered_map<D3D2ShaderStruct const*, ResourceUsageRange>& cbufferRanges,
		castl::vector<GPUExecutionBatch> const& executionBatchs
	)
	{
		for (uint32_t batchID = 0; batchID < executionBatchs.size(); ++batchID)
		{
			auto& batch = executionBatchs[batchID];
			auto& rwStates = batch.batchRWStates;
			for (auto& imageRWState : rwStates.imageRWStates)
			{
				imageRanges[imageRWState.first].Expand(batchID, imageRWState.second);
			}
			for (auto& bufferRWState : rwStates.bufferRWStates)
			{
				bufferRanges[bufferRWState.first].Expand(batchID, bufferRWState.second);
			}
			for (D3D2ShaderStruct const* cbufferStruct : rwStates.cBufferUsageStates)
			{
				cbufferRanges[cbufferStruct].encapsule(batchID);
			}
		}
	}

	void BuildPipelineStates(RenderBackend_D3D12* app
		, GPUGraph const& owningGraph
		, D3D12GraphLocalResourceManager const& resourceManager
		, castl::vector<RenderPassGPUData>& inoutRasterPassObjects
		, castl::vector<ComputePassGPUData>& inoutComputePassObjects
	)
	{
		//Raster Passes
		{
			auto& renderPasses = owningGraph.GetRenderPasses();
			CA_ASSERT_BREAK(renderPasses.size() == inoutRasterPassObjects.size(), "rasterPass Data Length Not Equal");
			for (size_t passID = 0; passID < renderPasses.size(); ++passID)
			{
				auto& rasterPass = renderPasses[passID];
				auto& rasterPassData = inoutRasterPassObjects[passID];

				auto& attachments = rasterPass.GetAttachments();
				CA_ASSERT_BREAK(rasterPass.GetDrawCallBatches().size() == rasterPassData.drawcallBatchs.size(), "drawcall batch Data Length Not Equal");
				for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
				{
					auto& batch = rasterPass.GetDrawCallBatches()[batchID];
					auto& batchData = rasterPassData.drawcallBatchs[batchID];

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
					batchData.topology = ETopologyToD3D12Topology(batchLevelDescData.m_InputAssemblyStates->topology);

					CA_ASSERT_BREAK(batchData.drawcalls.size() == batch.m_DrawCalls.size(), "Drawcall Size Incompatible");
					for (size_t drawcallID = 0; drawcallID < batch.m_DrawCalls.size(); ++drawcallID)
					{
						auto& drawcall = batch.m_DrawCalls[drawcallID];
						auto& drawcallData = batchData.drawcalls[drawcallID];
						CollectInputAssemblyBindings(pipelineStateKey.m_VertexInputBindingData
							, batch.m_VertexInputDescs
							, drawcall.GetVertexBuffers()
							, drawcallData.inputAssemblyBindingBuffers);
					}
				}
			}
		}

		//Compute Passes
		{
			auto& computePasses = owningGraph.GetComputePasses();
			CA_ASSERT_BREAK(computePasses.size() == inoutComputePassObjects.size(), "computePass Data Length Not Equal");
			for (size_t passID = 0; passID < computePasses.size(); ++passID)
			{
				auto& computePass = computePasses[passID];
				auto& computePassData = inoutComputePassObjects[passID];
				CA_ASSERT_BREAK(computePass.dispatchs.size() == computePassData.dispatchs.size(), "dispatch Data Length Not Equal");
				for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
				{
					auto& dispatch = computePass.dispatchs[dispatchID];
					auto& dispatchData = computePassData.dispatchs[dispatchID];

					dispatchData.pipelineInstances = app->GetComputePipelineManager().GetPipelineState(dispatch.m_ShaderInfo);
				}
			}
		}

	}

	void PrepareBatchResourceBarriers(castl::vector<GPUExecutionBatch>& executionBatchs
		, castl::unordered_map<ImageHandle, ResourceUsageRangeData>& imageLifetimes
		, castl::unordered_map<BufferHandle, ResourceUsageRangeData>& bufferLifetimes
		, castl::unordered_map<D3D2ShaderStruct const*, ResourceUsageRange> const& cbufferLifetimes
		, GPUConstantBufferManager& constantBufferManager
		, D3D12GraphLocalResourceManager& resourceManager
	)
	{
		castl::unordered_map<ImageHandle, ResourceState> imageStates;

		for (auto& pair : imageLifetimes)
		{
			ImageHandle const& image = pair.first;
			ResourceState cachedState;
			if (image.IsIntternal())
			{
				cachedState = ResourceState::InitializedState();
			}
			else
			{
				D3DImageObject const* pImage = static_cast<D3DImageObject const*>(image.GetExternalManagedTexture().get());
				cachedState = pImage->GetResourceState();
			}
			ResourceUsageRangeData const& usageRanges = pair.second;

			//Iterate Resource Ranges, Add barriers for each batch
			for (int bid = 0; bid < usageRanges.states.size(); ++bid)
			{
				ResourceUsageRangeData::BatchAndState const& batchAndState = usageRanges.states[bid];
				int currentBatchID = batchAndState.batchID;
				ResourceState const& currentState = batchAndState.state;
				bool isFirstState = bid == 0;
				bool isLastState = bid == (usageRanges.states.size() - 1);
				int lastBatchID = isFirstState ? -1 : usageRanges.states[bid - 1].batchID;
				bool stateHaveGap = isFirstState ? false : ((currentBatchID - lastBatchID) > 1);
				ResourceState const& lastState = isFirstState ? cachedState : usageRanges.states[bid - 1].state;

				ResourceBarrierUsageStates lastBarrierStates = DetermineResourceBarrierUsageStates(lastState);
				ResourceBarrierUsageStates currentBarrierStates = DetermineResourceBarrierUsageStates(currentState);

				D3D12_TEXTURE_BARRIER resouceBarrier{};
				resouceBarrier.AccessBefore = lastBarrierStates.accessState;
				resouceBarrier.AccessAfter = currentBarrierStates.accessState;
				resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
				resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
				resouceBarrier.LayoutBefore = lastBarrierStates.layoutState;
				resouceBarrier.LayoutAfter = currentBarrierStates.layoutState;
				resouceBarrier.pResource = resourceManager.GetImageResource(image)->gpuResource.GetResource();
				resouceBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);

				if (stateHaveGap)
				{
					CA_ASSERT_BREAK(!isFirstState, "First State Should Not Have Gap");
					CA_ASSERT_BREAK(lastBatchID >= 0, "Last Batch ID Should Not Be -1");

					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];

					//Have Gap, Using Split Barrier
					D3D12_TEXTURE_BARRIER releaseBarrier = resouceBarrier;
					releaseBarrier.SyncAfter |= D3D12_BARRIER_SYNC_SPLIT;
					lastBatch.releaseBarriers.AddImageBarrier(image, releaseBarrier);

					D3D12_TEXTURE_BARRIER aquireBarrier = resouceBarrier;
					aquireBarrier.SyncBefore |= D3D12_BARRIER_SYNC_SPLIT;
					currentBatch.aquireBarriers.AddImageBarrier(image, aquireBarrier);
				}
				else
				{
					auto& currentBatch = executionBatchs[currentBatchID];
					currentBatch.aquireBarriers.AddImageBarrier(image, resouceBarrier);
				}
			}
		}
		for (auto& pair : bufferLifetimes)
		{
			BufferHandle const& buffer = pair.first;
			ResourceUsageRangeData const& usageRanges = pair.second;
			
			ResourceState cachedState;
			if (buffer.IsIntternal())
			{
				cachedState = ResourceState::InitializedState();
			}
			else
			{
				D3DBufferObject const* pBuffer = static_cast<D3DBufferObject const*>(buffer.GetExternalManagedBuffer().get());
				cachedState = pBuffer->GetResourceState();
			}
			//Iterate Resource Ranges, Add barriers for each batch
			for (int bid = 0; bid < usageRanges.states.size(); ++bid)
			{
				ResourceUsageRangeData::BatchAndState const& batchAndState = usageRanges.states[bid];
				int currentBatchID = batchAndState.batchID;
				ResourceState const& currentState = batchAndState.state;

				bool isFirstState = bid == 0;
				bool isLastState = bid == (usageRanges.states.size() - 1);
				int lastBatchID = isFirstState ? -1 : usageRanges.states[bid - 1].batchID;
				bool stateHaveGap = isFirstState ? false : ((currentBatchID - lastBatchID) > 1);
				ResourceState const& lastState = isFirstState ? cachedState : usageRanges.states[bid - 1].state;

				ResourceBarrierUsageStates lastBarrierStates = DetermineResourceBarrierUsageStates(lastState);
				ResourceBarrierUsageStates currentBarrierStates = DetermineResourceBarrierUsageStates(currentState);

				D3D12_BUFFER_BARRIER resouceBarrier{};
				resouceBarrier.AccessBefore = lastBarrierStates.accessState;
				resouceBarrier.AccessAfter = currentBarrierStates.accessState;
				resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
				resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
				resouceBarrier.pResource = resourceManager.GetBufferResource(buffer)->gpuResource.GetResource();
				resouceBarrier.Offset = 0;
				resouceBarrier.Size = ULLONG_MAX;
				if (stateHaveGap)
				{
					CA_ASSERT_BREAK(!isFirstState, "First State Should Not Have Gap");
					CA_ASSERT_BREAK(lastBatchID >= 0, "Last Batch ID Should Not Be -1");

					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];

					//Have Gap, Using Split Barrier
					D3D12_BUFFER_BARRIER releaseBarrier = resouceBarrier;
					releaseBarrier.SyncAfter |= D3D12_BARRIER_SYNC_SPLIT;
					lastBatch.releaseBarriers.AddBufferBarrier(buffer, releaseBarrier);

					D3D12_BUFFER_BARRIER aquireBarrier = resouceBarrier;
					releaseBarrier.SyncBefore |= D3D12_BARRIER_SYNC_SPLIT;
					currentBatch.aquireBarriers.AddBufferBarrier(buffer, aquireBarrier);

				}
				else
				{
					auto& currentBatch = executionBatchs[currentBatchID];
					currentBatch.aquireBarriers.AddBufferBarrier(buffer, resouceBarrier);
				}
			}
		}

		//Constant Buffers Initialized In First Used Batch And Remain Unchanged In Whole Lifetime
		for (auto& pair : cbufferLifetimes)
		{
			if (pair.second.empty())
				continue;
			D3D2ShaderStruct const* pStruct = pair.first;
			int initialBatchID = pair.second.head();
			auto& initialBatch = executionBatchs[initialBatchID];
			initialBatch.cbufferBarriers.AddCBuffer(constantBufferManager.GetConstantBufferHandle(pStruct), pStruct);
		}
	}

	void Execute(RenderBackend_D3D12* pBackend
		, GPUGraph const& owningGraph
		, CommandListManager& commandListMgr
		, LinearMemoryManager& linearMemoryManager
		, D3D12GraphLocalResourceManager& resourceManager
		, GPUDescriptorHeap& resourceHeap
		, GPUDescriptorHeap& samplerHeap
		, castl::vector<GPUExecutionBatch> const& executeBatchs
		, castl::vector<RenderPassGPUData> const& rasterPassGPUData)
	{
		ID3D12GraphicsCommandList7* pCommand = commandListMgr.DirectCommand();
		std::array<ID3D12DescriptorHeap*, 2> descHeaps = { resourceHeap.GetHeap().Get(),samplerHeap.GetHeap().Get()};
		pCommand->SetDescriptorHeaps(descHeaps.size(), descHeaps.data());
		for (int executeBatchID = 0; executeBatchID < executeBatchs.size(); ++executeBatchID)
		{
			auto& executeBatch = executeBatchs[executeBatchID];
			executeBatch.ExecuteAquireBarriers(pCommand, linearMemoryManager, resourceManager);

			//Rasterize Passes
			for (int rasterPassID : executeBatch.rasterPassRefs)
			{
				auto& rasterData = rasterPassGPUData[rasterPassID];
				auto& pass = owningGraph.GetRenderPasses()[rasterPassID];
				CA_ASSERT_BREAK(rasterData.drawcallBatchs.size() == pass.GetDrawCallBatches().size(), "Raster Pass Batch Count Inompatible");
				
				//Set Render Targets
				{
					castl::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
					rtvs.reserve(pass.GetAttachments().size());
					for (int attachmentID = 0; attachmentID < pass.GetAttachments().size(); ++attachmentID)
					{
						if (attachmentID != pass.GetDepthAttachmentIndex())
						{
							ImageHandle const& imageHandle = pass.GetAttachments()[attachmentID];
							auto pImageResource = resourceManager.GetImageResource(imageHandle);
							auto defaultImageView = GPUTextureView::CreateDefaultForRenderTarget(pImageResource->resourceDesc.format);
							auto foundView = pImageResource->resourceViews.find(defaultImageView);
							CA_ASSERT_BREAK(foundView != pImageResource->resourceViews.end(), "Attachment Image View Not Found");
							rtvs.push_back(foundView->second.rtv.CPUHandle());
						}
					}
					if (pass.HasDepthAttachment())
					{
						ImageHandle const& imageHandle = pass.GetAttachments()[pass.GetDepthAttachmentIndex()];
						auto pImageResource = resourceManager.GetImageResource(imageHandle);
						auto defaultImageView = GPUTextureView::CreateDefaultForRenderTarget(pImageResource->resourceDesc.format);
						auto foundView = pImageResource->resourceViews.find(defaultImageView);
						CA_ASSERT_BREAK(foundView != pImageResource->resourceViews.end(), "Depth Attachment Image View Not Found");
						D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = foundView->second.dsv.CPUHandle();
						pCommand->OMSetRenderTargets(rtvs.size(), rtvs.data(), FALSE, &dsvHandle);
					}
					else
					{
						pCommand->OMSetRenderTargets(rtvs.size(), rtvs.data(), FALSE, NULL);
					}

				}
	
				for (int batchID = 0; batchID < rasterData.drawcallBatchs.size(); ++batchID)
				{
					auto& batchData = rasterData.drawcallBatchs[batchID];
					auto& batch = pass.GetDrawCallBatches()[batchID];
					pCommand->SetPipelineState(batchData.pipelineInstances->GetPipelineState().Get());
					pCommand->SetGraphicsRootSignature(batchData.pipelineInstances->GetRootSignature().Get());
					pCommand->IASetPrimitiveTopology(batchData.topology);

					//Bind Descriptor Tables
					{
						int resourceHeapID = batchData.pipelineInstances->GetResourceHeapParamID();
						int samplerHeapID = batchData.pipelineInstances->GetSamplerHeapParamID();
						bool hasResourceHeap = resourceHeapID != -1;
						bool hasSamplerHeap = samplerHeapID != -1;
						//Descriptor Table Should Match Root Signature Param ID
						if (hasResourceHeap)
						{
							pCommand->SetGraphicsRootDescriptorTable(resourceHeapID
								, batchData.pResourceBindingInstance->GetBindingInfo().descriptorAllocation.GPUHandle());
						}
						if (hasSamplerHeap)
						{
							pCommand->SetGraphicsRootDescriptorTable(samplerHeapID
								, batchData.pResourceBindingInstance->GetBindingInfo().samplerAllocation.GPUHandle());
						}
					}

					//Commands For Drawcalls
					for (int drawcallID = 0; drawcallID < batch.m_DrawCalls.size(); ++drawcallID)
					{
						auto& drawcall = batch.m_DrawCalls[drawcallID];
						auto& drawInfo = drawcall.GetDrawInfo();
						auto& drawcallData = batchData.drawcalls[drawcallID];

						//Vertex Assembly Input
						{
							castl::vector<D3D12_VERTEX_BUFFER_VIEW>vertexBufferViews(drawcallData.inputAssemblyBindingBuffers.size());
							for (size_t vbid = 0; vbid < drawcallData.inputAssemblyBindingBuffers.size(); ++vbid)
							{
								auto&& [vertexInputDesc, bufferHandle] = drawcallData.inputAssemblyBindingBuffers[vbid];
								auto pBufferResource = resourceManager.GetBufferResource(bufferHandle);
								D3D12_VERTEX_BUFFER_VIEW& view = vertexBufferViews[vbid];
								view.BufferLocation = pBufferResource->gpuResource.GetResource()->GetGPUVirtualAddress();
								view.SizeInBytes = pBufferResource->resourceDesc.SizeInByte();
								view.StrideInBytes = vertexInputDesc.stride;
							}
							pCommand->IASetVertexBuffers(0, vertexBufferViews.size(), vertexBufferViews.data());
						}


						if (drawInfo.drawIndexed)
						{
							pCommand->DrawIndexedInstanced(drawInfo.indexCount, drawInfo.instanceCount, drawInfo.indexOffset, drawInfo.vertexOffset, drawInfo.firstInstanceID);
						}
						else
						{
							pCommand->DrawInstanced(drawInfo.vertexCount, drawInfo.instanceCount, drawInfo.vertexOffset, drawInfo.firstInstanceID);
						}
					}
				}
			}

			//Compute Passes

			//Transfer Passes
			for (int transferPassID : executeBatch.transferPassRefs)
			{
				GPUDataTransfers const& transferPass = owningGraph.GetDataTransfers()[transferPassID];
				for (auto& imageUploads : transferPass.m_ImageDataUploads)
				{
					auto&& [targetImageHandle, dataRef] = imageUploads;
					auto pImageResource = resourceManager.GetImageResource(targetImageHandle);
					auto targetGPUResource = pImageResource->gpuResource.GetResource();

					D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
					UINT numRows;
					UINT64 rowSizeInBytes;
					UINT64 totalBytes;
					D3D12_RESOURCE_DESC resourceDesc = targetGPUResource->GetDesc();
					const UINT subresourceNum = 1;
					pBackend->GetDevice()->GetCopyableFootprints(&resourceDesc
						, 0, subresourceNum, 0
						, &layouts, &numRows, &rowSizeInBytes, &totalBytes);

					ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(totalBytes);
					D3D12_SUBRESOURCE_DATA subresourceData{};
					subresourceData.pData = dataRef.pData;
					{
						size_t rowPitch;
						size_t slicePitch;
						DirectX::ComputePitch(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height
							, rowPitch, slicePitch, DirectX::CP_FLAGS_NONE);
						subresourceData.RowPitch = rowPitch;
						subresourceData.SlicePitch = slicePitch;
					}
					ThrowIfFailed(UpdateSubresources((ID3D12GraphicsCommandList*)pCommand
						, targetGPUResource, stagingBuffer
						, 0
						, subresourceNum, totalBytes, &layouts, &numRows, &rowSizeInBytes, &subresourceData));
				}
				for (auto& bufferUploads : transferPass.m_BufferDataUploads)
				{
					auto&& [targetBufferHandle, dataRef] = bufferUploads;
					auto pBufferHandle = resourceManager.GetBufferResource(targetBufferHandle);

					ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(dataRef.dataSize);
					UINT8* mappedStagingData;
					stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedStagingData));
					memcpy(mappedStagingData, dataRef.pData, dataRef.dataSize);
					stagingBuffer->Unmap(0, nullptr);

					pCommand->CopyBufferRegion(pBufferHandle->gpuResource.GetResource(), 0, stagingBuffer, 0, dataRef.dataSize);
				}
			}

			executeBatch.ExecuteReleaseBarriers(pCommand);
		}
		pCommand->Close();
		castl::vector<ID3D12CommandList*> commands = { pCommand };
		pBackend->GetDirectQueue()->ExecuteCommandLists(1, commands.data());
		ComPtr<ID3D12Fence> fance;
		pBackend->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fance));
		pBackend->GetDirectQueue()->Signal(fance.Get(), 1);
		while (fance->GetCompletedValue() < 1)
		{
			//Do Nothing;
		}
	}

	void InitArraySizes(GPUGraph const& owningGraph,
		castl::vector<PassRWState>& passRWStates,
		castl::vector<PassRWState>& computePassRWStates,
		castl::vector<PassRWState>& transferPassRWStates,
		castl::vector<RenderPassGPUData>& rasterPassGPUDataList,
		castl::vector<ComputePassGPUData>& computePassGPUDataList
	)
	{
		passRWStates.resize(owningGraph.GetRenderPasses().size());
		rasterPassGPUDataList.resize(owningGraph.GetRenderPasses().size());
		for (size_t rasterPassID = 0; rasterPassID < owningGraph.GetRenderPasses().size(); ++rasterPassID)
		{
			auto& pass = owningGraph.GetRenderPasses()[rasterPassID];
			auto& passData = rasterPassGPUDataList[rasterPassID];
			passData.drawcallBatchs.resize(pass.GetDrawCallBatches().size());
			for (size_t drawcalBatchID = 0; drawcalBatchID < pass.GetDrawCallBatches().size(); ++drawcalBatchID)
			{
				passData.drawcallBatchs[drawcalBatchID].drawcalls.resize(pass.GetDrawCallBatches()[drawcalBatchID].m_DrawCalls.size());
			}
		}
		computePassRWStates.resize(owningGraph.GetComputePasses().size());
		for (size_t computePassID = 0; computePassID < owningGraph.GetComputePasses().size(); ++computePassID)
		{
			auto& pass = owningGraph.GetComputePasses()[computePassID];
			auto& passData = computePassGPUDataList[computePassID];
			passData.dispatchs.resize(pass.dispatchs.size());
		}
		transferPassRWStates.resize(owningGraph.GetDataTransfers().size());
	}

	void D3D12GPUGraphExecutor::CompileAndExecute(GPUGraph const& owningGraph)
	{
		//收集当前图中所有的资源
		//并注册到m_LocalResourceManager中
		//整理每个pass的读写状态
		castl::vector<PassRWState> passRWStates;
		castl::vector<PassRWState> computePassRWStates;
		castl::vector<PassRWState> transferPassRWStates;
		castl::vector<RenderPassGPUData> rasterPassGPUDataList;
		castl::vector<ComputePassGPUData> computePassGPUDataList;
		InitArraySizes(owningGraph
			, passRWStates, computePassRWStates, transferPassRWStates
			, rasterPassGPUDataList, computePassGPUDataList
		);
		Prepare(owningGraph
			, GetApp()
			, m_LocalResourceManager
			, m_ConstantBufferManager
			, m_ShaderResourceInstances
			, passRWStates, computePassRWStates, transferPassRWStates
			, rasterPassGPUDataList
			, computePassGPUDataList);
		//整理出多个连续的无依赖batch
		castl::vector<GPUExecutionBatch> executionBatchs;
		BuildDependencyFreeBatchs(owningGraph
			, passRWStates
			, computePassRWStates
			, transferPassRWStates
			, executionBatchs
		);

		//统计资源的生命周期
		castl::unordered_map<ImageHandle, ResourceUsageRangeData> imageLifeTimes;
		castl::unordered_map<BufferHandle, ResourceUsageRangeData> bufferLifeTimes;
		castl::unordered_map<D3D2ShaderStruct const*, ResourceUsageRange> cbufferLifeTimes;
		BuildResourceUsageRanges(imageLifeTimes, bufferLifeTimes, cbufferLifeTimes, executionBatchs);

		//依据资源的生命周期实际分配资源
		m_LocalResourceManager.AllocateAliasedResources(executionBatchs.size(), imageLifeTimes, bufferLifeTimes
			, cbufferLifeTimes, m_ConstantBufferManager);
		//为资源创建 descriptor
		m_LocalResourceManager.PrepareResourceDescriptors(m_DescriptorAllocatorSet);
		//构建每个shader实例的资源绑定集合
		m_ShaderResourceInstances.for_each([&](auto& shaderSet, castl::shared_ptr<GPUResourceBindingInstance>& pInstance)->void
		{
			pInstance->BuildDescriptors(m_LocalResourceManager, m_ResourceGPUHeap, m_SamplerGPUHeap);
		});

		//创建PipelineBarriers, 目前确保在资源创建完毕后再调用
		PrepareBatchResourceBarriers(executionBatchs
			, imageLifeTimes
			, bufferLifeTimes
			, cbufferLifeTimes
			, m_ConstantBufferManager
			, m_LocalResourceManager);

		//创建PSO:TODO 并行化
		BuildPipelineStates(GetApp()
			, owningGraph
			, m_LocalResourceManager
			, rasterPassGPUDataList
			, computePassGPUDataList);

		Execute(GetApp()
			, owningGraph
			, m_CommandListManager
			, m_StagingMemoryManager
			, m_LocalResourceManager
			, m_ResourceGPUHeap
			, m_SamplerGPUHeap
			, executionBatchs
			, rasterPassGPUDataList);


	}
}