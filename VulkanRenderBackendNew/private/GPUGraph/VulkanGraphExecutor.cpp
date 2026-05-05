#include <GPUGraph/VulkanGraphExecutor.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanCommandListManager.h>
#include <ResourceManagement/VulkanLinearMemoryManager.h>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>
#include <VulkanObjects/VulkanShaderStruct.h>
#include <GPUGraph/VulkanResourceBindingInstance.h>
#include <ShaderLibrary/ShaderImporter_Vulkan.h>
#include <Compiler.h>
#include <CATimer/Timer.h>
#include <Hasher.h>
#include <chrono>

namespace graphics_backend
{
	// Helper: Convert EShaderTypeFlags to vk::PipelineStageFlags
	static vk::PipelineStageFlags ToPipelineStageFlags(EShaderTypeFlags flags)
	{
		vk::PipelineStageFlags result{};
		IterateShaderTypeFlags(flags, [&](EShaderTypeMask mask)
		{
			switch (mask)
			{
			case EShaderTypeMask::eVert: result |= vk::PipelineStageFlagBits::eVertexShader; break;
			case EShaderTypeMask::eFrag: result |= vk::PipelineStageFlagBits::eFragmentShader; break;
			case EShaderTypeMask::eComp: result |= vk::PipelineStageFlagBits::eComputeShader; break;
			case EShaderTypeMask::eTessCtr: result |= vk::PipelineStageFlagBits::eTessellationControlShader; break;
			case EShaderTypeMask::eTessEvl: result |= vk::PipelineStageFlagBits::eTessellationEvaluationShader; break;
			case EShaderTypeMask::eGeom: result |= vk::PipelineStageFlagBits::eGeometryShader; break;
			default: break;
			}
		});
		return result;
	}

	// ComputeAccessToVulkanAccess: EShaderResourceAccess -> vk::AccessFlags
	static vk::AccessFlags ComputeAccessToVulkanAccess(
		ShaderCompilerSlang::EShaderResourceAccess access)
	{
		switch (access)
		{
		case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
			return vk::AccessFlagBits::eShaderRead;
		case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			return vk::AccessFlagBits::eShaderWrite;
		case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		default:
			return vk::AccessFlags{};
		}
	}

	// ComputeAccessToImageLayout: EShaderResourceAccess -> vk::ImageLayout
	static vk::ImageLayout ComputeAccessToImageLayout(
		ShaderCompilerSlang::EShaderResourceAccess access)
	{
		switch (access)
		{
		case ShaderCompilerSlang::EShaderResourceAccess::eReadOnly:
			return vk::ImageLayout::eShaderReadOnlyOptimal;
		case ShaderCompilerSlang::EShaderResourceAccess::eWriteOnly:
			return vk::ImageLayout::eGeneral;
		case ShaderCompilerSlang::EShaderResourceAccess::eReadWrite:
			return vk::ImageLayout::eGeneral;
		default:
			return vk::ImageLayout::eUndefined;
		}
	}

	// Helper functions for getting descriptors
	GPUTextureDescriptor GetDescriptor(GPUGraph const& graph, ImageHandle const& image)
	{
		switch (image.GetType())
		{
		case ImageHandle::ImageType::External:
			return image.GetTexturePtr<VulkanTexture>()->GetDescriptor();
		case ImageHandle::ImageType::Backbuffer:
			return image.GetWindowPtr<VulkanWindowHandle>()->GetBackbufferDescriptor();
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
			return buffer.GetBufferPtr<VulkanBuffer>()->GetDescriptor();
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

	// VulkanShaderResourceSet implementation
	void VulkanShaderResourceSet::Init(RenderBackend_Vulkan* pApp, ShaderInfo const& info
		, castl::vector<ShaderStructDic const*> const& structs)
	{
		shaderInfo = info;
		shaderStructs = structs;
		resourceDic.clear();

		auto findShaderStructOfName = [&](cacore::NameHash const& inName) -> VulkanShaderStruct const*
		{
			for (auto pMap : structs)
			{
				auto& structMap = *pMap;
				auto found = structMap.find(inName);
				if (found != structMap.end())
				{
					return static_cast<VulkanShaderStruct const*>(found->second.get());
				}
			}
			return nullptr;
		};

		auto pShaderFileInfo = pApp->GetShaderFileInfo(info);
		if (pShaderFileInfo == nullptr)
			return;

		auto& reflectionData = pShaderFileInfo->reflectionData;
		auto& bindingInfo = reflectionData.m_BindingInfo;
		auto& rootHierarchy = bindingInfo.m_BindingDataHierarchies[bindingInfo.m_RootHierarchyID];

		for (uint32_t hierarchyID : rootHierarchy.m_SubBindingHierarchies)
		{
			auto& hierarchy = bindingInfo.m_BindingDataHierarchies[hierarchyID];
			auto sourceStruct = findShaderStructOfName(hierarchy.m_Name);
			CA_ASSERT_BREAK(sourceStruct != nullptr, "Root Struct [{}] Not Found", hierarchy.m_Name);
			resourceDic.insert(castl::make_pair(hierarchy.m_Name, sourceStruct));
		}

		// Compute hash
		hash = 0;
		cacore::hash_combine(hash, info.path.GetHash());
		for (auto const& [name, structPtr] : resourceDic)
		{
			cacore::hash_combine(hash, name.GetHash());
		}
	}

	// VulkanExecutorRWState implementation
	bool VulkanExecutorRWState::Depends(VulkanExecutorRWState const& successor) const
	{
		for (auto& pair : imageRWStates)
		{
			auto&& [img, rwState] = pair;
			auto found = successor.imageRWStates.find(img);
			if (found != successor.imageRWStates.end())
			{
				if (rwState.Write() || found->second.Write())
					return true;
				if (!rwState.CompatibleToCombine(found->second))
					return true;
			}
		}
		for (auto& pair : bufferRWStates)
		{
			auto&& [buf, rwState] = pair;
			auto found = successor.bufferRWStates.find(buf);
			if (found != successor.bufferRWStates.end())
			{
				if (rwState.Write() || found->second.Write())
					return true;
				if (!rwState.CompatibleToCombine(found->second))
					return true;
			}
		}
		return false;
	}

	void VulkanExecutorRWState::Append(VulkanExecutorRWState const& other)
	{
		batchResourceQueueTypes |= other.batchResourceQueueTypes;
		for (auto pair : other.imageRWStates)
		{
			auto found = imageRWStates.find(pair.first);
			if (found == imageRWStates.end())
				imageRWStates.insert(pair);
			else
				imageRWStates[pair.first].Combine(pair.second);
		}
		for (auto pair : other.bufferRWStates)
		{
			auto found = bufferRWStates.find(pair.first);
			if (found == bufferRWStates.end())
				bufferRWStates.insert(pair);
			else
				bufferRWStates[pair.first].Combine(pair.second);
		}
		for (auto pair : other.cBufferUsageStates)
		{
			auto found = cBufferUsageStates.find(pair.first);
			if (found == cBufferUsageStates.end())
				cBufferUsageStates.insert(pair);
			else
				cBufferUsageStates[pair.first] |= pair.second;
		}
	}

	void VulkanExecutorRWState::SetImageRWState(ImageHandle const& image,
		vk::PipelineStageFlags stages, vk::AccessFlags access,
		vk::ImageLayout layout, EGPUQueueType queueType)
	{
		VulkanResourceState newState{ access, stages, layout, queueType, true };
		auto found = imageRWStates.find(image);
		if (found != imageRWStates.end())
			found->second.Combine(newState);
		else
			imageRWStates.insert(castl::make_pair(image, newState));
		batchResourceQueueTypes |= static_cast<EGPUQueueTypeFlags>(queueType);
	}

	void VulkanExecutorRWState::SetBufferRWState(BufferHandle const& buffer,
		vk::PipelineStageFlags stages, vk::AccessFlags access, EGPUQueueType queueType)
	{
		VulkanResourceState newState{ access, stages, vk::ImageLayout::eUndefined, queueType, false };
		auto found = bufferRWStates.find(buffer);
		if (found != bufferRWStates.end())
			found->second.Combine(newState);
		else
			bufferRWStates.insert(castl::make_pair(buffer, newState));
		batchResourceQueueTypes |= static_cast<EGPUQueueTypeFlags>(queueType);
	}

	void VulkanExecutorRWState::SetCBufferUsageState(VulkanShaderStruct const* pCBufferStruct,
		vk::PipelineStageFlags stages, EGPUQueueType queueType)
	{
		auto found = cBufferUsageStates.find(pCBufferStruct);
		if (found != cBufferUsageStates.end())
			found->second |= static_cast<EGPUQueueTypeFlags>(queueType);
		else
			cBufferUsageStates.insert(castl::make_pair(pCBufferStruct,
				static_cast<EGPUQueueTypeFlags>(queueType)));
	}

	// VulkanPassDependency implementation
	void VulkanPassDependency::RemoveSelfDeps()
	{
		for (VulkanPassDependency* dep : successors)
			dep->predecessorCount--;
	}

	void VulkanPassDependency::CheckAddSuccessor(VulkanPassDependency* successor)
	{
		if (rwState.Depends(successor->rwState))
		{
			successors.push_back(successor);
			successor->predecessorCount++;
		}
	}

	// VulkanRenderStateBarriers implementation
	void VulkanRenderStateBarriers::AddImageBarrier(ImageHandle const& handle, vk::ImageMemoryBarrier const& barrier)
	{
		imageBarriers.push_back({ handle, barrier });
	}

	void VulkanRenderStateBarriers::AddBufferBarrier(BufferHandle const& handle, vk::BufferMemoryBarrier const& barrier)
	{
		bufferBarriers.push_back({ handle, barrier });
	}

	void VulkanRenderStateBarriers::ExecuteBarriers(vk::CommandBuffer cmdBuf)
	{
		if (!imageBarriers.empty())
		{
			castl::vector<vk::ImageMemoryBarrier> barriers;
			barriers.reserve(imageBarriers.size());
			for (auto& ib : imageBarriers)
				barriers.push_back(ib.barrier);
			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags{},
				{}, {}, barriers);
		}
		if (!bufferBarriers.empty())
		{
			castl::vector<vk::BufferMemoryBarrier> barriers;
			barriers.reserve(bufferBarriers.size());
			for (auto& bb : bufferBarriers)
				barriers.push_back(bb.barrier);
			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags{},
				{}, barriers, {});
		}
	}

