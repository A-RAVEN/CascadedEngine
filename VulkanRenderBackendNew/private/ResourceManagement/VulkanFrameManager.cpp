#include <ResourceManagement/VulkanFrameManager.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <string>

namespace graphics_backend
{
	// --- VulkanFrameBoundResourceManager ---

	void VulkanFrameBoundResourceManager::Init()
	{
		auto pApp = GetApp();

		pApp->InitSubObj(&m_CommandListManager);
		m_CommandListManager.Init();

		pApp->InitSubObj(&m_StagingMemoryManager);
		m_StagingMemoryManager.Init();

		m_DescriptorPool = nullptr;
		m_DirectFence = nullptr;
		m_ComputeFence = nullptr;
		m_CurrentMaxSets = 0;
		m_CurrentPoolSizes.clear();
	}

	void VulkanFrameBoundResourceManager::Release()
	{
		auto device = GetDevice();

		if (m_DescriptorPool)
		{
			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}
		if (m_DirectFence)
		{
			device.destroyFence(m_DirectFence);
			m_DirectFence = nullptr;
		}
		if (m_ComputeFence)
		{
			device.destroyFence(m_ComputeFence);
			m_ComputeFence = nullptr;
		}
		for (auto& sem : m_CrossQueueSemaphores)
		{
			if (sem)
				device.destroySemaphore(sem);
		}
		m_CrossQueueSemaphores.clear();

		m_StagingMemoryManager.Release();
		m_CommandListManager.Release();
	}

	void VulkanFrameBoundResourceManager::Reset()
	{
		m_CommandListManager.Reset();
		m_StagingMemoryManager.Reset();
		m_CrossQueueSemaphoreIndex = 0;
	}

	vk::Semaphore VulkanFrameBoundResourceManager::AllocCrossQueueSemaphore()
	{
		if (m_CrossQueueSemaphoreIndex < static_cast<int>(m_CrossQueueSemaphores.size()))
			return m_CrossQueueSemaphores[m_CrossQueueSemaphoreIndex++];

		auto device = GetDevice();
		vk::SemaphoreCreateInfo semInfo{};
		vk::Semaphore sem = device.createSemaphore(semInfo);
		m_CrossQueueSemaphores.push_back(sem);
#ifndef NDEBUG
		SetVKObjectDebugName(GetDevice(), sem, ("Semaphore:cq" + std::to_string(m_CrossQueueSemaphores.size() - 1)).c_str());
#endif
		m_CrossQueueSemaphoreIndex = static_cast<int>(m_CrossQueueSemaphores.size());
		return sem;
	}

	void VulkanFrameBoundResourceManager::CreateFences()
	{
		auto device = GetDevice();
		vk::FenceCreateInfo fenceInfo{};
		m_DirectFence = device.createFence(fenceInfo);
		SetVKObjectDebugName(GetDevice(), m_DirectFence, "Fence:direct");
		m_ComputeFence = device.createFence(fenceInfo);
		SetVKObjectDebugName(GetDevice(), m_ComputeFence, "Fence:compute");
	}

	void VulkanFrameBoundResourceManager::ResetDescriptorPool()
	{
		// vkResetDescriptorPool (Vulkan Spec §12.2.2):
		//   "Resetting a descriptor pool destroys all descriptor sets allocated from
		//    the pool and returns all pool resources to the available state."
		// All previously allocated vkDescriptorSet handles become invalid, and any
		// newly allocated sets from this pool start with blank (undefined) contents.
		// This is why BuildDescriptors() must fully rewrite every descriptor set
		// every frame — descriptor write caching is structurally infeasible under
		// the current per-frame pool reset architecture.
		if (m_DescriptorPool)
		{
			GetDevice().resetDescriptorPool(m_DescriptorPool);
		}
	}

	void VulkanFrameBoundResourceManager::EnsurePoolCapacity(
		uint32_t maxSets, castl::vector<vk::DescriptorPoolSize> const& poolSizes)
	{
		auto device = GetDevice();

		if (m_DescriptorPool)
		{
			bool sufficient = (m_CurrentMaxSets >= maxSets);
			if (sufficient)
			{
				for (auto const& needed : poolSizes)
				{
					bool found = false;
					for (auto const& existing : m_CurrentPoolSizes)
					{
						if (existing.type == needed.type && existing.descriptorCount >= needed.descriptorCount)
						{
							found = true;
							break;
						}
					}
					if (!found)
					{
						sufficient = false;
						break;
					}
				}
			}

			if (sufficient)
				return;

			device.destroyDescriptorPool(m_DescriptorPool);
			m_DescriptorPool = nullptr;
		}

		if (maxSets == 0 || poolSizes.empty())
			return;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = maxSets;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		try
		{
			m_DescriptorPool = device.createDescriptorPool(poolInfo);
			m_CurrentMaxSets = maxSets;
			m_CurrentPoolSizes = poolSizes;
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanFrameBoundResourceManager: Failed to create descriptor pool: {}", e.what());
		}
	}

