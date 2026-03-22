#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <vk_mem_alloc.h>

namespace graphics_backend
{
	class VulkanMemoryManager : public VulkanSubobjectBase
	{
	public:
		VulkanMemoryManager() = default;
		VulkanMemoryManager(VulkanMemoryManager&& other) noexcept = default;
		VulkanMemoryManager& operator=(VulkanMemoryManager&& other) noexcept = default;

		void Init();
		virtual void Release() override;

		VmaAllocator GetAllocator() const { return m_Allocator; }

		// Buffer allocation
		VmaAllocation AllocateBuffer(vk::BufferCreateInfo const& bufferInfo
			, VmaAllocationCreateInfo const& allocInfo
			, vk::Buffer& outBuffer
			, VmaAllocationInfo* pAllocationInfo = nullptr);

		// Image allocation
		VmaAllocation AllocateImage(vk::ImageCreateInfo const& imageInfo
			, VmaAllocationCreateInfo const& allocInfo
			, vk::Image& outImage
			, VmaAllocationInfo* pAllocationInfo = nullptr);

		// Map/Unmap
		void* MapMemory(VmaAllocation allocation);
		void UnmapMemory(VmaAllocation allocation);

		// Free
		void FreeBuffer(vk::Buffer buffer, VmaAllocation allocation);
		void FreeImage(vk::Image image, VmaAllocation allocation);

	private:
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
	};
}
