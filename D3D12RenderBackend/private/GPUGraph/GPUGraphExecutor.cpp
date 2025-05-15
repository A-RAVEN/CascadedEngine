#include "GPUGraphExecutor.h"
#include <ShaderLibrary/D3D12ShaderStruct.h>
#include <Utils/InterfaceTranslation.h>
#include <RenderBackend_D3D12.h>

namespace graphics_backend
{



	class BasePassDependency
	{
	public:
		PassBase& thisPass;
		castl::unordered_set<BasePassDependency*> predecessors;
		castl::unordered_set<BasePassDependency*> successors;
		GPUGraph::EGraphStageType graphStageType;
		BasePassDependency(PassBase& pass, GPUGraph::EGraphStageType graphStage) : thisPass(pass), graphStageType(graphStage){}

		bool DependencyFree() const
		{
			return predecessors.empty();
		}

		void RemoveSelfDependency()
		{
			for (BasePassDependency* successor : successors)
			{
				successor->predecessors.erase(this);
			}
		}

		void CheckAddPredecessor(BasePassDependency* predPass)
		{
			bool hasDependency = false;

			auto checkDependency = [&](auto& thisSet, auto& otherSet)
			{
				for (auto& readImg : thisSet)
				{
					if (otherSet.find(readImg) != otherSet.end())
					{
						return true;
					}
				}
				return false;
			};

			//TODO: check Dependency
			//读/写依赖
			hasDependency = checkDependency(thisPass.m_ReadImages, predPass->thisPass.m_WriteImages);
			hasDependency = hasDependency || checkDependency(thisPass.m_ReadBuffers, predPass->thisPass.m_WriteBuffers);
			//写/读依赖
			hasDependency = hasDependency || checkDependency(thisPass.m_WriteImages, predPass->thisPass.m_ReadImages);
			hasDependency = hasDependency || checkDependency(thisPass.m_WriteBuffers, predPass->thisPass.m_ReadBuffers);
			
			if (hasDependency)
			{
				predecessors.insert(predPass);
				predPass->successors.insert(this);
			}
		}
	};

	static void ForeachRenderPassShaderStructs(RenderPass const& renderPass, castl::function<void(D3D2ShaderStruct const&)> callback)
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
			D3D2ShaderStruct* pStruct = static_cast<D3D2ShaderStruct*>(shaderStruct.get());
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