	// --- VulkanFrameContext ---

	void VulkanFrameContext::Init()
	{
		auto pApp = GetApp();
		pApp->InitSubObj(&m_FrameBoundResourceManager);
		m_FrameBoundResourceManager.Init();
		m_FirstFrame = true;
	}

	void VulkanFrameContext::Release()
	{
		m_Semaphore.acquire();
		m_FrameBoundResourceManager.Release();
		m_Semaphore.release();

		auto device = GetDevice();
		for (auto& sync : m_WindowSyncs)
		{
			if (sync.acquireSemaphore)
				device.destroySemaphore(sync.acquireSemaphore);
			if (sync.presentSemaphore)
				device.destroySemaphore(sync.presentSemaphore);
		}
		m_WindowSyncs.clear();
	}

	void VulkanFrameContext::Reset()
	{
		m_Semaphore.release();
	}

	void VulkanFrameContext::Aquire()
	{
		m_Semaphore.acquire();

		auto device = GetDevice();
		auto& resourceManager = m_FrameBoundResourceManager;

		if (!m_FirstFrame)
		{
			// NOTE: Fence wait removed — SubmitBatches already does per-batch
			// waitForFences + resetFences after every submit, so all GPU work
			// (and semaphore consumptions) are guaranteed complete by frame end.
			// Waiting here on an already-reset (unsignaled) fence would deadlock.
			// TODO: For async frames-in-flight, restore fence wait here and remove
			// the per-batch CPU serialization in SubmitBatches.

			// ResetDescriptorPool: vkResetDescriptorPool implicitly returns ALL descriptor
			// sets in the pool to the initial state — their contents are discarded.
			// Newly allocated sets are blank and require full vkUpdateDescriptorSets
			// rewrite every frame. This is the root architectural constraint that
			// makes descriptor write caching infeasible with the current per-frame
			// pool reset strategy. See ResetDescriptorPool() method docs.
			resourceManager.ResetDescriptorPool();
			resourceManager.Reset();
		}
		else
		{
			m_FirstFrame = false;
			resourceManager.CreateFences();
			// First frame: skip Reset() — all state (command buffers, fence, semaphore index)
			// is already default from Init(). Calling resetCommandPool on a never-used pool
			// may cause driver-specific issues on some GPU implementations.
		}

	}

	void VulkanFrameContext::EnsureWindowSync(uint32_t imageCount)
	{
		while (m_WindowSyncs.size() < imageCount)
		{
			auto device = GetDevice();
			vk::SemaphoreCreateInfo semaphoreInfo{};
			WindowSync sync;
			sync.acquireSemaphore = device.createSemaphore(semaphoreInfo);
			sync.presentSemaphore = device.createSemaphore(semaphoreInfo);
			m_WindowSyncs.push_back(sync);
		}
	}

	WindowSync const& VulkanFrameContext::GetWindowSync(uint32_t index) const
	{
		return m_WindowSyncs[index];
	}

	// --- VulkanGPUFrameManager ---

	void VulkanGPUFrameManager::Init(uint64_t maxFrameCount)
	{
		CA_ASSERT_BREAK(maxFrameCount > 0, "maxFrameCount must be > 0");
		m_MaxFrameContexts = maxFrameCount;
		m_FrameIndex = 0;

		auto pApp = GetApp();
		for (uint64_t i = 0; i < m_MaxFrameContexts; ++i)
		{
			auto pContext = castl::make_unique<VulkanFrameContext>();
			pApp->InitSubObj(pContext.get());
			pContext->Init();
			m_FrameContexts.push_back(castl::move(pContext));
		}
	}

	VulkanGPUFrameManager::PFrameContext VulkanGPUFrameManager::AquireFrameContext()
	{
		uint64_t currentFrame = m_FrameIndex;
		++m_FrameIndex;
		uint64_t contextID = currentFrame % m_MaxFrameContexts;
		VulkanFrameContext& context = *m_FrameContexts[contextID];
		context.Aquire();

		return PFrameContext(&context, [](VulkanFrameContext* pContext)
		{
			if (pContext)
				pContext->Reset();
		});
	}

	void VulkanGPUFrameManager::WaitIdle()
	{
		GetDevice().waitIdle();
	}

	void VulkanGPUFrameManager::Release()
	{
		for (auto& context : m_FrameContexts)
		{
			context->Release();
		}
		m_FrameContexts.clear();
	}
}
