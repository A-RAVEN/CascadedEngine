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

		CA_LOG_INFO("VulkanCommandListManager initialized successfully");
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
			m_GraphicsCommand = AllocateCommandBuffer(m_GraphicsPool);
		}
		return m_GraphicsCommand;
	}

	vk::CommandBuffer& VulkanCommandListManager::ComputeCommand()
	{
		if (!m_ComputeCommand)
		{
			m_ComputeCommand = AllocateCommandBuffer(m_ComputePool);
		}
		return m_ComputeCommand;
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

		auto cmdBufs = GetDevice().allocateCommandBuffers(allocInfo);
		VK_RESULT_CHECK(cmdBufs.size() > 0 ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY);
		return cmdBufs[0];
	}

	void VulkanCommandListManager::FreeCommandBuffer(vk::CommandPool pool, vk::CommandBuffer cmdBuf)
	{
		GetDevice().freeCommandBuffers(pool, cmdBuf);
	}
}
