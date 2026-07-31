#include <ResourceManagement/VulkanResourceAliasing.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <algorithm>

namespace graphics_backend
{
	void VulkanResourceAliasing::Init()
	{
		m_ResourceLifetimes.clear();
		m_AliasedAllocations.clear();
		m_TotalAliasedSize = 0;
		m_TotalUnaliasedSize = 0;
		CA_LOG_INFO("VulkanResourceAliasing initialized");
	}

	void VulkanResourceAliasing::Release()
	{
		FreeAliasedPool();
		DestroyVirtualBlocks();
		m_ResourceLifetimes.clear();
		m_AliasedAllocations.clear();
		CA_LOG_INFO("VulkanResourceAliasing released");
	}

	void VulkanResourceAliasing::RegisterResource(uint64_t resourceId, ResourceLifetime const& lifetime)
	{
		m_ResourceLifetimes[resourceId] = lifetime;
		m_TotalUnaliasedSize += lifetime.size;
}

void VulkanResourceAliasing::ExtendResourceLifetime(uint64_t resourceId, uint32_t startBatch, uint32_t endBatch)
{
	auto it = m_ResourceLifetimes.find(resourceId);
	if (it != m_ResourceLifetimes.end())
	{
		it->second.startBatch = castl::min(it->second.startBatch, startBatch);
		it->second.endBatch = castl::max(it->second.endBatch, endBatch);
	}
	}

	void VulkanResourceAliasing::AnalyzeAndPlanAliasing()
	{
		if (m_ResourceLifetimes.empty())
			return;

		// Sort resources by start time
		castl::vector<castl::pair<uint64_t, ResourceLifetime>> sortedResources;
		sortedResources.reserve(m_ResourceLifetimes.size());
		for (auto const& [id, lifetime] : m_ResourceLifetimes)
		{
			sortedResources.push_back({ id, lifetime });
		}

		castl::sort(sortedResources.begin(), sortedResources.end(),
			[](auto const& a, auto const& b) {
				return a.second.startBatch < b.second.startBatch;
			});

		// Greedy interval scheduling for aliasing
		struct ActiveGroup
		{
			uint64_t currentOffset;
			uint32_t endBatch;
		};

		castl::vector<ActiveGroup> activeGroups;

		for (auto const& [resourceId, lifetime] : sortedResources)
		{
			// Align offset
			uint64_t alignedOffset = 0;
			bool foundGroup = false;

			// Try to find a non-overlapping group
			for (auto& group : activeGroups)
			{
				if (group.endBatch < lifetime.startBatch)
				{
					// Can reuse this group's memory
					alignedOffset = group.currentOffset;
					alignedOffset = (alignedOffset + lifetime.alignment - 1) & ~(lifetime.alignment - 1);
					group.currentOffset = alignedOffset + lifetime.size;
					group.endBatch = lifetime.endBatch;
					foundGroup = true;
					break;
				}
			}

			if (!foundGroup)
			{
				// Create new group: only consider groups whose lifetime overlaps with this resource
				uint64_t maxOffset = 0;
				for (auto const& group : activeGroups)
				{
					if (group.endBatch >= lifetime.startBatch) // overlapping → contributes to peak
						maxOffset = castl::max(maxOffset, group.currentOffset);
				}
				alignedOffset = (maxOffset + lifetime.alignment - 1) & ~(lifetime.alignment - 1);
				activeGroups.push_back({ alignedOffset + lifetime.size, lifetime.endBatch });
			}

			// Store allocation info
			AliasedAllocation alloc{};
			alloc.allocation = m_AliasedPoolAllocation;
			alloc.mappedPtr = m_AliasedPoolMappedPtr;
			alloc.offset = alignedOffset;
			alloc.size = lifetime.size;
			m_AliasedAllocations[resourceId] = alloc;

			// Update total size
			m_TotalAliasedSize = castl::max(m_TotalAliasedSize, alignedOffset + lifetime.size);
		}

		CA_LOG_INFO("VulkanResourceAliasing: Analyzed {} resources, total aliased size: {}, unaliased would be: {}"
			, m_ResourceLifetimes.size(), m_TotalAliasedSize, m_TotalUnaliasedSize);
	}

	AliasedAllocation VulkanResourceAliasing::GetAliasedAllocation(uint64_t resourceId) const
	{
		auto it = m_AliasedAllocations.find(resourceId);
		if (it != m_AliasedAllocations.end())
		{
			return it->second;
		}
		return {};
	}