	static void ForeachComputePassShaderStructs(ComputeBatch const& computePass, castl::function<void(D3D2ShaderStruct const&)> callback)
	{
		castl::deque<castl::shared_ptr<D3D2ShaderStruct>> shaderStructs;
		for (auto& shaderStruct : computePass.shaderStructs)
		{
			shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
		}
		for (auto& dispatch : computePass.dispatchs)
		{
			for (auto& shaderStruct : dispatch.shaderStructs)
			{
				shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(shaderStruct.second));
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
					shaderStructs.push_back(castl::static_pointer_cast<D3D2ShaderStruct>(subStruct));
				}
			}
		}
	}


	void PassBase::CollectShaderStructResourcesForBasePass(D3D2ShaderStruct const& shaderStruct)
	{
		auto pStructData = shaderStruct.GetStructData();
		auto& imageHandles = shaderStruct.GetImageHandles();
		auto& bufferHandles = shaderStruct.GetBufferHandles();

		for (auto& textureData : pStructData->m_Textures)
		{
			auto found = imageHandles.find(textureData.m_Name);
			if (found != imageHandles.end())
			{
				for (auto img : found->second)
				{
					switch (textureData.m_RWType)
					{
					case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
						m_ReadImages.insert(img.first);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
						m_WriteImages.insert(img.first);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
						m_ReadImages.insert(img.first);
						m_WriteImages.insert(img.first);
						break;
					}
				}
			}
		}

		for (auto& bufferData : pStructData->m_Buffers)
		{
			auto found = bufferHandles.find(bufferData.m_Name);
			if (found != bufferHandles.end())
			{
				for (auto buf : found->second)
				{
					switch (bufferData.m_RWType)
					{
					case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
						m_ReadBuffers.insert(buf);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
						m_WriteBuffers.insert(buf);
						break;
					case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
						m_ReadBuffers.insert(buf);
						m_WriteBuffers.insert(buf);
						break;
					}
				}
			}
		}
	}

	void RasterizationPass::Prepare(RenderPass const& renderPass)
	{
		pPass = &renderPass;
		auto& attachments = renderPass.GetAttachments();

		for (auto& attachment : attachments)
		{
			//TODO: Fully Check Read/Write State Of Attachments
			m_ReadImages.insert(attachment);
			m_WriteImages.insert(attachment);
		}

		auto& drawCallBatchs = renderPass.GetDrawCallBatches();
		for (auto& drawCallBatch : drawCallBatchs)
		{
			for (auto& drawCall : drawCallBatch.m_DrawCalls)
			{
				if (drawCall.GetDrawInfo().drawIndexed)
				{
					m_ReadBuffers.insert(drawCall.GetIndexBuffer().indexBufferHandle);
				}
				for (auto& vertexBuffer : drawCall.GetVertexBuffers())
				{
					m_ReadBuffers.insert(vertexBuffer.second);
				}
			}
		}

		ForeachRenderPassShaderStructs(renderPass, [&](D3D2ShaderStruct const& shaderStruct)
		{
			CollectShaderStructResourcesForBasePass(shaderStruct);
		});
	}
	
	void ComputePass::Prepare(ComputeBatch const& computePass)
	{
		pPass = &computePass;
		ForeachComputePassShaderStructs(computePass, [&](D3D2ShaderStruct const& shaderStruct)
		{
			CollectShaderStructResourcesForBasePass(shaderStruct);
		});
	}

	void TransferPass::Prepare(GPUDataTransfers const& transferPass)
	{
		pPass = &transferPass;
		for (auto& bufferWrites : transferPass.m_BufferDataUploads)
		{
			m_WriteBuffers.insert(bufferWrites.first);
		}
		for (auto& imgWrites : transferPass.m_ImageDataUploads)
		{
			m_WriteImages.insert(imgWrites.first);
		}
	}

	D3D12GPUGraphExecutor::D3D12GPUGraphExecutor(RenderBackend_D3D12* app) : 
		D3D12SubobjectBase(app)
		, m_LocalResourceManager(app)
		, m_ConstantBufferManager(app)
	{}


	struct PassRWState
	{
		castl::unordered_map<ImageHandle, ShaderCompilerSlang::EShaderResourceAccess> imageRWStates;
		castl::unordered_map<BufferHandle, ShaderCompilerSlang::EShaderResourceAccess> bufferRWStates;
		bool isWriting(ImageHandle const& image) const
		{
			auto found = imageRWStates.find(image);
			if(found != imageRWStates.end())
			{
				return found->second == ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
			}
			return false;
		}
		bool isWriting(BufferHandle const& buffer) const
		{
			auto found = bufferRWStates.find(buffer);
			if(found != bufferRWStates.end())
			{
				return found->second == ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
			}
			return false;
		}
		bool isReading(ImageHandle const& image) const
		{
			auto found = imageRWStates.find(image);
			if(found != imageRWStates.end())
			{
				return found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
			}
			return false;
		}
		bool isReading(BufferHandle const& buffer) const
		{
			auto found = bufferRWStates.find(buffer);
			if(found != bufferRWStates.end())
			{
				return found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadOnly
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite;
			}
			return false;
		}
		void SetImageRWState(ImageHandle const& image, ShaderCompilerSlang::EShaderResourceAccess access)
		{
			auto found = imageRWStates.find(image);
			if(found != imageRWStates.end())
			{
				assert(found->second == access
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite
					|| access == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
				found->second = (found->second != access) ? ShaderCompilerSlang::EShaderResourceAccess::eReadWrite : access;
			}
			else
			{
				imageRWStates.insert(castl::make_pair(image, access));
			}
		}
		void SetBufferRWState(BufferHandle const& buffer, ShaderCompilerSlang::EShaderResourceAccess access)
		{
			auto found = bufferRWStates.find(buffer);
			if(found != bufferRWStates.end())
			{
				assert(found->second == access
					|| found->second == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite
					|| access == ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
				found->second = (found->second != access) ? ShaderCompilerSlang::EShaderResourceAccess::eReadWrite : access;
			}
			else
			{
				bufferRWStates.insert(castl::make_pair(buffer, access));
			}
		}
	};

	class PassDependency
	{
	public:
		PassRWState& rwState;
		uint32_t passID;
		GPUGraph::EGraphStageType passType;
		uint32_t predecessorCount;
		castl::vector<PassDependency*> successors;

		PassDependency(PassRWState& rwState, uint32_t passID, GPUGraph::EGraphStageType passType)
			: rwState(rwState), passID(passID), passType(passType), predecessorCount(0) {}

	};

	void Prepare(GPUGraph const& owningGraph,
		D3D12GPUGraphExecutor& executor,
		castl::vector<PassRWState>& passRWStates,
		castl::vector<PassRWState>& computePassRWStates,
		castl::vector<PassRWState>& transferPassRWStates
	)
	{
		auto pApp = executor.GetApp();
		auto& resourceManager = executor.GetLocalResourceManager();
		auto& constantBufferManager = executor.GetConstantBufferManager();
		auto& shaderResourceInstances = executor.GetShaderResourceInstances();
		auto registerBufferToLocalResourceManager = [&](BufferHandle const& buffer)
		{
			auto descriptor = owningGraph.GetBufferManager().GetDescriptor(buffer.GetKey());
			CA_ASSERT_BREAK(descriptor != nullptr, "Buffer {} Not Registered", buffer.GetName());
			resourceManager.AddBuffer(buffer, *descriptor);
		};

		auto addPassShaderInstancesResourcesRWStates = [&](PassRWState& passRWState,
			std::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> const& passLocalBindingInstances)
		{
			for(auto pair : passLocalBindingInstances)
			{
				auto resourceBindingInstance = pair.second;
				resourceBindingInstance->IterateResourceUsages(
				[&](GPUResourceBindingInstance::ImageBindingInfo const& imageInfo)
				{
					passRWState.SetImageRWState(imageInfo.image, imageInfo.accessType);
				},
				[&](GPUResourceBindingInstance::BufferBindingInfo const& bufferInfo)
				{
					passRWState.SetBufferRWState(bufferInfo.buffer, bufferInfo.accessType);
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
				passRWState.SetImageRWState(attachment, ShaderCompilerSlang::EShaderResourceAccess::eReadWrite);
			}

			std::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for(auto& drawcallBatchs : renderPass.GetDrawCallBatches())
			{
				for (auto& drawcall : drawcallBatchs.m_DrawCalls)
				{
					//将drawcall的index和vertexbuffer注册进m_LocalResourceManager中
					if (drawcall.GetDrawInfo().drawIndexed)
					{
						auto& indesxBuffer = drawcall.GetIndexBuffer().indexBufferHandle;
						registerBufferToLocalResourceManager(indesxBuffer);
						passRWState.SetBufferRWState(indesxBuffer, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						registerBufferToLocalResourceManager(vertBuf.second);
						passRWState.SetBufferRWState(vertBuf.second, ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
				}
				auto pipelineData = PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), drawcallBatchs.pipelineStateDesc);
				castl::vector<ShaderStructDic const*> shaderStructs;
				shaderStructs.push_back(&drawcallBatchs.shaderStructs);
				shaderStructs.push_back(&renderPass.GetShaderStructs());
				ShaderResourceSet resourceSet;
				resourceSet.Init(pApp, pipelineData.m_ShaderInfo, {shaderStructs});
				auto resourceBindingInstance = shaderResourceInstances.get_or_create(resourceSet, [&](ShaderResourceSet const& resourceInstance) -> castl::shared_ptr<GPUResourceBindingInstance>
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
			std::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
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
			std::unordered_map<ShaderResourceSet, castl::shared_ptr<GPUResourceBindingInstance>> passLocalBindingInstances;
			for (auto& bufferWrites : transferPass.m_BufferDataUploads)
			{
				registerBufferToLocalResourceManager(bufferWrites.first);
				passRWState.SetBufferRWState(bufferWrites.first, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly);
			}
			for (auto& imgWrites : transferPass.m_ImageDataUploads)
			{
				auto descriptor = owningGraph.GetImageManager().GetDescriptor(imgWrites.first.GetKey());
				CA_ASSERT_BREAK(descriptor != nullptr, "Image {} Not Registered", imgWrites.first.GetName());
				resourceManager.AddTexture(imgWrites.first, *descriptor, GPUTextureView::CreateDefaultForRenderTarget(descriptor->format));
				passRWState.SetImageRWState(imgWrites.first, ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly);
			}
		}

		//将m_ConstantBufferManager中的constant buffer资源注册到m_LocalResourceManager中
		constantBufferManager.BuildResources(resourceManager);
		//将m_ShaderResourceInstances中的资源注册到m_LocalResourceManager中
		shaderResourceInstances.for_each([&](ShaderResourceSet const& resourceSet
			, castl::shared_ptr<GPUResourceBindingInstance>& resourceInstance)
		{
			resourceInstance->BuildResources(owningGraph, resourceManager);
		});
	}


	void D3D12GPUGraphExecutor::Init(GPUGraph const& owningGraph)
	{
		//收集当前图中所有的资源
		//并注册到m_LocalResourceManager中
		//整理每个pass的读写状态
		castl::vector<PassRWState> passRWStates;
		castl::vector<PassRWState> computePassRWStates;
		castl::vector<PassRWState> transferPassRWStates;
		Prepare(owningGraph, *this, passRWStates, computePassRWStates, transferPassRWStates);

		auto& renderPasses =owningGraph.GetRenderPasses();
		m_RasterizePasses.resize(renderPasses.size());
		for (size_t passID = 0; passID < m_RasterizePasses.size(); ++passID)
		{
			m_RasterizePasses[passID].Prepare(renderPasses[passID]);
		}

		auto& computePasses = owningGraph.GetComputePasses();
		m_ComputePasses.resize(computePasses.size());
		for (size_t passID = 0; passID < m_ComputePasses.size(); ++passID)
		{
			m_ComputePasses[passID].Prepare(computePasses[passID]);
		}

		auto& transferPasses = owningGraph.GetDataTransfers();
		m_TransferPasses.resize(transferPasses.size());
		for (size_t passID = 0; passID < m_TransferPasses.size(); ++passID)
		{
			m_TransferPasses[passID].Prepare(transferPasses[passID]);
		}

		auto& stages = owningGraph.GetGraphStages();
		std::list<BasePassDependency> passDeps;
		//passDeps.reserve(stages.size());
		size_t rasterPassID = 0;
		size_t computePassID = 0;
		size_t transferPassID = 0;
		for (auto& stage : stages)
		{
			switch (stage)
			{
			case GPUGraph::EGraphStageType::eRenderPass:
			{
				passDeps.emplace_back(m_RasterizePasses[rasterPassID], stage);
				++rasterPassID;
			}
				break;
			case GPUGraph::EGraphStageType::eComputePass:
			{
				passDeps.emplace_back(m_ComputePasses[computePassID], stage);
				++computePassID;
			}
				break;
			case GPUGraph::EGraphStageType::eTransferPass:
			{
				passDeps.emplace_back(m_TransferPasses[transferPassID], stage);
				++transferPassID;
			}
				break;
			default:
				break;
			}
		}

		for (auto endDeps = ++passDeps.begin(); endDeps != passDeps.end(); ++endDeps)
		{
			for (auto predDeps = passDeps.begin(); predDeps != endDeps; ++predDeps)
			{
				endDeps->CheckAddPredecessor(predDeps.operator->());
			}
		}

		castl::vector<BasePassDependency*> depFreeDeps;
		for (BasePassDependency& dep : passDeps)
		{
			if (dep.DependencyFree())
			{
				depFreeDeps.push_back(&dep);
			}
			else
			{

				m_GraphNodes.emplace_back();
				GraphNode& lastNode = m_GraphNodes.back();
				for (BasePassDependency* pFreeDep : depFreeDeps)
				{
					BasePassDependency& freeDep = *pFreeDep;
					freeDep.RemoveSelfDependency();
					switch (freeDep.graphStageType)
					{
					case GPUGraph::EGraphStageType::eRenderPass:
					{
						lastNode.m_RasterPasses.push_back(static_cast<RasterizationPass*>(&freeDep.thisPass));
					}
					break;
					case GPUGraph::EGraphStageType::eComputePass:
					{
						lastNode.m_ComputePasses.push_back(static_cast<ComputePass*>(&freeDep.thisPass));
					}
					break;
					case GPUGraph::EGraphStageType::eTransferPass:
					{
						lastNode.m_TransferPasses.push_back(static_cast<TransferPass*>(&freeDep.thisPass));
					}
					break;
					default:
						break;
					}
				}

			}
		}


		for (auto& node : m_GraphNodes)
		{
			D3D12PassResourceStates newStates;

			auto addTextureToUsageState = [&](D3D12_COMMAND_LIST_TYPE queueType
				, ImageHandle const& image
				, ETextureAccessType accessType)
			{
				auto descriptor = owningGraph.GetImageManager().GetDescriptor(image.GetKey());
				CA_ASSERT_BREAK(descriptor != nullptr, "Image {} Not Registered", image.GetName());
				D3D12TextureUsageState usageState{};
				usageState.accessState = ETextureAccessTypeToD3D12BarrierAccess(descriptor->format, accessType);
				usageState.layoutState = ETextureAccessTypeToD3D12BarrierLayout(descriptor->format, accessType);
				usageState.queueType = queueType;
				newStates.textureUsageStates.push_back(castl::make_pair(image, usageState));
			};

			auto addBufferToUsageState = [&](D3D12_COMMAND_LIST_TYPE queueType
				, BufferHandle const& buffer
				, EBufferUsage bufferUsage
				, bool write)
			{
				D3D12BufferUsageState usageState{};
				usageState.accessState = EBufferUsageTranslate(bufferUsage);
				usageState.queueType = queueType;
				newStates.bufferUsageStates.push_back(castl::make_pair(buffer, usageState));
			};

			auto processShaderStruct = [&](D3D12_COMMAND_LIST_TYPE queueType, D3D2ShaderStruct const& shaderStruct)
			{
				for (auto& imgListPair : shaderStruct.GetImageHandles())
				{
					ETextureAccessType accessType = shaderStruct.GetTextureAccessType(imgListPair.first);

					for (auto& imgPair : imgListPair.second)
					{
						addTextureToUsageState(queueType, imgPair.first, accessType);
					}
				}
				for (auto& bufListPair : shaderStruct.GetBufferHandles())
				{
					auto rwType = shaderStruct.GetBufferRWType(bufListPair.first);
					for (auto& buf : bufListPair.second)
					{
						addBufferToUsageState(queueType, buf, EBufferUsage::eStructuredBuffer, rwType != ShaderCompilerSlang::EShaderResourceAccess::eReadOnly);
					}
				}
			};

#pragma region Render Passes
			for (auto pRenderPass : node.m_RasterPasses)
			{
				auto& renderPass = *(pRenderPass->pPass);
				for (auto& attachment : renderPass.GetAttachments())
				{
					addTextureToUsageState(D3D12_COMMAND_LIST_TYPE_DIRECT, attachment, ETextureAccessType::eRT);
				}

				ForeachShaderStructs<D3D2ShaderStruct>(renderPass.GetShaderStructs(), [&](D3D2ShaderStruct const& shaderStruct)
				{
					processShaderStruct(D3D12_COMMAND_LIST_TYPE_DIRECT, shaderStruct);
				});

				for (auto& batch : renderPass.GetDrawCallBatches())
				{
					ForeachShaderStructs<D3D2ShaderStruct>(batch.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
					{
						processShaderStruct(D3D12_COMMAND_LIST_TYPE_DIRECT, shaderStruct);
					});

					for (auto& drawcall : batch.m_DrawCalls)
					{
						if (drawcall.GetDrawInfo().drawIndexed)
						{
							addBufferToUsageState(D3D12_COMMAND_LIST_TYPE_DIRECT
								, drawcall.GetIndexBuffer().indexBufferHandle
								, EBufferUsage::eIndexBuffer
								, false);
						}
						for (auto& vertBuf : drawcall.GetVertexBuffers())
						{
							addBufferToUsageState(D3D12_COMMAND_LIST_TYPE_DIRECT
								, vertBuf.second
								, EBufferUsage::eVertexBuffer
								, false);
						}
					}
				}
			}
#pragma endregion
		
#pragma Compute Passes
			for (auto& pComputePass : node.m_ComputePasses)
			{
				auto& computePass = *pComputePass->pPass;
				
				ForeachShaderStructs<D3D2ShaderStruct>(computePass.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
				{
					processShaderStruct(D3D12_COMMAND_LIST_TYPE_COMPUTE, shaderStruct);
				});
				for (auto& dispatch : computePass.dispatchs)
				{
					ForeachShaderStructs<D3D2ShaderStruct>(dispatch.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
					{
						processShaderStruct(D3D12_COMMAND_LIST_TYPE_COMPUTE, shaderStruct);
					});
				}
			}
#pragma endregion

			for (auto& pTransferPass : node.m_TransferPasses)
			{
				auto& transferPass = pTransferPass->pPass;
				for (auto& uploads : transferPass->m_ImageDataUploads)
				{
					addTextureToUsageState(D3D12_COMMAND_LIST_TYPE_COPY, uploads.first, ETextureAccessType::eTransferDst);
				}

				for (auto& uploads : transferPass->m_BufferDataUploads)
				{
					addBufferToUsageState(D3D12_COMMAND_LIST_TYPE_COPY, uploads.first, EBufferUsage::eDataDst, false);
				}

			}

#pragma 


		}

		//m_LocalResourceManager.AddGPUPassResourceStates()
	}

	// void ForeachResourceInGraph(GPUGraph const& owningGraph, castl::function<void(ImageHandle const&)> imageCallback
	// 	, castl::function<void(BufferHandle const&)> bufferCallback)
	// {
	// 	auto shaderStructCallback = [&](D3D2ShaderStruct const& shaderStruct)
	// 	{
	// 		for (auto& imgListPair : shaderStruct.GetImageHandles())
	// 		{
	// 			for (auto& imgPair : imgListPair.second)
	// 			{
	// 				imageCallback(imgPair.first);
	// 			}
	// 		}
	// 		for (auto& bufListPair : shaderStruct.GetBufferHandles())
	// 		{
	// 			for (auto& buf : bufListPair.second)
	// 			{
	// 				bufferCallback(buf);
	// 			}
	// 		}
	// 	};

	// 	for (auto& renderPass : owningGraph.GetRenderPasses())
	// 	{
	// 		for (auto& attachment : renderPass.GetAttachments())
	// 		{
	// 			imageCallback(attachment);
	// 		}

	// 		ForeachShaderStructs<D3D2ShaderStruct>(renderPass.GetShaderStructs(), [&](D3D2ShaderStruct const& shaderStruct)
	// 		{
	// 			shaderStructCallback(shaderStruct);
	// 		});

	// 		for (auto& batch : renderPass.GetDrawCallBatches())
	// 		{
	// 			ForeachShaderStructs<D3D2ShaderStruct>(batch.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
	// 			{
	// 				shaderStructCallback(shaderStruct);
	// 			});

	// 			for (auto& drawcall : batch.m_DrawCalls)
	// 			{
	// 				if (drawcall.GetDrawInfo().drawIndexed)
	// 				{
	// 					bufferCallback(drawcall.GetIndexBuffer().indexBufferHandle);
	// 				}
	// 				for (auto& vertBuf : drawcall.GetVertexBuffers())
	// 				{
	// 					bufferCallback(vertBuf.second);
	// 				}
	// 			}
	// 		}
	// 	}

	// 	for (auto& computePass : owningGraph.GetComputePasses())
	// 	{
	// 		ForeachShaderStructs<D3D2ShaderStruct>(computePass.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
	// 		{
	// 			shaderStructCallback(shaderStruct);
	// 		});
	// 		for (auto& dispatch : computePass.dispatchs)
	// 		{
	// 			ForeachShaderStructs<D3D2ShaderStruct>(dispatch.shaderStructs, [&](D3D2ShaderStruct const& shaderStruct)
	// 			{
	// 				shaderStructCallback(shaderStruct);
	// 			});
	// 		}
	// 	}
	// }

}