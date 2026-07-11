#include <ResourceManagement/VulkanFrameManager.h>
#include <RenderBackend_Vulkan.h>

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
		m_FenceSubmitted = false;
	}

	vk::Semaphore VulkanFrameBoundResourceManager::AllocCrossQueueSemaphore()
	{
		if (m_CrossQueueSemaphoreIndex < static_cast<int>(m_CrossQueueSemaphores.size()))
			return m_CrossQueueSemaphores[m_CrossQueueSemaphoreIndex++];

		auto device = GetDevice();
		vk::SemaphoreCreateInfo semInfo{};
		vk::Semaphore sem = device.createSemaphore(semInfo);
		m_CrossQueueSemaphores.push_back(sem);
		m_CrossQueueSemaphoreIndex = static_cast<int>(m_CrossQueueSemaphores.size()) - 1;
		return sem;
	}

	void VulkanFrameBoundResourceManager::CreateFences()
	{
		auto device = GetDevice();
		vk::FenceCreateInfo fenceInfo{};
		m_DirectFence = device.createFence(fenceInfo);
		m_ComputeFence = device.createFence(fenceInfo);
	}

	void VulkanFrameBoundResourceManager::ResetDescriptorPool()
	{
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
			// Only wait for fences if they were actually submitted (previous frame may have aborted early)
			if (resourceManager.IsFenceSubmitted())
			{
				vk::Fence directFence = resourceManager.GetDirectFence();
				if (directFence)
				{
					device.waitForFences(directFence, VK_TRUE, UINT64_MAX);
					device.resetFences(directFence);
				}

				vk::Fence computeFence = resourceManager.GetComputeFence();
				if (computeFence)
				{
					device.waitForFences(computeFence, VK_TRUE, UINT64_MAX);
					device.resetFences(computeFence);
				}
			}

			resourceManager.ResetDescriptorPool();
		}
		else
		{
			m_FirstFrame = false;
			resourceManager.CreateFences();
		}

		resourceManager.Reset();
	}

	void VulkanFrameContext::EnsureWindowSync(uint32_t index)
	{
		while (index >= m_WindowSyncs.size())
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
		auto device = GetDevice();
		for (auto& context : m_FrameContexts)
		{
			auto& resourceManager = context->GetResourceManager();
			vk::Fence directFence = resourceManager.GetDirectFence();
			if (directFence)
			{
				device.waitForFences(directFence, VK_TRUE, UINT64_MAX);
			}
			vk::Fence computeFence = resourceManager.GetComputeFence();
			if (computeFence)
			{
				device.waitForFences(computeFence, VK_TRUE, UINT64_MAX);
			}
		}
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
