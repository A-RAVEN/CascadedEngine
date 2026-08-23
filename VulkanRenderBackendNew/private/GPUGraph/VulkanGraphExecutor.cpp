#include <GPUGraph/VulkanGraphExecutor.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/InterfaceTranslator.h>
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

			// AccessToPipelineStages: Map vk::AccessFlags → minimum vk::PipelineStageFlags
		// Used by ExecuteBarriers to compute tight stage masks instead of eAllCommands.
		// computeOnly=true for barriers recorded on the compute command buffer: a
		// compute-only queue family does not support VERTEX_SHADER/FRAGMENT_SHADER stages,
		// so shader-access flags map to COMPUTE_SHADER alone there.
		static vk::PipelineStageFlags AccessToPipelineStages(vk::AccessFlags access, bool computeOnly = false)
		{
			vk::PipelineStageFlags stages{};
			if (access & vk::AccessFlagBits::eIndirectCommandRead)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: vk::PipelineStageFlagBits::eDrawIndirect;
			if (access & (vk::AccessFlagBits::eIndexRead | vk::AccessFlagBits::eVertexAttributeRead))
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: vk::PipelineStageFlagBits::eVertexInput;
			if (access & vk::AccessFlagBits::eUniformRead)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eComputeShader
					: (vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader);
			if (access & vk::AccessFlagBits::eShaderRead)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eComputeShader
					: (vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader);
			if (access & vk::AccessFlagBits::eShaderWrite)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eComputeShader
					: (vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader);
			if (access & vk::AccessFlagBits::eColorAttachmentRead)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: vk::PipelineStageFlagBits::eColorAttachmentOutput;
			if (access & vk::AccessFlagBits::eColorAttachmentWrite)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: vk::PipelineStageFlagBits::eColorAttachmentOutput;
			if (access & vk::AccessFlagBits::eDepthStencilAttachmentRead)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: (vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests);
			if (access & vk::AccessFlagBits::eDepthStencilAttachmentWrite)
				stages |= computeOnly
					? vk::PipelineStageFlagBits::eAllCommands
					: (vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests);
			if (access & (vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite))
				stages |= vk::PipelineStageFlagBits::eTransfer;
			if (access & (vk::AccessFlagBits::eHostRead | vk::AccessFlagBits::eHostWrite))
				stages |= vk::PipelineStageFlagBits::eHost;
			if (access & vk::AccessFlagBits::eMemoryRead)
				stages |= vk::PipelineStageFlagBits::eAllCommands;
			if (access & vk::AccessFlagBits::eMemoryWrite)
				stages |= vk::PipelineStageFlagBits::eAllCommands;
			return stages;
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
		hash = cacore::hash_combine(hash, info.path.GetHash());
		for (auto const& [name, structPtr] : resourceDic)
		{
			hash = cacore::hash_combine(hash, name.GetHash());
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

	void VulkanRenderStateBarriers::ExecuteBarriers(vk::CommandBuffer cmdBuf, bool computeOnly)
	{
		if (imageBarriers.empty() && bufferBarriers.empty())
			return;

		// Collect all barriers and compute tight stage masks from access flags
		castl::vector<vk::ImageMemoryBarrier> imgBarriers;
		imgBarriers.reserve(imageBarriers.size());
		castl::vector<vk::BufferMemoryBarrier> bufBarriers;
		bufBarriers.reserve(bufferBarriers.size());

		vk::PipelineStageFlags srcStage{};
		vk::PipelineStageFlags dstStage{};

		for (auto& ib : imageBarriers)
		{
			imgBarriers.push_back(ib.barrier);
			srcStage |= AccessToPipelineStages(ib.barrier.srcAccessMask, computeOnly);
			dstStage |= AccessToPipelineStages(ib.barrier.dstAccessMask, computeOnly);
		}
		for (auto& bb : bufferBarriers)
		{
			bufBarriers.push_back(bb.barrier);
			srcStage |= AccessToPipelineStages(bb.barrier.srcAccessMask, computeOnly);
			dstStage |= AccessToPipelineStages(bb.barrier.dstAccessMask, computeOnly);
		}

		// Merge image + buffer barriers into a single vkCmdPipelineBarrier call with tight stage masks
		cmdBuf.pipelineBarrier(
			srcStage ? srcStage : vk::PipelineStageFlagBits::eAllCommands,
			dstStage ? dstStage : vk::PipelineStageFlagBits::eAllCommands,
			vk::DependencyFlags{},
			{}, bufBarriers, imgBarriers);
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

	int VulkanGraphExecutor::GetQueueFamilyIndex(EGPUQueueType queueType) const
	{
		switch (queueType)
		{
		case EGPUQueueType::eDirect:   return m_GraphicsQueueFamily;
		case EGPUQueueType::eCompute:  return m_ComputeQueueFamily;
		default:                        return static_cast<int>(vk::QueueFamilyIgnored);
		}
	}

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
		CA_LOG_INFO("VulkanGraphExecutor: CompileAndExecute entry");
		if (!graph)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Null graph");
			return;
		}

		m_CurrentFrameContext = std::move(frameContext);

		// Propagate App pointer to nested child subobjects
		GetApp()->InitSubObj(&m_LocalResourceManager);
		GetApp()->InitSubObj(&m_ConstantBufferManager);

		// Task 1.2: Cache queue family indices from QueueContext
		{
			auto const& queueContext = GetApp()->GetQueueContext();
			m_GraphicsQueueFamily = queueContext.GetGraphicsQueueFamily();
			m_ComputeQueueFamily = queueContext.GetComputeQueueFamily();
		}

		// Task 1.4: Validate compute queue availability for asyncCompute
		if (m_ComputeQueueFamily == static_cast<int>(vk::QueueFamilyIgnored))
		{
			CA_LOG_WARN("VulkanGraphExecutor: No dedicated compute queue family available; asyncCompute will be degraded to synchronous");
		}

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

		m_PrepareTime = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - prepareStart).count();

		// Phase 2: Build dependency-free batches
		BuildDependencyFreeBatches(*graph);

		// Phase 3: Build resource usage ranges
		BuildResourceUsageRanges();

		// Phase 4: Register CBuffer resources for aliasing
		RegisterCBufferForAliasing(*graph);

		// Phase 4.5: Allocate aliased resources
		if (!AllocateAliasedResources())
		{
			CA_LOG_ERR("VulkanGraphExecutor: AllocateAliasedResources failed, aborting frame");
			return;
		}

		// Swapchain acquire — moved here to avoid acquiring images on frames that will abort
		if (!graph->GetFinalizePass().isEmpty())
		{
			// D3: acquire semaphores are per WINDOW POSITION, present semaphores per
			// (window × swapchain image). EnsureWindowSync is called per valid window
			// with its swapchain image count — arrays grow on demand and cover every
			// position the acquire-wait / present loops index by window position.
			uint32_t windowIdx = 0;
			for (auto& backBufferImage : graph->GetFinalizePass().m_PresentBackBuffers)
			{
				VulkanWindowHandle* pWindow = backBufferImage.GetWindowPtr<VulkanWindowHandle>();
				if (pWindow && pWindow->IsValid())
				{
					if (pWindow->NeedsRecreation())
						pWindow->RecreateSwapchain();

					m_CurrentFrameContext->EnsureWindowSync(windowIdx + 1, pWindow->GetSwapchainImageCount());

					// Acquire semaphore is per-window (free due to per-batch CPU serialization).
					vk::Semaphore acquireSem = m_CurrentFrameContext->GetAcquireSync(windowIdx);
					pWindow->AcquireNextImage(acquireSem);
					// acquiredImageIndex now stored in pWindow->GetCurrentImageIndex()
				}
				++windowIdx;
			}
		}

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
		CA_LOG_INFO("VulkanGraphExecutor: Execute entry, batches={}", m_ExecutionBatches.size());
		Execute(*graph);
		m_ExecuteTime = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - executeStart).count();

		// Apply external resource states
		ApplyExternalResourceStates();

		// Present windows — only when there is at least one present backbuffer. A graph
		// whose finalize pass has image usages but no present backbuffer (e.g. a plain
		// Finalize(texture)) must not call PresentWindows: it indexes
		// m_PresentBackBuffers[0] unconditionally. [AUDIT] the old guard used
		// FinalizePass::isEmpty(), which is false whenever m_ImageUsages is non-empty, so
		// it did NOT protect the [0] access on an empty m_PresentBackBuffers (OOB read).
		if (!graph->GetFinalizePass().m_PresentBackBuffers.empty())
		{
			PresentWindows(*graph);
		}

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
			EGPUQueueType queueType = (computePass.asyncCompute && m_ComputeQueueFamily != static_cast<int>(vk::QueueFamilyIgnored)) ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

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
uint64_t resourceId = 				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, ETextureAccessType::eRT, static_cast<uint32_t>(passID));
				if (attachment.IsIntternal()) m_LocalResourceManager.RegisterTextureHandle(attachment, resourceId);

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
uint64_t resourceId = 						m_LocalResourceManager.RegisterTemporaryBuffer(
							descriptor, EBufferUsage::eIndexBuffer, static_cast<uint32_t>(passID));
						if (indexBuffer.IsIntternal()) m_LocalResourceManager.RegisterBufferHandle(indexBuffer, resourceId);
						passRWState.SetBufferRWState(indexBuffer,
							vk::PipelineStageFlagBits::eVertexInput,
							vk::AccessFlagBits::eIndexRead, EGPUQueueType::eDirect);
					}

					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						auto& vertBuffer = vertBuf.second;
						auto descriptor = GetDescriptor(graph, vertBuffer);