	// VulkanCBufferInitializeBarriers implementation
	void VulkanCBufferInitializeBarriers::AddCBuffer(uint64_t resourceId,
		VulkanShaderStruct const* shaderStruct)
	{
		cbufferData.push_back(castl::make_pair(resourceId, shaderStruct));
	}

	// VulkanGPUExecutionBatch implementation
	VulkanRenderStateBarriers& VulkanGPUExecutionBatch::GetAquireBarriers(EGPUQueueTypeFlags queueFlags)
	{
		if (queueFlags == EGPUQueueType::eCompute)
			return computeAquireBarriers;
		return aquireBarriers;
	}

	VulkanRenderStateBarriers& VulkanGPUExecutionBatch::GetReleaseBarriers(EGPUQueueTypeFlags queueFlags)
	{
		if (queueFlags == EGPUQueueType::eCompute)
			return computeReleaseBarriers;
		return releaseBarriers;
	}

	VulkanCBufferInitializeBarriers& VulkanGPUExecutionBatch::GetCBufferBarriers(EGPUQueueTypeFlags queueFlags)
	{
		if (queueFlags == EGPUQueueType::eCompute)
			return computeCBufferBarriers;
		return cbufferBarriers;
	}

	// ===================== VulkanGraphExecutor =====================

	// Task 3.7: Release() now only releases frame-level resources (LocalResourceManager, CBufferManager, ShaderResourceInstances).
	// Cross-frame caches lifecycle managed by RenderBackend_Vulkan. FrameContext reference released on exit.
	void VulkanGraphExecutor::Release()
	{
		m_LocalResourceManager.Release();
		m_ConstantBufferManager.Release();
		m_ShaderResourceInstances.clear();
		m_CurrentFrameContext.reset();
		CA_LOG_INFO("VulkanGraphExecutor released");
	}

	// Task 3.1: New signature — accepts PFrameContext&& for per-frame resources.
	// Task 3.3a: Swapchain acquire moved here (Prepare phase), not in PresentWindows.
	// Task 3.8: Clear descriptor set references before returning (pool will be reset on next Aquire).
	void VulkanGraphExecutor::CompileAndExecute(castl::shared_ptr<GPUGraph> const& graph,
		VulkanGPUFrameManager::PFrameContext&& frameContext)
	{
		if (!graph)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Null graph");
			return;
		}

		m_CurrentFrameContext = std::move(frameContext);

		auto prepareStart = std::chrono::high_resolution_clock::now();

		// Phase 1: Prepare
		Prepare(*graph);

		// Task 3.2: After Prepare, compute descriptor pool requirements and ensure pool capacity
		{
			uint32_t maxSets = 0;
			uint32_t uniformBufferCount = 0;
			uint32_t sampledImageCount = 0;
			uint32_t storageBufferCount = 0;
			uint32_t storageImageCount = 0;
			uint32_t samplerCount = 0;

			for (auto& [hash, instance] : m_ShaderResourceInstances)
			{
				if (!instance) continue;
				auto* pFileInfo = instance->GetShaderFileInfo();
				if (pFileInfo)
					maxSets += static_cast<uint32_t>(pFileInfo->shaderBindingInfo.setLayoutInfos.size());

				for (auto& cbuffer : instance->GetCBufferBindings())
					if (cbuffer.pCBufferStruct) ++uniformBufferCount;

				for (auto& image : instance->GetImageBindings())
				{
					if (image.resourceUsages == EResourceUsage::eShaderUnorderedAccess)
						++storageImageCount;
					else
						++sampledImageCount;
				}

				for (auto& buffer : instance->GetBufferBindings())
					++storageBufferCount;

				for (auto& sampler : instance->GetSamplerBindings())
					++samplerCount;
			}

			castl::vector<vk::DescriptorPoolSize> poolSizes;
			if (uniformBufferCount > 0)
				poolSizes.push_back({ vk::DescriptorType::eUniformBuffer, uniformBufferCount });
			if (sampledImageCount > 0)
				poolSizes.push_back({ vk::DescriptorType::eSampledImage, sampledImageCount });
			if (storageBufferCount > 0)
				poolSizes.push_back({ vk::DescriptorType::eStorageBuffer, storageBufferCount });
			if (storageImageCount > 0)
				poolSizes.push_back({ vk::DescriptorType::eStorageImage, storageImageCount });
			if (samplerCount > 0)
				poolSizes.push_back({ vk::DescriptorType::eSampler, samplerCount });

			auto& resourceManager = m_CurrentFrameContext->GetResourceManager();
			resourceManager.EnsurePoolCapacity(maxSets, poolSizes);
		}

		// Task 3.3a: Swapchain acquire — for each backbuffer, ensure window sync and acquire next image
		if (!graph->GetFinalizePass().isEmpty())
		{
			uint32_t windowIdx = 0;
			for (auto& backBufferImage : graph->GetFinalizePass().m_PresentBackBuffers)
			{
				VulkanWindowHandle* pWindow = backBufferImage.GetWindowPtr<VulkanWindowHandle>();
				if (pWindow && pWindow->IsValid())
				{
					if (pWindow->NeedsRecreation())
						pWindow->RecreateSwapchain();

					m_CurrentFrameContext->EnsureWindowSync(windowIdx);
					auto const& sync = m_CurrentFrameContext->GetWindowSync(windowIdx);
					pWindow->AcquireNextImage(sync.acquireSemaphore);
				}
				++windowIdx;
			}
		}