	void VulkanResourceAliasing::UpdateAliasedAllocationsMap()
	{
		uint32_t updatedCount = 0;
		for (auto& [resourceId, alloc] : m_AliasedAllocations)
		{
			alloc.allocation = m_AliasedPoolAllocation;
			alloc.mappedPtr = m_AliasedPoolMappedPtr;
			alloc.deviceMemory = m_AliasedPoolDeviceMemory;
			++updatedCount;
		}
		CA_LOG_INFO("VulkanResourceAliasing: Updated {} aliased allocations with pool handle", updatedCount);
	}

	void VulkanResourceAliasing::ReplanWithRealAlignment(castl::unordered_map<uint64_t, VkMemoryRequirements> const& realMemReqs)
	{
		for (auto& [resourceId, lifetime] : m_ResourceLifetimes)
		{
			auto it = realMemReqs.find(resourceId);
			if (it != realMemReqs.end())
			{
				lifetime.alignment = it->second.alignment;
				lifetime.size = it->second.size;
			}
		}

		// Clear previous plan and re-run scheduling with corrected sizes/alignments
		m_AliasedAllocations.clear();
		m_TotalAliasedSize = 0;
		AnalyzeAndPlanAliasing();

		CA_LOG_INFO("VulkanResourceAliasing: Replanned with real alignment, total aliased size: {}", m_TotalAliasedSize);
	}

	void VulkanResourceAliasing::UpdateAliasedAllocationForLateResource(uint64_t resourceId, AliasedAllocation const& alloc, uint64_t alignedSize)
	{
		m_AliasedAllocations[resourceId] = alloc;
		m_AliasedAllocations[resourceId].deviceMemory = m_AliasedPoolDeviceMemory;
		m_TotalAliasedSize = alloc.offset + alignedSize;
	}

	bool VulkanResourceAliasing::AllocateAliasedPool(uint64_t totalSize, uint32_t memoryTypeBits)
	{
		if (totalSize == 0)
			return true;

		// Validate against device limits to prevent oversized allocation
		// Validate against conservative size limit (maxMemoryAllocationSize is in
		// VkPhysicalDeviceMaintenance3Properties, queried via pNext chain — skip for now)
		constexpr VkDeviceSize kMaxSafePoolSize = VkDeviceSize(256) * 1024 * 1024;
		if (totalSize > kMaxSafePoolSize)
		{
			CA_LOG_WARN("VulkanResourceAliasing: Pool size {} exceeds conservative limit {}; allocation may fail",
				totalSize, kMaxSafePoolSize);
		}

		auto& memoryManager = GetApp()->GetMemoryManager();

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
		allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkMemoryRequirements memReq{};
		memReq.size = totalSize;
		memReq.alignment = 256;
		memReq.memoryTypeBits = memoryTypeBits;

		VmaAllocationInfo allocInfoOut{};
		m_AliasedPoolAllocation = memoryManager.AllocateMemory(memReq, allocInfo, &allocInfoOut);
		if (m_AliasedPoolAllocation == VK_NULL_HANDLE)
		{
			CA_LOG_ERR("VulkanResourceAliasing: Failed to allocate aliased pool of size {}", totalSize);
			return false;
		}

		// VMA_ALLOCATION_CREATE_MAPPED_BIT guarantees pMappedData is filled when HOST_VISIBLE
		m_AliasedPoolMappedPtr = allocInfoOut.pMappedData;
		m_AliasedPoolDeviceMemory = allocInfoOut.deviceMemory;
		m_AliasedPoolMemoryType = allocInfoOut.memoryType;
		if (!m_AliasedPoolMappedPtr)
		{
			CA_LOG_ERR("VulkanResourceAliasing: Aliased pool allocated but pMappedData is null (unexpected)");
			memoryManager.FreeMemory(m_AliasedPoolAllocation);
			m_AliasedPoolAllocation = VK_NULL_HANDLE;
			return false;
		}

		m_TotalAliasedSize = totalSize;
		CA_LOG_INFO("VulkanResourceAliasing: Allocated aliased pool of size {}", totalSize);
		return true;
	}

