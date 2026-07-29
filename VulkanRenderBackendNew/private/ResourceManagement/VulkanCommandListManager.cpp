#include <ResourceManagement/VulkanCommandListManager.h>
#include <RenderBackend_Vulkan.h>
#include <VulkanQueue/QueueContext.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	void VulkanCommandListManager::Init()
	{
		auto device = GetDevice();
		auto const& queueContext = GetQueueContext();

		// Create graphics command pool
		vk::CommandPoolCreateInfo graphicsPoolInfo{};
		graphicsPoolInfo.queueFamilyIndex = queueContext.GetGraphicsQueueFamily();
		graphicsPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		try { m_GraphicsPool = device.createCommandPool(graphicsPoolInfo); }
		catch (vk::SystemError const& e) {
			CA_LOG_ERR("VulkanCommandListManager: Failed to create graphics command pool: {}", e.what());
			return;
		}

		// Create compute command pool
		vk::CommandPoolCreateInfo computePoolInfo{};
		computePoolInfo.queueFamilyIndex = queueContext.GetComputeQueueFamily();
		computePoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		try { m_ComputePool = device.createCommandPool(computePoolInfo); }
		catch (vk::SystemError const& e) {
			CA_LOG_ERR("VulkanCommandListManager: Failed to create compute command pool: {}", e.what());
			device.destroyCommandPool(m_GraphicsPool);
			m_GraphicsPool = nullptr;
			return;
		}

		// Create transfer command pool
		vk::CommandPoolCreateInfo transferPoolInfo{};
		transferPoolInfo.queueFamilyIndex = queueContext.GetTransferQueueFamily();
		transferPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		try { m_TransferPool = device.createCommandPool(transferPoolInfo); }
		catch (vk::SystemError const& e) {
			CA_LOG_ERR("VulkanCommandListManager: Failed to create transfer command pool: {}", e.what());
			device.destroyCommandPool(m_ComputePool);
			m_ComputePool = nullptr;
			device.destroyCommandPool(m_GraphicsPool);
			m_GraphicsPool = nullptr;
			return;
		}

		CA_LOG_INFO("VulkanCommandListManager initialized successfully: "
			"GraphicsPool={} (qf={}), ComputePool={} (qf={}), TransferPool={} (qf={})",
			reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_GraphicsPool)), queueContext.GetGraphicsQueueFamily(),
			reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_ComputePool)), queueContext.GetComputeQueueFamily(),
			reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_TransferPool)), queueContext.GetTransferQueueFamily());
	}

	void VulkanCommandListManager::Release()
	{
		auto device = GetDevice();

		if (m_GraphicsPool)
		{
			device.destroyCommandPool(m_GraphicsPool);
			m_GraphicsPool = nullptr;
		}
		if (m_ComputePool)
		{
			device.destroyCommandPool(m_ComputePool);
			m_ComputePool = nullptr;
		}
		if (m_TransferPool)
		{
			device.destroyCommandPool(m_TransferPool);
			m_TransferPool = nullptr;
		}

		m_AllocatedGraphicsCmdBufs.clear();
		m_AllocatedComputeCmdBufs.clear();
		CA_LOG_INFO("VulkanCommandListManager released");
	}

	vk::CommandBuffer VulkanCommandListManager::GraphicsCommand()
	{
		auto cmdBuf = AllocateCommandBuffer(m_GraphicsPool);
		m_AllocatedGraphicsCmdBufs.push_back(cmdBuf);
		return cmdBuf;
	}

	vk::CommandBuffer VulkanCommandListManager::ComputeCommand()
	{
		auto cmdBuf = AllocateCommandBuffer(m_ComputePool);
		m_AllocatedComputeCmdBufs.push_back(cmdBuf);
		return cmdBuf;
	}

	vk::CommandBuffer& VulkanCommandListManager::TransferCommand()
	{
		if (!m_TransferCommand)
		{
			m_TransferCommand = AllocateCommandBuffer(m_TransferPool);
		}
		return m_TransferCommand;
	}

	void VulkanCommandListManager::BeginCommandBuffer(vk::CommandBuffer cmdBuf)
	{
		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		cmdBuf.begin(beginInfo);
	}

	void VulkanCommandListManager::EndCommandBuffer(vk::CommandBuffer cmdBuf)
	{
		cmdBuf.end();
	}

	void VulkanCommandListManager::Reset()
	{
		auto device = GetDevice();
		// Free per-call allocated command buffers before resetting pools
		if (m_GraphicsPool && !m_AllocatedGraphicsCmdBufs.empty())
		{
			device.freeCommandBuffers(m_GraphicsPool, m_AllocatedGraphicsCmdBufs);
			m_AllocatedGraphicsCmdBufs.clear();
		}
		if (m_ComputePool && !m_AllocatedComputeCmdBufs.empty())
		{
			device.freeCommandBuffers(m_ComputePool, m_AllocatedComputeCmdBufs);
			m_AllocatedComputeCmdBufs.clear();
		}
		if (m_GraphicsPool)
		{
			device.resetCommandPool(m_GraphicsPool);
		}
		if (m_ComputePool)
		{
			device.resetCommandPool(m_ComputePool);
		}
		if (m_TransferPool)
		{
			device.resetCommandPool(m_TransferPool);
		}
		m_TransferCommand = nullptr;
	}

	vk::CommandBuffer VulkanCommandListManager::AllocateCommandBuffer(vk::CommandPool pool)
	{
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = pool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		try
		{
			auto cmdBufs = GetDevice().allocateCommandBuffers(allocInfo);
			if (cmdBufs.empty())
			{
				CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers returned empty vector, pool={}",
					reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(pool)));
				CA_LOG_ERR("  GraphicsPool={}, ComputePool={}, TransferPool={}",
					reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_GraphicsPool)),
					reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_ComputePool)),
					reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(m_TransferPool)));
				fflush(stdout);
			}
			if (!cmdBufs.empty())
			{
				return cmdBufs[0];
			}
			CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers returned empty vector");
			fflush(stdout);
			__debugbreak();
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers threw exception: {} (pool={})",
				e.what(), reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(pool)));
			fflush(stdout);
			throw;
		}
		catch (...)
		{
			throw;
		}
	}

	void VulkanCommandListManager::FreeCommandBuffer(vk::CommandPool pool, vk::CommandBuffer cmdBuf)
	{
		GetDevice().freeCommandBuffers(pool, cmdBuf);
	}
}
