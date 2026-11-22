#include "GPUGraphExecutor.h"
#include <GPUGraph/D3D12GPUTiming.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <Utils/InterfaceTranslation.h>
#include <RenderBackend_D3D12.h>
#include <GPUGraph/GPUPipelineInstance.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>
#include <D3D12Debug.h>
#include <CASTL/CASet.h>

namespace graphics_backend
{

	// ===================== vulkan-gpu-queries 任务 2.1：计时的私有辅助 =====================
	// 名称规则（design D3）：渲染 pass 取 RenderPass::GetName()，无效时 render[<原始序号>]；
	// 计算 pass 没有名字，用 compute[<原始序号>]。
	namespace
	{
		// 把设备 tick 换算成纳秒：先除后乘，避免 ticks * 1e9 溢出。frequency 是"每秒 tick 数"。
		uint64_t TimingTicksToNanoseconds(uint64_t ticks, uint64_t frequency)
		{
			if (frequency == 0)
			{
				return 0;
			}
			uint64_t const wholeSeconds = ticks / frequency;
			uint64_t const remainder = ticks % frequency;
			return wholeSeconds * 1000000000ull + (remainder * 1000000000ull) / frequency;
		}

		castl::string GetRenderPassTimingName(GPUGraph const& graph, uint32_t rasterPassID)
		{
			auto const& name = graph.GetRenderPasses()[rasterPassID].GetName();
			if (!name.Get().empty())
			{
				return name.string();
			}
			return castl::string("render[") + std::to_string(rasterPassID) + "]";
		}

		castl::string GetComputePassTimingName(uint32_t computePassID)
		{
			return castl::string("compute[") + std::to_string(computePassID) + "]";
		}

		bool WaitForTimingFence(ID3D12Fence* pFence, uint64_t value)
		{
			if (pFence == nullptr)
			{
				return false;
			}
			if (pFence->GetCompletedValue() >= value)
			{
				return true;
			}
			HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (eventHandle == nullptr)
			{
				return false;
			}
			bool const ok = SUCCEEDED(pFence->SetEventOnCompletion(value, eventHandle))
				&& (WaitForSingleObject(eventHandle, INFINITE) == WAIT_OBJECT_0);
			CloseHandle(eventHandle);
			return ok;
		}
	}

