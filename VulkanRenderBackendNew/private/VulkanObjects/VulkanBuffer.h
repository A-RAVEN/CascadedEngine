#pragma once
#include <GPUBuffer.h>
#include <Utils/VulkanSubobjectBase.h>
#include <vk_mem_alloc.h>

namespace graphics_backend
{
	class VulkanBuffer : public GPUBuffer, public VulkanSubobjectBase
	{
	public:
		VulkanBuffer() = default;
		VulkanBuffer(VulkanBuffer&& other) noexcept = default;
		VulkanBuffer& operator=(VulkanBuffer&& other) noexcept = default;

		void Init(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags);
		virtual void Release() override;

		// GPUBuffer interface
		virtual GPUBufferDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		virtual void SetName(castl::string const& name) override;
		virtual castl::string const& GetName() const override { return m_Name; }

		// Vulkan-specific methods
		vk::Buffer GetBuffer() const { return m_Buffer; }
		VmaAllocation GetAllocation() const { return m_Allocation; }

		// Map/Unmap for CPU-accessible buffers
		void* Map();
		void Unmap();

		// Upload data (uses staging buffer for GPU-only buffers)
		void UploadData(void const* pData, uint64_t size, uint64_t offset = 0);

		// Get current buffer state for barriers
		vk::PipelineStageFlags GetPipelineStageFlags() const { return m_PipelineStageFlags; }
		vk::AccessFlags GetAccessFlags() const { return m_AccessFlags; }

		void SetPipelineStageFlags(vk::PipelineStageFlags flags) { m_PipelineStageFlags = flags; }
		void SetAccessFlags(vk::AccessFlags flags) { m_AccessFlags = flags; }

	private:
		vk::Buffer m_Buffer;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		VmaAllocationInfo m_AllocationInfo{};
		GPUBufferDescriptor m_Descriptor{};
		EBufferUsageFlags m_UsageFlags{};
		castl::string m_Name;

		vk::PipelineStageFlags m_PipelineStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
		vk::AccessFlags m_AccessFlags = vk::AccessFlagBits::eNone;
		void* m_MappedPtr = nullptr;
	};
}
