#include <ResourceManagement/VulkanLinearMemoryManager.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>

namespace graphics_backend
{
	void VulkanLinearMemoryManager::Init(uint64_t pageSize)
	{
		m_PageSize = pageSize;
		m_Pages.clear();
	}

	void VulkanLinearMemoryManager::Release()
	{
		auto device = GetDevice();
		auto& memoryManager = GetApp()->GetMemoryManager();
		auto allocator = memoryManager.GetAllocator();

		for (auto& page : m_Pages)
		{
			if (page.buffer)
			{
				device.destroyBuffer(page.buffer);
			}
			if (page.allocation)
			{
				vmaFreeMemory(allocator, page.allocation);
			}
		}
		m_Pages.clear();
	}

	void VulkanLinearMemoryManager::AllocatePage()
	{
		auto device = GetDevice();
		auto& memoryManager = GetApp()->GetMemoryManager();
		auto allocator = memoryManager.GetAllocator();

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = m_PageSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
		allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer vkBuffer;
		VmaAllocation allocation;
		VmaAllocationInfo allocResultInfo{};

		VkBufferCreateInfo vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
		VkResult result = vmaCreateBuffer(allocator, &vkBufferInfo, &allocInfo,
			&vkBuffer, &allocation, &allocResultInfo);

		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("VulkanLinearMemoryManager: Failed to allocate page of {} bytes", m_PageSize);
			return;
		}

		try
		{
			Page page;
			page.buffer = vk::Buffer(vkBuffer);
			page.allocation = allocation;
			page.mappedPtr = allocResultInfo.pMappedData;
			page.size = m_PageSize;
			page.currentOffset = 0;
			m_Pages.push_back(page);
		}
		catch (...)
		{
			vmaDestroyBuffer(allocator, vkBuffer, allocation);
			CA_LOG_ERR("VulkanLinearMemoryManager: AllocatePage push_back failed, destroying buffer");
			throw;
		}
	}

	VulkanLinearMemoryManager::StagingAllocation VulkanLinearMemoryManager::AllocUploadStagingBuffer(
		uint64_t size, uint64_t alignment)
	{
		// Try to allocate from the last page first
		if (!m_Pages.empty())
		{
			Page& lastPage = m_Pages.back();
			uint64_t alignedOffset = (lastPage.currentOffset + alignment - 1) & ~(alignment - 1);

			if (alignedOffset + size <= lastPage.size)
			{
				lastPage.currentOffset = alignedOffset + size;
				return {
					lastPage.buffer,
					alignedOffset,
					static_cast<uint8_t*>(lastPage.mappedPtr) + alignedOffset,
					size
				};
			}
		}

		// Need a new page
		// F18: reject oversized allocations BEFORE allocating a fresh page — a new
		// (up to 64MB) page would otherwise sit in m_Pages permanently unused.
		if (size > m_PageSize)
		{
			CA_LOG_ERR("VulkanLinearMemoryManager: Allocation size {} exceeds page size {}, cannot allocate", size, m_PageSize);
			return {};
		}

		// F18a: AllocatePage() can fail silently (VMA error). If the previous page was
		// full, falling through to m_Pages.back() would return a STALE allocation
		// reusing already-consumed staging memory — return failure instead.
		size_t pageCountBefore = m_Pages.size();
		AllocatePage();
		if (m_Pages.size() <= pageCountBefore)
		{
			CA_LOG_ERR("VulkanLinearMemoryManager: AllocatePage failed (VMA), allocation of {} bytes aborted", size);
			return {};
		}

		Page& newPage = m_Pages.back();
		uint64_t alignedOffset = 0;

		newPage.currentOffset = alignedOffset + size;
		return {
			newPage.buffer,
			alignedOffset,
			static_cast<uint8_t*>(newPage.mappedPtr) + alignedOffset,
			size
		};
	}

	void VulkanLinearMemoryManager::Reset()
	{
		// F19 PRECONDITION: rewinding page offsets reuses staging regions that GPU work
		// may still reference. This is safe ONLY because the caller guarantees GPU
		// completion before Reset() — VulkanFrameContext::Aquire() runs after
		// SubmitBatches' per-batch waitForFences (single frame in flight). If async
		// frames-in-flight are introduced, per-page fence tracking MUST be added here.
		// Trim old pages: AllocUploadStagingBuffer only probes m_Pages.back(),
		// so pages before the last are dead weight. Keep at least 1 page.
		if (m_Pages.size() > 1)
		{
			auto device = GetDevice();
			auto& memoryManager = GetApp()->GetMemoryManager();
			auto allocator = memoryManager.GetAllocator();

			for (size_t i = 0; i + 1 < m_Pages.size(); ++i)
			{
				if (m_Pages[i].buffer)
					device.destroyBuffer(m_Pages[i].buffer);
				if (m_Pages[i].allocation)
					vmaFreeMemory(allocator, m_Pages[i].allocation);
			}

			Page lastPage = m_Pages.back();
			m_Pages.clear();
			m_Pages.push_back(lastPage);
		}

		for (auto& page : m_Pages)
		{
			page.currentOffset = 0;
		}
	}
}