	void VulkanResourceAliasing::FreeAliasedPool()
	{
		if (m_AliasedPoolAllocation == VK_NULL_HANDLE)
			return;

		auto& memoryManager = GetApp()->GetMemoryManager();

		// VMA_ALLOCATION_CREATE_MAPPED_BIT persistent mapping is released by FreeMemory
		memoryManager.FreeMemory(m_AliasedPoolAllocation);
		m_AliasedPoolAllocation = VK_NULL_HANDLE;
		m_AliasedPoolDeviceMemory = VK_NULL_HANDLE;
		m_AliasedPoolMemoryType = 0;
		m_AliasedPoolMappedPtr = nullptr;

		m_AliasedAllocations.clear();
		m_TotalAliasedSize = 0;
		CA_LOG_INFO("VulkanResourceAliasing: Freed aliased pool");
	}

	bool VulkanResourceAliasing::LifetimesOverlap(ResourceLifetime const& a, ResourceLifetime const& b) const
	{
		return !(a.endBatch < b.startBatch || b.endBatch < a.startBatch);
	}

	float VulkanResourceAliasing::GetAliasingEfficiency() const
	{
		if (m_TotalUnaliasedSize == 0)
			return 1.0f;
		return static_cast<float>(m_TotalAliasedSize) / static_cast<float>(m_TotalUnaliasedSize);
	}

	// ===== Phase 1: Virtual Block Allocation =====

	bool VulkanResourceAliasing::CreateVirtualBlocks(uint64_t blockSize)
	{
		m_DefaultBlockSize = blockSize;
		auto deviceProps = GetPhysicalDevice().getProperties();

		// Create VirtualBlock per memory type (simplified: one for buffers, one for images)
		// In production, query VkPhysicalDeviceMemoryProperties for exact memoryTypeBits per resource type
		for (uint32_t memType = 0; memType < 2; ++memType)
		{
			VirtualBlockPool pool;
			pool.memoryTypeIndex = memType;
			pool.isBufferPool = (memType == 0);

			VmaVirtualBlockCreateInfo vbInfo{};
			vbInfo.size = blockSize;
			// Buddy algorithm (default) — supports individual vmaVirtualFree for per-batch lifecycle
			vbInfo.flags = 0;

			VkResult result = vmaCreateVirtualBlock(&vbInfo, &pool.virtualBlock);
			if (result != VK_SUCCESS)
			{
				CA_LOG_ERR("VulkanResourceAliasing: Failed to create VirtualBlock for memoryType {}: {}",
					memType, static_cast<int>(result));
				return false;
			}

			m_BlockPools.push_back(pool);
			CA_LOG_INFO("VulkanResourceAliasing: Created VirtualBlock for memoryType {} (size={}MB, isBuffer={})",
				memType, blockSize / (1024 * 1024), pool.isBufferPool);
		}

		return true;
	}

	void VulkanResourceAliasing::DestroyVirtualBlocks()
	{
		for (auto& pool : m_BlockPools)
		{
			if (pool.virtualBlock)
			{
				vmaDestroyVirtualBlock(pool.virtualBlock);
				pool.virtualBlock = VK_NULL_HANDLE;
			}
			if (pool.physicalAllocation)
			{
				GetApp()->GetMemoryManager().FreeMemory(pool.physicalAllocation);
				pool.physicalAllocation = VK_NULL_HANDLE;
			}
		}
		m_BlockPools.clear();
		m_ActiveVirtualAllocs.clear();
		CA_LOG_INFO("VulkanResourceAliasing: Destroyed all VirtualBlocks");
	}

	bool VulkanResourceAliasing::AllocResource(uint64_t resourceId, uint64_t size, uint64_t alignment,
		uint32_t startBatch, uint32_t endBatch, bool isBuffer,
		uint64_t& outOffset, uint32_t& outBlockIndex)
	{
		// Select pool: buffer → index 0, image → index 1
		uint32_t poolIndex = isBuffer ? 0u : 1u;
		if (poolIndex >= m_BlockPools.size())
			return false;

		auto& pool = m_BlockPools[poolIndex];

		VmaVirtualAllocationCreateInfo allocInfo{};
		allocInfo.size = size;
		allocInfo.alignment = alignment;

		VmaVirtualAllocation va;
		VkResult result = vmaVirtualAllocate(pool.virtualBlock, &allocInfo, &va, &outOffset);
		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("VulkanResourceAliasing: Virtual allocation failed for resource {}: {}",
				resourceId, static_cast<int>(result));
			return false;
		}