		m_PrepareTime = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - prepareStart).count();

		// Phase 2: Build dependency-free batches
		BuildDependencyFreeBatches(*graph);

		// Phase 3: Build resource usage ranges
		BuildResourceUsageRanges();

		// Phase 4: Register CBuffer resources for aliasing
		RegisterCBufferForAliasing(*graph);

		// Phase 4.5: Allocate aliased resources
		AllocateAliasedResources();

		// Phase 5: Build resources (resolve CBuffer resource IDs etc.)
		for (auto& [hash, instance] : m_ShaderResourceInstances)
		{
			if (instance)
				instance->BuildResources(m_LocalResourceManager, m_ConstantBufferManager, *graph);
		}

		// Prepare batch resource barriers
		PrepareBatchResourceBarriers(*graph);

		// Phase 6: Build pipeline states
		BuildPipelineStates(*graph);

		// Build descriptors from FrameContext descriptor pool
		{
			auto& resourceManager = m_CurrentFrameContext->GetResourceManager();
			vk::DescriptorPool pool = resourceManager.GetDescriptorPool();
			if (pool)
			{
				for (auto& [hash, instance] : m_ShaderResourceInstances)
				{
					if (instance)
						instance->BuildDescriptors(m_LocalResourceManager, pool);
				}
			}
		}

		// Phase 7: Execute
		auto executeStart = std::chrono::high_resolution_clock::now();
		Execute(*graph);
		m_ExecuteTime = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - executeStart).count();

		// Apply external resource states
		ApplyExternalResourceStates();

		// Present windows
		PresentWindows(*graph);

		// Task 3.8: Clear descriptor set references (pool will be reset on next Aquire)
		for (auto& [hash, instance] : m_ShaderResourceInstances)
		{
			if (instance)
			{
				instance->Release();
			}
		}

		// Task 3.6: Reset frame-level temporary data
		Reset();

		CA_LOG_INFO("VulkanGraphExecutor: Prepare={}us, Execute={}us, Batches={}",
			m_PrepareTime, m_ExecuteTime, m_ExecutionBatches.size());
	}

	void VulkanGraphExecutor::Prepare(GPUGraph const& graph)
	{
		InitArraySizes(graph);
		CollectResources(graph);
		CollectShaderBindings(graph);
		RegisterComputeResources(graph);
		RegisterCBufferUsageStates(graph);
	}

	void VulkanGraphExecutor::RegisterCBufferForAliasing(GPUGraph const& graph)
	{
		for (auto& [hash, instance] : m_ShaderResourceInstances)
		{
			if (!instance) continue;

			for (auto& cbuffer : instance->GetCBufferBindings())
			{
				if (!cbuffer.pCBufferStruct) continue;

				VulkanShaderStruct const* pStruct = cbuffer.pCBufferStruct;
				uint64_t bufferSize = pStruct->GetCBufferSize();
				GPUBufferDescriptor desc = GPUBufferDescriptor::Create(1, static_cast<uint32_t>(bufferSize));

				uint64_t resourceId = m_ConstantBufferManager.GetOrCreateResourceId(
					pStruct, m_LocalResourceManager, desc);

				auto lifetimeIt = m_CBufferLifetimes.find(pStruct);
				if (lifetimeIt != m_CBufferLifetimes.end())
				{
					auto const& lifeTime = lifetimeIt->second.lifeTime;
					if (!lifeTime.empty())
					{
						m_LocalResourceManager.MarkResourceUse(resourceId, *lifeTime.begin());
						m_LocalResourceManager.MarkResourceUse(resourceId, *lifeTime.rbegin());
					}
				}
			}
		}
	}

	void VulkanGraphExecutor::RegisterCBufferUsageStates(GPUGraph const& graph)
	{
		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& passData = m_RasterPassGPUData[passID];
			auto& passRWState = m_RasterPassRWStates[passID];

			for (auto& batchData : passData.drawcallBatchs)
			{
				auto* pBindingInstance = batchData.pResourceBindingInstance;
				if (!pBindingInstance) continue;

				for (auto& cbuffer : pBindingInstance->GetCBufferBindings())
				{
					if (!cbuffer.pCBufferStruct) continue;
					vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
					passRWState.SetCBufferUsageState(cbuffer.pCBufferStruct, stages, EGPUQueueType::eDirect);
				}
			}
		}

		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passData = m_ComputePassGPUData[passID];
			auto& passRWState = m_ComputePassRWStates[passID];
			EGPUQueueType queueType = computePass.asyncCompute ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

			for (auto& dispatchData : passData.dispatchs)
			{
				auto* pBindingInstance = dispatchData.pResourceBindingInstance;
				if (!pBindingInstance) continue;

				for (auto& cbuffer : pBindingInstance->GetCBufferBindings())
				{
					if (!cbuffer.pCBufferStruct) continue;
					vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eComputeShader;
					passRWState.SetCBufferUsageState(cbuffer.pCBufferStruct, stages, queueType);
				}
			}
		}
	}

	void VulkanGraphExecutor::InitArraySizes(GPUGraph const& graph)
	{
		m_RasterPassRWStates.resize(graph.GetRenderPasses().size());
		m_RasterPassGPUData.resize(graph.GetRenderPasses().size());

		for (size_t rasterPassID = 0; rasterPassID < graph.GetRenderPasses().size(); ++rasterPassID)
		{
			auto& pass = graph.GetRenderPasses()[rasterPassID];
			auto& passData = m_RasterPassGPUData[rasterPassID];
			passData.drawcallBatchs.resize(pass.GetDrawCallBatches().size());
			for (size_t batchID = 0; batchID < pass.GetDrawCallBatches().size(); ++batchID)
			{
				passData.drawcallBatchs[batchID].drawcalls.resize(
					pass.GetDrawCallBatches()[batchID].m_DrawCalls.size());
			}
		}

		m_ComputePassRWStates.resize(graph.GetComputePasses().size());
		m_ComputePassGPUData.resize(graph.GetComputePasses().size());
		for (size_t computePassID = 0; computePassID < graph.GetComputePasses().size(); ++computePassID)
		{
			auto& pass = graph.GetComputePasses()[computePassID];
			auto& passData = m_ComputePassGPUData[computePassID];
			passData.dispatchs.resize(pass.dispatchs.size());
		}

		m_TransferPassRWStates.resize(graph.GetDataTransfers().size());
	}

	void VulkanGraphExecutor::CollectResources(GPUGraph const& graph)
	{
		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& renderPass = graph.GetRenderPasses()[passID];
			auto& passRWState = m_RasterPassRWStates[passID];

			int attachmentID = 0;
			for (auto& attachment : renderPass.GetAttachments())
			{
				auto descriptor = GetDescriptor(graph, attachment);
				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, ETextureAccessType::eRT, static_cast<uint32_t>(passID));

				if (attachmentID == renderPass.GetDepthAttachmentIndex())
				{
					passRWState.SetImageRWState(attachment,
						vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
						vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
						vk::ImageLayout::eDepthStencilAttachmentOptimal,
						EGPUQueueType::eDirect);
				}
				else
				{
					passRWState.SetImageRWState(attachment,
						vk::PipelineStageFlagBits::eColorAttachmentOutput,
						vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
						vk::ImageLayout::eColorAttachmentOptimal,
						EGPUQueueType::eDirect);
				}
				++attachmentID;
			}

			for (auto& batch : renderPass.GetDrawCallBatches())
			{
				for (auto& drawcall : batch.m_DrawCalls)
				{
					if (drawcall.GetDrawInfo().drawIndexed)
					{
						auto& indexBuffer = drawcall.GetIndexBuffer().indexBufferHandle;
						auto descriptor = GetDescriptor(graph, indexBuffer);
						m_LocalResourceManager.RegisterTemporaryBuffer(
							descriptor, EBufferUsage::eIndexBuffer, static_cast<uint32_t>(passID));
						passRWState.SetBufferRWState(indexBuffer,
							vk::PipelineStageFlagBits::eVertexInput,
							vk::AccessFlagBits::eIndexRead, EGPUQueueType::eDirect);
					}

					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						auto& vertBuffer = vertBuf.second;
						auto descriptor = GetDescriptor(graph, vertBuffer);
						m_LocalResourceManager.RegisterTemporaryBuffer(
							descriptor, EBufferUsage::eVertexBuffer, static_cast<uint32_t>(passID));
						passRWState.SetBufferRWState(vertBuffer,
							vk::PipelineStageFlagBits::eVertexInput,
							vk::AccessFlagBits::eVertexAttributeRead, EGPUQueueType::eDirect);
					}
				}
			}
		}

		for (size_t passID = 0; passID < graph.GetDataTransfers().size(); ++passID)
		{
			auto& transferPass = graph.GetDataTransfers()[passID];
			auto& passRWState = m_TransferPassRWStates[passID];

			for (auto& bufferWrites : transferPass.m_BufferDataUploads)
			{
				auto& uploadBuffer = bufferWrites.first;
				passRWState.SetBufferRWState(uploadBuffer,
					vk::PipelineStageFlagBits::eTransfer,
					vk::AccessFlagBits::eTransferWrite, EGPUQueueType::eDirect);
			}

			for (auto& imgWrites : transferPass.m_ImageDataUploads)
			{
				auto descriptor = GetDescriptor(graph, imgWrites.first);
				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, ETextureAccessTypeFlags{}, static_cast<uint32_t>(passID));
				passRWState.SetImageRWState(imgWrites.first,
					vk::PipelineStageFlagBits::eTransfer,
					vk::AccessFlagBits::eTransferWrite,
					vk::ImageLayout::eTransferDstOptimal, EGPUQueueType::eDirect);
			}
		}

		if (!graph.GetFinalizePass().isEmpty())
		{
			for (auto& img : graph.GetFinalizePass().m_ImageUsages)
			{
				auto descriptor = GetDescriptor(graph, img.first);
				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, img.second, static_cast<uint32_t>(m_ExecutionBatches.size()));
				m_FinalizePassRWState.SetImageRWState(img.first,
					vk::PipelineStageFlagBits::eFragmentShader,
					vk::AccessFlagBits::eShaderRead,
					vk::ImageLayout::eShaderReadOnlyOptimal, EGPUQueueType::eDirect);
			}
			for (auto& backBuffer : graph.GetFinalizePass().m_PresentBackBuffers)
			{
				m_FinalizePassRWState.SetImageRWState(backBuffer,
					vk::PipelineStageFlagBits::eBottomOfPipe,
					vk::AccessFlagBits::eNone,
					vk::ImageLayout::ePresentSrcKHR, EGPUQueueType::eDirect);
			}
		}

		graph.GetBufferManager().Foreach([&](ResourceHandleKeyData const& handleKey, auto& desc)
		{
			m_LocalResourceManager.RegisterTemporaryBuffer(desc, EBufferUsageFlags{}, 0);
		});
		graph.GetImageManager().Foreach([&](ResourceHandleKeyData const& handleKey, auto& desc)
		{
			m_LocalResourceManager.RegisterTemporaryTexture(desc, ETextureAccessTypeFlags{}, 0);
		});
	}

	void VulkanGraphExecutor::CollectShaderBindings(GPUGraph const& graph)
	{
		auto createOrGetBindingInstance = [&](VulkanShaderResourceSet const& resourceSet)
			-> VulkanResourceBindingInstance*
		{
			auto it = m_ShaderResourceInstances.find(resourceSet.hash);
			if (it != m_ShaderResourceInstances.end())
				return it->second.get();

			auto bindingInstance = castl::make_shared<VulkanResourceBindingInstance>();
			GetApp()->InitSubObj(bindingInstance.get());
			bindingInstance->Init(resourceSet);
			m_ShaderResourceInstances[resourceSet.hash] = bindingInstance;
			return bindingInstance.get();
		};

		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& renderPass = graph.GetRenderPasses()[passID];
			auto& passData = m_RasterPassGPUData[passID];

			for (size_t batchID = 0; batchID < renderPass.GetDrawCallBatches().size(); ++batchID)
			{
				auto& batch = renderPass.GetDrawCallBatches()[batchID];
				auto& batchData = passData.drawcallBatchs[batchID];

				ShaderInfo const& shaderInfo = batch.pipelineStateDesc.m_ShaderInfo;
				if (!shaderInfo.isValid()) continue;

				castl::vector<ShaderStructDic const*> shaderStructs;
				if (!batch.shaderStructs.empty())
					shaderStructs.push_back(&batch.shaderStructs);
				if (!renderPass.GetShaderStructs().empty())
					shaderStructs.push_back(&renderPass.GetShaderStructs());

				VulkanShaderResourceSet resourceSet;
				resourceSet.Init(GetApp(), shaderInfo, shaderStructs);
				batchData.pResourceBindingInstance = createOrGetBindingInstance(resourceSet);
			}
		}

		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passData = m_ComputePassGPUData[passID];

			for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				auto& dispatchData = passData.dispatchs[dispatchID];

				ShaderInfo const& shaderInfo = dispatch.m_ShaderInfo;
				if (!shaderInfo.isValid()) continue;

				castl::vector<ShaderStructDic const*> shaderStructs;
				if (!dispatch.shaderStructs.empty())
					shaderStructs.push_back(&dispatch.shaderStructs);
				if (!computePass.shaderStructs.empty())
					shaderStructs.push_back(&computePass.shaderStructs);

				VulkanShaderResourceSet resourceSet;
				resourceSet.Init(GetApp(), shaderInfo, shaderStructs);
				dispatchData.pResourceBindingInstance = createOrGetBindingInstance(resourceSet);
			}
		}
	}

	void VulkanGraphExecutor::RegisterComputeResources(GPUGraph const& graph)
	{
		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passRWState = m_ComputePassRWStates[passID];
			EGPUQueueType queueType = computePass.asyncCompute ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

			for (auto& dispatchData : m_ComputePassGPUData[passID].dispatchs)
			{
				auto* pBindingInstance = dispatchData.pResourceBindingInstance;
				if (!pBindingInstance) continue;

				for (auto& imageBinding : pBindingInstance->GetImageBindings())
				{
					for (auto& [imageHandle, textureView] : imageBinding.bindings)
					{
						passRWState.SetImageRWState(imageHandle,
							vk::PipelineStageFlagBits::eComputeShader,
							ComputeAccessToVulkanAccess(imageBinding.bindingInfo.accessType),
							ComputeAccessToImageLayout(imageBinding.bindingInfo.accessType),
							queueType);
					}
				}

				for (auto& bufferBinding : pBindingInstance->GetBufferBindings())
				{
					for (auto& bufferHandle : bufferBinding.bindings)
					{
						passRWState.SetBufferRWState(bufferHandle,
							vk::PipelineStageFlagBits::eComputeShader,
							ComputeAccessToVulkanAccess(bufferBinding.bindingInfo.accessType),
							queueType);
					}
				}
			}
		}
	}

	void VulkanGraphExecutor::BuildDependencyFreeBatches(GPUGraph const& graph)
	{
		using EGraphStageType = GPUGraph::EGraphStageType;

		bool hasFinalBatch = !graph.GetFinalizePass().isEmpty();
		auto& stages = graph.GetGraphStages();
		size_t stageCount = stages.size() + (hasFinalBatch ? 1 : 0);

		castl::vector<VulkanPassDependency> passDeps;
		passDeps.reserve(stageCount);
		castl::list<VulkanPassDependency*> pendingDependencies;

		size_t stageID = 0;
		for (stageID = 0; stageID < stages.size(); ++stageID)
		{
			auto stage = stages[stageID];
			auto passID = graph.GetPassIndices()[stageID];

			switch (stage)
			{
			case EGraphStageType::eRenderPass:
				passDeps.emplace_back(m_RasterPassRWStates[passID], passID, stage);
				break;
			case EGraphStageType::eComputePass:
				passDeps.emplace_back(m_ComputePassRWStates[passID], passID, stage);
				break;
			case EGraphStageType::eTransferPass:
				passDeps.emplace_back(m_TransferPassRWStates[passID], passID, stage);
				break;
			}
			pendingDependencies.push_back(&passDeps.back());
		}

		if (hasFinalBatch)
		{
			passDeps.emplace_back(m_FinalizePassRWState, 0, EGraphStageType::eFinalPass);
			pendingDependencies.push_back(&passDeps.back());
		}

		for (size_t prevPass = 0; prevPass < passDeps.size() - 1; ++prevPass)
			for (size_t latterPass = prevPass + 1; latterPass < passDeps.size(); ++latterPass)
				passDeps[prevPass].CheckAddSuccessor(&passDeps[latterPass]);

		while (!pendingDependencies.empty())
		{
			VulkanGPUExecutionBatch& newBatch = m_ExecutionBatches.emplace_back();
			newBatch.anyComputeQueueOperations = false;

			castl::vector<VulkanPassDependency*> passFreeDeps;
			auto depItr = pendingDependencies.begin();

			while (depItr != pendingDependencies.end())
			{
				VulkanPassDependency* dep = *depItr;
				if (dep->DepsFree())
				{
					depItr = pendingDependencies.erase(depItr);
					passFreeDeps.push_back(dep);
					newBatch.batchRWStates.Append(dep->rwState);

					switch (dep->passType)
					{
					case EGraphStageType::eRenderPass:
						newBatch.rasterPassRefs.push_back(dep->passID);
						break;
					case EGraphStageType::eComputePass:
						newBatch.anyComputeQueueOperations =
							newBatch.anyComputeQueueOperations ||
							graph.GetComputePasses()[dep->passID].asyncCompute;
						newBatch.computePassRefs.push_back(dep->passID);
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

			for (VulkanPassDependency* dep : passFreeDeps)
				dep->RemoveSelfDeps();
		}
	}

	void VulkanGraphExecutor::BuildResourceUsageRanges()
	{
		for (uint32_t batchID = 0; batchID < m_ExecutionBatches.size(); ++batchID)
		{
			auto& batch = m_ExecutionBatches[batchID];
			auto& rwStates = batch.batchRWStates;

			for (auto& imageRWState : rwStates.imageRWStates)
				m_ImageLifetimes[imageRWState.first].Expand(batchID, imageRWState.second);

			for (auto& bufferRWState : rwStates.bufferRWStates)
				m_BufferLifetimes[bufferRWState.first].Expand(batchID, bufferRWState.second);

			for (auto cbufferStruct : rwStates.cBufferUsageStates)
				m_CBufferLifetimes[cbufferStruct.first].Encapsule(batchID, cbufferStruct.second);
		}
	}

	void VulkanGraphExecutor::AllocateAliasedResources()
	{
		m_LocalResourceManager.AllocateAliasedResources();
	}

	void VulkanGraphExecutor::PrepareBatchResourceBarriers(GPUGraph const& graph)
	{
		for (auto& pair : m_ImageLifetimes)
		{
			ImageHandle const& image = pair.first;
			VulkanResourceState cachedState = VulkanResourceState::InitializedState();
			VulkanResourceUsageRangeData const& usageRanges = pair.second;

			for (int bid = 0; bid < usageRanges.states.size(); ++bid)
			{
				auto& batchAndState = usageRanges.states[bid];
				int currentBatchID = batchAndState.batchID;
				VulkanResourceState const& currentState = batchAndState.state;

				bool isFirstState = bid == 0;
				int lastBatchID = isFirstState ? -1 : usageRanges.states[bid - 1].batchID;
				bool stateHaveGap = isFirstState ? false : ((currentBatchID - lastBatchID) > 1);
				VulkanResourceState const& lastState = isFirstState ? cachedState : usageRanges.states[bid - 1].state;

				if (lastState.accessFlags == currentState.accessFlags &&
					lastState.imageLayout == currentState.imageLayout)
					continue;

				auto& currentBatch = m_ExecutionBatches[currentBatchID];

				vk::ImageMemoryBarrier barrier{};
				barrier.srcAccessMask = lastState.accessFlags;
				barrier.dstAccessMask = currentState.accessFlags;
				barrier.oldLayout = lastState.imageLayout;
				barrier.newLayout = currentState.imageLayout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = m_LocalResourceManager.GetTexture(image);
				barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

				if (stateHaveGap)
				{
					auto& lastBatch = m_ExecutionBatches[lastBatchID];
					lastBatch.releaseBarriers.AddImageBarrier(image, barrier);
					currentBatch.aquireBarriers.AddImageBarrier(image, barrier);
				}
				else
				{
					currentBatch.aquireBarriers.AddImageBarrier(image, barrier);
				}
			}
		}

		for (auto& pair : m_BufferLifetimes)
		{
			BufferHandle const& buffer = pair.first;
			VulkanResourceUsageRangeData const& usageRanges = pair.second;
			VulkanResourceState cachedState = VulkanResourceState::InitializedState();

			for (int bid = 0; bid < usageRanges.states.size(); ++bid)
			{
				auto& batchAndState = usageRanges.states[bid];
				int currentBatchID = batchAndState.batchID;
				VulkanResourceState const& currentState = batchAndState.state;

				bool isFirstState = bid == 0;
				int lastBatchID = isFirstState ? -1 : usageRanges.states[bid - 1].batchID;
				bool stateHaveGap = isFirstState ? false : ((currentBatchID - lastBatchID) > 1);
				VulkanResourceState const& lastState = isFirstState ? cachedState : usageRanges.states[bid - 1].state;

				if (lastState.accessFlags == currentState.accessFlags)
					continue;

				auto& currentBatch = m_ExecutionBatches[currentBatchID];

				vk::BufferMemoryBarrier barrier{};
				barrier.srcAccessMask = lastState.accessFlags;
				barrier.dstAccessMask = currentState.accessFlags;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = m_LocalResourceManager.GetBuffer(buffer);
				barrier.offset = 0;
				barrier.size = VK_WHOLE_SIZE;

				if (stateHaveGap)
				{
					auto& lastBatch = m_ExecutionBatches[lastBatchID];
					lastBatch.releaseBarriers.AddBufferBarrier(buffer, barrier);
					currentBatch.aquireBarriers.AddBufferBarrier(buffer, barrier);
				}
				else
				{
					currentBatch.aquireBarriers.AddBufferBarrier(buffer, barrier);
				}
			}
		}

		for (auto& pair : m_CBufferLifetimes)
		{
			auto& cbufferUsage = pair.second;
			if (cbufferUsage.lifeTime.empty()) continue;

			VulkanShaderStruct const* pStruct = pair.first;
			uint64_t resourceId = m_ConstantBufferManager.GetResourceId(pStruct);
			if (resourceId == 0) continue;
			int initialBatchID = *cbufferUsage.lifeTime.begin();
			auto& initialBatch = m_ExecutionBatches[initialBatchID];
			EGPUQueueTypeFlags queueFlags = cbufferUsage.queueTypes;
			initialBatch.GetCBufferBarriers(queueFlags).AddCBuffer(resourceId, pStruct);
		}
	}

	void VulkanGraphExecutor::BuildPipelineStates(GPUGraph const& graph)
	{
		auto pApp = GetApp();
		auto& pipelineLibrary = pApp->GetPipelineLibrary();
		auto& pipelineLibraryCache = pApp->GetPipelineLibraryCache();

		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& rasterPass = graph.GetRenderPasses()[passID];
			auto& rasterPassData = m_RasterPassGPUData[passID];

			auto const& attachments = rasterPass.GetAttachments();
			if (attachments.empty()) continue;

			for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
			{
				auto& batch = rasterPass.GetDrawCallBatches()[batchID];
				auto& batchData = rasterPassData.drawcallBatchs[batchID];

				ShaderInfo const& shaderInfo = batch.pipelineStateDesc.m_ShaderInfo;
				if (!shaderInfo.isValid()) continue;

				VulkanShaderFileInfo const* pFileInfo = pApp->GetShaderFileInfo(shaderInfo);
				if (pFileInfo == nullptr) continue;

				cahash::sha256_hash::result_type vertexProgramHash{};
				cahash::sha256_hash::result_type fragmentProgramHash{};
				cacore::NameHash vertexEntryPointName;
				cacore::NameHash fragmentEntryPointName;
				bool hasVertex = false;
				bool hasFragment = false;

				for (auto const& programInfo : pFileInfo->entryPointToShaderProgram)
				{
					if (programInfo.shaderType == ECompileShaderType::eVert)
					{
						vertexProgramHash = programInfo.programHash;
						vertexEntryPointName = programInfo.entryPointName;
						hasVertex = true;
					}
					else if (programInfo.shaderType == ECompileShaderType::eFrag)
					{
						fragmentProgramHash = programInfo.programHash;
						fragmentEntryPointName = programInfo.entryPointName;
						hasFragment = true;
					}
				}

				vk::ShaderModule vertexShaderModule = hasVertex ? pApp->GetOrCreateShaderModule(vertexProgramHash) : nullptr;
				vk::ShaderModule fragmentShaderModule = hasFragment ? pApp->GetOrCreateShaderModule(fragmentProgramHash) : nullptr;

				vk::PipelineLayout pipelineLayout = pApp->GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);

				vk::PipelineShaderStageCreateInfo vertexShaderStage{};
				vertexShaderStage.stage = vk::ShaderStageFlagBits::eVertex;
				vertexShaderStage.module = vertexShaderModule;
				vertexShaderStage.pName = vertexEntryPointName.c_str();

				vk::PipelineShaderStageCreateInfo fragmentShaderStage{};
				fragmentShaderStage.stage = vk::ShaderStageFlagBits::eFragment;
				fragmentShaderStage.module = fragmentShaderModule;
				fragmentShaderStage.pName = fragmentEntryPointName.c_str();

				if (pipelineLibrary.IsSupported())
				{
					vk::PipelineVertexInputStateCreateInfo vertexInputState{};
					vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
					inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

					vk::PipelineViewportStateCreateInfo viewportState{};
					viewportState.viewportCount = 1;
					viewportState.scissorCount = 1;

					vk::PipelineRasterizationStateCreateInfo rasterizationState{};
					rasterizationState.polygonMode = vk::PolygonMode::eFill;
					rasterizationState.cullMode = vk::CullModeFlagBits::eBack;
					rasterizationState.frontFace = vk::FrontFace::eClockwise;
					rasterizationState.lineWidth = 1.0f;

					vk::PipelineMultisampleStateCreateInfo multisampleState{};
					multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

					vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
					colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
						vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

					vk::PipelineColorBlendStateCreateInfo colorBlendState{};
					colorBlendState.attachmentCount = 1;
					colorBlendState.pAttachments = &colorBlendAttachment;

					auto vertexInputLib = pipelineLibrary.CreateVertexInputLibrary(vertexInputState, inputAssemblyState);
					auto preRasterLib = pipelineLibrary.CreatePreRasterizationLibrary(
						vertexShaderStage, nullptr, nullptr, nullptr, viewportState, rasterizationState);
					auto fragmentLib = pipelineLibrary.CreateFragmentLibrary(fragmentShaderStage);
					auto fragmentOutputLib = pipelineLibrary.CreateFragmentOutputLibrary(
						multisampleState, nullptr, colorBlendState);

					PipelineLibraryParts parts{};
					parts.vertexInputLibrary = vertexInputLib;
					parts.preRasterizationLibrary = preRasterLib;
					parts.fragmentLibrary = fragmentLib;
					parts.fragmentOutputLibrary = fragmentOutputLib;
					parts.isComplete = true;

					vk::RenderPass dummyRenderPass = VK_NULL_HANDLE;
					batchData.pipeline = pipelineLibrary.LinkPipeline(parts, pipelineLayout, dummyRenderPass, 0);
				}
				else
				{
					vk::GraphicsPipelineCreateInfo createInfo{};
					createInfo.layout = pipelineLayout;
					batchData.pipeline = pipelineLibrary.CreateMonolithicPipeline(createInfo);
				}

				batchData.pipelineLayout = pipelineLayout;
				batchData.topology = vk::PrimitiveTopology::eTriangleList;
			}
		}

		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& computePassData = m_ComputePassGPUData[passID];

			for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				auto& dispatchData = computePassData.dispatchs[dispatchID];

				ShaderInfo const& shaderInfo = dispatch.m_ShaderInfo;
				if (!shaderInfo.isValid()) continue;

				VulkanShaderFileInfo const* pFileInfo = pApp->GetShaderFileInfo(shaderInfo);
				if (pFileInfo == nullptr) continue;

				cahash::sha256_hash::result_type computeProgramHash{};
				cacore::NameHash computeEntryPointName;
				bool hasCompute = false;

				for (auto const& programInfo : pFileInfo->entryPointToShaderProgram)
				{
					if (programInfo.shaderType == ECompileShaderType::eComp)
					{
						computeProgramHash = programInfo.programHash;
						computeEntryPointName = programInfo.entryPointName;
						hasCompute = true;
						break;
					}
				}

				if (!hasCompute) continue;

				vk::ShaderModule computeShaderModule = pApp->GetOrCreateShaderModule(computeProgramHash);
				vk::PipelineLayout pipelineLayout = pApp->GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);

				vk::PipelineShaderStageCreateInfo computeShaderStage{};
				computeShaderStage.stage = vk::ShaderStageFlagBits::eCompute;
				computeShaderStage.module = computeShaderModule;
				computeShaderStage.pName = computeEntryPointName.c_str();

				vk::ComputePipelineCreateInfo createInfo{};
				createInfo.layout = pipelineLayout;
				createInfo.stage = computeShaderStage;

				auto device = GetDevice();
				try
				{
					auto result = device.createComputePipeline(nullptr, createInfo);
					if (result.result == vk::Result::eSuccess)
						dispatchData.pipeline = result.value;
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Failed to create compute pipeline: {}", e.what());
				}

				dispatchData.pipelineLayout = pipelineLayout;
			}
		}
	}

	// Task 3.3: CommandBuffer from FrameContext's CommandListManager
	void VulkanGraphExecutor::Execute(GPUGraph const& graph)
	{
		for (auto& batch : m_ExecutionBatches)
			RecordBatchCommands(batch, graph);

		SubmitBatches(graph);
	}

	// Task 3.3: Get CommandBuffer from FrameContext's CommandListManager
	// Task 3.5: Staging buffer allocation via LinearMemoryManager
	void VulkanGraphExecutor::RecordBatchCommands(VulkanGPUExecutionBatch& batch, GPUGraph const& graph)
	{
		auto& resourceManager = m_CurrentFrameContext->GetResourceManager();
		auto& cmdListManager = resourceManager.GetCommandListManager();

		batch.directCommandBuffer = cmdListManager.GraphicsCommand();

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		batch.directCommandBuffer.begin(beginInfo);

		if (batch.aquireBarriers.AnyBarrier())
			batch.aquireBarriers.ExecuteBarriers(batch.directCommandBuffer);

		// Task 3.5: Upload CBuffer data via LinearMemoryManager staging buffer
		auto device = GetDevice();
		auto cmdBuf = batch.directCommandBuffer;
		auto& stagingManager = resourceManager.GetStagingMemoryManager();

		auto uploadCBufferBarriers = [&](VulkanCBufferInitializeBarriers& cbufferBarriers)
		{
			if (!cbufferBarriers.AnyBarrier()) return;

			for (auto& [resourceId, pStruct] : cbufferBarriers.cbufferData)
			{
				if (!pStruct) continue;

				vk::Buffer gpuBuffer = m_LocalResourceManager.GetBuffer(resourceId);
				if (!gpuBuffer) continue;

				uint64_t bufferSize = pStruct->GetCBufferSize();
				if (bufferSize == 0) continue;

				auto stagingAlloc = stagingManager.AllocUploadStagingBuffer(bufferSize, 256);
				if (!stagingAlloc.mappedPtr) continue;

				pStruct->ComputeMaxChildrenVersion();
				pStruct->UpdateUniformBuffer(0, stagingAlloc.mappedPtr, static_cast<uint32_t>(bufferSize), 0);

				vk::BufferCopy copyRegion{};
				copyRegion.srcOffset = stagingAlloc.offset;
				copyRegion.dstOffset = 0;
				copyRegion.size = bufferSize;
				cmdBuf.copyBuffer(stagingAlloc.buffer, gpuBuffer, 1, &copyRegion);

				vk::BufferMemoryBarrier barrier{};
				barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = gpuBuffer;
				barrier.offset = 0;
				barrier.size = bufferSize;

				cmdBuf.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
					vk::DependencyFlags{},
					{}, barrier, {});
			}
		};

		uploadCBufferBarriers(batch.cbufferBarriers);
		uploadCBufferBarriers(batch.computeCBufferBarriers);

		for (int rasterPassID : batch.rasterPassRefs)
			RecordRenderPass(batch, rasterPassID, graph);

		for (int computePassID : batch.computePassRefs)
			RecordComputePass(batch, computePassID, graph);

		for (int transferPassID : batch.transferPassRefs)
			RecordTransferPass(batch, transferPassID, graph);

		if (batch.releaseBarriers.AnyBarrier())
			batch.releaseBarriers.ExecuteBarriers(batch.directCommandBuffer);

		batch.directCommandBuffer.end();
	}

	void VulkanGraphExecutor::RecordRenderPass(VulkanGPUExecutionBatch& batch,
		uint32_t rasterPassID, GPUGraph const& graph)
	{
		auto& rasterPass = graph.GetRenderPasses()[rasterPassID];
		auto& rasterData = m_RasterPassGPUData[rasterPassID];
		auto cmdBuf = batch.directCommandBuffer;

		auto const& attachments = rasterPass.GetAttachments();
		if (attachments.empty()) return;

		GPUTextureDescriptor firstAttachmentDesc = GetDescriptor(graph, attachments[0]);

		vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(firstAttachmentDesc.width),
			static_cast<float>(firstAttachmentDesc.height), 0.0f, 1.0f };
		vk::Rect2D scissor{ {0, 0}, {firstAttachmentDesc.width, firstAttachmentDesc.height} };
		cmdBuf.setViewport(0, viewport);
		cmdBuf.setScissor(0, scissor);

		// Build render pass cache key
		RenderBackend_Vulkan::RenderPassCacheKey rpKey;
		for (size_t i = 0; i < attachments.size(); ++i)
		{
			auto desc = GetDescriptor(graph, attachments[i]);
			if (static_cast<int>(i) == rasterPass.GetDepthAttachmentIndex())
			{
				rpKey.depthFormat = VulkanTexture::ConvertFormat(desc.format);
				rpKey.hasDepth = true;
			}
			else
			{
				rpKey.colorFormats.push_back(VulkanTexture::ConvertFormat(desc.format));
			}
		}

		vk::RenderPass renderPass = GetApp()->GetOrCreateRenderPass(rpKey);
		if (!renderPass) return;

		castl::vector<vk::ImageView> attachmentViews;
		for (auto& attachment : attachments)
		{
			vk::ImageView view = m_LocalResourceManager.GetTextureView(attachment);
			if (view) attachmentViews.push_back(view);
		}

		vk::Framebuffer framebuffer = GetApp()->GetOrCreateFramebuffer(renderPass, attachmentViews,
			firstAttachmentDesc.width, firstAttachmentDesc.height);
		if (!framebuffer) return;

		castl::vector<vk::ClearValue> clearValues;
		for (size_t i = 0; i < attachments.size(); ++i)
		{
			auto const& config = rasterPass.GetAttachmentConfig(i);
			vk::ClearValue clearValue{};
			if (static_cast<int>(i) == rasterPass.GetDepthAttachmentIndex())
			{
				clearValue.depthStencil.depth = config.clearValue.depthStencil.depth;
				clearValue.depthStencil.stencil = config.clearValue.depthStencil.stencil;
			}
			else
			{
				clearValue.color.float32[0] = config.clearValue.color.r;
				clearValue.color.float32[1] = config.clearValue.color.g;
				clearValue.color.float32[2] = config.clearValue.color.b;
				clearValue.color.float32[3] = config.clearValue.color.a;
			}
			clearValues.push_back(clearValue);
		}

		vk::RenderPassBeginInfo renderPassBegin{};
		renderPassBegin.renderPass = renderPass;
		renderPassBegin.framebuffer = framebuffer;
		renderPassBegin.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassBegin.renderArea.extent = vk::Extent2D{ firstAttachmentDesc.width, firstAttachmentDesc.height };
		renderPassBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBegin.pClearValues = clearValues.data();

		cmdBuf.beginRenderPass(renderPassBegin, vk::SubpassContents::eInline);

		for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
		{
			auto& drawcallBatch = rasterPass.GetDrawCallBatches()[batchID];
			auto& batchData = rasterData.drawcallBatchs[batchID];

			if (batchData.pipeline)
				cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.pipeline);

			if (batchData.pResourceBindingInstance && batchData.pipelineLayout)
			{
				auto descriptorSets = batchData.pResourceBindingInstance->GetDescriptorSetsSorted();
				if (!descriptorSets.empty())
				{
					uint32_t firstSet = descriptorSets[0].first;
					castl::vector<vk::DescriptorSet> sets;
					sets.push_back(descriptorSets[0].second);

					for (uint32_t i = 1; i <= descriptorSets.size(); ++i)
					{
						bool isContinuation = (i < descriptorSets.size())
							&& (descriptorSets[i].first == descriptorSets[i - 1].first + 1);

						if (!isContinuation)
						{
							cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
								batchData.pipelineLayout, firstSet,
								static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
							sets.clear();
							if (i < descriptorSets.size())
							{
								firstSet = descriptorSets[i].first;
								sets.push_back(descriptorSets[i].second);
							}
						}
						else
						{
							sets.push_back(descriptorSets[i].second);
						}
					}
				}
			}

			for (size_t drawcallID = 0; drawcallID < drawcallBatch.m_DrawCalls.size(); ++drawcallID)
			{
				auto& drawcall = drawcallBatch.m_DrawCalls[drawcallID];
				auto& drawInfo = drawcall.GetDrawInfo();

				auto const& vertBuffers = drawcall.GetVertexBuffers();
				if (!vertBuffers.empty())
				{
					castl::vector<vk::Buffer> vertexBuffers;
					castl::vector<vk::DeviceSize> offsets;
					for (auto const& [name, bufferHandle] : vertBuffers)
					{
						vk::Buffer buffer = m_LocalResourceManager.GetBuffer(bufferHandle);
						if (buffer)
						{
							vertexBuffers.push_back(buffer);
							offsets.push_back(0);
						}
					}
					if (!vertexBuffers.empty())
						cmdBuf.bindVertexBuffers(0, vertexBuffers, offsets);
				}

				auto const& vp = drawcall.GetViewPort();
				if (vp.Valid())
				{
					vk::Viewport dynViewport{ static_cast<float>(vp->x), static_cast<float>(vp->y),
						static_cast<float>(vp->width), static_cast<float>(vp->height), 0.0f, 1.0f };
					cmdBuf.setViewport(0, dynViewport);
				}
				auto const& sc = drawcall.GetScissor();
				if (sc.Valid())
				{
					vk::Rect2D dynScissor{ {sc->x, sc->y}, {static_cast<uint32_t>(sc->width), static_cast<uint32_t>(sc->height)} };
					cmdBuf.setScissor(0, dynScissor);
				}

				if (drawInfo.drawIndexed)
				{
					auto const& indexBufferData = drawcall.GetIndexBuffer();
					if (indexBufferData.IsValid())
					{
						vk::Buffer indexBuffer = m_LocalResourceManager.GetBuffer(indexBufferData.indexBufferHandle);
						if (indexBuffer)
						{
							vk::IndexType indexType = indexBufferData.indexBufferType == EIndexBufferType::e16
								? vk::IndexType::eUint16 : vk::IndexType::eUint32;
							cmdBuf.bindIndexBuffer(indexBuffer, indexBufferData.indexBufferOffset, indexType);
						}
					}
					cmdBuf.drawIndexed(drawInfo.indexCount, drawInfo.instanceCount,
						drawInfo.indexOffset, drawInfo.vertexOffset, drawInfo.firstInstanceID);
				}
				else
				{
					cmdBuf.draw(drawInfo.vertexCount, drawInfo.instanceCount,
						drawInfo.vertexOffset, drawInfo.firstInstanceID);
				}
			}
		}

		cmdBuf.endRenderPass();
	}

	void VulkanGraphExecutor::RecordComputePass(VulkanGPUExecutionBatch& batch,
		uint32_t computePassID, GPUGraph const& graph)
	{
		auto& computePass = graph.GetComputePasses()[computePassID];
		auto& computeData = m_ComputePassGPUData[computePassID];
		auto cmdBuf = batch.directCommandBuffer;

		vk::CommandBuffer targetCmdBuf = batch.anyComputeQueueOperations && batch.computeCommandBuffer
			? batch.computeCommandBuffer : cmdBuf;

		for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
		{
			auto& dispatch = computePass.dispatchs[dispatchID];
			auto& dispatchData = computeData.dispatchs[dispatchID];

			if (dispatchData.pipeline)
				targetCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, dispatchData.pipeline);

			if (dispatchData.pResourceBindingInstance && dispatchData.pipelineLayout)
			{
				auto descriptorSets = dispatchData.pResourceBindingInstance->GetDescriptorSetsSorted();
				if (!descriptorSets.empty())
				{
					uint32_t firstSet = descriptorSets[0].first;
					castl::vector<vk::DescriptorSet> sets;
					sets.push_back(descriptorSets[0].second);

					for (uint32_t i = 1; i <= descriptorSets.size(); ++i)
					{
						bool isContinuation = (i < descriptorSets.size())
							&& (descriptorSets[i].first == descriptorSets[i - 1].first + 1);

						if (!isContinuation)
						{
							targetCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
								dispatchData.pipelineLayout, firstSet,
								static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
							sets.clear();
							if (i < descriptorSets.size())
							{
								firstSet = descriptorSets[i].first;
								sets.push_back(descriptorSets[i].second);
							}
						}
						else
						{
							sets.push_back(descriptorSets[i].second);
						}
					}
				}
			}

			if (dispatch.x > 0 && dispatch.y > 0 && dispatch.z > 0)
				targetCmdBuf.dispatch(dispatch.x, dispatch.y, dispatch.z);
		}

		if (!computePass.dispatchs.empty())
		{
			vk::MemoryBarrier memoryBarrier{};
			memoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			memoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead;

			targetCmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags{}, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
		}
	}

	// Task 3.5: Image upload staging also via LinearMemoryManager
	void VulkanGraphExecutor::RecordTransferPass(VulkanGPUExecutionBatch& batch,
		uint32_t transferPassID, GPUGraph const& graph)
	{
		auto& transferPass = graph.GetDataTransfers()[transferPassID];
		auto cmdBuf = batch.directCommandBuffer;
		auto device = GetDevice();
		auto const& uploadDataHolder = graph.GetUploadDataHolder();
		auto& resourceManager = m_CurrentFrameContext->GetResourceManager();
		auto& stagingManager = resourceManager.GetStagingMemoryManager();

		// Record buffer uploads via LinearMemoryManager
		for (auto& bufferUploads : transferPass.m_BufferDataUploads)
		{
			auto& [targetBufferHandle, dataRef] = bufferUploads;

			vk::Buffer targetBuffer = m_LocalResourceManager.GetBuffer(targetBufferHandle);
			if (!targetBuffer) continue;

			void const* pSourceData = nullptr;
			if (dataRef.copied && dataRef.dataIndex < uploadDataHolder.m_Data.size())
				pSourceData = uploadDataHolder.GetPtr(dataRef.dataIndex);
			else if (!dataRef.copied)
				pSourceData = dataRef.pData;

			if (!pSourceData || dataRef.dataSize == 0) continue;

			auto stagingAlloc = stagingManager.AllocUploadStagingBuffer(dataRef.dataSize, 256);
			if (!stagingAlloc.mappedPtr) continue;

			memcpy(stagingAlloc.mappedPtr, pSourceData, dataRef.dataSize);

			vk::BufferCopy copyRegion{};
			copyRegion.srcOffset = stagingAlloc.offset;
			copyRegion.dstOffset = dataRef.dstOffset;
			copyRegion.size = dataRef.dataSize;
			cmdBuf.copyBuffer(stagingAlloc.buffer, targetBuffer, 1, &copyRegion);

			vk::BufferMemoryBarrier barrier{};
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead |
				vk::AccessFlagBits::eIndexRead | vk::AccessFlagBits::eShaderRead;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = targetBuffer;
			barrier.offset = dataRef.dstOffset;
			barrier.size = dataRef.dataSize;

			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::DependencyFlags{}, 0, nullptr, 1, &barrier, 0, nullptr);
		}

		// Record image uploads via LinearMemoryManager
		for (auto& imageUploads : transferPass.m_ImageDataUploads)
		{
			auto& [targetImageHandle, dataRef] = imageUploads;

			vk::Image targetImage = m_LocalResourceManager.GetTexture(targetImageHandle);
			if (!targetImage) continue;

			void const* pSourceData = nullptr;
			if (dataRef.copied && dataRef.dataIndex < uploadDataHolder.m_Data.size())
				pSourceData = uploadDataHolder.GetPtr(dataRef.dataIndex);
			else if (!dataRef.copied)
				pSourceData = dataRef.pData;

			if (!pSourceData || dataRef.dataSize == 0) continue;

			auto desc = GetDescriptor(graph, targetImageHandle);

			auto stagingAlloc = stagingManager.AllocUploadStagingBuffer(dataRef.dataSize, 256);
			if (!stagingAlloc.mappedPtr) continue;

			memcpy(stagingAlloc.mappedPtr, pSourceData, dataRef.dataSize);

			vk::ImageMemoryBarrier transitionBarrier{};
			transitionBarrier.oldLayout = vk::ImageLayout::eUndefined;
			transitionBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
			transitionBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			transitionBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			transitionBarrier.image = targetImage;
			transitionBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			transitionBarrier.subresourceRange.baseMipLevel = 0;
			transitionBarrier.subresourceRange.levelCount = 1;
			transitionBarrier.subresourceRange.baseArrayLayer = 0;
			transitionBarrier.subresourceRange.layerCount = 1;
			transitionBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
			transitionBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe,
				vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &transitionBarrier);

			vk::BufferImageCopy copyRegion{};
			copyRegion.bufferOffset = stagingAlloc.offset;
			copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageOffset = vk::Offset3D{ 0, 0, 0 };
			copyRegion.imageExtent = vk::Extent3D{ desc.width, desc.height, 1 };

			cmdBuf.copyBufferToImage(stagingAlloc.buffer, targetImage,
				vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

			vk::ImageMemoryBarrier shaderReadBarrier{};
			shaderReadBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			shaderReadBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			shaderReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			shaderReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			shaderReadBarrier.image = targetImage;
			shaderReadBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			shaderReadBarrier.subresourceRange.baseMipLevel = 0;
			shaderReadBarrier.subresourceRange.levelCount = 1;
			shaderReadBarrier.subresourceRange.baseArrayLayer = 0;
			shaderReadBarrier.subresourceRange.layerCount = 1;
			shaderReadBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			shaderReadBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &shaderReadBarrier);
		}
	}

	// Task 3.4: SubmitBatches — no more waitForFences(UINT64_MAX), use window semaphores for last batch
	void VulkanGraphExecutor::SubmitBatches(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		auto queue = device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);
		auto& resourceManager = m_CurrentFrameContext->GetResourceManager();

		for (size_t i = 0; i < m_ExecutionBatches.size(); ++i)
		{
			auto& batch = m_ExecutionBatches[i];

			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &batch.directCommandBuffer;

			bool isLastBatch = (i == m_ExecutionBatches.size() - 1);

			if (isLastBatch && batch.hasFinalizePass)
			{
				uint32_t windowCount = m_CurrentFrameContext->GetWindowSyncCount();
				castl::vector<vk::Semaphore> waitSemaphores;
				castl::vector<vk::Semaphore> signalSemaphores;
				castl::vector<vk::PipelineStageFlags> waitStages;

				for (uint32_t w = 0; w < windowCount; ++w)
				{
					auto const& sync = m_CurrentFrameContext->GetWindowSync(w);
					waitSemaphores.push_back(sync.acquireSemaphore);
					signalSemaphores.push_back(sync.presentSemaphore);
					waitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
				}

				submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
				submitInfo.pWaitSemaphores = waitSemaphores.data();
				submitInfo.pWaitDstStageMask = waitStages.data();
				submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
				submitInfo.pSignalSemaphores = signalSemaphores.data();
			}

			queue.submit(submitInfo, resourceManager.GetDirectFence());
		}
	}

	void VulkanGraphExecutor::ApplyExternalResourceStates()
	{
		for (auto& pair : m_ImageLifetimes)
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
					auto texturePtr = image.GetTexturePtr<VulkanTexture>();
					texturePtr->SetResourceState(lastState.state);
					break;
				}
				case ImageHandle::ImageType::Backbuffer:
				{
					auto pWindow = image.GetWindowPtr<VulkanWindowHandle>();
					pWindow->ApplyCurrentBackBufferResourceState(lastState.state);
					break;
				}
				}
			}
		}

		for (auto& pair : m_BufferLifetimes)
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
					auto bufPtr = buffer.GetBufferPtr<VulkanBuffer>();
					bufPtr->SetResourceState(lastState.state);
					break;
				}
				}
			}
		}
	}

	// PresentWindows: now uses per-window present semaphore from FrameContext
	void VulkanGraphExecutor::PresentWindows(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		auto presentQueue = device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);

		uint32_t windowIdx = 0;
		for (auto& backBufferImage : graph.GetFinalizePass().m_PresentBackBuffers)
		{
			VulkanWindowHandle* pWindow = backBufferImage.GetWindowPtr<VulkanWindowHandle>();
			if (!pWindow || !pWindow->IsValid()) continue;

			if (pWindow->NeedsRecreation())
				pWindow->RecreateSwapchain();

			auto const& sync = m_CurrentFrameContext->GetWindowSync(windowIdx);
			pWindow->Present(presentQueue, sync.presentSemaphore);
			++windowIdx;
		}
	}

	// Task 3.6: Reset() now only clears frame-level temporary data.
	// m_Fences, m_Semaphores, m_DescriptorPool, m_PendingStagingBuffers REMOVED.
	void VulkanGraphExecutor::Reset()
	{
		m_LocalResourceManager.ReleaseAllResources();
		m_RasterPassRWStates.clear();
		m_ComputePassRWStates.clear();
		m_TransferPassRWStates.clear();
		m_ExecutionBatches.clear();
		m_ImageLifetimes.clear();
		m_BufferLifetimes.clear();
		m_CBufferLifetimes.clear();
		m_ConstantBufferManager.Clear();
		m_RasterPassGPUData.clear();
		m_ComputePassGPUData.clear();
		m_ShaderResourceInstances.clear();
		m_CurrentGraph.reset();
	}
}
