#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class VulkanCommandListManager : public VulkanSubobjectBase
	{
	public:
		VulkanCommandListManager() = default;
		VulkanCommandListManager(VulkanCommandListManager&& other) noexcept = default;
		VulkanCommandListManager& operator=(VulkanCommandListManager&& other) noexcept = default;

		void Init();
		virtual void Release() override;

		// Get command buffer for graphics queue
		vk::CommandBuffer& GraphicsCommand();
		vk::CommandBuffer& ComputeCommand();
		vk::CommandBuffer& TransferCommand();

		// Begin/End command buffer
		void BeginCommandBuffer(vk::CommandBuffer cmdBuf);
		void EndCommandBuffer(vk::CommandBuffer cmdBuf);

		// Reset all pools for new frame
		void Reset();

		// Allocate a new command buffer
		vk::CommandBuffer AllocateCommandBuffer(vk::CommandPool pool);
		void FreeCommandBuffer(vk::CommandPool pool, vk::CommandBuffer cmdBuf);

		vk::CommandPool GetGraphicsPool() const { return m_GraphicsPool; }
		vk::CommandPool GetComputePool() const { return m_ComputePool; }
		vk::CommandPool GetTransferPool() const { return m_TransferPool; }

	private:
		vk::CommandPool m_GraphicsPool;
		vk::CommandPool m_ComputePool;
		vk::CommandPool m_TransferPool;

		vk::CommandBuffer m_GraphicsCommand;
		vk::CommandBuffer m_ComputeCommand;
		vk::CommandBuffer m_TransferCommand;

		castl::vector<vk::CommandBuffer> m_AllocatedCommandBuffers;
	};
}