	GPUTextureDescriptor GetDescriptor(GPUGraph const& graph, ImageHandle const& image)
	{
		switch (image.GetType())
		{
		case ImageHandle::ImageType::External:
			return image.GetTexturePtr<D3DImageObject>()->GetDescriptor();
		case ImageHandle::ImageType::Backbuffer:
			return image.GetWindowPtr<WindowContext>()->GetBackbufferDescriptor();
		case ImageHandle::ImageType::Internal:
		{
			GPUTextureDescriptor const* pdesc = graph.GetImageManager().GetDescriptor(image.GetKey());
			CA_ASSERT_BREAK(pdesc != nullptr, "Internal Image Not Registered");
			return *pdesc;
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Image Handle");
			return {};
		}
	}

	GPUBufferDescriptor GetDescriptor(GPUGraph const& graph, BufferHandle const& buffer)
	{
		switch (buffer.GetType())
		{
		case BufferHandle::BufferType::External:
			return buffer.GetBufferPtr<D3DBufferObject>()->GetDescriptor();
		case BufferHandle::BufferType::Internal:
		{
			GPUBufferDescriptor const* pdesc = graph.GetBufferManager().GetDescriptor(buffer.GetKey());
			CA_ASSERT_BREAK(pdesc != nullptr, "Internal Buffer Not Registered");
			return *pdesc;
		}
		default:
			CA_LOG_ERR_BREAK("Invalid Buffer Handle");
			return {};
		}
	}


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
			D3D12_VIEWPORT defaultViewport;
			D3D12_RECT defaultSissors;
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
	{
	}

	struct PassRWState
	{
		castl::unordered_map<ImageHandle, ResourceState> imageRWStates;
		castl::unordered_map<BufferHandle, ResourceState> bufferRWStates;
		castl::unordered_map<D3D2ShaderStruct const*, EGPUQueueTypeFlags> cBufferUsageStates;
		EGPUQueueTypeFlags batchResourceQueueTypes = 0;

		bool Depends(PassRWState const& successor) const
		{
			for (auto& pair : imageRWStates)
			{
				auto&& [img, rwState] = pair;
				auto found = successor.imageRWStates.find(img);
				if (found != successor.imageRWStates.end())
				{
					if (rwState.Write() || found->second.Write())
					{
						return true;
					}
					if (!rwState.CompatibleToCombine(found->second))
					{
						return true;
					}
				}
			}
			for (auto& pair : bufferRWStates)
			{
				auto&& [buf, rwState] = pair;
				auto found = successor.bufferRWStates.find(buf);
				if (found != successor.bufferRWStates.end())
				{
					if (rwState.Write() || found->second.Write())
					{
						return true;
					}
					if (!rwState.CompatibleToCombine(found->second))
					{
						return true;
					}
				}
			}
			return false;
		}

		void Append(PassRWState const& other)
		{
			batchResourceQueueTypes |= other.batchResourceQueueTypes;
			for (auto pair : other.imageRWStates)
			{
				auto found = imageRWStates.find(pair.first);
				if (found == imageRWStates.end())
				{
					imageRWStates.insert(pair);
				}
				else
				{
					imageRWStates[pair.first].Combine(pair.second);
				}
			}
			for (auto pair : other.bufferRWStates)
			{
				auto found = bufferRWStates.find(pair.first);
				if (found == bufferRWStates.end())
				{
					bufferRWStates.insert(pair);
				}
				else
				{
					bufferRWStates[pair.first].Combine(pair.second);
				}
			}
			for (auto pair : other.cBufferUsageStates)
			{
				auto found = cBufferUsageStates.find(pair.first);
				if (found == cBufferUsageStates.end())
				{
					cBufferUsageStates.insert(pair);
				}
				else
				{
					cBufferUsageStates[pair.first] |= pair.second;
				}
			}
		}

		void SetImageRWStateNoView(ImageHandle const& image
			, EShaderTypeFlags stages
			, EResourceUsageFlags usages
			, ShaderCompilerSlang::EShaderResourceAccess access
			, EGPUQueueType queueType)
		{

			ResourceState newResourceState{
				access,
				stages,
				usages,
				queueType,
				true
			};

			auto found = imageRWStates.find(image);
			if (found != imageRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.CompatibleToCombine(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				imageRWStates.insert(castl::make_pair(image, newResourceState));
			}
			batchResourceQueueTypes |= newResourceState.queueTypes;
		}

		void SetImageRWState(ImageHandle const& image
			, EShaderTypeFlags stages
			, EResourceUsageFlags usages
			, ShaderCompilerSlang::EShaderResourceAccess access
			, EGPUQueueType queueType)
		{

			ResourceState newResourceState{
				access,
				stages,
				usages,
				queueType,
				true
			};
			if (newResourceState.NotUsed())
				return;

			auto found = imageRWStates.find(image);
			if (found != imageRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.CompatibleToCombine(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				imageRWStates.insert(castl::make_pair(image, newResourceState));
			}
			batchResourceQueueTypes |= newResourceState.queueTypes;
		}
		void SetBufferRWState(BufferHandle const& buffer
			, EShaderTypeFlags stages
			, EResourceUsageFlags usages
			, ShaderCompilerSlang::EShaderResourceAccess access
			, EGPUQueueType queueType
		)
		{

			ResourceState newResourceState{
				access,
				stages,
				usages,
				queueType,
				false
			};
			if (newResourceState.NotUsed())
				return;

			auto found = bufferRWStates.find(buffer);
			if (found != bufferRWStates.end())
			{
				CA_ASSERT_BREAK(found->second.CompatibleToCombine(newResourceState), "Resource State Not Compatible");
				found->second.Combine(newResourceState);
			}
			else
			{
				bufferRWStates.insert(castl::make_pair(buffer, newResourceState));
			}
			batchResourceQueueTypes |= newResourceState.queueTypes;
		}
		void SetCBufferUsageState(D3D2ShaderStruct const* pCBufferStruct
			, EShaderTypeFlags stages
			, EGPUQueueType queueType)
		{
			if (stages == 0)
				return;
			auto found = cBufferUsageStates.find(pCBufferStruct);
			if (found != cBufferUsageStates.end())
			{
				found->second |= queueType;
			}
			else
			{
				cBufferUsageStates.insert(castl::make_pair(pCBufferStruct, queueType));
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
			if (rwState.Depends(successor->rwState))
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
		PassRWState& finalPassRWStates,
		castl::vector<RenderPassGPUData>& rasterGPUData,
		castl::vector<ComputePassGPUData>& computeGPUData
	)
	{
		//将一个pass中所有资源的读写状态注册进PassRWState中，包括CBuffer
		auto addPassShaderInstancesResourcesRWStates = [&](EGPUQueueType queueType, PassRWState& passRWState,
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
						passRWState.SetImageRWState(img.image
							, imageInfo.usingStages
							, imageInfo.resourceUsages
							, imageInfo.bindingInfo.accessType
							, queueType);
					}
				},
					[&](GPUResourceBindingInstance::BufferBindingElement const& bufferInfo)
				{
					for (auto& buf : bufferInfo.bindings)
					{
						passRWState.SetBufferRWState(buf
							, bufferInfo.usingStages
							, bufferInfo.resourceUsages
							, bufferInfo.bindingInfo.accessType
							, queueType
						);
					}
				},
					[&](GPUResourceBindingInstance::CBufferBindingElement const& cbufferInfo)
				{
					passRWState.SetCBufferUsageState(cbufferInfo.pCBufferStruct
						, cbufferInfo.usingStages
						, queueType);
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
					auto descriptor = GetDescriptor(owningGraph, attachment);
					resourceManager.AddTexture(attachment, descriptor);
					if (attachmentID == renderPass.GetDepthAttachmentIndex())
					{
						passRWState.SetImageRWState(attachment, EShaderTypeMask::eNone
							, EResourceUsage::eDepthStencilTarget
							, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
							, EGPUQueueType::eDirect);
					}
					else
					{
						passRWState.SetImageRWState(attachment
							, EShaderTypeMask::eNone
							, EResourceUsage::eRenderTarget
							, ShaderCompilerSlang::EShaderResourceAccess::eReadWrite
							, EGPUQueueType::eDirect);
					}
					++attachmentID;
				}
			}


			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			auto& drawcalBatchs = renderPass.GetDrawCallBatches();

			for (size_t batchID = 0; batchID < drawcalBatchs.size(); ++batchID)
			{
				auto& drawcallBatch = drawcalBatchs[batchID];
				auto& batchGPUData = rasterPassGPUData.drawcallBatchs[batchID];
				for (auto& drawcall : drawcallBatch.m_DrawCalls)
				{
					//将drawcall的index和vertexbuffer注册进m_LocalResourceManager中
					if (drawcall.GetDrawInfo().drawIndexed)
					{
						auto& indesxBuffer = drawcall.GetIndexBuffer().indexBufferHandle;
						auto descriptor = GetDescriptor(owningGraph, indesxBuffer);
						//registerBufferToLocalResourceManager(indesxBuffer);
						resourceManager.AddBuffer(indesxBuffer, descriptor);

						passRWState.SetBufferRWState(indesxBuffer
							, EShaderTypeMask::eNone
							, EResourceUsage::eIndexInput
							, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
							, EGPUQueueType::eDirect
						);
					}
					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						auto& vertBuffer = vertBuf.second;
						auto descriptor = GetDescriptor(owningGraph, vertBuffer);
						passRWState.SetBufferRWState(vertBuffer
							, EShaderTypeMask::eNone
							, EResourceUsage::eVertexInput
							, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
							, EGPUQueueType::eDirect
						);
					}
				}
				auto pipelineData = PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), drawcallBatch.pipelineStateDesc);
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&drawcallBatch.shaderStructs);
				shaderStructs.push_back(&renderPass.GetShaderStructs());
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, pipelineData.m_ShaderInfo, shaderStructs);
				castl::shared_ptr<GPUResourceBindingInstance> resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
				{
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance);
				})->second;

				batchGPUData.pResourceBindingInstance = resourceBindingInstance.get();

				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(EGPUQueueType::eDirect, passRWState, passLocalBindingInstances);
		}
		CA_ASSERT_BREAK(computePassRWStates.size() == owningGraph.GetComputePasses().size(), "Compute Pass RW State Size Incompatible");
		for (size_t passID = 0; passID < owningGraph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = owningGraph.GetComputePasses()[passID];
			auto& passRWState = computePassRWStates[passID];
			auto& computePassGPUData = computeGPUData[passID];
			EGPUQueueType queueType = computePass.asyncCompute ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;
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
					return pApp->NewSubObject_Shared<GPUResourceBindingInstance>(resourceInstance);
				})->second;
				dispatchGPUData.pResourceBindingInstance = resourceBindingInstance.get();
				passLocalBindingInstances.insert(castl::make_pair(resourceSet, resourceBindingInstance));
			}
			addPassShaderInstancesResourcesRWStates(queueType, passRWState, passLocalBindingInstances);
		}
		CA_ASSERT_BREAK(transferPassRWStates.size() == owningGraph.GetDataTransfers().size(), "Transfer Pass RW State Size Incompatible");
		for (size_t passID = 0; passID < owningGraph.GetDataTransfers().size(); ++passID)
		{
			auto& transferPass = owningGraph.GetDataTransfers()[passID];
			auto& passRWState = transferPassRWStates[passID];
			castl::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for (auto& bufferWrites : transferPass.m_BufferDataUploads)
			{
				auto& uploadBuffer = bufferWrites.first;
				auto descriptor = GetDescriptor(owningGraph, uploadBuffer);
				//registerBufferToLocalResourceManager(bufferWrites.first);
				passRWState.SetBufferRWState(uploadBuffer
					, EShaderTypeMask::eNone
					, EResourceUsage::eCopy
					, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
					, EGPUQueueType::eDirect
				);
			}
			for (auto& imgWrites : transferPass.m_ImageDataUploads)
			{
				auto descriptor = GetDescriptor(owningGraph, imgWrites.first);
				resourceManager.AddTexture(imgWrites.first, descriptor);
				passRWState.SetImageRWStateNoView(imgWrites.first
					, EShaderTypeMask::eNone
					, EResourceUsage::eCopy
					, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
					, EGPUQueueType::eDirect
				);
			}
		}
		if (!owningGraph.GetFinalizePass().isEmpty())
		{
			for (auto& img : owningGraph.GetFinalizePass().m_ImageUsages)
			{
				auto descriptor = GetDescriptor(owningGraph, img.first);
				resourceManager.AddTexture(img.first, descriptor);
				finalPassRWStates.SetImageRWState(img.first, EShaderTypeMask::eFrag
					, EResourceUsage::eShaderResource
					, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
					, EGPUQueueType::eDirect);
			}
			for (auto& backBuffer : owningGraph.GetFinalizePass().m_PresentBackBuffers)
			{
				finalPassRWStates.SetImageRWState(backBuffer
					, EShaderTypeMask::eNone
					, EResourceUsage::ePresent
					, ShaderCompilerSlang::EShaderResourceAccess::eUnknown
					, EGPUQueueType::eDirect);
			}
		}
		shaderResourceInstances.for_each([&](ShaderResourceSet const& resourceSet
			, castl::shared_ptr<GPUResourceBindingInstance>& resourceInstance)
		{
			resourceInstance->IterateResourceUsages([&](auto const& imageInfo) {},
				[&](auto const& bufferInfo) {},
				[&](GPUResourceBindingInstance::CBufferBindingElement const& cbufferInfo)
			{
				constantBufferManager.GetConstantBufferHandle(cbufferInfo.pCBufferStruct);
			});
		});

		//将m_ConstantBufferManager中的constant buffer资源注册到m_LocalResourceManager中
		constantBufferManager.BuildResources(resourceManager);
		//将图中的资源注册到m_LocalResourceManager中
		owningGraph.GetBufferManager().Foreach([&](ResourceHandleKeyData const& handleKey
			, auto& desc)
		{
			resourceManager.AddBuffer(handleKey, desc);
		});
		owningGraph.GetImageManager().Foreach([&](ResourceHandleKeyData const& handleKey
			, auto& desc)
		{
			resourceManager.AddTexture(handleKey, desc);
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

		bool AnyBarrier() const
		{
			return !cbufferData.empty();
		}

		void CbufferInitializeBarriers(ID3D12GraphicsCommandList7* pCommandList
			, D3D12GraphLocalResourceManager& resourceManager
			, castl::vector<D3D12_BUFFER_BARRIER>& beforeBarriers
			, castl::vector<D3D12_BUFFER_BARRIER>& afterBarriers) const
		{
			//CBuffer Barriers
			for (auto& cbufferData : cbufferData)
			{
				BufferHandle const& bufferHandle = cbufferData.first;
				ID3D12Resource* targetBuffer = resourceManager.GetBufferD3D12Resource(bufferHandle);

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
			for (auto& cbufferData : cbufferData)
			{
				BufferHandle const& bufferHandle = cbufferData.first;
				ID3D12Resource* targetBuffer = resourceManager.GetBufferD3D12Resource(bufferHandle);

				D3D2ShaderStruct const* pStruct = cbufferData.second;
				ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(pStruct->GetCBufferSize());
				UINT8* mappedStagingData;
				stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedStagingData));
				pStruct->ComputeMaxChildrenVersion();
				pStruct->UpdateUniformBuffer(0, mappedStagingData, pStruct->GetCBufferSize(), 0);
				stagingBuffer->Unmap(0, nullptr);
				pCommandList->CopyBufferRegion(targetBuffer, 0, stagingBuffer, 0, pStruct->GetCBufferSize());
			}
		}


		void ExecuteCBufferInitializationBarriers(ID3D12GraphicsCommandList7* pCommandList
			, LinearMemoryManager& linearMemoryManager
			, D3D12GraphLocalResourceManager& resourceManager) const
		{
			//CBuffer Barriers
			if (!cbufferData.empty())
			{
				castl::vector<D3D12_BUFFER_BARRIER> toCopyDestBarriers;
				castl::vector<D3D12_BUFFER_BARRIER> toCBufferBarriers;
				CbufferInitializeBarriers(pCommandList, resourceManager, toCopyDestBarriers, toCBufferBarriers);

				//none->copyDest
				CD3DX12_BARRIER_GROUP toCopyDestBarrierGroup(toCopyDestBarriers.size(), toCopyDestBarriers.data());
				pCommandList->Barrier(1, &toCopyDestBarrierGroup);

				CBufferCopyInitialize(pCommandList, linearMemoryManager, resourceManager);

				//copyDest->CBuffer
				CD3DX12_BARRIER_GROUP toCBufferBarrierGroup(toCBufferBarriers.size(), toCBufferBarriers.data());
				pCommandList->Barrier(1, &toCBufferBarrierGroup);
			}
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
		bool IsEmpty() const
		{
			return imageBarriers.empty() && bufferBarriers.empty();
		}
		bool AnyBarrier() const
		{
			return !IsEmpty();
		}

		void ExecuteBarriers(ID3D12GraphicsCommandList7* pCommandList) const
		{
			castl::vector<D3D12_BARRIER_GROUP> barrierGroups;
			castl::vector<D3D12_TEXTURE_BARRIER> images;
			for (auto& aquireImageBarriers : imageBarriers)
			{
				images.push_back(aquireImageBarriers.second);
			}
			if (!images.empty())
			{
				CD3DX12_BARRIER_GROUP imageBarrierGroup(images.size(), images.data());
				barrierGroups.push_back(imageBarrierGroup);
			}
			castl::vector<D3D12_BUFFER_BARRIER> buffers;

			for (auto& aquireBufferBarriers : bufferBarriers)
			{
				buffers.push_back(aquireBufferBarriers.second);
			}
			if (!buffers.empty())
			{
				CD3DX12_BARRIER_GROUP bufferBarrierGroup(buffers.size(), buffers.data());
				barrierGroups.push_back(bufferBarrierGroup);
			}
			if (!barrierGroups.empty())
			{
				pCommandList->Barrier(barrierGroups.size(), barrierGroups.data());
			}
		}

	};

	//struct GPUFinalizeBatch
	//{
	//	RenderStateBarriers finalizeBarriers;

	//	void ExecuteFinalizeBarriers(ID3D12GraphicsCommandList7* pCommandList) const
	//	{
	//		castl::vector<D3D12_BARRIER_GROUP> barrierGroups;
	//		castl::vector<D3D12_TEXTURE_BARRIER> imageBarriers;
	//		for (auto& aquireImageBarriers : finalizeBarriers.imageBarriers)
	//		{
	//			imageBarriers.push_back(aquireImageBarriers.second);
	//		}
	//		if (!imageBarriers.empty())
	//		{
	//			CD3DX12_BARRIER_GROUP imageBarrierGroup(imageBarriers.size(), imageBarriers.data());
	//			barrierGroups.push_back(imageBarrierGroup);
	//		}
	//		castl::vector<D3D12_BUFFER_BARRIER> bufferBarriers;

	//		for (auto& aquireBufferBarriers : finalizeBarriers.bufferBarriers)
	//		{
	//			bufferBarriers.push_back(aquireBufferBarriers.second);
	//		}
	//		if (!bufferBarriers.empty())
	//		{
	//			CD3DX12_BARRIER_GROUP bufferBarrierGroup(bufferBarriers.size(), bufferBarriers.data());
	//			barrierGroups.push_back(bufferBarrierGroup);
	//		}
	//		if (!barrierGroups.empty())
	//		{
	//			pCommandList->Barrier(barrierGroups.size(), barrierGroups.data());
	//		}
	//	}

	//};

	struct GPUExecutionBatch
	{
		std::vector<uint32_t> rasterPassRefs;
		std::vector<uint32_t> computePassRefs;
		std::vector<uint32_t> transferPassRefs;
		bool hasFinalizePass;
		//General Direct Queue Aquire/Release Barriers，Do Barriers For Both Direct And Compute Queue
		RenderStateBarriers aquireBarriers;
		//bool aquireForDirectAndComputeQueues;
		RenderStateBarriers releaseBarriers;
		//bool releaseForDirectAndComputeQueues;

		RenderStateBarriers computeAquireBarriers;
		RenderStateBarriers computeReleaseBarriers;

		CBufferInitializeBarriers cbufferBarriers;
		CBufferInitializeBarriers computeCBufferBarriers;


		PassRWState batchRWStates;
		bool anyComputeQueueOperations;

		//QueueFenceSyncPoint
		EGPUQueueTypeFlags aquireBarriersEmitFenceQueues = EGPUQueueType::eNone;
		EGPUQueueTypeFlags bodyCommandsEmitFenceQueues = EGPUQueueType::eNone;
		EGPUQueueTypeFlags releaseBarriersEmitFenceQueues = EGPUQueueType::eNone;

		castl::set<uint32_t> computeWaitingDirectBatches;
		castl::set<uint32_t> directWaitingComputeBatches;

		RenderStateBarriers& GetReleaseBarriers(bool computeLocal)
		{
			return computeLocal ? computeReleaseBarriers : releaseBarriers;
		}

		RenderStateBarriers& GetAquireBarriers(bool computeLocal)
		{
			return computeLocal ? computeAquireBarriers : aquireBarriers;
		}

		RenderStateBarriers& GetReleaseBarriers(EGPUQueueTypeFlags queueFlags)
		{
			if (queueFlags == EGPUQueueType::eCompute)
				return computeReleaseBarriers;
			return releaseBarriers;
		}

		RenderStateBarriers& GetAquireBarriers(EGPUQueueTypeFlags queueFlags)
		{
			if (queueFlags == EGPUQueueType::eCompute)
				return computeAquireBarriers;
			return aquireBarriers;
		}

		CBufferInitializeBarriers& GetCBufferBarriers(EGPUQueueTypeFlags queueFlags)
		{
			if (queueFlags == EGPUQueueType::eCompute)
				return computeCBufferBarriers;
			return cbufferBarriers;
		}
	};



	void BuildDependencyFreeBatchs(GPUGraph const& owningGraph,
		//D3D12GPUGraphExecutor& executor,
		castl::vector<PassRWState> const& rasterPassRWStates,
		castl::vector<PassRWState> const& computePassRWStates,
		castl::vector<PassRWState> const& transferPassRWStates,
		PassRWState const& finalPassRWStates,
		castl::vector<GPUExecutionBatch>& outExecutionBatchs
	)
	{
		using EGraphStageType = GPUGraph::EGraphStageType;
		bool hasFinalBatch = !owningGraph.GetFinalizePass().isEmpty();
		auto& stages = owningGraph.GetGraphStages();
		CA_ASSERT_BREAK(stages.size() == (rasterPassRWStates.size() + computePassRWStates.size() + transferPassRWStates.size()), "Graph Nodes Size Not Equal");
		size_t stageCount = stages.size() + (hasFinalBatch ? 1 : 0);
		castl::vector<PassDependency> passDeps;
		passDeps.reserve(stageCount);
		castl::list<PassDependency*> pendingDependencies;
		size_t stageID = 0;
		for (stageID; stageID < owningGraph.GetGraphStages().size(); ++stageID)
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

		if (!owningGraph.GetFinalizePass().isEmpty())
		{
			passDeps.emplace_back(finalPassRWStates, 0, EGraphStageType::eFinalPass);
			pendingDependencies.push_back(&passDeps.back());
		}

		//if (owningGraph.GetGraphStages().size() < 2)
		//	return;
		for (size_t prevPass = 0; prevPass < passDeps.size() - 1; ++prevPass)
		{
			for (size_t latterPass = prevPass + 1; latterPass < passDeps.size(); ++latterPass)
			{
				passDeps[prevPass].CheckAddSuccessor(&passDeps[latterPass]);
			}
		}
		while (!pendingDependencies.empty())
		{
			GPUExecutionBatch& newBatch = outExecutionBatchs.emplace_back();
			newBatch.anyComputeQueueOperations = false;
			std::vector<PassDependency*> passFreeDeps;
			auto depItr = pendingDependencies.begin();
			while (depItr != pendingDependencies.end())
			{
				PassDependency* dep = *depItr;
				if (dep->DepsFree())
				{
					depItr = pendingDependencies.erase(depItr);
					passFreeDeps.push_back(dep);
					//add to newPass
					newBatch.batchRWStates.Append(dep->rwState);
					switch (dep->passType)
					{
					case EGraphStageType::eRenderPass:
						newBatch.rasterPassRefs.push_back(dep->passID);
						break;
					case EGraphStageType::eComputePass:
						{
							newBatch.anyComputeQueueOperations = newBatch.anyComputeQueueOperations || owningGraph.GetComputePasses()[dep->passID].asyncCompute;
							newBatch.computePassRefs.push_back(dep->passID);
						}
						break;
					case EGraphStageType::eTransferPass:
						newBatch.transferPassRefs.push_back(dep->passID);
						break;
					case EGraphStageType::eFinalPass:
						newBatch.hasFinalizePass = true;
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

		////Add Finalize Batch Here
		//bool NeedFinalizeBatch = !owningGraph.GetPresentBackBuffers().empty();
		//if (NeedFinalizeBatch)
		//{
		//	GPUExecutionBatch& newBatch = outExecutionBatchs.emplace_back();
		//	newBatch.anyComputeQueueOperations = false;
		//	for (auto& backBuffer : owningGraph.GetPresentBackBuffers())
		//	{
		//		newBatch.batchRWStates.SetImageRWState(backBuffer
		//			, EShaderTypeMask::eNone
		//			, EResourceUsage::ePresent
		//			, ShaderCompilerSlang::EShaderResourceAccess::eUnknown
		//			, EGPUQueueType::eDirect);
		//	}
		//}
	}

	void ApplyExternalResourceStates(GPUGraph const& owningGraph, castl::unordered_map<ImageHandle, ResourceUsageRangeData>& imageRanges,
		castl::unordered_map<BufferHandle, ResourceUsageRangeData>& bufferRanges)
	{
		for (auto pair : imageRanges)
		{
			auto&& [image, resourceUsageRange] = pair;
			if (!image.IsIntternal())
			{
				CA_ASSERT_BREAK(!resourceUsageRange.states.empty(), "Image {} Has Empty States", image.GetName());
				auto& lastState = resourceUsageRange.states.back();
				switch (image.GetType())
				{
				case ImageHandle::ImageType::External:
				{
					auto texturePtr = image.GetTexturePtr<D3DImageObject>();
					texturePtr->ApplyResourceState(lastState.state);
					break;
				}
				case ImageHandle::ImageType::Backbuffer:
				{
					WindowContext* pWindow = image.GetWindowPtr<WindowContext>();
					pWindow->ApplyCurrentBackBufferResourceState(lastState.state);
					break;
				}
				}
			}
		}

		//for (auto& backBufferImage : owningGraph.GetFinalizePass().m_PresentBackBuffers)
		//{
		//	WindowContext* pWindow = backBufferImage.GetWindowPtr<WindowContext>();
		//	pWindow->ApplyCurrentBackBufferResourceState(ResourceState::PresentState());
		//}

		for (auto pair : bufferRanges)
		{
			auto&& [buffer, resourceUsageRange] = pair;
			if (!buffer.IsIntternal())
			{
				CA_ASSERT_BREAK(!resourceUsageRange.states.empty(), "Buffer {} Has Empty States", buffer.GetName());
				auto& lastState = resourceUsageRange.states.back();
				switch (buffer.GetType())
				{
				case BufferHandle::BufferType::External:
				{
					auto bufPtr = buffer.GetBufferPtr<D3DBufferObject>();
					bufPtr->ApplyResourceState(lastState.state);
					break;
				}
				}
			}
		}



	}

	void BuildResourceUsageRanges(castl::unordered_map<ImageHandle, ResourceUsageRangeData>& imageRanges,
		castl::unordered_map<BufferHandle, ResourceUsageRangeData>& bufferRanges,
		castl::unordered_map<D3D2ShaderStruct const*, CBufferUsageData>& cbufferRanges,
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
			for (auto cbufferStruct : rwStates.cBufferUsageStates)
			{
				cbufferRanges[cbufferStruct.first].Encapsule(batchID, cbufferStruct.second);
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

				GPUTextureDescriptor desc = GetDescriptor(owningGraph, rasterPass.GetAttachments()[0]);
				cacore::HashObj<ViewRectData> passLevelViewport = ViewRectData{
					0,0,
					static_cast<int>(desc.width),
					static_cast<int>(desc.height)
				};
				cacore::HashObj<ViewRectData> passLevelSissors = passLevelViewport;

				auto& attachments = rasterPass.GetAttachments();
				CA_ASSERT_BREAK(rasterPass.GetDrawCallBatches().size() == rasterPassData.drawcallBatchs.size(), "drawcall batch Data Length Not Equal");
				for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
				{
					auto& batch = rasterPass.GetDrawCallBatches()[batchID];
					auto& batchData = rasterPassData.drawcallBatchs[batchID];

					auto& batchLevelViewport = cacore::HashObj<ViewRectData>::SelectIfValid(batch.m_ViewPort, passLevelViewport);
					auto& batchLevelSissor = cacore::HashObj<ViewRectData>::SelectIfValid(batch.m_Sissor, passLevelSissors);

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

						auto& drawcallLevelViewport = cacore::HashObj<ViewRectData>::SelectIfValid(drawcall.GetViewPort(), batchLevelViewport);
						auto& drawcallLevelSissor = cacore::HashObj<ViewRectData>::SelectIfValid(drawcall.GetScissor(), batchLevelSissor);

						drawcallData.defaultViewport = ViewRectDataToD3D12Viewport(drawcallLevelViewport.Get());
						drawcallData.defaultSissors = ViewRectDataToD3D12Rect(drawcallLevelSissor.Get());

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

	void PrepareBatchResourceBarriers(GPUGraph const& owningGraph
		//, GPUFinalizeBatch& finalizeBatch
		, castl::vector<GPUExecutionBatch>& executionBatchs
		, castl::unordered_map<ImageHandle, ResourceUsageRangeData>& imageLifetimes
		, castl::unordered_map<BufferHandle, ResourceUsageRangeData>& bufferLifetimes
		, castl::unordered_map<D3D2ShaderStruct const*, CBufferUsageData> const& cbufferLifetimes
		, GPUConstantBufferManager& constantBufferManager
		, D3D12GraphLocalResourceManager& resourceManager
	)
	{
		for (auto& pair : imageLifetimes)
		{
			ImageHandle const& image = pair.first;
			ResourceState cachedState;
			if (image.IsIntternal())
			{
				cachedState = ResourceState::InitializedState();
			}
			else if (image.GetType() == ImageHandle::ImageType::External)
			{
				D3DImageObject const* pImage = static_cast<D3DImageObject const*>(image.GetExternalManagedTexture().get());
				cachedState = pImage->GetResourceState();
			}
			else if (image.GetType() == ImageHandle::ImageType::Backbuffer)
			{
				WindowContext const* pWindow = static_cast<WindowContext const*>(image.GetWindowHandle().get());
				cachedState = pWindow->GetCurrentBackBufferResourceState();
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

				if (lastState == currentState)
				{
					//No State Change, No Barrier Needed
					continue;
				}

				ResourceBarrierUsageStates lastBarrierStates = DetermineResourceBarrierUsageStates(lastState);
				ResourceBarrierUsageStates currentBarrierStates = DetermineResourceBarrierUsageStates(currentState);

				bool currentHasQueueLocalLayout = currentBarrierStates.HasQueueLocalState();
				bool lastHasQueueLocalLayout = lastBarrierStates.HasQueueLocalState();
				bool computeWaitDirect = currentState.hasComputeQueue() && lastState.hasDirectQueue();
				bool directWaitCompute = currentState.hasDirectQueue() && lastState.hasComputeQueue();
				bool anyCrossQueueWaiting = computeWaitDirect || directWaitCompute;
				bool asyncSplitQueueType = (currentHasQueueLocalLayout || lastHasQueueLocalLayout) && anyCrossQueueWaiting;

				if (anyCrossQueueWaiting)
				{
					//We Need Fence To Sync Resource Dependencies Between Queues
					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];
					if (computeWaitDirect)
					{
						lastBatch.releaseBarriersEmitFenceQueues |= EGPUQueueType::eDirect;
						currentBatch.computeWaitingDirectBatches.insert(lastBatchID);
					}
					if (directWaitCompute)
					{
						lastBatch.releaseBarriersEmitFenceQueues |= EGPUQueueType::eCompute;
						currentBatch.directWaitingComputeBatches.insert(lastBatchID);
					}
					if (lastState.isSharedBetweenQueues())
					{
						lastBatch.bodyCommandsEmitFenceQueues |= EGPUQueueType::eCompute;
					}
					if (currentState.isSharedBetweenQueues())
					{
						currentBatch.aquireBarriersEmitFenceQueues |= EGPUQueueType::eDirect;
					}
				}

				if (asyncSplitQueueType)
				{
					//Special Transition For Transition Between Queue Local Layouts
					//previous queue local layout -> common layout -> next queue local layout

					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];
					//When Queue Changed We Need Special, More Restrict Barrier
					D3D12_TEXTURE_BARRIER resouceBarrier{};
					resouceBarrier.AccessBefore = lastBarrierStates.accessState;
					resouceBarrier.AccessAfter = currentBarrierStates.accessState;
					resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
					resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
					resouceBarrier.pResource = resourceManager.GetImageD3D12Resource(image);
					resouceBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);

					//Before, Transition to an intermediate layout
					if(lastBarrierStates.queueLocalLayoutState != currentBarrierStates.layoutState)
					{
						D3D12_TEXTURE_BARRIER releaseBarrier = resouceBarrier;
						releaseBarrier.LayoutBefore = lastBarrierStates.queueLocalLayoutState;
						releaseBarrier.LayoutAfter = currentBarrierStates.layoutState;

						lastBatch.GetReleaseBarriers(lastState.queueTypes).AddImageBarrier(image, releaseBarrier);
					}
					//After, Transition from intermediate layout to target layout
					if(currentBarrierStates.layoutState != currentBarrierStates.queueLocalLayoutState)
					{
						D3D12_TEXTURE_BARRIER aquireBarrier = resouceBarrier;
						aquireBarrier.LayoutBefore = lastBarrierStates.layoutState;
						aquireBarrier.LayoutAfter = currentBarrierStates.queueLocalLayoutState;

						currentBatch.GetAquireBarriers(currentState.queueTypes).AddImageBarrier(image, aquireBarrier);
					}
				}
				else
				{
					D3D12_TEXTURE_BARRIER resouceBarrier{};
					resouceBarrier.AccessBefore = lastBarrierStates.accessState;
					resouceBarrier.AccessAfter = currentBarrierStates.accessState;
					resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
					resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
					resouceBarrier.LayoutBefore = lastBarrierStates.queueLocalLayoutState;
					resouceBarrier.LayoutAfter = currentBarrierStates.queueLocalLayoutState;
					resouceBarrier.pResource = resourceManager.GetImageD3D12Resource(image);
					resouceBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(UINT_MAX);

					if (stateHaveGap)
					{
						CA_ASSERT_BREAK(!isFirstState, "First State Should Not Have Gap");
						CA_ASSERT_BREAK(lastBatchID >= 0, "Last Batch ID Should Not Be -1");

						auto& currentBatch = executionBatchs[currentBatchID];
						auto& lastBatch = executionBatchs[lastBatchID];

						//Have Gap, Using Split Barrier
						D3D12_TEXTURE_BARRIER releaseBarrier = resouceBarrier;
						//releaseBarrier.SyncAfter |= D3D12_BARRIER_SYNC_SPLIT;
						releaseBarrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
						lastBatch.GetReleaseBarriers(lastState.queueTypes).AddImageBarrier(image, releaseBarrier);


						D3D12_TEXTURE_BARRIER aquireBarrier = resouceBarrier;
						//aquireBarrier.SyncBefore |= D3D12_BARRIER_SYNC_SPLIT;
						aquireBarrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
						currentBatch.GetAquireBarriers(currentState.queueTypes).AddImageBarrier(image, aquireBarrier);
					}
					else
					{
						auto& currentBatch = executionBatchs[currentBatchID];
						currentBatch.GetAquireBarriers(currentState.queueTypes).AddImageBarrier(image, resouceBarrier);
					}
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

				bool usageCompatible = ResourceUsageCompatible(lastState.resourceUsage, currentState.queueTypes)
					&& ResourceUsageCompatible(currentState.resourceUsage, lastState.queueTypes);

				bool computeWaitDirect = currentState.hasComputeQueue() && lastState.hasDirectQueue();
				bool directWaitCompute = currentState.hasDirectQueue() && lastState.hasComputeQueue();
				bool anyCrossQueueWaiting = computeWaitDirect || directWaitCompute;

				if (anyCrossQueueWaiting)
				{
					//We Need Fence To Sync Resource Dependencies Between Queues
					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];
					if (computeWaitDirect)
					{
						lastBatch.releaseBarriersEmitFenceQueues |= EGPUQueueType::eDirect;
						currentBatch.computeWaitingDirectBatches.insert(lastBatchID);
					}
					if (directWaitCompute)
					{
						lastBatch.releaseBarriersEmitFenceQueues |= EGPUQueueType::eCompute;
						currentBatch.directWaitingComputeBatches.insert(lastBatchID);
					}
					if (lastState.isSharedBetweenQueues())
					{
						lastBatch.bodyCommandsEmitFenceQueues |= EGPUQueueType::eCompute;
					}
					if (currentState.isSharedBetweenQueues())
					{
						currentBatch.aquireBarriersEmitFenceQueues |= EGPUQueueType::eDirect;
					}
				}
				bool asyncSplitQueueType = !usageCompatible;
				if (asyncSplitQueueType)
				{
					auto& currentBatch = executionBatchs[currentBatchID];
					auto& lastBatch = executionBatchs[lastBatchID];

					//When Queue Changed We Need Special, More Restrict Barrier
					D3D12_BUFFER_BARRIER resouceBarrier{};
					resouceBarrier.AccessBefore = lastBarrierStates.accessState;
					resouceBarrier.AccessAfter = currentBarrierStates.accessState;
					resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
					resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
					resouceBarrier.pResource = resourceManager.GetBufferD3D12Resource(buffer);
					resouceBarrier.Offset = 0;
					resouceBarrier.Size = ULLONG_MAX;

					D3D12_BARRIER_ACCESS commonAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
					D3D12_BARRIER_SYNC commonSync = D3D12_BARRIER_SYNC_NONE;

					//Before, Transition to an intermediate layout
					{
						D3D12_BUFFER_BARRIER releaseBarrier = resouceBarrier;
						releaseBarrier.AccessAfter = commonAccess;
						releaseBarrier.SyncAfter = commonSync;

						lastBatch.GetReleaseBarriers(lastState.queueTypes).AddBufferBarrier(buffer, releaseBarrier);
					}
					//After, Transition from intermediate layout to target layout
					{
						D3D12_BUFFER_BARRIER aquireBarrier = resouceBarrier;
						aquireBarrier.AccessBefore = commonAccess;
						aquireBarrier.SyncBefore = commonSync;

						currentBatch.GetAquireBarriers(currentState.queueTypes).AddBufferBarrier(buffer, aquireBarrier);
					}
				}
				else
				{
					D3D12_BUFFER_BARRIER resouceBarrier{};
					resouceBarrier.AccessBefore = lastBarrierStates.accessState;
					resouceBarrier.AccessAfter = currentBarrierStates.accessState;
					resouceBarrier.SyncBefore = lastBarrierStates.barrierSync;
					resouceBarrier.SyncAfter = currentBarrierStates.barrierSync;
					resouceBarrier.pResource = resourceManager.GetBufferD3D12Resource(buffer);
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
						//releaseBarrier.SyncAfter |= D3D12_BARRIER_SYNC_SPLIT;
						releaseBarrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
						lastBatch.GetReleaseBarriers(lastState.queueTypes).AddBufferBarrier(buffer, releaseBarrier);

						D3D12_BUFFER_BARRIER aquireBarrier = resouceBarrier;
						//releaseBarrier.SyncBefore |= D3D12_BARRIER_SYNC_SPLIT;
						releaseBarrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
						currentBatch.GetAquireBarriers(currentState.queueTypes).AddBufferBarrier(buffer, aquireBarrier);

					}
					else
					{
						auto& currentBatch = executionBatchs[currentBatchID];
						currentBatch.GetAquireBarriers(currentState.queueTypes).AddBufferBarrier(buffer, resouceBarrier);
					}
				}



			}
		}

		//Constant Buffers Initialized In First Used Batch And Remain Unchanged In Whole Lifetime
		for (auto& pair : cbufferLifetimes)
		{
			auto& cbufferUsage = pair.second;
			if (cbufferUsage.lifeTime.empty())
				continue;
			D3D2ShaderStruct const* pStruct = pair.first;
			int initialBatchID = cbufferUsage.lifeTime.head();
			auto& initialBatch = executionBatchs[initialBatchID];

			initialBatch.GetCBufferBarriers(cbufferUsage.queueTypes).AddCBuffer(constantBufferManager.GetConstantBufferHandle(pStruct), pStruct);
			if (cbufferUsage.queueTypes.BitCount() > 1)
			{
				initialBatch.aquireBarriersEmitFenceQueues |= EGPUQueueType::eDirect;
			}
		}
	}

	class BatchCommandExecutionRanges
	{
	public:
		struct QueueExecutionRange
		{
			castl::vector<ID3D12GraphicsCommandList7*> commands;
			int waitingFenceID = 0;
			int signalFenceID = 0;
			void Signal(int fenceID)
			{
				if (fenceID == 0)return;
				signalFenceID = fenceID;
			}
			void Wait(int fenceID)
			{
				if (fenceID == 0)return;
				waitingFenceID = fenceID;
			}
			bool Signaled() const
			{
				return signalFenceID != 0;
			}
			bool Waited() const
			{
				return waitingFenceID != 0;
			}
			void Submit(ID3D12CommandQueue* queue, ID3D12Fence* fence, ID3D12Fence* waitFence)
			{
				if (waitingFenceID != 0)
				{
					queue->Wait(waitFence, waitingFenceID);
				}
				if (!commands.empty())
				{
					queue->ExecuteCommandLists(commands.size(), (ID3D12CommandList *const*)commands.data());
				}
				if (signalFenceID != 0)
				{
					queue->Signal(fence, signalFenceID);
				}
			}
		};

		struct CombinedQueueExecutionRange
		{
			QueueExecutionRange directQueueRange;
			QueueExecutionRange computeQueueRange;
			
			void Signal(int directFenceID, int computeFenceID)
			{
				CA_ASSERT_BREAK(directFenceID == 0 || !directQueueRange.Signaled(), "DirectQueue Should Not Be Signaled");
				CA_ASSERT_BREAK(computeFenceID == 0 || !computeQueueRange.Signaled(), "ComputeQueue Should Not Be Signaled");
				directQueueRange.Signal(directFenceID);
				computeQueueRange.Signal(computeFenceID);
			}
			void Wait(int directWaitFenceID, int computeWaitFenceID)
			{
				CA_ASSERT_BREAK((directWaitFenceID == 0) || (!directQueueRange.Waited()), "DirectQueue Should Not Wait");
				CA_ASSERT_BREAK((computeWaitFenceID == 0) || (!computeQueueRange.Waited()), "ComputeQueue Should Not Wait");
				directQueueRange.Wait(directWaitFenceID);
				computeQueueRange.Wait(computeWaitFenceID);
			}
			bool isSignaled() const
			{
				return directQueueRange.Signaled() || computeQueueRange.Signaled();
			}

			void Submit(RenderBackend_D3D12* pBackend, FrameLocalFences& fences)
			{
				{
					auto queue = pBackend->GetComputeQueue().Get();
					auto fence = fences.m_ComputeQueueFence.Get();
					auto waitFence = fences.m_DirectQueueFence.Get();
					computeQueueRange.Submit(queue, fence, waitFence);
				}
				{
					auto queue = pBackend->GetDirectQueue().Get();
					auto fence = fences.m_DirectQueueFence.Get();
					auto waitFence = fences.m_ComputeQueueFence.Get();
					directQueueRange.Submit(queue, fence, waitFence);
				}
			}

		};

		castl::vector<CombinedQueueExecutionRange> ranges;

		CombinedQueueExecutionRange& EnsureNonSignaledRange()
		{
			if (ranges.empty() || ranges.back().isSignaled())
			{
				ranges.emplace_back();
			}
			return ranges.back();
		}


	};



	struct SignalFenceIDPair
	{
		int directSignal = 0;
		int computeSignal = 0;

		bool AnySignal() const
		{
			return directSignal != 0 || computeSignal != 0;
		}

		void Signals(BatchCommandExecutionRanges::CombinedQueueExecutionRange& range) const
		{
			range.Signal(directSignal, computeSignal);
		}

		void Wait(BatchCommandExecutionRanges::CombinedQueueExecutionRange& range) const
		{
			//Signaled On Direct Queue, Waited By Compute Queue;
			range.Wait(computeSignal, directSignal);
		}

		void AssignSignals(EGPUQueueTypeFlags flags, int& directFenceCounter, int& computeFenceCounter)
		{
			directSignal = 0;
			computeSignal = 0;
			if (flags & EGPUQueueType::eDirect)
			{
				directSignal = ++directFenceCounter;
			}
			if (flags & EGPUQueueType::eCompute)
			{
				computeSignal = ++computeFenceCounter;
			}
		}

		SignalFenceIDPair Greater(SignalFenceIDPair const& other) const
		{
			SignalFenceIDPair result;
			result.directSignal = std::max(directSignal, other.directSignal);
			result.computeSignal = std::max(computeSignal, other.computeSignal);
			return result;
		}
	};

	class ReleaseResourceCommandSet
	{
		ID3D12GraphicsCommandList7* pDirectCmd = nullptr;
		ID3D12GraphicsCommandList7* pComputeCmd = nullptr;
	public:
		void Record(RenderBackend_D3D12* backend
			, GPUExecutionBatch const& executionBatch
			, CommandListManager& cmdListMgr
		)
		{
			if (executionBatch.releaseBarriers.AnyBarrier())
			{
				pDirectCmd = cmdListMgr.DirectCommand();
				executionBatch.releaseBarriers.ExecuteBarriers(pDirectCmd);
				pDirectCmd->Close();
			}
			if (executionBatch.computeReleaseBarriers.AnyBarrier())
			{
				pComputeCmd = cmdListMgr.ComputeCommand();
				executionBatch.computeReleaseBarriers.ExecuteBarriers(pComputeCmd);
				pComputeCmd->Close();
			}
		}

		void CollectCommands(BatchCommandExecutionRanges::CombinedQueueExecutionRange& range)
		{
			if (pDirectCmd)
			{
				range.directQueueRange.commands.push_back(pDirectCmd);
				pDirectCmd = nullptr;
			}
			if (pComputeCmd)
			{
				range.computeQueueRange.commands.push_back(pComputeCmd);
				pComputeCmd = nullptr;
			}
		}

	};

	class AquireResourceCommandSet
	{
		ID3D12GraphicsCommandList7* pDirectCmd = nullptr;
		ID3D12GraphicsCommandList7* pComputeCmd = nullptr;

	public:
		void Record(RenderBackend_D3D12* backend
			, GPUExecutionBatch const& executionBatch
			, CommandListManager& cmdListMgr
			, LinearMemoryManager& linearMemoryManager
			, D3D12GraphLocalResourceManager& resourceManager
		)
		{
			if (executionBatch.aquireBarriers.AnyBarrier() || executionBatch.cbufferBarriers.AnyBarrier())
			{
				pDirectCmd = cmdListMgr.DirectCommand();
				executionBatch.cbufferBarriers.ExecuteCBufferInitializationBarriers(pDirectCmd, linearMemoryManager, resourceManager);
				executionBatch.aquireBarriers.ExecuteBarriers(pDirectCmd);
				pDirectCmd->Close();
			}
			if (executionBatch.computeAquireBarriers.AnyBarrier() || executionBatch.computeCBufferBarriers.AnyBarrier())
			{
				pComputeCmd = cmdListMgr.ComputeCommand();
				executionBatch.computeCBufferBarriers.ExecuteCBufferInitializationBarriers(pComputeCmd, linearMemoryManager, resourceManager);
				executionBatch.computeAquireBarriers.ExecuteBarriers(pComputeCmd);
				pComputeCmd->Close();
			}
		}

		void CollectCommands(BatchCommandExecutionRanges::CombinedQueueExecutionRange& range)
		{
			if (pDirectCmd)
			{
				range.directQueueRange.commands.push_back(pDirectCmd);
				pDirectCmd = nullptr;
			}
			if (pComputeCmd)
			{
				range.computeQueueRange.commands.push_back(pComputeCmd);
				pComputeCmd = nullptr;
			}
		}
		
	};

	class BatchExecutionContext
	{
	public:
		ID3D12GraphicsCommandList7* pDirectCmd = nullptr;
		ID3D12GraphicsCommandList7* pComputeCmd = nullptr;


		AquireResourceCommandSet aquireCommands;
		ReleaseResourceCommandSet releaseCommands;

		//int computeWaitingDirectFence = 0;
		//int directWaitingComputeFence = 0;
		SignalFenceIDPair waitSignals;

		SignalFenceIDPair aquireBarrierSignals;
		SignalFenceIDPair bodySignals;
		SignalFenceIDPair releaseSignals;
		SignalFenceIDPair finalSignals;

		void Record(
			RenderBackend_D3D12* pBackend
			, GPUGraph const& owningGraph
			, CommandListManager& commandListMgr
			, LinearMemoryManager& linearMemoryManager
			, D3D12GraphLocalResourceManager& resourceManager
			, CPUDescriptorAllocatorSet& cpuDescriptorAllocatorSet
			, GPUDescriptorHeap& resourceHeap
			, GPUDescriptorHeap& samplerHeap
			, GPUExecutionBatch const& executeBatch
			, castl::vector<RenderPassGPUData> const& rasterPassGPUData
			, castl::vector<ComputePassGPUData> const& computePassGPUData
			, GPUFrameManager::PFrameContext& pFrameContext
			, D3D12GPUTiming* pTiming
		)
		{
			std::array<ID3D12DescriptorHeap*, 2> descHeaps = { resourceHeap.GetHeap().Get(),samplerHeap.GetHeap().Get() };

			aquireCommands.Record(pBackend, executeBatch, commandListMgr, linearMemoryManager, resourceManager);
			releaseCommands.Record(pBackend, executeBatch, commandListMgr);

			// 本批次从哪两个槽位开始写时间戳（普通 ExecuteGraph 不传 pTiming，整段逻辑不执行）。
			uint32_t const timingSlotBeginDirect = (pTiming != nullptr) ? pTiming->SlotCount(EGPUQueueType::eDirect) : 0;
			uint32_t const timingSlotBeginCompute = (pTiming != nullptr) ? pTiming->SlotCount(EGPUQueueType::eCompute) : 0;

			pDirectCmd = nullptr;
			pComputeCmd = nullptr;

			auto getDirectCmd = [&]() -> ID3D12GraphicsCommandList7*
			{
				if (!pDirectCmd)
				{
					pDirectCmd = commandListMgr.DirectCommand();
					pDirectCmd->SetDescriptorHeaps(descHeaps.size(), descHeaps.data());
				}
				return pDirectCmd;
			};

			auto getComputeCmd = [&](bool preferCompute) -> ID3D12GraphicsCommandList7*
			{
				if(!preferCompute)
					return getDirectCmd();

				if (!pComputeCmd)
				{
					pComputeCmd = commandListMgr.ComputeCommand();
					pComputeCmd->SetDescriptorHeaps(descHeaps.size(), descHeaps.data());
				}
				return pComputeCmd;
			};

			auto closeCommands = [&]()
			{
				if (pDirectCmd)
				{
					pDirectCmd->Close();
				}
				if (pComputeCmd)
				{
					pComputeCmd->Close();
				}
			};

			//Rasterize Passes
			for (int rasterPassID : executeBatch.rasterPassRefs)
			{
				auto pCommand = getDirectCmd();
				auto& rasterData = rasterPassGPUData[rasterPassID];
				auto& pass = owningGraph.GetRenderPasses()[rasterPassID];
				CA_ASSERT_BREAK(rasterData.drawcallBatchs.size() == pass.GetDrawCallBatches().size(), "Raster Pass Batch Count Inompatible");

				// 计时（任务 2.1）：起点记在 BeginRenderPass **之前**（render pass 外），
				// 终点记在 EndRenderPass 之后。普通 ExecuteGraph 的 pTiming 为空，这段不执行。
				if (pTiming != nullptr)
				{
					pTiming->RecordPassBegin(pCommand, EGPUQueueType::eDirect, EGPUTimedPassType::eRender
						, static_cast<uint32_t>(rasterPassID)
						, GetRenderPassTimingName(owningGraph, static_cast<uint32_t>(rasterPassID)));
				}

				//Set Render Targets
				{
					if (pass.HasDepthAttachment())
					{
						CA_ASSERT_BREAK(pass.GetAttachments().size() - 1 == pass.GetDepthAttachmentIndex(), "Depth Attachment Should Be The Last One Of Attachments");
					}
					castl::vector<D3D12_RENDER_PASS_RENDER_TARGET_DESC> colorAttachments;
					colorAttachments.reserve(pass.GetColorAttachmentCount());
					for (int attachmentID = 0; attachmentID < pass.GetColorAttachmentCount(); ++attachmentID)
					{
						auto& config = pass.GetAttachmentConfig(attachmentID);
						ImageHandle const& imageHandle = pass.GetAttachments()[attachmentID];
						auto defaultImageView = GPUTextureView::CreateDefaultForRenderTarget();
						auto rtv = resourceManager.EnsureResourceView(imageHandle
							, EResourceViewType::eRTV
							, cpuDescriptorAllocatorSet
							, defaultImageView);
						D3D12_RENDER_PASS_RENDER_TARGET_DESC rtDesc = {};
						rtDesc.cpuDescriptor = rtv.CPUHandle();
						rtDesc.BeginningAccess.Type = EAttachmentLoadOpToD3D12RenderPassBeginAccess(config.loadOp);
						rtDesc.EndingAccess.Type = EAttachmentStoreOpToD3D12RenderPassEndAccess(config.storeOp);
						if (config.loadOp == EAttachmentLoadOp::eClear)
						{
							TranslateClearColor(config.clearValue, rtDesc.BeginningAccess.Clear.ClearValue.Color);
						}
						colorAttachments.push_back(rtDesc);
					}
					if (pass.HasDepthAttachment())
					{
						uint32_t depthAttachmentID = pass.GetDepthAttachmentIndex();
						auto& config = pass.GetAttachmentConfig(depthAttachmentID);
						ImageHandle const& imageHandle = pass.GetAttachments()[depthAttachmentID];
						auto defaultImageView = GPUTextureView::CreateDefaultForRenderTarget();
						auto dsv = resourceManager.EnsureResourceView(imageHandle
							, EResourceViewType::eDSV
							, cpuDescriptorAllocatorSet
							, defaultImageView);

						D3D12_RENDER_PASS_DEPTH_STENCIL_DESC dsvDesc = {};
						dsvDesc.cpuDescriptor = dsv.CPUHandle();
						dsvDesc.DepthBeginningAccess.Type = EAttachmentLoadOpToD3D12RenderPassBeginAccess(config.loadOp);
						dsvDesc.DepthEndingAccess.Type = EAttachmentStoreOpToD3D12RenderPassEndAccess(config.storeOp);
						if (config.loadOp == EAttachmentLoadOp::eClear)
						{
							TranslateClearDepthStencil(config.clearValue, dsvDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil);
						}
						pCommand->BeginRenderPass(colorAttachments.size(), colorAttachments.data(), &dsvDesc, D3D12_RENDER_PASS_FLAG_NONE);
					}
					else
					{
						pCommand->BeginRenderPass(colorAttachments.size(), colorAttachments.data(), nullptr, D3D12_RENDER_PASS_FLAG_NONE);
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

						pCommand->RSSetViewports(1, &drawcallData.defaultViewport);
						pCommand->RSSetScissorRects(1, &drawcallData.defaultSissors);

						//Vertex Assembly Input
						{
							castl::vector<D3D12_VERTEX_BUFFER_VIEW>vertexBufferViews(drawcallData.inputAssemblyBindingBuffers.size());
							for (size_t vbid = 0; vbid < drawcallData.inputAssemblyBindingBuffers.size(); ++vbid)
							{
								auto&& [vertexInputDesc, bufferHandle] = drawcallData.inputAssemblyBindingBuffers[vbid];
								auto pBufferResource = resourceManager.GetBufferD3D12Resource(bufferHandle);
								D3D12_VERTEX_BUFFER_VIEW& view = vertexBufferViews[vbid];
								view.BufferLocation = pBufferResource->GetGPUVirtualAddress();
								view.SizeInBytes = pBufferResource->GetDesc().Width;
								view.StrideInBytes = vertexInputDesc.stride;
							}
							pCommand->IASetVertexBuffers(0, vertexBufferViews.size(), vertexBufferViews.data());
						}


						if (drawInfo.drawIndexed)
						{
							auto& indexBufferData = drawcall.GetIndexBuffer();
							auto pBufferResource = resourceManager.GetBufferD3D12Resource(indexBufferData.indexBufferHandle);

							D3D12_INDEX_BUFFER_VIEW bufferView{};
							bufferView.BufferLocation = pBufferResource->GetGPUVirtualAddress();
							bufferView.SizeInBytes = pBufferResource->GetDesc().Width;
							bufferView.Format = indexBufferData.indexBufferType == EIndexBufferType::e16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
							pCommand->IASetIndexBuffer(&bufferView);
							pCommand->DrawIndexedInstanced(drawInfo.indexCount, drawInfo.instanceCount, drawInfo.indexOffset, drawInfo.vertexOffset, drawInfo.firstInstanceID);
						}
						else
						{
							pCommand->DrawInstanced(drawInfo.vertexCount, drawInfo.instanceCount, drawInfo.vertexOffset, drawInfo.firstInstanceID);
						}
					}
				}

				pCommand->EndRenderPass();

				if (pTiming != nullptr)
				{
					pTiming->RecordPassEnd(pCommand, EGPUQueueType::eDirect);
				}
			}

			//Compute Passes
			for (int computePassID : executeBatch.computePassRefs)
			{
				auto& computeData = computePassGPUData[computePassID];
				auto& pass = owningGraph.GetComputePasses()[computePassID];

				auto pCommand = getComputeCmd(pass.asyncCompute);

				// 实际记录的队列（任务 2.1 / spec 场景二）：没请求异步时记在 direct 上，
				// 结果必须如实报 eDirect，不能按 pass 类型伪报成 compute。
				EGPUQueueType const computeQueueType = pass.asyncCompute
					? EGPUQueueType::eCompute : EGPUQueueType::eDirect;
				if (pTiming != nullptr)
				{
					pTiming->RecordPassBegin(pCommand, computeQueueType, EGPUTimedPassType::eCompute
						, static_cast<uint32_t>(computePassID)
						, GetComputePassTimingName(static_cast<uint32_t>(computePassID)));
				}

				for (int dispatchID = 0; dispatchID < computeData.dispatchs.size(); ++dispatchID)
				{
					auto& dispatchData = computeData.dispatchs[dispatchID];
					auto& dispatchInfo = pass.dispatchs[dispatchID];
					pCommand->SetPipelineState(dispatchData.pipelineInstances->Get().Get());
					pCommand->SetComputeRootSignature(dispatchData.pipelineInstances->GetRootSignature().Get());

					//Bind Descriptor Tables
					{
						int resourceHeapID = dispatchData.pipelineInstances->GetResourceHeapParamID();
						int samplerHeapID = dispatchData.pipelineInstances->GetSamplerHeapParamID();
						bool hasResourceHeap = resourceHeapID != -1;
						bool hasSamplerHeap = samplerHeapID != -1;
						//Descriptor Table Should Match Root Signature Param ID
						if (hasResourceHeap)
						{
							pCommand->SetComputeRootDescriptorTable(resourceHeapID
								, dispatchData.pResourceBindingInstance->GetBindingInfo().descriptorAllocation.GPUHandle());
						}
						if (hasSamplerHeap)
						{
							pCommand->SetComputeRootDescriptorTable(samplerHeapID
								, dispatchData.pResourceBindingInstance->GetBindingInfo().samplerAllocation.GPUHandle());
						}
					}
					pCommand->Dispatch(dispatchInfo.x, dispatchInfo.y, dispatchInfo.z);
				}

				if (pTiming != nullptr)
				{
					pTiming->RecordPassEnd(pCommand, computeQueueType);
				}
			}

			//Transfer Passes
			for (int transferPassID : executeBatch.transferPassRefs)
			{
				auto& dataHolder = owningGraph.GetUploadDataHolder();
				auto pCommand = getDirectCmd();
				GPUDataTransfers const& transferPass = owningGraph.GetDataTransfers()[transferPassID];
				for (auto& imageUploads : transferPass.m_ImageDataUploads)
				{
					auto&& [targetImageHandle, dataRef] = imageUploads;
					auto pImageResource = resourceManager.GetImageD3D12Resource(targetImageHandle);

					D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
					UINT numRows;
					UINT64 rowSizeInBytes;
					UINT64 totalBytes;
					D3D12_RESOURCE_DESC resourceDesc = pImageResource->GetDesc();
					const UINT subresourceNum = 1;
					pBackend->GetDevice()->GetCopyableFootprints(&resourceDesc
						, 0
						, subresourceNum
						, 0
						, &layouts, &numRows, &rowSizeInBytes, &totalBytes);

					ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(totalBytes);
					D3D12_SUBRESOURCE_DATA subresourceData{};
					subresourceData.pData = dataRef.copied ? dataHolder.GetPtr(dataRef.dataIndex) : dataRef.pData;
					{
						size_t rowPitch;
						size_t slicePitch;
						DirectX::ComputePitch(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height
							, rowPitch, slicePitch, DirectX::CP_FLAGS_NONE);
						subresourceData.RowPitch = rowPitch;
						subresourceData.SlicePitch = slicePitch;
					}
					//ThrowIfFailed(UpdateSubresources(pCommand, pImageResource, stagingBuffer, 0, 0, 1, &subresourceData));
					ThrowIfFailed(UpdateSubresources(pCommand
						, pImageResource, stagingBuffer
						, 0
						, subresourceNum, totalBytes, &layouts, &numRows, &rowSizeInBytes, &subresourceData));
				}
				for (auto& bufferUploads : transferPass.m_BufferDataUploads)
				{
					auto&& [targetBufferHandle, dataRef] = bufferUploads;
					auto pBufferResource = resourceManager.GetBufferD3D12Resource(targetBufferHandle);

					ID3D12Resource* stagingBuffer = linearMemoryManager.AllocUploadStagingBuffer(dataRef.dataSize);
					UINT8* mappedStagingData;
					stagingBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedStagingData));
					auto pData = dataRef.copied ? dataHolder.GetPtr(dataRef.dataIndex) : dataRef.pData;
					memcpy(mappedStagingData, pData, dataRef.dataSize);
					stagingBuffer->Unmap(0, nullptr);

					pCommand->CopyBufferRegion(pBufferResource, dataRef.dstOffset, stagingBuffer, 0, dataRef.dataSize);
				}
			}

			// 查询解析必须在命令列表 Close() 之前记录（任务 2.1）：只解析本批次新写的槽位区间。
			if (pTiming != nullptr)
			{
				pTiming->ResolveRecordedRange(pDirectCmd, EGPUQueueType::eDirect, timingSlotBeginDirect);
				pTiming->ResolveRecordedRange(pComputeCmd, EGPUQueueType::eCompute, timingSlotBeginCompute);
			}

			closeCommands();
		}

		void AssignFenceIDs(int& directFenceCounter
			, int& computeFenceCounter
			, GPUExecutionBatch const& executionBatch)
		{
			aquireBarrierSignals.AssignSignals(executionBatch.aquireBarriersEmitFenceQueues, directFenceCounter, computeFenceCounter);
			bodySignals.AssignSignals(executionBatch.bodyCommandsEmitFenceQueues, directFenceCounter, computeFenceCounter);
			releaseSignals.AssignSignals(executionBatch.releaseBarriersEmitFenceQueues, directFenceCounter, computeFenceCounter);
			finalSignals = aquireBarrierSignals.Greater(bodySignals).Greater(releaseSignals);
		}

		void FindWaitFenceIDs(castl::vector<BatchExecutionContext> const& allcommand
			, GPUExecutionBatch const& executionBatch)
		{
			//computeWaitingDirectFence = 0;
			//directWaitingComputeFence = 0; 
			waitSignals = {};
			for (int batchID : executionBatch.computeWaitingDirectBatches)
			{
				waitSignals.directSignal = std::max(waitSignals.directSignal, allcommand[batchID].finalSignals.directSignal);
			}
			for (int batchID : executionBatch.directWaitingComputeBatches)
			{
				waitSignals.computeSignal = std::max(waitSignals.computeSignal, allcommand[batchID].finalSignals.computeSignal);
			}
		}

		void CollectCommands(BatchCommandExecutionRanges& ranges)
		{
			auto& aquireRange = ranges.EnsureNonSignaledRange();
			waitSignals.Wait(aquireRange);
			aquireCommands.CollectCommands(aquireRange);
			aquireBarrierSignals.Signals(aquireRange);

			auto& executionRange = ranges.EnsureNonSignaledRange();
			aquireBarrierSignals.Wait(executionRange);
			if (pDirectCmd)
			{
				executionRange.directQueueRange.commands.push_back(pDirectCmd);
			}
			if (pComputeCmd)
			{
				executionRange.computeQueueRange.commands.push_back(pComputeCmd);
			}
			bodySignals.Signals(executionRange);

			auto& releaseRange = ranges.EnsureNonSignaledRange();
			bodySignals.Wait(releaseRange);
			releaseCommands.CollectCommands(releaseRange);
			releaseSignals.Signals(releaseRange);
		}

	};

	void Execute(RenderBackend_D3D12* pBackend
		, GPUGraph const& owningGraph
		, CommandListManager& commandListMgr
		, LinearMemoryManager& linearMemoryManager
		, D3D12GraphLocalResourceManager& resourceManager
		, CPUDescriptorAllocatorSet& cpuDescriptorAllocatorSet
		, GPUDescriptorHeap& resourceHeap
		, GPUDescriptorHeap& samplerHeap
		, castl::vector<GPUExecutionBatch> const& executeBatchs
		, castl::vector<RenderPassGPUData> const& rasterPassGPUData
		, castl::vector<ComputePassGPUData> const& computePassGPUData
		, GPUFrameManager::PFrameContext& pFrameContext
		, D3D12GPUTiming* pTiming
	)
	{
		std::vector<BatchExecutionContext> contexts(executeBatchs.size());
		for (int executeBatchID = 0; executeBatchID < executeBatchs.size(); ++executeBatchID)
		{
			auto& executeBatch = executeBatchs[executeBatchID];
			BatchExecutionContext& batchContext = contexts[executeBatchID];
			batchContext.Record(pBackend
				, owningGraph
				, commandListMgr
				, linearMemoryManager
				, resourceManager
				, cpuDescriptorAllocatorSet
				, resourceHeap
				, samplerHeap
				, executeBatch
				, rasterPassGPUData
				, computePassGPUData
				, pFrameContext
				, pTiming);
		}
		int directFenceCounter = 0;
		int computeFenceCounter = 0;
		for (int executeBatchID = 0; executeBatchID < executeBatchs.size(); ++executeBatchID)
		{
			auto& executeBatch = executeBatchs[executeBatchID];
			BatchExecutionContext& batchContext = contexts[executeBatchID];
			batchContext.AssignFenceIDs(directFenceCounter, computeFenceCounter, executeBatch);
		}
		for (int executeBatchID = 0; executeBatchID < executeBatchs.size(); ++executeBatchID)
		{
			auto& executeBatch = executeBatchs[executeBatchID];
			BatchExecutionContext& batchContext = contexts[executeBatchID];
			batchContext.FindWaitFenceIDs(contexts, executeBatch);
		}

		BatchCommandExecutionRanges executionRanges;
		for (int executeBatchID = 0; executeBatchID < executeBatchs.size(); ++executeBatchID)
		{
			auto& executeBatch = executeBatchs[executeBatchID];
			BatchExecutionContext& batchContext = contexts[executeBatchID];
			batchContext.CollectCommands(executionRanges);
		}

		FrameLocalFences& frameLocalFences = pFrameContext->GetResourceManager().GetFrameLocalFences();
		for (auto& range : executionRanges.ranges)
		{
			range.Submit(pBackend, frameLocalFences);
		}

	}

	void PresentWindows(GPUGraph const& owningGraph, RenderBackend_D3D12* backend)
	{
		for (auto& backBufferImage : owningGraph.GetFinalizePass().m_PresentBackBuffers)
		{
			WindowContext* pWindow = backBufferImage.GetWindowPtr<WindowContext>();
			// editor-window-panel（⑩ 展示证明）：若该窗口登记了 backbuffer 呈现前捕获
			//（Readback(Backbuffer) 登记），在 IDXGISwapChain::Present 之前把已渲染的 backbuffer 拷走。
			backend->CaptureRequestedBackbuffer(pWindow);
			pWindow->Present();
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
		computePassGPUDataList.resize(owningGraph.GetComputePasses().size());
		for (size_t computePassID = 0; computePassID < owningGraph.GetComputePasses().size(); ++computePassID)
		{
			auto& pass = owningGraph.GetComputePasses()[computePassID];
			auto& passData = computePassGPUDataList[computePassID];
			passData.dispatchs.resize(pass.dispatchs.size());
		}
		transferPassRWStates.resize(owningGraph.GetDataTransfers().size());
	}

	void D3D12GPUGraphExecutor::CompileAndExecute(GPUGraph const& owningGraph, GPUFrameManager::PFrameContext&& frameContext)
	{
		//Setup Frame Context
		m_CurrentFrameContext = castl::move(frameContext);

		auto& frameBoundResourceManager = m_CurrentFrameContext->GetResourceManager();

		auto& descriptorAllocatorSet = frameBoundResourceManager.GetDescriptorAllocatorSet();
		auto& commandListManager = frameBoundResourceManager.GetCommandListManager();
		auto& stagingMemoryAllocator = frameBoundResourceManager.GetStagingMemoryManager();
		auto& resourceGPUHeap = frameBoundResourceManager.GetResourceGPUHeap();
		auto& samplerGPUHeap = frameBoundResourceManager.GetSamplerGPUHeap();
		auto& aliasesdMemoryAllocator = frameBoundResourceManager.GetAliasedMemoryAllocator();

		m_LocalResourceManager.SetAllocator(&aliasesdMemoryAllocator);
		//收集当前图中所有的资源
		//并注册到m_LocalResourceManager中
		//整理每个pass的读写状态
		castl::vector<PassRWState> passRWStates;
		castl::vector<PassRWState> computePassRWStates;
		castl::vector<PassRWState> transferPassRWStates;
		PassRWState finalizePassRWStates;
		castl::vector<RenderPassGPUData> rasterPassGPUDataList;
		castl::vector<ComputePassGPUData> computePassGPUDataList;
		//GPUFinalizeBatch finalizeBatch;
		InitArraySizes(owningGraph
			, passRWStates, computePassRWStates, transferPassRWStates
			, rasterPassGPUDataList, computePassGPUDataList
		);
		Prepare(owningGraph
			, GetApp()
			, m_LocalResourceManager
			, m_ConstantBufferManager
			, m_ShaderResourceInstances
			, passRWStates, computePassRWStates, transferPassRWStates, finalizePassRWStates
			, rasterPassGPUDataList
			, computePassGPUDataList);
		//整理出多个连续的无依赖batch
		castl::vector<GPUExecutionBatch> executionBatchs;
		BuildDependencyFreeBatchs(owningGraph
			, passRWStates
			, computePassRWStates
			, transferPassRWStates
			, finalizePassRWStates
			, executionBatchs
		);

		//统计资源的生命周期
		castl::unordered_map<ImageHandle, ResourceUsageRangeData> imageLifeTimes;
		castl::unordered_map<BufferHandle, ResourceUsageRangeData> bufferLifeTimes;
		castl::unordered_map<D3D2ShaderStruct const*, CBufferUsageData> cbufferLifeTimes;
		BuildResourceUsageRanges(imageLifeTimes, bufferLifeTimes, cbufferLifeTimes, executionBatchs);

		//依据资源的生命周期实际分配资源
		m_LocalResourceManager.AllocateAliasedResources(executionBatchs.size(), imageLifeTimes, bufferLifeTimes
			, cbufferLifeTimes, m_ConstantBufferManager);
		m_LocalResourceManager.CommitAliasedResources();
		//为资源创建 descriptor
		//TODO:似乎不用在这里构建Resource View，在下面做
		//m_LocalResourceManager.PrepareResourceDescriptors(descriptorAllocatorSet);
		//构建每个shader实例的资源绑定集合
		m_ShaderResourceInstances.for_each([&](auto& shaderSet, castl::shared_ptr<GPUResourceBindingInstance>& pInstance)->void
		{
			pInstance->BuildDescriptors(m_LocalResourceManager
				, descriptorAllocatorSet
				, resourceGPUHeap
				, samplerGPUHeap
				, m_ConstantBufferManager);
		});

		//创建PipelineBarriers, 目前确保在资源创建完毕后再调用
		PrepareBatchResourceBarriers(owningGraph
			//, finalizeBatch
			, executionBatchs
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
			, commandListManager
			, stagingMemoryAllocator
			, m_LocalResourceManager
			, descriptorAllocatorSet
			, resourceGPUHeap
			, samplerGPUHeap
			//, finalizeBatch
			, executionBatchs
			, rasterPassGPUDataList
			, computePassGPUDataList
			, m_CurrentFrameContext
			, m_pTiming);

		m_CurrentFrameContext->Signal(GetApp()->GetDirectQueue(), GetApp()->GetComputeQueue());

		ApplyExternalResourceStates(owningGraph, imageLifeTimes, bufferLifeTimes);

		PresentWindows(owningGraph, GetApp());

		Reset();
	}
	void D3D12GPUGraphExecutor::Release()
	{
		m_LocalResourceManager.Release();
		m_ConstantBufferManager.Release();
		m_ShaderResourceInstances.clear([](auto& key, auto& instance)
		{
			instance->Release();
		});
	}
	void D3D12GPUGraphExecutor::Reset()
	{
		m_LocalResourceManager.Reset();
		m_ConstantBufferManager.Clear();
		//m_CurrentFrameContext->GPUWaitIdle();
		m_CurrentFrameContext = nullptr;
	}

	// ===================== vulkan-gpu-queries 任务 2.1：D3D12 逐 pass 计时 =====================
	// 实现放在这个已有 .cpp 里（不新增 .cpp）：新增源文件只有重新 configure 才会进 CMake 的
	// `file(GLOB_RECURSE ...)`，而本机当前连不上依赖的远端、无法重新 configure。理由同
	// `CRenderBackend.h` 里那段注释。计时辅助（tick 换算 / 名称规则 / fence 等待）在本文件顶部。

	D3D12GPUTiming::QueueState* D3D12GPUTiming::GetQueueState(EGPUQueueType queue)
	{
		if (queue == EGPUQueueType::eDirect)
		{
			return &m_Direct;
		}
		if (queue == EGPUQueueType::eCompute)
		{
			return &m_Compute;
		}
		return nullptr;
	}

	D3D12GPUTiming::QueueState const* D3D12GPUTiming::GetQueueState(EGPUQueueType queue) const
	{
		if (queue == EGPUQueueType::eDirect)
		{
			return &m_Direct;
		}
		if (queue == EGPUQueueType::eCompute)
		{
			return &m_Compute;
		}
		return nullptr;
	}

	bool D3D12GPUTiming::InitQueue(RenderBackend_D3D12* pBackend, EGPUQueueType queue, uint32_t passCount)
	{
		QueueState* pState = GetQueueState(queue);
		if (pState == nullptr || passCount == 0)
		{
			return true; // 未使用的队列不创建任何资源（design D4）
		}

		ComPtr<ID3D12Device> pDevice = pBackend->GetDevice();
		ComPtr<ID3D12CommandQueue> pQueue = (queue == EGPUQueueType::eCompute)
			? pBackend->GetComputeQueue() : pBackend->GetDirectQueue();
		if (pDevice == nullptr || pQueue == nullptr)
		{
			CA_LOG_ERR("D3D12GPUTiming: 队列 {} 不可用，无法创建计时查询", (queue == EGPUQueueType::eCompute) ? "compute" : "direct");
			return false;
		}

		// D3D12 的时间戳频率来自命令队列本身（direct 与 compute 可以不同）。
		UINT64 frequency = 0;
		if (FAILED(pQueue->GetTimestampFrequency(&frequency)) || frequency == 0)
		{
			CA_LOG_ERR("D3D12GPUTiming: 队列 {} 不支持时间戳（GetTimestampFrequency 失败）",
				(queue == EGPUQueueType::eCompute) ? "compute" : "direct");
			return false;
		}
		pState->timestampFrequency = frequency;
		pState->slotCount = passCount * 2; // 每条 pass 一对 begin/end

		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		heapDesc.Count = pState->slotCount;
		heapDesc.NodeMask = 0;
		HRESULT hr = pDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&pState->queryHeap));
		if (FAILED(hr) || pState->queryHeap == nullptr)
		{
			CA_LOG_ERR("D3D12GPUTiming: 创建时间戳查询堆失败（HRESULT 0x{:08X}）", static_cast<uint32_t>(hr));
			return false;
		}

		D3D12_RESOURCE_DESC bufferDesc{};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Alignment = 0;
		bufferDesc.Width = static_cast<UINT64>(pState->slotCount) * sizeof(uint64_t);
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.SampleDesc.Quality = 0;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heapProps{};
		heapProps.Type = D3D12_HEAP_TYPE_READBACK;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		// READBACK 堆的 CPU 回读缓冲：结果对象自己持有（design D5），不放进会被复用的帧上下文。
		hr = pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pState->readbackBuffer));
		if (FAILED(hr) || pState->readbackBuffer == nullptr)
		{
			CA_LOG_ERR("D3D12GPUTiming: 创建计时回读缓冲失败（HRESULT 0x{:08X}）", static_cast<uint32_t>(hr));
			return false;
		}

		// 每条队列一个 fence：同一个 fence 上较大的信号值不能证明另一条队列也执行完，
		// 所以两条队列各等各的 fence。
		hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pState->fence));
		if (FAILED(hr) || pState->fence == nullptr)
		{
			CA_LOG_ERR("D3D12GPUTiming: 创建计时 fence 失败（HRESULT 0x{:08X}）", static_cast<uint32_t>(hr));
			return false;
		}

		pState->ticks.resize(pState->slotCount, 0);
		return true;
	}

	std::unique_ptr<D3D12GPUTiming> D3D12GPUTiming::Create(
		RenderBackend_D3D12* pBackend,
		GPUGraph const& graph,
		uint64_t submissionID)
	{
		if (pBackend == nullptr || !pBackend->DeviceInited())
		{
			CA_LOG_ERR("D3D12GPUTiming: 后端未初始化，无法计时");
			return nullptr;
		}

		GPUTimingCapabilities const directCaps = pBackend->GetTimingCapabilities(EGPUQueueType::eDirect);
		GPUTimingCapabilities const computeCaps = pBackend->GetTimingCapabilities(EGPUQueueType::eCompute);

		// 按图**实际使用的队列**统计 pass 数：渲染 pass 走 direct；显式异步计算 pass 走 compute，
		// 未请求异步的计算 pass 走 direct（design D4）。
		uint32_t directPassCount = static_cast<uint32_t>(graph.GetRenderPasses().size());
		uint32_t computePassCount = 0;
		for (auto const& pass : graph.GetComputePasses())
		{
			if (!pass.asyncCompute)
			{
				++directPassCount;
				continue;
			}
			if (!computeCaps.passTimestamps)
			{
				CA_LOG_ERR("当前后端不支持计时：显式异步计算 pass 需要 compute 队列的时间戳，但该队列不支持");
				return nullptr;
			}
			++computePassCount;
		}

		if (directPassCount > 0 && !directCaps.passTimestamps)
		{
			CA_LOG_ERR("当前后端不支持计时：direct 队列的 passTimestamps 为 false，图里有 {} 条 direct pass", directPassCount);
			return nullptr;
		}
		if (directPassCount == 0 && computePassCount == 0)
		{
			CA_LOG_ERR("D3D12GPUTiming: 这张图没有可计时的渲染 / 计算 pass（传输与 finalize pass 不计时）");
			return nullptr;
		}

		auto pTiming = std::unique_ptr<D3D12GPUTiming>(new D3D12GPUTiming());
		pTiming->m_SubmissionID = submissionID;
		if (!pTiming->InitQueue(pBackend, EGPUQueueType::eDirect, directPassCount))
		{
			return nullptr;
		}
		if (!pTiming->InitQueue(pBackend, EGPUQueueType::eCompute, computePassCount))
		{
			return nullptr;
		}
		return pTiming;
	}

	uint32_t D3D12GPUTiming::SlotCount(EGPUQueueType queue) const
	{
		return (queue == EGPUQueueType::eCompute) ? m_NextSlotCompute : m_NextSlotDirect;
	}

	void D3D12GPUTiming::RecordPassBegin(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue,
		EGPUTimedPassType passType, uint32_t passIndex, castl::string const& name)
	{
		QueueState* pState = GetQueueState(queue);
		if (pCmd == nullptr || pState == nullptr || pState->queryHeap == nullptr)
		{
			return;
		}

		uint32_t& nextSlot = (queue == EGPUQueueType::eCompute) ? m_NextSlotCompute : m_NextSlotDirect;
		if (nextSlot + 1 >= pState->slotCount)
		{
			CA_LOG_ERR("D3D12GPUTiming: 队列 {} 的查询槽位超出分配（已用 {} / 共 {}），本条 pass 不计时",
				(queue == EGPUQueueType::eCompute) ? "compute" : "direct", nextSlot, pState->slotCount);
			return;
		}

		uint32_t const beginSlot = nextSlot;
		nextSlot += 2;
		pCmd->EndQuery(pState->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, beginSlot);

		PendingPass pending{};
		pending.passType = passType;
		pending.passIndex = passIndex;
		pending.queue = queue;
		pending.name = name;
		pending.beginSlot = beginSlot;
		m_PendingPasses.push_back(pending);

		((queue == EGPUQueueType::eCompute) ? m_OpenSlotCompute : m_OpenSlotDirect) = beginSlot;
	}

	void D3D12GPUTiming::RecordPassEnd(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue)
	{
		QueueState* pState = GetQueueState(queue);
		uint32_t& openSlot = (queue == EGPUQueueType::eCompute) ? m_OpenSlotCompute : m_OpenSlotDirect;
		if (pCmd == nullptr || pState == nullptr || pState->queryHeap == nullptr)
		{
			return;
		}
		if (openSlot == UINT32_MAX)
		{
			CA_LOG_ERR("D3D12GPUTiming: 队列 {} 的 pass 终点找不到对应起点，跳过（不写脏数据）",
				(queue == EGPUQueueType::eCompute) ? "compute" : "direct");
			return;
		}

		pCmd->EndQuery(pState->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, openSlot + 1);
		openSlot = UINT32_MAX;
	}

	void D3D12GPUTiming::ResolveRecordedRange(ID3D12GraphicsCommandList7* pCmd, EGPUQueueType queue, uint32_t slotBegin)
	{
		QueueState* pState = GetQueueState(queue);
		if (pCmd == nullptr || pState == nullptr || pState->queryHeap == nullptr || pState->readbackBuffer == nullptr)
		{
			return;
		}
		uint32_t const slotEnd = SlotCount(queue);
		if (slotEnd <= slotBegin)
		{
			return;
		}
		// 查询解析必须在命令列表 Close() 之前记录（任务 2.1）：这里由调用方在 closeCommands() 之前调。
		// 64 位时间戳的偏移按 8 字节对齐（slotBegin * sizeof(uint64_t)）。
		pCmd->ResolveQueryData(pState->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
			slotBegin, slotEnd - slotBegin, pState->readbackBuffer.Get(),
			static_cast<UINT64>(slotBegin) * sizeof(uint64_t));
	}

	void D3D12GPUTiming::SignalAfterExecution(RenderBackend_D3D12* pBackend)
	{
		if (pBackend == nullptr)
		{
			return;
		}
		// 信号排在本次图执行已提交的命令列表之后（同一条队列上的 Signal 是顺序执行的）。
		if (m_Direct.fence != nullptr && m_Direct.queryHeap != nullptr)
		{
			ComPtr<ID3D12CommandQueue> pQueue = pBackend->GetDirectQueue();
			if (pQueue != nullptr)
			{
				pQueue->Signal(m_Direct.fence.Get(), kFenceValue);
			}
		}
		if (m_Compute.fence != nullptr && m_Compute.queryHeap != nullptr)
		{
			ComPtr<ID3D12CommandQueue> pQueue = pBackend->GetComputeQueue();
			if (pQueue != nullptr)
			{
				pQueue->Signal(m_Compute.fence.Get(), kFenceValue);
			}
		}
	}

	bool D3D12GPUTiming::Finish(RenderBackend_D3D12* pBackend)
	{
		if (m_Ready)
		{
			return !m_Passes.empty();
		}
		(void)pBackend;

		// 1) 等对应队列完成，再读回原始 tick（design D7：读完才允许 CPU 使用）。
		for (EGPUQueueType queue : { EGPUQueueType::eDirect, EGPUQueueType::eCompute })
		{
			QueueState* pState = GetQueueState(queue);
			if (pState == nullptr || pState->queryHeap == nullptr || pState->readbackBuffer == nullptr)
			{
				continue;
			}
			if (!WaitForTimingFence(pState->fence.Get(), kFenceValue))
			{
				CA_LOG_ERR("D3D12GPUTiming: 等待队列 {} 的计时 fence 失败",
					(queue == EGPUQueueType::eCompute) ? "compute" : "direct");
				m_Passes.clear();
				return false;
			}

			D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(pState->slotCount) * sizeof(uint64_t) };
			uint8_t* pData = nullptr;
			HRESULT const hr = pState->readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
			if (FAILED(hr) || pData == nullptr)
			{
				CA_LOG_ERR("D3D12GPUTiming: 映射计时回读缓冲失败（HRESULT 0x{:08X}）", static_cast<uint32_t>(hr));
				m_Passes.clear();
				return false;
			}
			pState->ticks.resize(pState->slotCount);
			memcpy(pState->ticks.data(), pData, static_cast<size_t>(pState->slotCount) * sizeof(uint64_t));
			pState->readbackBuffer->Unmap(0, nullptr);
		}

		// 2) 换算成纳秒并校验。任何一条无效就整次失败，不把坏数据截断成 0（design D7）。
		m_Passes.clear();
		m_Passes.reserve(m_PendingPasses.size());
		for (auto const& pending : m_PendingPasses)
		{
			QueueState const* pState = GetQueueState(pending.queue);
			if (pState == nullptr || pending.beginSlot + 1 >= pState->ticks.size())
			{
				CA_LOG_ERR("D3D12GPUTiming: pass（类型 {}，序号 {}）的查询槽位 {} 没有落进回读范围",
					static_cast<uint32_t>(pending.passType), pending.passIndex, pending.beginSlot);
				m_Passes.clear();
				return false;
			}
			uint64_t const beginNs = TimingTicksToNanoseconds(pState->ticks[pending.beginSlot], pState->timestampFrequency);
			uint64_t const endNs = TimingTicksToNanoseconds(pState->ticks[pending.beginSlot + 1], pState->timestampFrequency);
			if (endNs < beginNs)
			{
				CA_LOG_ERR("D3D12GPUTiming: pass（类型 {}，序号 {}）的结束时间早于开始时间（{} ns < {} ns），计时显式失败",
					static_cast<uint32_t>(pending.passType), pending.passIndex, endNs, beginNs);
				m_Passes.clear();
				return false;
			}
			GPUPassTiming result{};
			result.passType = pending.passType;
			result.passIndex = pending.passIndex;
			result.queue = pending.queue;
			result.name = pending.name;
			result.beginNs = beginNs;
			result.endNs = endNs;
			m_Passes.push_back(castl::move(result));
		}

		m_Ready = true;
		return true;
	}
}