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
		m_GraphicsPool = device.createCommandPool(graphicsPoolInfo);

		// Create compute command pool
		vk::CommandPoolCreateInfo computePoolInfo{};
		computePoolInfo.queueFamilyIndex = queueContext.GetComputeQueueFamily();
		computePoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		m_ComputePool = device.createCommandPool(computePoolInfo);

		// Create transfer command pool
		vk::CommandPoolCreateInfo transferPoolInfo{};
		transferPoolInfo.queueFamilyIndex = queueContext.GetTransferQueueFamily();
		transferPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		m_TransferPool = device.createCommandPool(transferPoolInfo);

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

		m_AllocatedCommandBuffers.clear();
		CA_LOG_INFO("VulkanCommandListManager released");
	}

	vk::CommandBuffer& VulkanCommandListManager::GraphicsCommand()
	{
		if (!m_GraphicsCommand)
		{
			fprintf(stderr, "[DIAG] GraphicsCommand: allocating from GraphicsPool\n");
			fflush(stderr);
			m_GraphicsCommand = AllocateCommandBuffer(m_GraphicsPool);
			fprintf(stderr, "[DIAG] GraphicsCommand: allocation done\n");
			fflush(stderr);
		}
		return m_GraphicsCommand;
	}

	vk::CommandBuffer& VulkanCommandListManager::ComputeCommand()
	{
		if (!m_ComputeCommand)
		{
			fprintf(stderr, "[DIAG] ComputeCommand: allocating from ComputePool\n");
			fflush(stderr);
			m_ComputeCommand = AllocateCommandBuffer(m_ComputePool);
			fprintf(stderr, "[DIAG] ComputeCommand: allocation done\n");
			fflush(stderr);
		}
		return m_ComputeCommand;
	}

	vk::CommandBuffer& VulkanCommandListManager::TransferCommand()
	{
		if (!m_TransferCommand)
		{
			fprintf(stderr, "[DIAG] TransferCommand: allocating from TransferPool\n");
			fflush(stderr);
			m_TransferCommand = AllocateCommandBuffer(m_TransferPool);
			fprintf(stderr, "[DIAG] TransferCommand: allocation done\n");
			fflush(stderr);
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
		m_GraphicsCommand = nullptr;
		m_ComputeCommand = nullptr;
		m_TransferCommand = nullptr;
	}

	vk::CommandBuffer VulkanCommandListManager::AllocateCommandBuffer(vk::CommandPool pool)
	{
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = pool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		fprintf(stderr, "[DIAG] AllocateCommandBuffer ENTER pool=%zu\n",
			(size_t)reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(pool)));
		fflush(stderr);

		try
		{
			auto cmdBufs = GetDevice().allocateCommandBuffers(allocInfo);
			fprintf(stderr, "[DIAG] allocateCommandBuffers returned %zu buffers\n", cmdBufs.size());
			fflush(stderr);
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
				fflush(stderr);
			fprintf(stderr, "[DIAG] VK_RESULT_CHECK: empty=%d, size=%zu, cond=%d\n",
				cmdBufs.empty() ? 1 : 0, cmdBufs.size(),
				(!cmdBufs.empty() ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY) == VK_SUCCESS ? 1 : 0);
			fflush(stderr);
			if (!cmdBufs.empty())
			{
				fprintf(stderr, "[DIAG] VK_RESULT_CHECK passed, returning buffer\n");
				fflush(stderr);
				return cmdBufs[0];
			}
			CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers returned empty vector");
			fflush(stdout);
			__debugbreak();
		}
		catch (vk::SystemError const& e)
		{
			fprintf(stderr, "[DIAG] allocateCommandBuffers THREW: %s\n", e.what());
			fflush(stderr);
			CA_LOG_ERR("VulkanCommandListManager: allocateCommandBuffers threw exception: {} (pool={})",
				e.what(), reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(pool)));
			fflush(stdout);
			throw;
		}
		catch (...)
		{
			fprintf(stderr, "[DIAG] allocateCommandBuffers THREW unknown exception\n");
			fflush(stderr);
			throw;
		}
	}

	void VulkanCommandListManager::FreeCommandBuffer(vk::CommandPool pool, vk::CommandBuffer cmdBuf)
	{
		GetDevice().freeCommandBuffers(pool, cmdBuf);
	}
}