		VirtualResourceAllocation alloc{};
		alloc.virtualAlloc = va;
		alloc.offset = outOffset;
		alloc.size = size;
		alloc.endBatch = endBatch;
		alloc.blockIndex = poolIndex;
		m_ActiveVirtualAllocs[resourceId] = alloc;
		outBlockIndex = poolIndex;
		return true;
	}

	void VulkanResourceAliasing::FreeResourcesUpToBatch(uint32_t batchIndex)
	{
		castl::vector<uint64_t> toFree;
		for (auto const& [resourceId, alloc] : m_ActiveVirtualAllocs)
		{
			if (alloc.endBatch < batchIndex)
				toFree.push_back(resourceId);
		}

		for (uint64_t resourceId : toFree)
		{
			auto it = m_ActiveVirtualAllocs.find(resourceId);
			if (it == m_ActiveVirtualAllocs.end())
				continue;

			auto& alloc = it->second;
			if (alloc.blockIndex < m_BlockPools.size() && m_BlockPools[alloc.blockIndex].virtualBlock)
			{
				vmaVirtualFree(m_BlockPools[alloc.blockIndex].virtualBlock, alloc.virtualAlloc);
			}
			m_ActiveVirtualAllocs.erase(it);
		}
	}

	uint64_t VulkanResourceAliasing::GetPeakVirtualUsage(uint32_t memoryTypeIndex) const
	{
		if (memoryTypeIndex >= m_BlockPools.size() || !m_BlockPools[memoryTypeIndex].virtualBlock)
			return 0;

		VmaStatistics stats{};
		vmaGetVirtualBlockStatistics(m_BlockPools[memoryTypeIndex].virtualBlock, &stats);
		// Return total allocated bytes within the virtual block
		return stats.allocationBytes;
	}

	// ===== Phase 2: Physical Commit =====

	bool VulkanResourceAliasing::CommitVirtualAllocations()
	{
		auto& memoryManager = GetApp()->GetMemoryManager();

		for (auto& pool : m_BlockPools)
		{
			VmaStatistics stats{};
			vmaGetVirtualBlockStatistics(pool.virtualBlock, &stats);
			uint64_t peakSize = stats.allocationBytes;
			if (peakSize == 0)
				continue;

			// If existing physical allocation is large enough, reuse it
			if (pool.physicalAllocation != VK_NULL_HANDLE && pool.physicalSize >= peakSize)
				continue;

			// Free old physical allocation if resizing
			if (pool.physicalAllocation != VK_NULL_HANDLE)
			{
				CA_LOG_INFO("VulkanResourceAliasing: Resizing physical allocation for block {}: {} → {}",
					pool.memoryTypeIndex, pool.physicalSize, peakSize);
				memoryManager.FreeMemory(pool.physicalAllocation);
				pool.physicalAllocation = VK_NULL_HANDLE;
			}

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
			allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VkMemoryRequirements memReq{};
			memReq.size = peakSize;
			memReq.alignment = 256;
			memReq.memoryTypeBits = 1u << pool.memoryTypeIndex;

			VmaAllocationInfo allocInfoOut{};
			pool.physicalAllocation = memoryManager.AllocateMemory(memReq, allocInfo, &allocInfoOut);
			if (pool.physicalAllocation == VK_NULL_HANDLE)
			{
				CA_LOG_ERR("VulkanResourceAliasing: Failed to commit physical memory for block {} (size={})",
					pool.memoryTypeIndex, peakSize);
				return false;
			}

			pool.deviceMemory = allocInfoOut.deviceMemory;
			pool.mappedPtr = allocInfoOut.pMappedData;
			pool.physicalSize = peakSize;
			CA_LOG_INFO("VulkanResourceAliasing: Committed physical memory for block {}: {} bytes",
				pool.memoryTypeIndex, peakSize);
		}

		return true;
	}

	void VulkanResourceAliasing::ResetVirtualBlocks()
	{
		for (auto& pool : m_BlockPools)
		{
			if (pool.virtualBlock)
			{
				vmaClearVirtualBlock(pool.virtualBlock);
			}
		}
		m_ActiveVirtualAllocs.clear();
		// Physical allocations preserved across frames
	}

	VirtualBlockPool const* VulkanResourceAliasing::GetBlockPool(uint32_t blockIndex) const
	{
		if (blockIndex < m_BlockPools.size())
			return &m_BlockPools[blockIndex];
		return nullptr;
	}
}