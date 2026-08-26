#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <vk_mem_alloc.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class VulkanMemoryManager : public VulkanSubobjectBase
	{
	public:
		VulkanMemoryManager() = default;
		// Owns a VMA allocator + the L3a allocation table. It must never be moved: a default move
		// would leave the moved-from m_Allocator non-null, so both objects would vmaDestroyAllocator
		// the same allocator (double-destroy). The backend holds it as a value member created by the
		// module factory (new/delete), so it is never moved.
		VulkanMemoryManager(VulkanMemoryManager&& other) noexcept = delete;
		VulkanMemoryManager& operator=(VulkanMemoryManager&& other) noexcept = delete;

		void Init();
		virtual void Release() override;

		VmaAllocator GetAllocator() const { return m_Allocator; }

		// Buffer allocation
		VmaAllocation AllocateBuffer(vk::BufferCreateInfo const& bufferInfo
			, VmaAllocationCreateInfo const& allocInfo
			, vk::Buffer& outBuffer
			, VmaAllocationInfo* pAllocationInfo = nullptr
			, VulkanSubobjectBase* owner = nullptr);

		// Image allocation
		VmaAllocation AllocateImage(vk::ImageCreateInfo const& imageInfo
			, VmaAllocationCreateInfo const& allocInfo
			, vk::Image& outImage
			, VmaAllocationInfo* pAllocationInfo = nullptr
			, VulkanSubobjectBase* owner = nullptr);

		// Raw memory allocation (for aliasing pools, etc.)
		VmaAllocation AllocateMemory(VkMemoryRequirements const& memReq
			, VmaAllocationCreateInfo const& allocInfo
			, VmaAllocationInfo* pAllocationInfo = nullptr
			, VulkanSubobjectBase* owner = nullptr);
		void FreeMemory(VmaAllocation allocation);

		// Map/Unmap
		void* MapMemory(VmaAllocation allocation);
		void UnmapMemory(VmaAllocation allocation);

		// Query allocation info (deviceMemory, offset, size, pMappedData)
		void GetAllocationInfo(VmaAllocation allocation, VmaAllocationInfo* outInfo) const
		{
			vmaGetAllocationInfo(m_Allocator, allocation, outInfo);
		}

		// Free
		void FreeBuffer(vk::Buffer buffer, VmaAllocation allocation);
		void FreeImage(vk::Image image, VmaAllocation allocation);

	private:
		// L3a (design D5, [AUDIT-R5-1]): register every live VMA allocation so backend
		// teardown can sweep any leftover before vmaDestroyAllocator. "Is this freed?" is
		// answered by presence in this table, NOT by a member flag on the owning object —
		// every release path (L1 deleter → VulkanX::Release → Free*, and L3a sweep →
		// owner->Release() → Free*) goes through the same idempotent Free*, so double-free
		// is structurally impossible.
		enum class Kind { Buffer, Image, Memory };
		struct AllocRecord
		{
			Kind kind;
			vk::Buffer buffer;
			vk::Image image;
			VulkanSubobjectBase* owner; // null == ownerless (aliased pool / raw / graph-local)
		};
		castl::unordered_map<VmaAllocation, AllocRecord> m_Allocations;

		VmaAllocator m_Allocator = VK_NULL_HANDLE;
	};
}