uint64_t resourceId = 						m_LocalResourceManager.RegisterTemporaryBuffer(
							descriptor, EBufferUsage::eVertexBuffer, static_cast<uint32_t>(passID));
						if (vertBuffer.IsIntternal()) m_LocalResourceManager.RegisterBufferHandle(vertBuffer, resourceId);
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
uint64_t resourceId = 				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, ETextureAccessTypeFlags{}, static_cast<uint32_t>(passID));
				if (imgWrites.first.IsIntternal()) m_LocalResourceManager.RegisterTextureHandle(imgWrites.first, resourceId);
				// F35: the tracked layout must match what RecordTransferPass actually leaves
				// the image in — its final inline transition ends at
				// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (not TRANSFER_DST_OPTIMAL).
				passRWState.SetImageRWState(imgWrites.first,
					vk::PipelineStageFlagBits::eTransfer,
					vk::AccessFlagBits::eTransferWrite,
					vk::ImageLayout::eShaderReadOnlyOptimal, EGPUQueueType::eDirect);
			}
		}

		if (!graph.GetFinalizePass().isEmpty())
		{
			for (auto& img : graph.GetFinalizePass().m_ImageUsages)
			{
				auto descriptor = GetDescriptor(graph, img.first);
uint64_t resourceId = 				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, img.second, static_cast<uint32_t>(m_ExecutionBatches.size()));
				if (img.first.IsIntternal()) m_LocalResourceManager.RegisterTextureHandle(img.first, resourceId);
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

				auto pipelineData = PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), batch.pipelineStateDesc);
				ShaderInfo const& shaderInfo = pipelineData.m_ShaderInfo;
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
			EGPUQueueType queueType = (computePass.asyncCompute && m_ComputeQueueFamily != static_cast<int>(vk::QueueFamilyIgnored)) ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

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
							graph.GetComputePasses()[dep->passID].asyncCompute && m_ComputeQueueFamily != static_cast<int>(vk::QueueFamilyIgnored);
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

	// Task 8.1: Map ETextureFormat to vk::ImageAspectFlags for barrier subresourceRange
	static vk::ImageAspectFlags GetImageAspectMask(ImageHandle const& image, GPUGraph const& graph)
	{
		GPUTextureDescriptor desc = GetDescriptor(graph, image);
		switch (desc.format)
		{
		case ETextureFormat::E_D24_UNORM_S8_UINT:
		case ETextureFormat::E_D32_SFLOAT_S8_UINT:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		case ETextureFormat::E_D32_SFLOAT:
		case ETextureFormat::E_D16_UNORM:
			return vk::ImageAspectFlagBits::eDepth;
		default:
			return vk::ImageAspectFlagBits::eColor;
		}
	}

	bool VulkanGraphExecutor::AllocateAliasedResources()
	{
		return m_LocalResourceManager.AllocateAliasedResources();
	}

	void VulkanGraphExecutor::PrepareBatchResourceBarriers(GPUGraph const& graph)
	{
		for (auto& pair : m_ImageLifetimes)
		{
			ImageHandle const& image = pair.first;
			VulkanResourceState cachedState = VulkanResourceState::InitializedImageState();

			// Task 2.2: Read back cachedState from the resource object (align with D3D12)
			switch (image.GetType())
			{
			case ImageHandle::ImageType::External:
				cachedState = image.GetTexturePtr<VulkanTexture>()->GetResourceState();
				break;
			case ImageHandle::ImageType::Backbuffer:
				cachedState = image.GetWindowPtr<VulkanWindowHandle>()->GetCurrentBackBufferResourceState();
				break;
			case ImageHandle::ImageType::Internal:
				break;
			}

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

				bool qfotNeeded = (lastState.queueType != currentState.queueType);

				// Task 3.5 + Task 12.1: Cross-frame QFOT -- only acquire, no release. Skip Internal
				if (qfotNeeded && isFirstState && image.GetType() != ImageHandle::ImageType::Internal)
				{
					int srcFamily = GetQueueFamilyIndex(lastState.queueType);
					int dstFamily = GetQueueFamilyIndex(currentState.queueType);

					auto& dstBatch = m_ExecutionBatches[currentBatchID];
					VulkanRenderStateBarriers& acquireContainer =
						(currentState.queueType == EGPUQueueType::eCompute)
						? dstBatch.computeAquireBarriers : dstBatch.aquireBarriers;

					vk::ImageMemoryBarrier acquireBarrier{};
					acquireBarrier.srcAccessMask = {};
					acquireBarrier.dstAccessMask = currentState.accessFlags;
					acquireBarrier.oldLayout = lastState.imageLayout;
					acquireBarrier.newLayout = currentState.imageLayout;
					acquireBarrier.srcQueueFamilyIndex = srcFamily;
					acquireBarrier.dstQueueFamilyIndex = dstFamily;
					acquireBarrier.image = m_LocalResourceManager.GetTexture(image);
					if (!acquireBarrier.image) continue;
					acquireBarrier.subresourceRange.aspectMask = GetImageAspectMask(image, graph);
					acquireBarrier.subresourceRange.baseMipLevel = 0;
					acquireBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
					acquireBarrier.subresourceRange.baseArrayLayer = 0;
					acquireBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
					acquireContainer.AddImageBarrier(image, acquireBarrier);
				}

				// QFOT release+acquire for non-first-state cross-queue transitions
				if (qfotNeeded && !isFirstState)
				{
					int srcFamily = GetQueueFamilyIndex(lastState.queueType);
					int dstFamily = GetQueueFamilyIndex(currentState.queueType);

					// Release on src queue family
					{
						auto& srcBatch = m_ExecutionBatches[lastBatchID];
						VulkanRenderStateBarriers& releaseContainer =
							(lastState.queueType == EGPUQueueType::eCompute)
							? srcBatch.computeReleaseBarriers : srcBatch.releaseBarriers;

						vk::ImageMemoryBarrier releaseBarrier{};
						releaseBarrier.srcAccessMask = lastState.accessFlags;
						releaseBarrier.dstAccessMask = {};
						releaseBarrier.oldLayout = lastState.imageLayout;
						releaseBarrier.newLayout = lastState.imageLayout;
						releaseBarrier.srcQueueFamilyIndex = srcFamily;
						releaseBarrier.dstQueueFamilyIndex = dstFamily;
						releaseBarrier.image = m_LocalResourceManager.GetTexture(image);
						if (!releaseBarrier.image) continue;
						releaseBarrier.subresourceRange.aspectMask = GetImageAspectMask(image, graph);
						releaseBarrier.subresourceRange.baseMipLevel = 0;
						releaseBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
						releaseBarrier.subresourceRange.baseArrayLayer = 0;
						releaseBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
						releaseContainer.AddImageBarrier(image, releaseBarrier);
					}

					// Acquire on dst queue family
					{
						auto& dstBatch = m_ExecutionBatches[currentBatchID];
						VulkanRenderStateBarriers& acquireContainer =
							(currentState.queueType == EGPUQueueType::eCompute)
							? dstBatch.computeAquireBarriers : dstBatch.aquireBarriers;

						vk::ImageMemoryBarrier acquireBarrier{};
						acquireBarrier.srcAccessMask = {};
						acquireBarrier.dstAccessMask = currentState.accessFlags;
						acquireBarrier.oldLayout = lastState.imageLayout;
						acquireBarrier.newLayout = currentState.imageLayout;
						acquireBarrier.srcQueueFamilyIndex = srcFamily;
						acquireBarrier.dstQueueFamilyIndex = dstFamily;
						acquireBarrier.image = m_LocalResourceManager.GetTexture(image);
						if (!acquireBarrier.image) continue;
						acquireBarrier.subresourceRange.aspectMask = GetImageAspectMask(image, graph);
						acquireBarrier.subresourceRange.baseMipLevel = 0;
						acquireBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
						acquireBarrier.subresourceRange.baseArrayLayer = 0;
						acquireBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
						acquireContainer.AddImageBarrier(image, acquireBarrier);
					}
				}

				// Task 11.3: Skip duplicate regular barrier if QFOT acquire already emitted; route by queueType
				bool const qfotAcquireEmitted = qfotNeeded && (!isFirstState || image.GetType() != ImageHandle::ImageType::Internal);
				if (qfotAcquireEmitted)
					continue;

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
				if (!barrier.image) continue;
				barrier.subresourceRange.aspectMask = GetImageAspectMask(image, graph);
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

				// Task 11.1/11.4: Route regular barrier by queueType
				if (stateHaveGap)
				{
					// Same-queue gap: only need acquire barrier, skip redundant release
					VulkanRenderStateBarriers& acquireContainer = (currentState.queueType == EGPUQueueType::eCompute)
						? currentBatch.computeAquireBarriers : currentBatch.aquireBarriers;
					acquireContainer.AddImageBarrier(image, barrier);
				}
				else
				{
					VulkanRenderStateBarriers& acquireContainer = (currentState.queueType == EGPUQueueType::eCompute)
						? currentBatch.computeAquireBarriers : currentBatch.aquireBarriers;
					acquireContainer.AddImageBarrier(image, barrier);
				}
			}
		}

		for (auto& pair : m_BufferLifetimes)
		{
			BufferHandle const& buffer = pair.first;
			VulkanResourceUsageRangeData const& usageRanges = pair.second;
			VulkanResourceState cachedState = VulkanResourceState::InitializedBufferState();

			// Task 2.2: Read back cachedState for External buffers
			if (buffer.GetType() == BufferHandle::BufferType::External)
				cachedState = buffer.GetBufferPtr<VulkanBuffer>()->GetResourceState();

			for (int bid = 0; bid < usageRanges.states.size(); ++bid)
			{
				auto& batchAndState = usageRanges.states[bid];
				int currentBatchID = batchAndState.batchID;
				VulkanResourceState const& currentState = batchAndState.state;

				bool isFirstState = bid == 0;
				int lastBatchID = isFirstState ? -1 : usageRanges.states[bid - 1].batchID;
				bool stateHaveGap = isFirstState ? false : ((currentBatchID - lastBatchID) > 1);
				VulkanResourceState const& lastState = isFirstState ? cachedState : usageRanges.states[bid - 1].state;

				bool qfotNeeded = (lastState.queueType != currentState.queueType);

				// Task 3.5 + Task 12.2: Cross-frame QFOT for buffers -- only acquire, no release. Skip non-External
				if (qfotNeeded && isFirstState && buffer.GetType() == BufferHandle::BufferType::External)
				{
					int srcFamily = GetQueueFamilyIndex(lastState.queueType);
					int dstFamily = GetQueueFamilyIndex(currentState.queueType);

					auto& dstBatch = m_ExecutionBatches[currentBatchID];
					VulkanRenderStateBarriers& acquireContainer =
						(currentState.queueType == EGPUQueueType::eCompute)
						? dstBatch.computeAquireBarriers : dstBatch.aquireBarriers;

					vk::BufferMemoryBarrier acquireBarrier{};
					acquireBarrier.srcAccessMask = {};
					acquireBarrier.dstAccessMask = currentState.accessFlags;
					acquireBarrier.srcQueueFamilyIndex = srcFamily;
					acquireBarrier.dstQueueFamilyIndex = dstFamily;
					acquireBarrier.buffer = m_LocalResourceManager.GetBuffer(buffer);
					if (!acquireBarrier.buffer) continue;
					acquireBarrier.offset = 0;
					acquireBarrier.size = VK_WHOLE_SIZE;
					acquireContainer.AddBufferBarrier(buffer, acquireBarrier);
				}

				// QFOT release+acquire for non-first-state cross-queue transitions
				if (qfotNeeded && !isFirstState)
				{
					int srcFamily = GetQueueFamilyIndex(lastState.queueType);
					int dstFamily = GetQueueFamilyIndex(currentState.queueType);

					// Release on src queue family
					{
						auto& srcBatch = m_ExecutionBatches[lastBatchID];
						VulkanRenderStateBarriers& releaseContainer =
							(lastState.queueType == EGPUQueueType::eCompute)
							? srcBatch.computeReleaseBarriers : srcBatch.releaseBarriers;

						vk::BufferMemoryBarrier releaseBarrier{};
						releaseBarrier.srcAccessMask = lastState.accessFlags;
						releaseBarrier.dstAccessMask = {};
						releaseBarrier.srcQueueFamilyIndex = srcFamily;
						releaseBarrier.dstQueueFamilyIndex = dstFamily;
						releaseBarrier.buffer = m_LocalResourceManager.GetBuffer(buffer);
						if (!releaseBarrier.buffer) continue;
						releaseBarrier.offset = 0;
						releaseBarrier.size = VK_WHOLE_SIZE;
						releaseContainer.AddBufferBarrier(buffer, releaseBarrier);
					}

					// Acquire on dst queue family
					{
						auto& dstBatch = m_ExecutionBatches[currentBatchID];
						VulkanRenderStateBarriers& acquireContainer =
							(currentState.queueType == EGPUQueueType::eCompute)
							? dstBatch.computeAquireBarriers : dstBatch.aquireBarriers;

						vk::BufferMemoryBarrier acquireBarrier{};
						acquireBarrier.srcAccessMask = {};
						acquireBarrier.dstAccessMask = currentState.accessFlags;
						acquireBarrier.srcQueueFamilyIndex = srcFamily;
						acquireBarrier.dstQueueFamilyIndex = dstFamily;
						acquireBarrier.buffer = m_LocalResourceManager.GetBuffer(buffer);
						if (!acquireBarrier.buffer) continue;
						acquireBarrier.offset = 0;
						acquireBarrier.size = VK_WHOLE_SIZE;
						acquireContainer.AddBufferBarrier(buffer, acquireBarrier);
					}
				}

				// Task 11.3: Skip duplicate regular barrier if QFOT acquire already emitted
				if (qfotNeeded && (!isFirstState || buffer.GetType() == BufferHandle::BufferType::External))
					continue;

				if (lastState.accessFlags == currentState.accessFlags)
					continue;

				auto& currentBatch = m_ExecutionBatches[currentBatchID];

				vk::BufferMemoryBarrier barrier{};
				barrier.srcAccessMask = lastState.accessFlags;
				barrier.dstAccessMask = currentState.accessFlags;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = m_LocalResourceManager.GetBuffer(buffer);
				if (!barrier.buffer) continue;
				barrier.offset = 0;
				barrier.size = VK_WHOLE_SIZE;

				// Task 11.2/11.4: Route regular barrier by queueType
				if (stateHaveGap)
				{
					// Same-queue gap: only need acquire barrier, skip redundant release
					VulkanRenderStateBarriers& acquireContainer = (currentState.queueType == EGPUQueueType::eCompute)
						? currentBatch.computeAquireBarriers : currentBatch.aquireBarriers;
					acquireContainer.AddBufferBarrier(buffer, barrier);
				}
				else
				{
					VulkanRenderStateBarriers& acquireContainer = (currentState.queueType == EGPUQueueType::eCompute)
						? currentBatch.computeAquireBarriers : currentBatch.aquireBarriers;
					acquireContainer.AddBufferBarrier(buffer, barrier);
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
		auto pipelineCache = pApp->GetPipelineCache();

		// Helper to map StencilStates to VkStencilOpState
		auto FillVkStencilOpState = [](DepthStencilStates::StencilStates const& src) -> vk::StencilOpState
		{
			vk::StencilOpState result{};
			result.failOp = EStencilOpToVkStencilOp(src.failOp);
			result.passOp = EStencilOpToVkStencilOp(src.passOp);
			result.depthFailOp = EStencilOpToVkStencilOp(src.depthFailOp);
			result.compareOp = ECompareOpToVkCompareOp(src.compareOp);
			result.compareMask = src.compareMask;
			result.writeMask = src.writeMask;
			result.reference = src.reference;
			return result;
		};

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

				auto pipelineData = PipelineDescData::CombindDescData(rasterPass.GetPipelineStates(), batch.pipelineStateDesc);
				ShaderInfo const& shaderInfo = pipelineData.m_ShaderInfo;
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

				if (!hasVertex || !hasFragment)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Shader missing vertex or fragment entry point, skipping pipeline creation");
					continue;
				}

				vk::ShaderModule vertexShaderModule = pApp->GetOrCreateShaderModule(vertexProgramHash);
				vk::ShaderModule fragmentShaderModule = pApp->GetOrCreateShaderModule(fragmentProgramHash);
				if (!vertexShaderModule || !fragmentShaderModule)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Failed to create shader module(s), skipping pipeline creation");
					continue;
				}

				vk::PipelineLayout pipelineLayout = pApp->GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);
				if (!pipelineLayout)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Failed to create pipeline layout, skipping pipeline creation");
					continue;
				}

				vk::PipelineShaderStageCreateInfo vertexShaderStage{};
				vertexShaderStage.stage = vk::ShaderStageFlagBits::eVertex;
				vertexShaderStage.module = vertexShaderModule;
				vertexShaderStage.pName = vertexEntryPointName.c_str();

				vk::PipelineShaderStageCreateInfo fragmentShaderStage{};
				fragmentShaderStage.stage = vk::ShaderStageFlagBits::eFragment;
				fragmentShaderStage.module = fragmentShaderModule;
				fragmentShaderStage.pName = fragmentEntryPointName.c_str();

				auto const& inputAssemblyData = pipelineData.m_InputAssemblyStates.Get();
				auto const& pipelineStateData = pipelineData.m_PipelineStates.Get();

				// ===== Vertex Input State (deterministic single-pass algorithm) =====
				vk::PipelineVertexInputStateCreateInfo vertexInputState{};
				castl::vector<vk::VertexInputBindingDescription> vertexBindings;
				castl::vector<vk::VertexInputAttributeDescription> vertexAttributes;
				{
					// D3D12-style: scan all streams for semantic match, binding index by stream name
					castl::vector<cacore::NameHash> seenStreamKeys;

					if (!pFileInfo->reflectionData.m_VertexAttributes.empty())
					{
						if (batch.m_VertexInputDescs.empty())
						{
							CA_LOG_WARN("VulkanGraphExecutor: Shader has {} vertex attributes but batch has no vertex input descriptors",
								pFileInfo->reflectionData.m_VertexAttributes.size());
						}

						for (auto const& reflAttr : pFileInfo->reflectionData.m_VertexAttributes)
						{
							cacore::NameHash semanticNameHash(reflAttr.m_SematicName);

							// Scan all registered streams for matching semanticName + sematicIndex
							bool found = false;
							for (auto const& streamPair : batch.m_VertexInputDescs)
							{
								auto const& slotDesc = streamPair.second.Get();
								for (auto const& attr : slotDesc.attributes)
								{
									if (attr.semanticName == semanticNameHash && attr.sematicIndex == reflAttr.m_SematicIndex)
									{
										// Find or create binding index by stream name (map key)
										cacore::NameHash const& streamName = streamPair.first;
										int bindingIdx = -1;
										for (size_t k = 0; k < seenStreamKeys.size(); ++k)
										{
											if (seenStreamKeys[k] == streamName)
											{
												bindingIdx = static_cast<int>(k);
												break;
											}
										}
										if (bindingIdx < 0)
										{
											bindingIdx = static_cast<int>(seenStreamKeys.size());
											seenStreamKeys.push_back(streamName);

											vk::VertexInputBindingDescription binding{};
											binding.binding = static_cast<uint32_t>(bindingIdx);
											binding.stride = slotDesc.stride;
											binding.inputRate = slotDesc.perInstance
												? vk::VertexInputRate::eInstance
												: vk::VertexInputRate::eVertex;
											vertexBindings.push_back(binding);
										}

										vk::VertexInputAttributeDescription vkAttr{};
										vkAttr.location = reflAttr.m_Location;
										vkAttr.binding = static_cast<uint32_t>(bindingIdx);
										vkAttr.format = EVertexInputFormatToVkFormat(attr.format);
										vkAttr.offset = attr.offset;
										vertexAttributes.push_back(vkAttr);
										found = true;
										break;
									}
								}
								if (found) break;
							}
							if (!found)
							{
								CA_LOG_WARN("VulkanGraphExecutor: No vertex input found for shader attribute {}[{}]",
									reflAttr.m_SematicName, reflAttr.m_SematicIndex);
							}
						}
					}

					vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
					vertexInputState.pVertexBindingDescriptions = vertexBindings.data();
					vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
					vertexInputState.pVertexAttributeDescriptions = vertexAttributes.data();

					// F37: publish the binding order so RecordRenderPass binds vertex buffers
					// in the same sequence (indices must match the pipeline's bindings).
					batchData.vertexBindingStreamOrder = seenStreamKeys;
				}

				vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
				inputAssemblyState.topology = ETopologyToVkTopology(inputAssemblyData.topology);

				vk::PipelineViewportStateCreateInfo viewportState{};
				viewportState.viewportCount = 1;
				viewportState.scissorCount = 1;

				// ===== Dynamic State (VK_DYNAMIC_STATE_VIEWPORT + VK_DYNAMIC_STATE_SCISSOR) =====
				vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
				vk::PipelineDynamicStateCreateInfo dynamicState{};
				dynamicState.dynamicStateCount = 2;
				dynamicState.pDynamicStates = dynamicStates;

				vk::PipelineRasterizationStateCreateInfo rasterizationState{};
				rasterizationState.polygonMode = EPolygonModeToVkPolygonMode(pipelineStateData.rasterizationStates.polygonMode);
				rasterizationState.cullMode = ECullModeToVkCullModeFlags(pipelineStateData.rasterizationStates.cullMode);
				rasterizationState.frontFace = EFrontFaceToVkFrontFace(pipelineStateData.rasterizationStates.frontFace);
				rasterizationState.lineWidth = 1.0f;

				vk::PipelineMultisampleStateCreateInfo multisampleState{};
				multisampleState.rasterizationSamples = VulkanTexture::ConvertSampleCount(pipelineStateData.msCount);

				// ===== Depth-Stencil State (shared between GPL and Monolithic) =====
				vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
				{
					auto const& ds = pipelineStateData.depthStencilStates;
					depthStencilState.depthTestEnable = ds.depthTestEnable ? VK_TRUE : VK_FALSE;
					depthStencilState.depthWriteEnable = ds.depthWriteEnable ? VK_TRUE : VK_FALSE;
					depthStencilState.depthCompareOp = ECompareOpToVkCompareOp(ds.depthCompareOp);
					depthStencilState.stencilTestEnable = ds.stencilTestEnable ? VK_TRUE : VK_FALSE;
					depthStencilState.front = FillVkStencilOpState(ds.stencilStateFront);
					depthStencilState.back = FillVkStencilOpState(ds.stencilStateBack);
				}

				// ===== RenderPass (shared between GPL and Monolithic) =====
				RenderBackend_Vulkan::RenderPassCacheKey rpKey{};
				{
					auto const& rpAttachments = rasterPass.GetAttachments();
					for (size_t i = 0; i < rpAttachments.size(); ++i)
					{
						auto desc = GetDescriptor(graph, rpAttachments[i]);
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
				}
				vk::RenderPass renderPass = GetApp()->GetOrCreateRenderPass(rpKey);
				if (!renderPass)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Failed to create render pass, skipping pipeline creation");
					continue;
				}

				// ===== MRT Blend State =====
				uint32_t attachmentCount = rasterPass.GetColorAttachmentCount();
				castl::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
				blendAttachments.resize(attachmentCount);
				for (uint32_t i = 0; i < attachmentCount; ++i)
				{
					auto const& src = pipelineStateData.colorAttachments.attachmentBlendStates[i];
					auto& dst = blendAttachments[i];
					dst.blendEnable = src.blendEnable ? VK_TRUE : VK_FALSE;
					dst.srcColorBlendFactor = EBlendFactorToVkBlendFactor(src.sourceColorBlendFactor);
					dst.dstColorBlendFactor = EBlendFactorToVkBlendFactor(src.destColorBlendFactor);
					dst.srcAlphaBlendFactor = EBlendFactorToVkBlendFactor(src.sourceAlphaBlendFactor);
					dst.dstAlphaBlendFactor = EBlendFactorToVkBlendFactor(src.destAlphaBlendFactor);
					dst.colorBlendOp = EBlendOpToVkBlendOp(src.colorBlendOp);
					dst.alphaBlendOp = EBlendOpToVkBlendOp(src.alphaBlendOp);
					dst.colorWriteMask = EColorChannelMaskToVkColorComponentFlags(src.channelMask);
				}

				vk::PipelineColorBlendStateCreateInfo colorBlendState{};
				colorBlendState.logicOpEnable = VK_FALSE;
				colorBlendState.logicOp = vk::LogicOp::eClear;
				colorBlendState.attachmentCount = attachmentCount;
				colorBlendState.pAttachments = blendAttachments.data();

				// Fill RenderStateCombination for cache lookup
				RenderStateCombination stateCombo;
				stateCombo.vertexBindings = vertexBindings;
				stateCombo.vertexAttributes = vertexAttributes;
				stateCombo.topology = inputAssemblyState.topology;
				stateCombo.blendAttachments = blendAttachments;
				stateCombo.depthFormat = rpKey.depthFormat;
				stateCombo.colorFormats = rpKey.colorFormats;
				stateCombo.sampleCount = multisampleState.rasterizationSamples;
				stateCombo.depthTestEnable = pipelineStateData.depthStencilStates.depthTestEnable;
				stateCombo.depthWriteEnable = pipelineStateData.depthStencilStates.depthWriteEnable;
				stateCombo.stencilTestEnable = pipelineStateData.depthStencilStates.stencilTestEnable;
				stateCombo.depthCompareOp = depthStencilState.depthCompareOp;
				stateCombo.stencilFront = depthStencilState.front;
				stateCombo.stencilBack = depthStencilState.back;
				stateCombo.primitiveRestartEnable = inputAssemblyData.topology == ETopology::eTriangleStrip ||
					inputAssemblyData.topology == ETopology::eLineStrip;
				stateCombo.vertexShaderHash = vertexProgramHash;
				if (hasFragment)
					stateCombo.fragmentShaderHash = fragmentProgramHash;

				size_t cacheKey = pipelineLibraryCache.GenerateHashKey(stateCombo);
				batchData.pipeline = pipelineLibraryCache.TryGetCachedPipeline(cacheKey);

				if (!batchData.pipeline)
				{
					if (pipelineLibrary.IsSupported())
					{
						// ===== GPL Path =====
						auto vertexInputLib = pipelineLibrary.CreateVertexInputLibrary(
							vertexInputState, inputAssemblyState, pipelineCache);
						auto preRasterLib = pipelineLibrary.CreatePreRasterizationLibrary(
							vertexShaderStage, nullptr, nullptr, nullptr,
							pipelineLayout,
								viewportState, rasterizationState, &dynamicState, pipelineCache);
						auto fragmentLib = pipelineLibrary.CreateFragmentLibrary(
							fragmentShaderStage, pipelineLayout, renderPass, &depthStencilState, pipelineCache);
						auto fragmentOutputLib = pipelineLibrary.CreateFragmentOutputLibrary(
							multisampleState, &depthStencilState, colorBlendState, renderPass, pipelineCache);

						PipelineLibraryParts parts{};
						parts.vertexInputLibrary = vertexInputLib;
						parts.preRasterizationLibrary = preRasterLib;
						parts.fragmentLibrary = fragmentLib;
						parts.fragmentOutputLibrary = fragmentOutputLib;
						parts.isComplete = true;

						batchData.pipeline = pipelineLibrary.LinkPipeline(
							parts, pipelineLayout, renderPass, 0, pipelineCache);

						if (!batchData.pipeline)
						{
							// F25: do NOT destroy the library parts here — every Create*Library
							// pushes its handle into VulkanPipelineLibrary::m_CreatedPipelines,
							// which owns destruction at Release(). Destroying them here would
							// double-destroy the same handles later.
							CA_LOG_ERR("BuildPipelineStates: GPL LinkPipeline failed; library parts stay owned by "
								"VulkanPipelineLibrary::Release");
						}
					}
					else
					{
						// ===== Monolithic Path =====
						vk::PipelineShaderStageCreateInfo stages[] = { vertexShaderStage, fragmentShaderStage };

						vk::GraphicsPipelineCreateInfo createInfo{};
						createInfo.stageCount = 2;
						createInfo.pStages = stages;
						createInfo.pVertexInputState = &vertexInputState;
						createInfo.pInputAssemblyState = &inputAssemblyState;
						createInfo.pViewportState = &viewportState;
						createInfo.pDynamicState = &dynamicState;
						createInfo.pRasterizationState = &rasterizationState;
						createInfo.pMultisampleState = &multisampleState;
						createInfo.pDepthStencilState = &depthStencilState;
						createInfo.pColorBlendState = &colorBlendState;
						createInfo.layout = pipelineLayout;
						createInfo.renderPass = renderPass;

						batchData.pipeline = pipelineLibrary.CreateMonolithicPipeline(
							createInfo, pipelineCache);
					}

					if (batchData.pipeline)
						pipelineLibraryCache.CachePipeline(cacheKey, batchData.pipeline);
				}

				batchData.pipelineLayout = pipelineLayout;
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
				if (!computeShaderModule)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Failed to create compute shader module, skipping pipeline creation");
					continue;
				}
				vk::PipelineLayout pipelineLayout = pApp->GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);
				if (!pipelineLayout)
				{
					CA_LOG_WARN("VulkanGraphExecutor: Failed to create compute pipeline layout, skipping pipeline creation");
					continue;
				}

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
					auto result = device.createComputePipeline(pipelineCache, createInfo);
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
		CA_LOG_INFO("VulkanGraphExecutor: RecordBatchCommands entry");
		auto& resourceManager = m_CurrentFrameContext->GetResourceManager();
		auto& cmdListManager = resourceManager.GetCommandListManager();
		auto device = GetDevice();
		auto& stagingManager = resourceManager.GetStagingMemoryManager();

		auto uploadCBufferBarriers = [&](VulkanCBufferInitializeBarriers& cbufferBarriers, vk::CommandBuffer targetCmdBuf, bool isComputeCmdBuf)
		{
			if (!cbufferBarriers.AnyBarrier()) return;

			// F33: on the compute command buffer the dst stage must not include
			// VERTEX_SHADER/FRAGMENT_SHADER (compute-only queue families don't support
			// graphics stages) — COMPUTE_SHADER covers the uniform read there.
			vk::PipelineStageFlags dstStages = isComputeCmdBuf
				? vk::PipelineStageFlagBits::eComputeShader
				: (vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader);

			for (auto& [resourceId, pStruct] : cbufferBarriers.cbufferData)
			{
				if (!pStruct) continue;

				vk::Buffer gpuBuffer = m_LocalResourceManager.GetBuffer(resourceId);
				if (!gpuBuffer) continue;

				uint64_t bufferSize = pStruct->GetCBufferSize();
				if (bufferSize == 0) continue;

				auto stagingAlloc = stagingManager.AllocUploadStagingBuffer(bufferSize, 256);
				if (!stagingAlloc.mappedPtr) { CA_LOG_ERR("RecordBatchCommands: AllocUploadStagingBuffer failed for CBuffer"); continue; }

				pStruct->ComputeMaxChildrenVersion();
				pStruct->UpdateUniformBuffer(0, stagingAlloc.mappedPtr, static_cast<uint32_t>(bufferSize), 0);

				vk::BufferCopy copyRegion{};
				copyRegion.srcOffset = stagingAlloc.offset;
				copyRegion.dstOffset = 0;
				copyRegion.size = bufferSize;
				targetCmdBuf.copyBuffer(stagingAlloc.buffer, gpuBuffer, 1, &copyRegion);

				vk::BufferMemoryBarrier barrier{};
				barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = gpuBuffer;
				barrier.offset = 0;
				barrier.size = bufferSize;

				targetCmdBuf.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					dstStages,
					vk::DependencyFlags{},
					{}, barrier, {});
			}
		};

		// Task 4.1: Determine if compute command buffer is needed
		bool needsComputeCmdBuf = batch.computeAquireBarriers.AnyBarrier()
			|| batch.computeReleaseBarriers.AnyBarrier()
			|| batch.anyComputeQueueOperations
			|| batch.computeCBufferBarriers.AnyBarrier();

		if (needsComputeCmdBuf)
		{
			batch.computeCommandBuffer = cmdListManager.ComputeCommand();

			vk::CommandBufferBeginInfo cBeginInfo{};
			cBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
			batch.computeCommandBuffer.begin(cBeginInfo);

			// Task 4.2: computeAquireBarriers first on compute queue
			if (batch.computeAquireBarriers.AnyBarrier())
				batch.computeAquireBarriers.ExecuteBarriers(batch.computeCommandBuffer, true);

			// Upload compute CBuffer data on compute command buffer
			uploadCBufferBarriers(batch.computeCBufferBarriers, batch.computeCommandBuffer, true);
		}

		// === Direct command buffer ===
		batch.directCommandBuffer = cmdListManager.GraphicsCommand();

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		batch.directCommandBuffer.begin(beginInfo);

		if (batch.aquireBarriers.AnyBarrier())
			batch.aquireBarriers.ExecuteBarriers(batch.directCommandBuffer);


		// Upload direct CBuffer data on direct command buffer
		uploadCBufferBarriers(batch.cbufferBarriers, batch.directCommandBuffer, false);


		// Task 4.4: If no compute cmd buf, upload compute CBuffer data on direct (fallback)
		if (!needsComputeCmdBuf)
			uploadCBufferBarriers(batch.computeCBufferBarriers, batch.directCommandBuffer, false);


		for (int rasterPassID : batch.rasterPassRefs)
			RecordRenderPass(batch, rasterPassID, graph);


		// RecordComputePass routes to computeCommandBuffer when available
		for (int computePassID : batch.computePassRefs)
			RecordComputePass(batch, computePassID, graph, graph.GetComputePasses()[computePassID].asyncCompute && m_ComputeQueueFamily != static_cast<int>(vk::QueueFamilyIgnored));

		for (int transferPassID : batch.transferPassRefs)
			RecordTransferPass(batch, transferPassID, graph);

		if (batch.releaseBarriers.AnyBarrier())
			batch.releaseBarriers.ExecuteBarriers(batch.directCommandBuffer);

		batch.directCommandBuffer.end();

		// === Finalize compute command buffer ===
		if (needsComputeCmdBuf)
		{
			// Task 4.2: computeReleaseBarriers last on compute queue
			if (batch.computeReleaseBarriers.AnyBarrier())
				batch.computeReleaseBarriers.ExecuteBarriers(batch.computeCommandBuffer, true);

			batch.computeCommandBuffer.end();
		}
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

		vk::Viewport viewport{ 0.0f, static_cast<float>(firstAttachmentDesc.height), static_cast<float>(firstAttachmentDesc.width),
			-static_cast<float>(firstAttachmentDesc.height), 0.0f, 1.0f };
		vk::Rect2D scissor{ {0, 0}, {firstAttachmentDesc.width, firstAttachmentDesc.height} };
		cmdBuf.setViewport(0, viewport);
		cmdBuf.setScissor(0, scissor);

		// Build render pass cache key
		RenderBackend_Vulkan::RenderPassCacheKey rpKey{};
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
		attachmentViews.reserve(attachments.size());
		for (auto& attachment : attachments)
		{
			vk::ImageView view = m_LocalResourceManager.GetTextureView(attachment);
			if (!view)
			{
				CA_LOG_WARN("RecordRenderPass: Skipping render pass due to null attachment ImageView");
				return;
			}
			attachmentViews.push_back(view);
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

			if (!batchData.pipeline) continue;
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
					// F37/F37a: bind each stream at its EXPLICIT pipeline binding index
					// (= position in batchData.vertexBindingStreamOrder, which matches
					// BuildPipelineStates' seenStreamKeys). Binding a compacted list at
					// firstBinding=0 would misalign indices when a drawcall lacks some
					// streams from the order.
					auto const& order = batchData.vertexBindingStreamOrder;
					for (size_t oi = 0; oi < order.size(); ++oi)
					{
						auto it = vertBuffers.find(order[oi]);
						if (it == vertBuffers.end()) continue;
						vk::Buffer buffer = m_LocalResourceManager.GetBuffer(it->second);
						if (!buffer) continue;
						vk::DeviceSize zero = 0;
						cmdBuf.bindVertexBuffers(static_cast<uint32_t>(oi), buffer, zero);
					}
					// Fallback: streams registered after pipeline build (absent from the
					// order) append after the ordered slots, in map order.
					uint32_t fallbackBinding = static_cast<uint32_t>(order.size());
					for (auto const& [name, bufferHandle] : vertBuffers)
					{
						bool alreadyBound = false;
						for (auto const& streamName : order)
						{
							if (streamName == name) { alreadyBound = true; break; }
						}
						if (alreadyBound) continue;
						vk::Buffer buffer = m_LocalResourceManager.GetBuffer(bufferHandle);
						if (!buffer) continue;
						vk::DeviceSize zero = 0;
						cmdBuf.bindVertexBuffers(fallbackBinding, buffer, zero);
						++fallbackBinding;
					}
				}

				auto const& vp = drawcall.GetViewPort();
				if (vp.Valid())
				{
					vk::Viewport dynViewport{ static_cast<float>(vp->x), static_cast<float>(vp->y + vp->height),
						static_cast<float>(vp->width), -static_cast<float>(vp->height), 0.0f, 1.0f };
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
		uint32_t computePassID, GPUGraph const& graph, bool asyncCompute)
	{
		auto& computePass = graph.GetComputePasses()[computePassID];
		auto& computeData = m_ComputePassGPUData[computePassID];

		// Task 7.2: Per-pass routing -- asyncCompute && computeCmdBuf -> compute, else -> direct
		vk::CommandBuffer targetCmdBuf = (asyncCompute && batch.computeCommandBuffer)
			? batch.computeCommandBuffer : batch.directCommandBuffer;

		for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
		{
			auto& dispatch = computePass.dispatchs[dispatchID];
			auto& dispatchData = computeData.dispatchs[dispatchID];

			if (!dispatchData.pipeline) continue;
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

		// Only inject memory barrier if compute pass actually writes to UAV/storage resources
		bool hasUAVWrite = false;
		if (computePassID < m_ComputePassRWStates.size())
		{
			auto& rwState = m_ComputePassRWStates[computePassID];
			for (auto& [handle, state] : rwState.imageRWStates)
			{
				if (state.Write()) { hasUAVWrite = true; break; }
			}
			if (!hasUAVWrite)
			{
				for (auto& [handle, state] : rwState.bufferRWStates)
				{
					if (state.Write()) { hasUAVWrite = true; break; }
				}
			}
		}

		if (!computePass.dispatchs.empty() && hasUAVWrite)
		{
			vk::MemoryBarrier memoryBarrier{};
			memoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			memoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead;

			// F34: dst stage must not include FRAGMENT_SHADER when recorded on the compute
			// command buffer (compute-only queue families don't support graphics stages).
			// COMPUTE_SHADER | TRANSFER is legal on both direct and compute queues.
			targetCmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags{}, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
		}
	}

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
			if (!stagingAlloc.mappedPtr) { CA_LOG_ERR("RecordTransferPass: AllocUploadStagingBuffer failed for image upload"); continue; }

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

			// F38: dstAccessMask includes eShaderRead — cover ALL stages that perform it
			// (VERTEX_SHADER / COMPUTE_SHADER read uniforms & buffers too, not just
			// VERTEX_INPUT attribute fetch and FRAGMENT_SHADER).
			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eVertexShader
					| vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
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
			if (!stagingAlloc.mappedPtr) { CA_LOG_ERR("RecordTransferPass: AllocUploadStagingBuffer failed for image upload"); continue; }

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

			// R4-14: dstAccessMask includes eShaderRead — cover ALL stages that perform it
			// (vertex/compute shaders sample textures too), mirroring the F38 fix on the
			// buffer-upload barrier.
			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader
					| vk::PipelineStageFlagBits::eComputeShader,
				vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, &shaderReadBarrier);
		}
	}

	// Task 3.4: SubmitBatches — no more waitForFences(UINT64_MAX), use window semaphores for last batch
	void VulkanGraphExecutor::SubmitBatches(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		auto graphicsQueue = device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);
		auto computeQueue = device.getQueue(queueContext.GetComputeQueueFamily(), 0);
		auto& resourceManager = m_CurrentFrameContext->GetResourceManager();

		// Task 10.2: Extract window semaphore sync logic to lambda
		// Pre-compute first batch that touches swapchain images (for acquire semaphore wait)
		int firstSwapchainBatch = -1;
		for (size_t idx = 0; idx < m_ExecutionBatches.size() && firstSwapchainBatch < 0; ++idx)
		{
			for (auto const& pair : m_ExecutionBatches[idx].batchRWStates.imageRWStates)
			{
				if (pair.first.GetType() == ImageHandle::ImageType::Backbuffer)
				{
					firstSwapchainBatch = static_cast<int>(idx);
					break;
				}
			}
		}

		// Acquire semaphore wait: only on the first batch that touches swapchain images
		// Iterate over windows (not all sync entries) — only window-indexed acquire
		// semaphores were signaled by AcquireNextImage; pushing unsignaled ones would hang.
		auto addAcquireWait = [&](int batchIdx,
			castl::vector<vk::Semaphore>& outWaitSems,
			castl::vector<vk::PipelineStageFlags>& outWaitStages)
		{
			if (batchIdx != firstSwapchainBatch) return;
			auto const& finalizePass = graph.GetFinalizePass();
			for (size_t w = 0; w < finalizePass.m_PresentBackBuffers.size(); ++w)
			{
				VulkanWindowHandle* pWindow = finalizePass.m_PresentBackBuffers[w].GetWindowPtr<VulkanWindowHandle>();
				// F3a/F36b: skip windows that never acquired this frame — null / invalid
				// windows and acquire-failed windows all have an UNSIGNALED acquire
				// semaphore; waiting on it would hang the GPU.
				if (!pWindow || !pWindow->IsValid() || pWindow->IsAcquireFailed()) continue;
				outWaitSems.push_back(m_CurrentFrameContext->GetAcquireSync(static_cast<uint32_t>(w)));
				outWaitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
			}
		};

		// Present semaphore signal: only on the last batch with finalize pass.
		// D3: index by (window position, ACQUIRED image index) — the present
		// semaphore must be per swapchain image (VUID-00067); the acquire side
		// remains window-position indexed.
		auto addPresentSignal = [&](bool isLastBatch, bool hasFinalizePass,
			castl::vector<vk::Semaphore>& outSignalSems)
		{
			if (!(isLastBatch && hasFinalizePass)) return;
			auto const& finalizePass = graph.GetFinalizePass();
			for (size_t w = 0; w < finalizePass.m_PresentBackBuffers.size(); ++w)
			{
				VulkanWindowHandle* pWindow = finalizePass.m_PresentBackBuffers[w].GetWindowPtr<VulkanWindowHandle>();
				// R3-1: mirror addAcquireWait's guard — a window whose acquire failed (or
				// that is invalid) gets NO present from PresentWindows this frame, so its
				// present semaphore must NOT be signaled here either: the binary semaphore
				// would stay signaled across frames and re-signaling it next frame violates
				// VUID-vkQueueSubmit-pSignalSemaphores-00067 (must be unsignaled at signal).
				if (!pWindow || !pWindow->IsValid() || pWindow->IsAcquireFailed()) continue;
				outSignalSems.push_back(m_CurrentFrameContext->GetPresentSync(
					static_cast<uint32_t>(w), pWindow->GetCurrentImageIndex()));
			}
		};

		auto wireSubmitSync = [&](vk::SubmitInfo& submitInfo,
			castl::vector<vk::Semaphore>& waitSems,
			castl::vector<vk::Semaphore>& signalSems,
			castl::vector<vk::PipelineStageFlags>& waitStages)
		{
			submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
			submitInfo.pWaitSemaphores = waitSems.empty() ? nullptr : waitSems.data();
			submitInfo.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
			submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
			submitInfo.pSignalSemaphores = signalSems.empty() ? nullptr : signalSems.data();
		};

		for (size_t i = 0; i < m_ExecutionBatches.size(); ++i)
		{
			auto& batch = m_ExecutionBatches[i];
			bool isLastBatch = (i == m_ExecutionBatches.size() - 1);
			bool directSubmitted = false;
			bool computeSubmitted = false;

			bool hasComputeCmdBuf = batch.computeCommandBuffer != vk::CommandBuffer{};
			bool hasDirectCmdBuf = batch.directCommandBuffer != vk::CommandBuffer{};
			bool hasCrossQueueSync = hasComputeCmdBuf && hasDirectCmdBuf &&
				(batch.computeAquireBarriers.AnyBarrier() || batch.computeReleaseBarriers.AnyBarrier());

			if (hasCrossQueueSync)
			{
				vk::Semaphore crossQueueSemaphore = resourceManager.AllocCrossQueueSemaphore();

				if (batch.computeAquireBarriers.AnyBarrier())
				{
					// Direct->Compute QFOT: direct queue signals, compute queue waits
					vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eComputeShader;

					vk::SubmitInfo directSubmit{};
					directSubmit.commandBufferCount = 1;
					directSubmit.pCommandBuffers = &batch.directCommandBuffer;

					// Window sync: acquire wait + present signal + cross-queue signal
					castl::vector<vk::Semaphore> waitSems, signalSems = { crossQueueSemaphore };
					castl::vector<vk::PipelineStageFlags> waitStages;
					addAcquireWait(static_cast<int>(i), waitSems, waitStages);
					addPresentSignal(isLastBatch, batch.hasFinalizePass, signalSems);
					wireSubmitSync(directSubmit, waitSems, signalSems, waitStages);

					graphicsQueue.submit(directSubmit, resourceManager.GetDirectFence());
					directSubmitted = true;

					vk::SubmitInfo computeSubmit{};
					computeSubmit.commandBufferCount = 1;
					computeSubmit.pCommandBuffers = &batch.computeCommandBuffer;
					computeSubmit.waitSemaphoreCount = 1;
					computeSubmit.pWaitSemaphores = &crossQueueSemaphore;
					computeSubmit.pWaitDstStageMask = &waitStage;
					computeQueue.submit(computeSubmit, resourceManager.GetComputeFence());
					computeSubmitted = true;
				}
				else
				{
					// Compute->Direct QFOT: compute queue signals, direct queue waits
					// Task 15.1: Use precise pipeline stages instead of eAllCommands
					vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eVertexShader |
						vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput;

					vk::SubmitInfo computeSubmit{};
					computeSubmit.commandBufferCount = 1;
					computeSubmit.pCommandBuffers = &batch.computeCommandBuffer;
					computeSubmit.signalSemaphoreCount = 1;
					computeSubmit.pSignalSemaphores = &crossQueueSemaphore;
					computeQueue.submit(computeSubmit, resourceManager.GetComputeFence());
					computeSubmitted = true;

					vk::SubmitInfo directSubmit{};
					directSubmit.commandBufferCount = 1;
					directSubmit.pCommandBuffers = &batch.directCommandBuffer;

					// Build wait semaphores: cross-queue first, then acquire if needed
					castl::vector<vk::Semaphore> waitSems = { crossQueueSemaphore };
					castl::vector<vk::Semaphore> signalSems;
					castl::vector<vk::PipelineStageFlags> waitStages = { waitStage };
					addAcquireWait(static_cast<int>(i), waitSems, waitStages);
					addPresentSignal(isLastBatch, batch.hasFinalizePass, signalSems);
					wireSubmitSync(directSubmit, waitSems, signalSems, waitStages);

					graphicsQueue.submit(directSubmit, resourceManager.GetDirectFence());
					directSubmitted = true;
				}
			}
			else
			{
				// No cross-queue sync needed -- submit independently
				if (hasComputeCmdBuf)
				{
					vk::SubmitInfo computeSubmitInfo{};
					computeSubmitInfo.commandBufferCount = 1;
					computeSubmitInfo.pCommandBuffers = &batch.computeCommandBuffer;
					computeQueue.submit(computeSubmitInfo, resourceManager.GetComputeFence());
					computeSubmitted = true;
				}

				if (hasDirectCmdBuf)
				{
					vk::SubmitInfo submitInfo{};
					submitInfo.commandBufferCount = 1;
					submitInfo.pCommandBuffers = &batch.directCommandBuffer;

					castl::vector<vk::Semaphore> waitSems, signalSems;
					castl::vector<vk::PipelineStageFlags> waitStages;
					addAcquireWait(static_cast<int>(i), waitSems, waitStages);
					addPresentSignal(isLastBatch, batch.hasFinalizePass, signalSems);
					wireSubmitSync(submitInfo, waitSems, signalSems, waitStages);

					graphicsQueue.submit(submitInfo, resourceManager.GetDirectFence());
					directSubmitted = true;
				}
			}

			// Task 6.3: Batch ordering -- wait for batch N to complete before submitting batch N+1
			{
				if (directSubmitted)
				{
					vk::Fence directFence = resourceManager.GetDirectFence();
					if (directFence)
					{
						vk::Result waitResult = device.waitForFences(directFence, VK_TRUE, 5000000000ULL);
						if (waitResult == vk::Result::eTimeout)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (direct) timed out after 5s");
						}
						else if (waitResult == vk::Result::eErrorDeviceLost)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (direct) DEVICE LOST");
						}
						else if (waitResult != vk::Result::eSuccess)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (direct) failed: {}", vk::to_string(waitResult));
						}
						device.resetFences(directFence);
					}
				}
				if (computeSubmitted)
				{
					vk::Fence computeFence = resourceManager.GetComputeFence();
					if (computeFence)
					{
						vk::Result waitResult = device.waitForFences(computeFence, VK_TRUE, 5000000000ULL);
						if (waitResult == vk::Result::eTimeout)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (compute) timed out after 5s");
						}
						else if (waitResult == vk::Result::eErrorDeviceLost)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (compute) DEVICE LOST");
						}
						else if (waitResult != vk::Result::eSuccess)
						{
							CA_LOG_ERR("VulkanGraphExecutor: waitForFences (compute) failed: {}", vk::to_string(waitResult));
						}
						device.resetFences(computeFence);
					}
				}
			}
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

	// PresentWindows: uses per-swapchain-image present semaphore from FrameContext
	void VulkanGraphExecutor::PresentWindows(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		// R5-10: vkQueuePresentKHR must run on a queue of a family that supports
		// presentation for the surface (VUID-vkQueuePresentKHR-pSwapchains-01292) — on
		// split-family devices the present family differs from the graphics family.
		// [AUDIT] defensive: only index [0] when present backbuffers exist — the caller
		// guards this, but any direct caller must not OOB-read an empty vector. A null
		// pFirstWindow falls through to the graphics queue below (harmless no-op).
		auto const& presentBackBuffers = graph.GetFinalizePass().m_PresentBackBuffers;
		VulkanWindowHandle* pFirstWindow = presentBackBuffers.empty()
			? nullptr
			: presentBackBuffers[0].GetWindowPtr<VulkanWindowHandle>();
		int presentFamily = (pFirstWindow) ? queueContext.FindPresentQueueFamily(pFirstWindow->GetSurface()) : -1;
		vk::Queue presentQueue = (presentFamily >= 0)
			? device.getQueue(presentFamily, 0)
			: device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);

		uint32_t windowIdx = 0;
		for (auto& backBufferImage : graph.GetFinalizePass().m_PresentBackBuffers)
		{
			VulkanWindowHandle* pWindow = backBufferImage.GetWindowPtr<VulkanWindowHandle>();
			if (!pWindow || !pWindow->IsValid())
			{
				// F36b: keep windowIdx aligned with m_PresentBackBuffers positions even for
				// skipped windows — GetWindowSync is indexed by window position.
				++windowIdx;
				continue;
			}

			// F3a: acquire failed this frame — no present was signaled for this window
			// (SubmitBatches skipped it); present the same image again would wait on an
			// unsignaled semaphore. Skip and let RecreateSwapchain handle the stale image.
			if (pWindow->IsAcquireFailed())
			{
				++windowIdx;
				continue;
			}

			// R4-1: recreating HERE would destroy the swapchain this frame's acquire used
			// and present with a stale image index — unreachable today (acquire loop
			// recreates first and clears the flag; m_SwapchainOutdated implies
			// m_AcquireFailed which is skipped above), so assert instead of acting.
			CA_ASSERT(!pWindow->NeedsRecreation(),
				"PresentWindows: NeedsRecreation must have been handled by the acquire loop");

			// Index by (window position, acquired image index) — consistent with
			// SubmitBatches::addPresentSignal, which signaled this window's present
			// semaphore for this exact image slot this frame.
			vk::Semaphore presentSem = m_CurrentFrameContext->GetPresentSync(windowIdx, pWindow->GetCurrentImageIndex());
			pWindow->Present(presentQueue, presentSem);
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
