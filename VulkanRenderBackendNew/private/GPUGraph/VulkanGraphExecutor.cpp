#include <GPUGraph/VulkanGraphExecutor.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanCommandListManager.h>
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

	// VulkanShaderResourceSet implementation (references D3D12 ShaderResourceSet::Init)
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

	void VulkanExecutorRWState::Append(VulkanExecutorRWState const& other)
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

	void VulkanExecutorRWState::SetImageRWState(ImageHandle const& image,
		vk::PipelineStageFlags stages, vk::AccessFlags access,
		vk::ImageLayout layout, EGPUQueueType queueType)
	{
		VulkanResourceState newState{ access, stages, layout, queueType, true };
		auto found = imageRWStates.find(image);
		if (found != imageRWStates.end())
		{
			found->second.Combine(newState);
		}
		else
		{
			imageRWStates.insert(castl::make_pair(image, newState));
		}
		batchResourceQueueTypes |= static_cast<EGPUQueueTypeFlags>(queueType);
	}

	void VulkanExecutorRWState::SetBufferRWState(BufferHandle const& buffer,
		vk::PipelineStageFlags stages, vk::AccessFlags access, EGPUQueueType queueType)
	{
		VulkanResourceState newState{ access, stages, vk::ImageLayout::eUndefined, queueType, false };
		auto found = bufferRWStates.find(buffer);
		if (found != bufferRWStates.end())
		{
			found->second.Combine(newState);
		}
		else
		{
			bufferRWStates.insert(castl::make_pair(buffer, newState));
		}
		batchResourceQueueTypes |= static_cast<EGPUQueueTypeFlags>(queueType);
	}

	void VulkanExecutorRWState::SetCBufferUsageState(VulkanShaderStruct const* pCBufferStruct,
		vk::PipelineStageFlags stages, EGPUQueueType queueType)
	{
		auto found = cBufferUsageStates.find(pCBufferStruct);
		if (found != cBufferUsageStates.end())
		{
			found->second |= static_cast<EGPUQueueTypeFlags>(queueType);
		}
		else
		{
			cBufferUsageStates.insert(castl::make_pair(pCBufferStruct,
				static_cast<EGPUQueueTypeFlags>(queueType)));
		}
	}

	// VulkanPassDependency implementation
	void VulkanPassDependency::RemoveSelfDeps()
	{
		for (VulkanPassDependency* dep : successors)
		{
			dep->predecessorCount--;
		}
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
			{
				barriers.push_back(ib.barrier);
			}
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
			{
				barriers.push_back(bb.barrier);
			}
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

	// VulkanGraphExecutor implementation
	void VulkanGraphExecutor::Init()
	{
		m_LocalResourceManager.Init();
		m_RasterPassRWStates.clear();
		m_ComputePassRWStates.clear();
		m_TransferPassRWStates.clear();
		m_ExecutionBatches.clear();
		m_ShaderResourceInstances.clear();
		CA_LOG_INFO("VulkanGraphExecutor initialized");
	}

	void VulkanGraphExecutor::Release()
	{
		auto device = GetDevice();

		// Wait for all fences
		for (auto fence : m_Fences)
		{
			if (fence)
			{
				device.waitForFences(fence, VK_TRUE, UINT64_MAX);
				device.destroyFence(fence);
			}
		}
		m_Fences.clear();

		// Destroy semaphores
		for (auto semaphore : m_Semaphores)
		{
			if (semaphore)
			{
				device.destroySemaphore(semaphore);
			}
		}
		m_Semaphores.clear();

		// Cleanup staging buffers (T089)
		CleanupStagingBuffers();

		// Cleanup all caches (T088)
		CleanupCaches();

		m_LocalResourceManager.Release();
		m_ShaderResourceInstances.clear();
		CA_LOG_INFO("VulkanGraphExecutor released");
	}

	void VulkanGraphExecutor::CompileAndExecute(thread_management::TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph)
	{
		if (!graph)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Null graph");
			return;
		}

		auto prepareStart = std::chrono::high_resolution_clock::now();

		// Phase 1: Prepare
		Prepare(*graph);
		m_PrepareTime = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - prepareStart).count();

		// Phase 2: Build dependency-free batches
		BuildDependencyFreeBatches(*graph);

		// Phase 3: Build resource usage ranges
		BuildResourceUsageRanges();

		// Phase 4: Allocate aliased resources
		AllocateAliasedResources();

		// Phase 4.5: Build resources (CBuffer GPU buffer allocation, image/buffer registration)
		for (auto& [hash, instance] : m_ShaderResourceInstances)
		{
			if (instance)
			{
				instance->BuildResources(m_LocalResourceManager, *graph);
				// Populate CBuffer resource ID map
				for (auto& cbuffer : instance->GetCBufferBindings())
				{
					if (cbuffer.pCBufferStruct && cbuffer.gpuBufferResourceId != 0)
					{
						m_CBufferResourceIdMap[cbuffer.pCBufferStruct] = cbuffer.gpuBufferResourceId;
					}
				}
			}
		}

		// Phase 5: Prepare batch resource barriers
		PrepareBatchResourceBarriers(*graph);

		// Phase 6: Build pipeline states
		BuildPipelineStates(*graph);

		// 4.1: Destroy old descriptor pool if exists
		auto device = GetDevice();
		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}

		// 4.2-4.3: Count descriptors and create descriptor pool
		if (!m_ShaderResourceInstances.empty())
		{
			uint32_t maxSets = 0;
			uint32_t uniformBufferCount = 0;
			uint32_t sampledImageCount = 0;
			uint32_t storageBufferCount = 0;
			uint32_t storageImageCount = 0;
			uint32_t samplerCount = 0;

			for (auto& [hash, instance] : m_ShaderResourceInstances)
			{
				if (!instance)
					continue;

				auto* pFileInfo = instance->GetShaderFileInfo();
				if (pFileInfo)
				{
					maxSets += static_cast<uint32_t>(pFileInfo->shaderBindingInfo.setLayoutInfos.size());
				}

				for (auto& cbuffer : instance->GetCBufferBindings())
				{
					if (cbuffer.pCBufferStruct)
						++uniformBufferCount;
				}

				for (auto& image : instance->GetImageBindings())
				{
					if (image.resourceUsages == EResourceUsage::eShaderUnorderedAccess)
						++storageImageCount;
					else
						++sampledImageCount;
				}

				for (auto& buffer : instance->GetBufferBindings())
				{
					++storageBufferCount;
				}

				for (auto& sampler : instance->GetSamplerBindings())
				{
					++samplerCount;
				}
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

			if (!poolSizes.empty() && maxSets > 0)
			{
				vk::DescriptorPoolCreateInfo poolInfo{};
				poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
				poolInfo.maxSets = maxSets;
				poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
				poolInfo.pPoolSizes = poolSizes.data();

				try
				{
					m_DescriptorPool = device.createDescriptorPool(poolInfo);
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Failed to create descriptor pool: {}", e.what());
				}
			}
		}

		// Build descriptors for all binding instances
		if (m_DescriptorPool)
		{
			for (auto& [hash, instance] : m_ShaderResourceInstances)
			{
				if (instance)
				{
					instance->BuildDescriptors(*this, m_DescriptorPool);
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

		// Reset for next frame
		Reset();

		CA_LOG_INFO("VulkanGraphExecutor: Prepare={}us, Execute={}us, Batches={}",
			m_PrepareTime, m_ExecuteTime, m_ExecutionBatches.size());
	}

	void VulkanGraphExecutor::Prepare(GPUGraph const& graph)
	{
		InitArraySizes(graph);
		CollectResources(graph);
		CollectShaderBindings(graph);
		RegisterCBufferUsageStates(graph);
	}

	void VulkanGraphExecutor::RegisterCBufferUsageStates(GPUGraph const& graph)
	{
		// Register CBuffer usage states for render passes
		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& passData = m_RasterPassGPUData[passID];
			auto& passRWState = m_RasterPassRWStates[passID];

			for (auto& batchData : passData.drawcallBatchs)
			{
				auto* pBindingInstance = batchData.pResourceBindingInstance;
				if (!pBindingInstance)
					continue;

				for (auto& cbuffer : pBindingInstance->GetCBufferBindings())
				{
					if (!cbuffer.pCBufferStruct)
						continue;
					vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
					passRWState.SetCBufferUsageState(cbuffer.pCBufferStruct, stages, EGPUQueueType::eDirect);
				}
			}
		}

		// Register CBuffer usage states for compute passes
		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passData = m_ComputePassGPUData[passID];
			auto& passRWState = m_ComputePassRWStates[passID];
			EGPUQueueType queueType = computePass.asyncCompute ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

			for (auto& dispatchData : passData.dispatchs)
			{
				auto* pBindingInstance = dispatchData.pResourceBindingInstance;
				if (!pBindingInstance)
					continue;

				for (auto& cbuffer : pBindingInstance->GetCBufferBindings())
				{
					if (!cbuffer.pCBufferStruct)
						continue;
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
		// Register render pass attachments
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

			// Register vertex and index buffers
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
							vk::AccessFlagBits::eIndexRead,
							EGPUQueueType::eDirect);
					}

					for (auto& vertBuf : drawcall.GetVertexBuffers())
					{
						auto& vertBuffer = vertBuf.second;
						auto descriptor = GetDescriptor(graph, vertBuffer);
						m_LocalResourceManager.RegisterTemporaryBuffer(
							descriptor, EBufferUsage::eVertexBuffer, static_cast<uint32_t>(passID));

						passRWState.SetBufferRWState(vertBuffer,
							vk::PipelineStageFlagBits::eVertexInput,
							vk::AccessFlagBits::eVertexAttributeRead,
							EGPUQueueType::eDirect);
					}
				}
			}
		}

		// Register compute pass resources
		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passRWState = m_ComputePassRWStates[passID];
			EGPUQueueType queueType = computePass.asyncCompute ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;

			// TODO: Register compute shader resources
		}

		// Register transfer pass resources
		for (size_t passID = 0; passID < graph.GetDataTransfers().size(); ++passID)
		{
			auto& transferPass = graph.GetDataTransfers()[passID];
			auto& passRWState = m_TransferPassRWStates[passID];

			for (auto& bufferWrites : transferPass.m_BufferDataUploads)
			{
				auto& uploadBuffer = bufferWrites.first;
				auto descriptor = GetDescriptor(graph, uploadBuffer);

				passRWState.SetBufferRWState(uploadBuffer,
					vk::PipelineStageFlagBits::eTransfer,
					vk::AccessFlagBits::eTransferWrite,
					EGPUQueueType::eDirect);
			}

			for (auto& imgWrites : transferPass.m_ImageDataUploads)
			{
				auto descriptor = GetDescriptor(graph, imgWrites.first);
				m_LocalResourceManager.RegisterTemporaryTexture(
					descriptor, ETextureAccessTypeFlags{}, static_cast<uint32_t>(passID));

				passRWState.SetImageRWState(imgWrites.first,
					vk::PipelineStageFlagBits::eTransfer,
					vk::AccessFlagBits::eTransferWrite,
					vk::ImageLayout::eTransferDstOptimal,
					EGPUQueueType::eDirect);
			}
		}

		// Register finalize pass resources
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
					vk::ImageLayout::eShaderReadOnlyOptimal,
					EGPUQueueType::eDirect);
			}

			for (auto& backBuffer : graph.GetFinalizePass().m_PresentBackBuffers)
			{
				m_FinalizePassRWState.SetImageRWState(backBuffer,
					vk::PipelineStageFlagBits::eBottomOfPipe,
					vk::AccessFlagBits::eNone,
					vk::ImageLayout::ePresentSrcKHR,
					EGPUQueueType::eDirect);
			}
		}

		// Register internal graph resources
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
		// Collect shader resource bindings for each pass
		// This creates VulkanResourceBindingInstance for each unique shader resource set
		// References D3D12 GPUGraphExecutor::Prepare binding flow

		auto createOrGetBindingInstance = [&](VulkanShaderResourceSet const& resourceSet)
			-> VulkanResourceBindingInstance*
		{
			auto it = m_ShaderResourceInstances.find(resourceSet.hash);
			if (it != m_ShaderResourceInstances.end())
			{
				return it->second.get();
			}

			auto bindingInstance = castl::make_shared<VulkanResourceBindingInstance>();
			GetApp()->InitSubObj(bindingInstance.get());
			bindingInstance->Init(resourceSet);

			m_ShaderResourceInstances[resourceSet.hash] = bindingInstance;
			return bindingInstance.get();
		};

		// Collect bindings for render passes
		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& renderPass = graph.GetRenderPasses()[passID];
			auto& passData = m_RasterPassGPUData[passID];

			for (size_t batchID = 0; batchID < renderPass.GetDrawCallBatches().size(); ++batchID)
			{
				auto& batch = renderPass.GetDrawCallBatches()[batchID];
				auto& batchData = passData.drawcallBatchs[batchID];

				ShaderInfo const& shaderInfo = batch.pipelineStateDesc.m_ShaderInfo;
				if (!shaderInfo.isValid())
					continue;

				castl::vector<ShaderStructDic const*> shaderStructs;
				if (!batch.shaderStructs.empty())
				{
					shaderStructs.push_back(&batch.shaderStructs);
				}
				if (!renderPass.GetShaderStructs().empty())
				{
					shaderStructs.push_back(&renderPass.GetShaderStructs());
				}

				VulkanShaderResourceSet resourceSet;
				resourceSet.Init(GetApp(), shaderInfo, shaderStructs);

				batchData.pResourceBindingInstance = createOrGetBindingInstance(resourceSet);
			}
		}

		// Collect bindings for compute passes
		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& passData = m_ComputePassGPUData[passID];

			for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				auto& dispatchData = passData.dispatchs[dispatchID];

				ShaderInfo const& shaderInfo = dispatch.m_ShaderInfo;
				if (!shaderInfo.isValid())
					continue;

				castl::vector<ShaderStructDic const*> shaderStructs;
				if (!dispatch.shaderStructs.empty())
				{
					shaderStructs.push_back(&dispatch.shaderStructs);
				}
				if (!computePass.shaderStructs.empty())
				{
					shaderStructs.push_back(&computePass.shaderStructs);
				}

				VulkanShaderResourceSet resourceSet;
				resourceSet.Init(GetApp(), shaderInfo, shaderStructs);

				dispatchData.pResourceBindingInstance = createOrGetBindingInstance(resourceSet);
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

		// Build dependency graph
		for (size_t prevPass = 0; prevPass < passDeps.size() - 1; ++prevPass)
		{
			for (size_t latterPass = prevPass + 1; latterPass < passDeps.size(); ++latterPass)
			{
				passDeps[prevPass].CheckAddSuccessor(&passDeps[latterPass]);
			}
		}

		// Topological sort to create batches
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
			{
				dep->RemoveSelfDeps();
			}
		}
	}

	void VulkanGraphExecutor::BuildResourceUsageRanges()
	{
		for (uint32_t batchID = 0; batchID < m_ExecutionBatches.size(); ++batchID)
		{
			auto& batch = m_ExecutionBatches[batchID];
			auto& rwStates = batch.batchRWStates;

			for (auto& imageRWState : rwStates.imageRWStates)
			{
				m_ImageLifetimes[imageRWState.first].Expand(batchID, imageRWState.second);
			}

			for (auto& bufferRWState : rwStates.bufferRWStates)
			{
				m_BufferLifetimes[bufferRWState.first].Expand(batchID, bufferRWState.second);
			}

			for (auto cbufferStruct : rwStates.cBufferUsageStates)
			{
				m_CBufferLifetimes[cbufferStruct.first].Encapsule(batchID, cbufferStruct.second);
			}
		}
	}

	void VulkanGraphExecutor::AllocateAliasedResources()
	{
		m_LocalResourceManager.AllocateAliasedResources();
	}

	void VulkanGraphExecutor::PrepareBatchResourceBarriers(GPUGraph const& graph)
	{
		// Generate image barriers
		for (auto& pair : m_ImageLifetimes)
		{
			ImageHandle const& image = pair.first;
			VulkanResourceState cachedState;

			if (image.IsIntternal())
			{
				cachedState = VulkanResourceState::InitializedState();
			}
			else if (image.GetType() == ImageHandle::ImageType::External)
			{
				VulkanTexture const* pImage = static_cast<VulkanTexture const*>(image.GetExternalManagedTexture().get());
				// Get current state from texture
				cachedState = VulkanResourceState::InitializedState();
			}
			else if (image.GetType() == ImageHandle::ImageType::Backbuffer)
			{
				VulkanWindowHandle const* pWindow = static_cast<VulkanWindowHandle const*>(image.GetWindowHandle().get());
				cachedState = VulkanResourceState::InitializedState();
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

				if (lastState.accessFlags == currentState.accessFlags &&
					lastState.imageLayout == currentState.imageLayout)
				{
					continue;
				}

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

		// Generate buffer barriers
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
				{
					continue;
				}

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

		// CBuffer initialization barriers
		for (auto& pair : m_CBufferLifetimes)
		{
			auto& cbufferUsage = pair.second;
			if (cbufferUsage.lifeTime.empty())
				continue;

			VulkanShaderStruct const* pStruct = pair.first;
			auto it = m_CBufferResourceIdMap.find(pStruct);
			if (it == m_CBufferResourceIdMap.end())
				continue;

			uint64_t resourceId = it->second;
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

		// Get ShaderLibrary
		auto resourceManager = pApp->GetResourceManager();
		auto shaderLibrary = (resourceManager != nullptr)
			? resourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib")
			: nullptr;

		// Build pipeline states for raster passes
		for (size_t passID = 0; passID < graph.GetRenderPasses().size(); ++passID)
		{
			auto& rasterPass = graph.GetRenderPasses()[passID];
			auto& rasterPassData = m_RasterPassGPUData[passID];

			// Get render pass format info
			auto const& attachments = rasterPass.GetAttachments();
			if (attachments.empty())
				continue;

			GPUTextureDescriptor firstAttachmentDesc = GetDescriptor(graph, attachments[0]);

			for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
			{
				auto& batch = rasterPass.GetDrawCallBatches()[batchID];
				auto& batchData = rasterPassData.drawcallBatchs[batchID];

				// Get shader info
				ShaderInfo const& shaderInfo = batch.pipelineStateDesc.m_ShaderInfo;
				if (!shaderInfo.isValid())
					continue;

				// Get VulkanShaderFileInfo from ShaderLibrary
				VulkanShaderFileInfo const* pFileInfo = pApp->GetShaderFileInfo(shaderInfo);
				if (pFileInfo == nullptr)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Failed to get shader file info for path: {}", shaderInfo.path.c_str());
					continue;
				}

				// Find vertex and fragment program hashes and entry points
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

				// Create shader modules
				vk::ShaderModule vertexShaderModule = VK_NULL_HANDLE;
				vk::ShaderModule fragmentShaderModule = VK_NULL_HANDLE;

				if (hasVertex)
				{
					vertexShaderModule = GetOrCreateShaderModule(vertexProgramHash);
				}
				if (hasFragment)
				{
					fragmentShaderModule = GetOrCreateShaderModule(fragmentProgramHash);
				}

				// Create pipeline layout from binding info
				vk::PipelineLayout pipelineLayout = GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);

				// Build shader stages
				vk::PipelineShaderStageCreateInfo vertexShaderStage{};
				vertexShaderStage.stage = vk::ShaderStageFlagBits::eVertex;
				vertexShaderStage.module = vertexShaderModule;
				vertexShaderStage.pName = vertexEntryPointName.c_str();

				vk::PipelineShaderStageCreateInfo fragmentShaderStage{};
				fragmentShaderStage.stage = vk::ShaderStageFlagBits::eFragment;
				fragmentShaderStage.module = fragmentShaderModule;
				fragmentShaderStage.pName = fragmentEntryPointName.c_str();

				// Create graphics pipeline
				if (pipelineLibrary.IsSupported())
				{
					// Vertex input state
					vk::PipelineVertexInputStateCreateInfo vertexInputState{};
					vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
					inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;
					inputAssemblyState.primitiveRestartEnable = VK_FALSE;

					// Viewport state (dynamic)
					vk::PipelineViewportStateCreateInfo viewportState{};
					viewportState.viewportCount = 1;
					viewportState.scissorCount = 1;

					// Rasterization state
					vk::PipelineRasterizationStateCreateInfo rasterizationState{};
					rasterizationState.depthClampEnable = VK_FALSE;
					rasterizationState.rasterizerDiscardEnable = VK_FALSE;
					rasterizationState.polygonMode = vk::PolygonMode::eFill;
					rasterizationState.cullMode = vk::CullModeFlagBits::eBack;
					rasterizationState.frontFace = vk::FrontFace::eClockwise;
					rasterizationState.lineWidth = 1.0f;

					// Multisample state
					vk::PipelineMultisampleStateCreateInfo multisampleState{};
					multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;
					multisampleState.sampleShadingEnable = VK_FALSE;

					// Color blend state
					vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
					colorBlendAttachment.blendEnable = VK_FALSE;
					colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
						vk::ColorComponentFlagBits::eG |
						vk::ColorComponentFlagBits::eB |
						vk::ColorComponentFlagBits::eA;

					vk::PipelineColorBlendStateCreateInfo colorBlendState{};
					colorBlendState.logicOpEnable = VK_FALSE;
					colorBlendState.attachmentCount = 1;
					colorBlendState.pAttachments = &colorBlendAttachment;

					// Create library parts
					auto vertexInputLib = pipelineLibrary.CreateVertexInputLibrary(vertexInputState, inputAssemblyState);
					auto preRasterLib = pipelineLibrary.CreatePreRasterizationLibrary(
						vertexShaderStage, nullptr, nullptr, nullptr, viewportState, rasterizationState);
					auto fragmentLib = pipelineLibrary.CreateFragmentLibrary(fragmentShaderStage);
					auto fragmentOutputLib = pipelineLibrary.CreateFragmentOutputLibrary(
						multisampleState, nullptr, colorBlendState);

					// Link pipeline
					PipelineLibraryParts parts{};
					parts.vertexInputLibrary = vertexInputLib;
					parts.preRasterizationLibrary = preRasterLib;
					parts.fragmentLibrary = fragmentLib;
					parts.fragmentOutputLibrary = fragmentOutputLib;
					parts.isComplete = true;

					// Create a simple render pass for linking
					// TODO: Use actual render pass from framebuffer
					vk::RenderPass dummyRenderPass = VK_NULL_HANDLE;

					batchData.pipeline = pipelineLibrary.LinkPipeline(parts, pipelineLayout, dummyRenderPass, 0);
				}
				else
				{
					// Fallback to monolithic pipeline creation
					vk::GraphicsPipelineCreateInfo createInfo{};
					createInfo.layout = pipelineLayout;
					// TODO: Fill in all required state

					batchData.pipeline = pipelineLibrary.CreateMonolithicPipeline(createInfo);
				}

				batchData.pipelineLayout = pipelineLayout;

				// Store topology
				batchData.topology = vk::PrimitiveTopology::eTriangleList;
			}
		}

		// Build pipeline states for compute passes
		for (size_t passID = 0; passID < graph.GetComputePasses().size(); ++passID)
		{
			auto& computePass = graph.GetComputePasses()[passID];
			auto& computePassData = m_ComputePassGPUData[passID];

			for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
			{
				auto& dispatch = computePass.dispatchs[dispatchID];
				auto& dispatchData = computePassData.dispatchs[dispatchID];

				// Get shader info
				ShaderInfo const& shaderInfo = dispatch.m_ShaderInfo;
				if (!shaderInfo.isValid())
					continue;

				// Get VulkanShaderFileInfo from ShaderLibrary
				VulkanShaderFileInfo const* pFileInfo = pApp->GetShaderFileInfo(shaderInfo);
				if (pFileInfo == nullptr)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Failed to get shader file info for compute path: {}", shaderInfo.path.c_str());
					continue;
				}

				// Find compute program hash and entry point
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

				if (!hasCompute)
				{
					CA_LOG_ERR("VulkanGraphExecutor: No compute shader found in file: {}", shaderInfo.path.c_str());
					continue;
				}

				// Create shader module
				vk::ShaderModule computeShaderModule = GetOrCreateShaderModule(computeProgramHash);

				// Create pipeline layout from binding info
				vk::PipelineLayout pipelineLayout = GetOrCreatePipelineLayout(pFileInfo->shaderBindingInfo);

				// Build compute shader stage
				vk::PipelineShaderStageCreateInfo computeShaderStage{};
				computeShaderStage.stage = vk::ShaderStageFlagBits::eCompute;
				computeShaderStage.module = computeShaderModule;
				computeShaderStage.pName = computeEntryPointName.c_str();

				// Create compute pipeline
				vk::ComputePipelineCreateInfo createInfo{};
				createInfo.layout = pipelineLayout;
				createInfo.stage = computeShaderStage;

				auto device = GetDevice();
				try
				{
					auto result = device.createComputePipeline(nullptr, createInfo);
					if (result.result == vk::Result::eSuccess)
					{
						dispatchData.pipeline = result.value;
					}
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Failed to create compute pipeline: {}", e.what());
				}

				dispatchData.pipelineLayout = pipelineLayout;
			}
		}
	}

	void VulkanGraphExecutor::Execute(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto& cmdListManager = GetApp()->GetCommandListManager();

		// Record command buffers for each batch
		for (auto& batch : m_ExecutionBatches)
		{
			RecordBatchCommands(batch, graph);
		}

		// Submit batches
		SubmitBatches(graph);
	}

	void VulkanGraphExecutor::RecordBatchCommands(VulkanGPUExecutionBatch& batch, GPUGraph const& graph)
	{
		auto& cmdListManager = GetApp()->GetCommandListManager();

		// Get command buffer for this batch
		batch.directCommandBuffer = cmdListManager.GraphicsCommand();

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		batch.directCommandBuffer.begin(beginInfo);

		// Execute aquire barriers
		if (batch.aquireBarriers.AnyBarrier())
		{
			batch.aquireBarriers.ExecuteBarriers(batch.directCommandBuffer);
		}

		// 6.2-6.5: Upload CBuffer data via staging buffer
		auto device = GetDevice();
		auto& memoryManager = GetApp()->GetMemoryManager();
		auto cmdBuf = batch.directCommandBuffer;

		auto uploadCBufferBarriers = [&](VulkanCBufferInitializeBarriers& cbufferBarriers)
		{
			if (!cbufferBarriers.AnyBarrier())
				return;

			for (auto& [resourceId, pStruct] : cbufferBarriers.cbufferData)
			{
				if (!pStruct)
					continue;

				vk::Buffer gpuBuffer = m_LocalResourceManager.GetBuffer(resourceId);
				if (!gpuBuffer)
					continue;

				uint64_t bufferSize = pStruct->GetCBufferSize();
				if (bufferSize == 0)
					continue;

				// Create staging buffer
				vk::BufferCreateInfo stagingBufferInfo{};
				stagingBufferInfo.size = bufferSize;
				stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
				stagingBufferInfo.sharingMode = vk::SharingMode::eExclusive;

				vk::Buffer stagingBuffer;
				VmaAllocation stagingAllocation;
				try
				{
					stagingBuffer = device.createBuffer(stagingBufferInfo);
					auto memRequirements = device.getBufferMemoryRequirements(stagingBuffer);
					VmaAllocationCreateInfo allocInfo{};
					allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

					VkResult allocResult = vmaAllocateMemory(memoryManager.GetAllocator(),
						reinterpret_cast<VkMemoryRequirements*>(&memRequirements),
						&allocInfo, &stagingAllocation, nullptr);

					if (allocResult != VK_SUCCESS)
					{
						device.destroyBuffer(stagingBuffer);
						CA_LOG_ERR("VulkanGraphExecutor: Failed to allocate CBuffer staging memory");
						continue;
					}

					vmaBindBufferMemory(memoryManager.GetAllocator(), stagingAllocation, stagingBuffer);

					// Map and write data
					void* pMappedData = nullptr;
					vmaMapMemory(memoryManager.GetAllocator(), stagingAllocation, &pMappedData);
					if (pMappedData)
					{
						pStruct->ComputeMaxChildrenVersion();
						pStruct->UpdateUniformBuffer(0, pMappedData, static_cast<uint32_t>(bufferSize), 0);
						vmaUnmapMemory(memoryManager.GetAllocator(), stagingAllocation);
					}

					// Track staging buffer for cleanup
					m_PendingStagingBuffers.push_back({ stagingBuffer, stagingAllocation });
				}
				catch (vk::SystemError const& e)
				{
					CA_LOG_ERR("VulkanGraphExecutor: Exception creating CBuffer staging buffer: {}", e.what());
					continue;
				}

				// Copy from staging to GPU buffer
				vk::BufferCopy copyRegion{};
				copyRegion.srcOffset = 0;
				copyRegion.dstOffset = 0;
				copyRegion.size = bufferSize;
				cmdBuf.copyBuffer(stagingBuffer, gpuBuffer, 1, &copyRegion);

				// 6.4: Buffer memory barrier to ensure shader visibility
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

		// Record render passes
		for (int rasterPassID : batch.rasterPassRefs)
		{
			RecordRenderPass(batch, rasterPassID, graph);
		}

		// Record compute passes
		for (int computePassID : batch.computePassRefs)
		{
			RecordComputePass(batch, computePassID, graph);
		}

		// Record transfer passes
		for (int transferPassID : batch.transferPassRefs)
		{
			RecordTransferPass(batch, transferPassID, graph);
		}

		// Execute release barriers
		if (batch.releaseBarriers.AnyBarrier())
		{
			batch.releaseBarriers.ExecuteBarriers(batch.directCommandBuffer);
		}

		batch.directCommandBuffer.end();
	}

	void VulkanGraphExecutor::RecordRenderPass(VulkanGPUExecutionBatch& batch,
		uint32_t rasterPassID, GPUGraph const& graph)
	{
		auto& rasterPass = graph.GetRenderPasses()[rasterPassID];
		auto& rasterData = m_RasterPassGPUData[rasterPassID];
		auto cmdBuf = batch.directCommandBuffer;
		auto device = GetDevice();

		auto const& attachments = rasterPass.GetAttachments();
		if (attachments.empty())
			return;

		GPUTextureDescriptor firstAttachmentDesc = GetDescriptor(graph, attachments[0]);

		// Setup viewport and scissor
		vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(firstAttachmentDesc.width),
			static_cast<float>(firstAttachmentDesc.height), 0.0f, 1.0f };
		vk::Rect2D scissor{ {0, 0}, {firstAttachmentDesc.width, firstAttachmentDesc.height} };
		cmdBuf.setViewport(0, viewport);
		cmdBuf.setScissor(0, scissor);

		// Build render pass cache key (T088)
		RenderPassCacheKey rpKey;
		bool hasDepth = rasterPass.HasDepthAttachment();

		for (size_t i = 0; i < attachments.size(); ++i)
		{
			auto desc = GetDescriptor(graph, attachments[i]);
			if (static_cast<int>(i) == rasterPass.GetDepthAttachmentIndex())
			{
				rpKey.depthFormat = vk::Format::eD32Sfloat; // TODO: Proper format conversion
				rpKey.hasDepth = true;
			}
			else
			{
				rpKey.colorFormats.push_back(vk::Format::eR8G8B8A8Unorm); // TODO: Proper format conversion
			}
		}

		// Get or create cached render pass (T088)
		vk::RenderPass renderPass = GetOrCreateRenderPass(rpKey);
		if (!renderPass)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to get/create render pass");
			return;
		}

		// Create framebuffer attachment views
		castl::vector<vk::ImageView> attachmentViews;
		for (auto& attachment : attachments)
		{
			vk::ImageView view = m_LocalResourceManager.GetTextureView(attachment);
			if (view)
			{
				attachmentViews.push_back(view);
			}
		}

		// Get or create cached framebuffer (T088)
		vk::Framebuffer framebuffer = GetOrCreateFramebuffer(renderPass, attachmentViews,
			firstAttachmentDesc.width, firstAttachmentDesc.height);
		if (!framebuffer)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to get/create framebuffer");
			return;
		}

		// Prepare clear values
		castl::vector<vk::ClearValue> clearValues;
		for (size_t i = 0; i < attachments.size(); ++i)
		{
			auto const& config = rasterPass.GetAttachmentConfig(i);
			if (static_cast<int>(i) == rasterPass.GetDepthAttachmentIndex())
			{
				vk::ClearValue clearValue{};
				clearValue.depthStencil.depth = config.clearValue.depthStencil.depth;
				clearValue.depthStencil.stencil = config.clearValue.depthStencil.stencil;
				clearValues.push_back(clearValue);
			}
			else
			{
				vk::ClearValue clearValue{};
				clearValue.color.float32[0] = config.clearValue.color.r;
				clearValue.color.float32[1] = config.clearValue.color.g;
				clearValue.color.float32[2] = config.clearValue.color.b;
				clearValue.color.float32[3] = config.clearValue.color.a;
				clearValues.push_back(clearValue);
			}
		}

		// Begin render pass
		vk::RenderPassBeginInfo renderPassBegin{};
		renderPassBegin.renderPass = renderPass;
		renderPassBegin.framebuffer = framebuffer;
		renderPassBegin.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderPassBegin.renderArea.extent = vk::Extent2D{ firstAttachmentDesc.width, firstAttachmentDesc.height };
		renderPassBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBegin.pClearValues = clearValues.data();

		cmdBuf.beginRenderPass(renderPassBegin, vk::SubpassContents::eInline);

		// Record draw calls
		for (size_t batchID = 0; batchID < rasterPass.GetDrawCallBatches().size(); ++batchID)
		{
			auto& drawcallBatch = rasterPass.GetDrawCallBatches()[batchID];
			auto& batchData = rasterData.drawcallBatchs[batchID];

			// Bind pipeline
			if (batchData.pipeline)
			{
				cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, batchData.pipeline);
			}

			// 7.3: Bind all descriptor sets by consecutive set index groups
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

			// Record draw calls
			for (size_t drawcallID = 0; drawcallID < drawcallBatch.m_DrawCalls.size(); ++drawcallID)
			{
				auto& drawcall = drawcallBatch.m_DrawCalls[drawcallID];
				auto& drawInfo = drawcall.GetDrawInfo();

				// Bind vertex buffers
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
					{
						cmdBuf.bindVertexBuffers(0, vertexBuffers, offsets);
					}
				}

				// Handle per-drawcall viewport/scissor
				auto const& vp = drawcall.GetViewPort();
				if (vp.Valid())
				{
					vk::Viewport dynViewport{
						static_cast<float>(vp->x), static_cast<float>(vp->y),
						static_cast<float>(vp->width), static_cast<float>(vp->height),
						0.0f, 1.0f
					};
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
					// Bind index buffer
					auto const& indexBufferData = drawcall.GetIndexBuffer();
					if (indexBufferData.IsValid())
					{
						vk::Buffer indexBuffer = m_LocalResourceManager.GetBuffer(indexBufferData.indexBufferHandle);
						if (indexBuffer)
						{
							vk::IndexType indexType = indexBufferData.indexBufferType == EIndexBufferType::e16
								? vk::IndexType::eUint16
								: vk::IndexType::eUint32;
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

		// End render pass
		cmdBuf.endRenderPass();

		// Note: No need to destroy render pass and framebuffer - they're cached (T088)
	}

	void VulkanGraphExecutor::RecordComputePass(VulkanGPUExecutionBatch& batch,
		uint32_t computePassID, GPUGraph const& graph)
	{
		auto& computePass = graph.GetComputePasses()[computePassID];
		auto& computeData = m_ComputePassGPUData[computePassID];
		auto cmdBuf = batch.directCommandBuffer;

		// Use compute command buffer if this is async compute, otherwise use direct
		vk::CommandBuffer targetCmdBuf = batch.anyComputeQueueOperations && batch.computeCommandBuffer
			? batch.computeCommandBuffer
			: cmdBuf;

		for (size_t dispatchID = 0; dispatchID < computePass.dispatchs.size(); ++dispatchID)
		{
			auto& dispatch = computePass.dispatchs[dispatchID];
			auto& dispatchData = computeData.dispatchs[dispatchID];

			// Bind compute pipeline
			if (dispatchData.pipeline)
			{
				targetCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, dispatchData.pipeline);
			}

			// 7.4: Bind all descriptor sets by consecutive set index groups
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

			// Dispatch compute workgroups
			if (dispatch.x > 0 && dispatch.y > 0 && dispatch.z > 0)
			{
				targetCmdBuf.dispatch(dispatch.x, dispatch.y, dispatch.z);
			}
		}

		// Add memory barrier between compute and subsequent operations
		if (!computePass.dispatchs.empty())
		{
			vk::MemoryBarrier memoryBarrier{};
			memoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			memoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead;

			targetCmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags{},
				1, &memoryBarrier,
				0, nullptr,
				0, nullptr
			);
		}
	}

	void VulkanGraphExecutor::RecordTransferPass(VulkanGPUExecutionBatch& batch,
		uint32_t transferPassID, GPUGraph const& graph)
	{
		auto& transferPass = graph.GetDataTransfers()[transferPassID];
		auto cmdBuf = batch.directCommandBuffer;
		auto device = GetDevice();
		auto& memoryManager = GetApp()->GetMemoryManager();

		auto const& uploadDataHolder = graph.GetUploadDataHolder();

		// Record buffer uploads
		for (auto& bufferUploads : transferPass.m_BufferDataUploads)
		{
			auto& [targetBufferHandle, dataRef] = bufferUploads;

			// Get target buffer
			vk::Buffer targetBuffer = m_LocalResourceManager.GetBuffer(targetBufferHandle);
			if (!targetBuffer)
			{
				CA_LOG_WARN("VulkanGraphExecutor: Target buffer not found for upload");
				continue;
			}

			// Get source data
			void const* pSourceData = nullptr;
			if (dataRef.copied && dataRef.dataIndex < uploadDataHolder.m_Data.size())
			{
				pSourceData = uploadDataHolder.GetPtr(dataRef.dataIndex);
			}
			else if (!dataRef.copied)
			{
				pSourceData = dataRef.pData;
			}

			if (!pSourceData || dataRef.dataSize == 0)
			{
				continue;
			}

			// Create staging buffer
			vk::BufferCreateInfo stagingBufferInfo{};
			stagingBufferInfo.size = dataRef.dataSize;
			stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			stagingBufferInfo.sharingMode = vk::SharingMode::eExclusive;

			vk::Buffer stagingBuffer;
			VmaAllocation stagingAllocation;
			try
			{
				stagingBuffer = device.createBuffer(stagingBufferInfo);

				// Allocate memory for staging buffer
				auto memRequirements = device.getBufferMemoryRequirements(stagingBuffer);
				VmaAllocationCreateInfo allocInfo{};
				allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

				VkResult allocResult = vmaAllocateMemory(memoryManager.GetAllocator(),
					reinterpret_cast<VkMemoryRequirements*>(&memRequirements),
					&allocInfo, &stagingAllocation, nullptr);

				if (allocResult != VK_SUCCESS)
				{
					device.destroyBuffer(stagingBuffer);
					CA_LOG_ERR("VulkanGraphExecutor: Failed to allocate staging memory");
					continue;
				}

				// Bind memory
				vmaBindBufferMemory(memoryManager.GetAllocator(), stagingAllocation, stagingBuffer);

				// Map and copy data to staging buffer
				void* pMappedData = nullptr;
				vmaMapMemory(memoryManager.GetAllocator(), stagingAllocation, &pMappedData);
				if (pMappedData)
				{
					memcpy(pMappedData, pSourceData, dataRef.dataSize);
					vmaUnmapMemory(memoryManager.GetAllocator(), stagingAllocation);
				}

				// Track staging buffer for cleanup (T089)
				m_PendingStagingBuffers.push_back({ stagingBuffer, stagingAllocation });
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_ERR("VulkanGraphExecutor: Exception creating staging buffer: {}", e.what());
				continue;
			}

			// Record copy command
			vk::BufferCopy copyRegion{};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = dataRef.dstOffset;
			copyRegion.size = dataRef.dataSize;

			cmdBuf.copyBuffer(stagingBuffer, targetBuffer, 1, &copyRegion);

			// Add barrier to ensure copy completes before buffer is used
			vk::BufferMemoryBarrier barrier{};
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead |
				vk::AccessFlagBits::eIndexRead |
				vk::AccessFlagBits::eShaderRead;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = targetBuffer;
			barrier.offset = dataRef.dstOffset;
			barrier.size = dataRef.dataSize;

			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eFragmentShader,
				vk::DependencyFlags{},
				0, nullptr,
				1, &barrier,
				0, nullptr
			);
		}

		// Record image uploads
		for (auto& imageUploads : transferPass.m_ImageDataUploads)
		{
			auto& [targetImageHandle, dataRef] = imageUploads;

			// Get target image
			vk::Image targetImage = m_LocalResourceManager.GetTexture(targetImageHandle);
			if (!targetImage)
			{
				CA_LOG_WARN("VulkanGraphExecutor: Target image not found for upload");
				continue;
			}

			// Get source data
			void const* pSourceData = nullptr;
			if (dataRef.copied && dataRef.dataIndex < uploadDataHolder.m_Data.size())
			{
				pSourceData = uploadDataHolder.GetPtr(dataRef.dataIndex);
			}
			else if (!dataRef.copied)
			{
				pSourceData = dataRef.pData;
			}

			if (!pSourceData || dataRef.dataSize == 0)
			{
				continue;
			}

			// Get image descriptor for dimensions
			auto desc = GetDescriptor(graph, targetImageHandle);

			// Create staging buffer for image data
			vk::BufferCreateInfo stagingBufferInfo{};
			stagingBufferInfo.size = dataRef.dataSize;
			stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;

			vk::Buffer stagingBuffer;
			VmaAllocation stagingAllocation;
			try
			{
				stagingBuffer = device.createBuffer(stagingBufferInfo);

				auto memRequirements = device.getBufferMemoryRequirements(stagingBuffer);
				VmaAllocationCreateInfo allocInfo{};
				allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

				VkResult allocResult = vmaAllocateMemory(memoryManager.GetAllocator(),
					reinterpret_cast<VkMemoryRequirements*>(&memRequirements),
					&allocInfo, &stagingAllocation, nullptr);

				if (allocResult != VK_SUCCESS)
				{
					device.destroyBuffer(stagingBuffer);
					CA_LOG_ERR("VulkanGraphExecutor: Failed to allocate image staging memory");
					continue;
				}

				vmaBindBufferMemory(memoryManager.GetAllocator(), stagingAllocation, stagingBuffer);

				void* pMappedData = nullptr;
				vmaMapMemory(memoryManager.GetAllocator(), stagingAllocation, &pMappedData);
				if (pMappedData)
				{
					memcpy(pMappedData, pSourceData, dataRef.dataSize);
					vmaUnmapMemory(memoryManager.GetAllocator(), stagingAllocation);
				}

				// Track staging buffer for cleanup (T089)
				m_PendingStagingBuffers.push_back({ stagingBuffer, stagingAllocation });
			}
			catch (vk::SystemError const& e)
			{
				CA_LOG_ERR("VulkanGraphExecutor: Exception creating image staging buffer: {}", e.what());
				continue;
			}

			// Transition image to transfer destination layout
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
				vk::DependencyFlags{},
				0, nullptr,
				0, nullptr,
				1, &transitionBarrier
			);

			// Copy buffer to image
			vk::BufferImageCopy copyRegion{};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageOffset = vk::Offset3D{ 0, 0, 0 };
			copyRegion.imageExtent = vk::Extent3D{ desc.width, desc.height, 1 };

			cmdBuf.copyBufferToImage(stagingBuffer, targetImage,
				vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

			// Transition image to shader read layout
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
				vk::DependencyFlags{},
				0, nullptr,
				0, nullptr,
				1, &shaderReadBarrier
			);
		}
	}

	void VulkanGraphExecutor::SubmitBatches(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		auto queue = device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);

		for (size_t i = 0; i < m_ExecutionBatches.size(); ++i)
		{
			auto& batch = m_ExecutionBatches[i];

			// Create fence for this batch
			vk::FenceCreateInfo fenceInfo{};
			vk::Fence fence = device.createFence(fenceInfo);
			m_Fences.push_back(fence);

			// Create semaphore
			vk::SemaphoreCreateInfo semaphoreInfo{};
			vk::Semaphore semaphore = device.createSemaphore(semaphoreInfo);
			m_Semaphores.push_back(semaphore);

			// Submit
			vk::SubmitInfo submitInfo{};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &batch.directCommandBuffer;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &semaphore;

			queue.submit(submitInfo, fence);
		}

		// Wait for all fences
		if (!m_Fences.empty())
		{
			device.waitForFences(m_Fences, VK_TRUE, UINT64_MAX);
			device.resetFences(m_Fences);
		}
	}

	void VulkanGraphExecutor::ApplyExternalResourceStates()
	{
		// Update external resource states after execution
		// This allows subsequent frames to know the current state
	}

	void VulkanGraphExecutor::PresentWindows(GPUGraph const& graph)
	{
		auto device = GetDevice();
		auto const& queueContext = GetApp()->GetQueueContext();
		auto presentQueue = device.getQueue(queueContext.GetGraphicsQueueFamily(), 0);

		size_t semaphoreIndex = 0;

		for (auto& backBufferImage : graph.GetFinalizePass().m_PresentBackBuffers)
		{
			VulkanWindowHandle* pWindow = backBufferImage.GetWindowPtr<VulkanWindowHandle>();
			if (!pWindow || !pWindow->IsValid())
			{
				CA_LOG_WARN("VulkanGraphExecutor: Invalid window handle for present");
				continue;
			}

			// Check if swapchain needs recreation
			if (pWindow->NeedsRecreation())
			{
				pWindow->RecreateSwapchain();
			}

			// Create acquire semaphore for this frame
			vk::SemaphoreCreateInfo semaphoreInfo{};
			vk::Semaphore acquireSemaphore = device.createSemaphore(semaphoreInfo);
			vk::Semaphore renderFinishedSemaphore = device.createSemaphore(semaphoreInfo);

			// Acquire next swapchain image
			uint32_t imageIndex = pWindow->AcquireNextImage(acquireSemaphore);
			if (pWindow->NeedsRecreation())
			{
				pWindow->RecreateSwapchain();
				device.destroySemaphore(acquireSemaphore);
				device.destroySemaphore(renderFinishedSemaphore);
				continue;
			}

			// If we have completed execution batches, wait on the last batch's semaphore
			vk::Semaphore waitSemaphore = renderFinishedSemaphore;
			if (!m_Semaphores.empty() && semaphoreIndex < m_Semaphores.size())
			{
				waitSemaphore = m_Semaphores[semaphoreIndex];
			}

			// Present with proper synchronization
			pWindow->Present(presentQueue, waitSemaphore);

			// Clean up temporary semaphores
			device.destroySemaphore(acquireSemaphore);
			if (waitSemaphore == renderFinishedSemaphore)
			{
				device.destroySemaphore(renderFinishedSemaphore);
			}

			++semaphoreIndex;
		}
	}

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
		m_CBufferResourceIdMap.clear();
		m_CurrentGraph.reset();

		// 4.4: Destroy descriptor pool
		auto device = GetDevice();
		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}

		// Cleanup staging buffers (T089)
		CleanupStagingBuffers();
	}

	// T085: Shader module creation from ShaderInfo
	vk::ShaderModule VulkanGraphExecutor::GetOrCreateShaderModule(cahash::sha256_hash::result_type const& programHash)
	{
		// Check cache
		auto it = m_ShaderModuleCache.find(programHash);
		if (it != m_ShaderModuleCache.end())
		{
			return it->second;
		}

		// Get SPIR-V code from ShaderLibrary
		auto pApp = GetApp();
		auto resourceManager = pApp->GetResourceManager();
		if (resourceManager == nullptr)
		{
			CA_LOG_ERR("VulkanGraphExecutor: No resource manager available for shader module creation");
			return vk::ShaderModule(nullptr);
		}

		auto shaderLibrary = resourceManager->GetOrLoadResource<ShaderLibrary>("VulkanShaderLibrary.shLib");
		if (shaderLibrary == nullptr)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to load ShaderLibrary for shader module creation");
			return vk::ShaderModule(nullptr);
		}

		VulkanShaderCode const* shaderCode = shaderLibrary->GetShaderCode(programHash);
		if (shaderCode == nullptr)
		{
			CA_LOG_ERR("VulkanGraphExecutor: ShaderCode not found for program hash: {}", programHash.toString());
			return vk::ShaderModule(nullptr);
		}

		if (shaderCode->spirvCode.empty())
		{
			CA_LOG_ERR("VulkanGraphExecutor: SPIR-V code is empty for program hash: {}", programHash.toString());
			return vk::ShaderModule(nullptr);
		}

		// Create shader module from SPIR-V
		auto device = GetDevice();
		vk::ShaderModuleCreateInfo moduleInfo{};
		moduleInfo.codeSize = shaderCode->spirvCode.size() * sizeof(uint32_t);
		moduleInfo.pCode = shaderCode->spirvCode.data();

		try
		{
			auto shaderModule = device.createShaderModule(moduleInfo);
			m_ShaderModuleCache[programHash] = shaderModule;
			CA_LOG_INFO("VulkanGraphExecutor: Created shader module ({} bytes SPIR-V)", moduleInfo.codeSize);
			return shaderModule;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to create shader module ({} bytes SPIR-V): {}", moduleInfo.codeSize, e.what());
			// Don't cache nullptr — allow retry
			return vk::ShaderModule(nullptr);
		}
	}

	// T086: Descriptor set layout creation from VulkanDescriptorSetLayoutInfo
	vk::DescriptorSetLayout VulkanGraphExecutor::GetOrCreateDescriptorSetLayout(VulkanDescriptorSetLayoutInfo const& setLayoutInfo)
	{
		size_t hash = setLayoutInfo.GetHash();

		auto it = m_DescriptorSetLayoutCache.find(hash);
		if (it != m_DescriptorSetLayoutCache.end())
		{
			return it->second;
		}

		auto device = GetDevice();
		castl::vector<vk::DescriptorSetLayoutBinding> vkBindings;
		auto createInfo = setLayoutInfo.GetCreateInfo(vkBindings);

		try
		{
			auto descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
			m_DescriptorSetLayoutCache[hash] = descriptorSetLayout;
			return descriptorSetLayout;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to create descriptor set layout for set {}: {}",
				setLayoutInfo.setIndex, e.what());
			return vk::DescriptorSetLayout(nullptr);
		}
	}

	// T086: Pipeline layout creation from VulkanShaderResourceBindingInfo
	vk::PipelineLayout VulkanGraphExecutor::GetOrCreatePipelineLayout(VulkanShaderResourceBindingInfo const& bindingInfo)
	{
		// Generate cache key from binding info
		// Hash includes: each set's setIndex + all binding's descriptorType, binding, descriptorCount, stageFlags
		size_t hash = 0;
		for (auto const& setLayoutInfo : bindingInfo.setLayoutInfos)
		{
			cacore::hash_combine(hash, setLayoutInfo.setIndex);
			for (auto const& binding : setLayoutInfo.bindings)
			{
				cacore::hash_combine(hash, binding.descriptorType);
				cacore::hash_combine(hash, binding.binding);
				cacore::hash_combine(hash, binding.descriptorCount);
				cacore::hash_combine(hash, binding.stageFlags);
			}
		}

		// Check PipelineLayout cache
		auto it = m_PipelineLayoutCache.find(hash);
		if (it != m_PipelineLayoutCache.end())
		{
			return it->second;
		}

		auto device = GetDevice();

		// Sort setLayoutInfos by setIndex to ensure correct ordering
		castl::vector<VulkanDescriptorSetLayoutInfo const*> sortedSetLayouts;
		sortedSetLayouts.reserve(bindingInfo.setLayoutInfos.size());
		for (auto const& setLayoutInfo : bindingInfo.setLayoutInfos)
		{
			sortedSetLayouts.push_back(&setLayoutInfo);
		}
		castl::sort(sortedSetLayouts.begin(), sortedSetLayouts.end(),
			[](VulkanDescriptorSetLayoutInfo const* a, VulkanDescriptorSetLayoutInfo const* b)
			{
				return a->setIndex < b->setIndex;
			});

		// Create DescriptorSetLayout for each set
		castl::vector<vk::DescriptorSetLayout> setLayouts;
		setLayouts.reserve(sortedSetLayouts.size());

		for (auto const* pSetLayoutInfo : sortedSetLayouts)
		{
			auto descriptorSetLayout = GetOrCreateDescriptorSetLayout(*pSetLayoutInfo);
			if (!descriptorSetLayout)
			{
				return vk::PipelineLayout(nullptr);
			}
			setLayouts.push_back(descriptorSetLayout);
		}

		// Create PipelineLayout from the assembled DescriptorSetLayouts
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();

		try
		{
			auto pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);
			m_PipelineLayoutCache[hash] = pipelineLayout;
			CA_LOG_INFO("VulkanGraphExecutor: Created pipeline layout with {} sets", setLayouts.size());
			return pipelineLayout;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to create pipeline layout: {}", e.what());
			return vk::PipelineLayout(nullptr);
		}
	}

	// T088: RenderPass caching
	vk::RenderPass VulkanGraphExecutor::GetOrCreateRenderPass(RenderPassCacheKey const& key)
	{
		// Create hash from key
		size_t hash = 0;
		for (auto const& fmt : key.colorFormats)
		{
			cacore::hash_combine(hash, static_cast<uint32_t>(fmt));
		}
		cacore::hash_combine(hash, static_cast<uint32_t>(key.depthFormat));
		cacore::hash_combine(hash, key.hasDepth);

		// Check cache
		auto it = m_RenderPassCache.find(hash);
		if (it != m_RenderPassCache.end())
		{
			return it->second;
		}

		auto device = GetDevice();

		// Create attachment descriptions
		castl::vector<vk::AttachmentDescription> attachments;
		castl::vector<vk::AttachmentReference> colorRefs;
		vk::AttachmentReference depthRef{};

		for (size_t i = 0; i < key.colorFormats.size(); ++i)
		{
			vk::AttachmentDescription attachment{};
			attachment.format = key.colorFormats[i];
			attachment.samples = vk::SampleCountFlagBits::e1;
			attachment.loadOp = vk::AttachmentLoadOp::eClear;
			attachment.storeOp = vk::AttachmentStoreOp::eStore;
			attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachments.push_back(attachment);

			colorRefs.push_back({ static_cast<uint32_t>(i), vk::ImageLayout::eColorAttachmentOptimal });
		}

		if (key.hasDepth)
		{
			vk::AttachmentDescription depthAttachment{};
			depthAttachment.format = key.depthFormat;
			depthAttachment.samples = vk::SampleCountFlagBits::e1;
			depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
			depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
			depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			attachments.push_back(depthAttachment);

			depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		// Create subpass
		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
		subpass.pColorAttachments = colorRefs.data();
		if (key.hasDepth)
		{
			subpass.pDepthStencilAttachment = &depthRef;
		}

		// Create render pass
		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		try
		{
			auto renderPass = device.createRenderPass(renderPassInfo);
			m_RenderPassCache[hash] = renderPass;
			CA_LOG_INFO("VulkanGraphExecutor: Created cached render pass");
			return renderPass;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to create render pass: {}", e.what());
			return nullptr;
		}
	}

	// T088: Framebuffer caching
	vk::Framebuffer VulkanGraphExecutor::GetOrCreateFramebuffer(vk::RenderPass renderPass,
		castl::vector<vk::ImageView> const& attachments, uint32_t width, uint32_t height)
	{
		// Create hash from render pass and attachments
		size_t hash = 0;
		cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkRenderPass>(renderPass)));
		for (auto const& view : attachments)
		{
			cacore::hash_combine(hash, reinterpret_cast<uintptr_t>(static_cast<VkImageView>(view)));
		}
		cacore::hash_combine(hash, width);
		cacore::hash_combine(hash, height);

		// Check cache
		auto it = m_FramebufferCache.find(hash);
		if (it != m_FramebufferCache.end())
		{
			return it->second;
		}

		auto device = GetDevice();

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = width;
		framebufferInfo.height = height;
		framebufferInfo.layers = 1;

		try
		{
			auto framebuffer = device.createFramebuffer(framebufferInfo);
			m_FramebufferCache[hash] = framebuffer;
			CA_LOG_INFO("VulkanGraphExecutor: Created cached framebuffer");
			return framebuffer;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanGraphExecutor: Failed to create framebuffer: {}", e.what());
			return nullptr;
		}
	}

	// T089: Staging buffer cleanup
	void VulkanGraphExecutor::CleanupStagingBuffers()
	{
		auto& memoryManager = GetApp()->GetMemoryManager();
		auto device = GetDevice();

		for (auto const& stagingInfo : m_PendingStagingBuffers)
		{
			if (stagingInfo.buffer)
			{
				device.destroyBuffer(stagingInfo.buffer);
			}
			if (stagingInfo.allocation)
			{
				vmaFreeMemory(memoryManager.GetAllocator(), stagingInfo.allocation);
			}
		}
		m_PendingStagingBuffers.clear();
	}

	// Cleanup all caches
	void VulkanGraphExecutor::CleanupCaches()
	{
		auto device = GetDevice();

		// Cleanup render passes
		for (auto const& [hash, renderPass] : m_RenderPassCache)
		{
			if (renderPass)
			{
				device.destroyRenderPass(renderPass);
			}
		}
		m_RenderPassCache.clear();

		// Cleanup framebuffers
		for (auto const& [hash, framebuffer] : m_FramebufferCache)
		{
			if (framebuffer)
			{
				device.destroyFramebuffer(framebuffer);
			}
		}
		m_FramebufferCache.clear();

		// Cleanup shader modules
		for (auto const& [hash, shaderModule] : m_ShaderModuleCache)
		{
			if (shaderModule)
			{
				device.destroyShaderModule(shaderModule);
			}
		}
		m_ShaderModuleCache.clear();

		// Cleanup pipeline layouts
		for (auto const& [hash, pipelineLayout] : m_PipelineLayoutCache)
		{
			if (pipelineLayout)
			{
				device.destroyPipelineLayout(pipelineLayout);
			}
		}
		m_PipelineLayoutCache.clear();

		// Cleanup descriptor set layouts
		for (auto const& [hash, setLayout] : m_DescriptorSetLayoutCache)
		{
			if (setLayout)
			{
				device.destroyDescriptorSetLayout(setLayout);
			}
		}
		m_DescriptorSetLayoutCache.clear();

		// Cleanup descriptor pool
		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}
	}
}
