#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <vk_mem_alloc.h>
#include <CASTL/CAVector.h>

namespace graphics_backend
{
	class VulkanLinearMemoryManager : public VulkanSubobjectBase
	{
	public:
		static constexpr uint64_t DEFAULT_PAGE_SIZE = 64 * 1024 * 1024; // 64MB

		struct StagingAllocation
		{
			vk::Buffer buffer;
			uint64_t offset;
			void* mappedPtr;
			uint64_t size;
		};

		VulkanLinearMemoryManager() = default;
		void Init(uint64_t pageSize = DEFAULT_PAGE_SIZE);
		virtual void Release() override;

		StagingAllocation AllocUploadStagingBuffer(uint64_t size, uint64_t alignment = 256);
		void Reset();

	private:
		struct Page
		{
			vk::Buffer buffer;
			VmaAllocation allocation;
			void* mappedPtr;
			uint64_t size;
			uint64_t currentOffset = 0;
		};

		castl::vector<Page> m_Pages;
		uint64_t m_PageSize = DEFAULT_PAGE_SIZE;

		void AllocatePage();
	};
}
