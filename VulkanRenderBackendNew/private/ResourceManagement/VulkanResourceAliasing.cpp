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
		m_ResourceLifetimes.clear();
		m_AliasedAllocations.clear();
		CA_LOG_INFO("VulkanResourceAliasing released");
	}

	void VulkanResourceAliasing::RegisterResource(uint64_t resourceId, ResourceLifetime const& lifetime)
	{
		m_ResourceLifetimes[resourceId] = lifetime;
		m_TotalUnaliasedSize += lifetime.size;
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
				// Create new group
				uint64_t maxOffset = 0;
				for (auto const& group : activeGroups)
				{
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

	bool VulkanResourceAliasing::AllocateAliasedPool(uint64_t totalSize)
	{
		if (totalSize == 0)
			return true;

		auto& memoryManager = GetApp()->GetMemoryManager();

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkMemoryRequirements memReq{};
		memReq.size = totalSize;
		memReq.alignment = 256;
		memReq.memoryTypeBits = 0xFFFFFFFF;

		m_AliasedPoolAllocation = memoryManager.AllocateMemory(memReq, allocInfo);
		if (m_AliasedPoolAllocation == VK_NULL_HANDLE)
		{
			CA_LOG_ERR("VulkanResourceAliasing: Failed to allocate aliased pool of size {}", totalSize);
			return false;
		}

		m_AliasedPoolMappedPtr = memoryManager.MapMemory(m_AliasedPoolAllocation);
		if (!m_AliasedPoolMappedPtr)
		{
			CA_LOG_ERR("VulkanResourceAliasing: Failed to map aliased pool");
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

		if (m_AliasedPoolMappedPtr)
		{
			memoryManager.UnmapMemory(m_AliasedPoolAllocation);
			m_AliasedPoolMappedPtr = nullptr;
		}

		memoryManager.FreeMemory(m_AliasedPoolAllocation);
		m_AliasedPoolAllocation = VK_NULL_HANDLE;

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
}