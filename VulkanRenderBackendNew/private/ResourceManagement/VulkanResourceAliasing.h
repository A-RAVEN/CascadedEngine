#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
#include <vk_mem_alloc.h>

namespace graphics_backend
{
	// Resource lifetime interval
	struct ResourceLifetime
	{
		uint32_t startBatch;
		uint32_t endBatch;
		uint64_t size;
		uint64_t alignment;
		bool isBuffer; // true = buffer, false = image

		auto operator<=>(ResourceLifetime const& other) const = default;
	};

	// Aliased memory allocation
	struct AliasedAllocation
	{
		VmaAllocation allocation;
		void* mappedPtr;
		uint64_t offset;
		uint64_t size;
		VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
	};

	// VirtualBlock pool — maps D3D12 VirtualBlock concept
	struct VirtualBlockPool
	{
		VmaVirtualBlock virtualBlock = VK_NULL_HANDLE;
		VmaAllocation physicalAllocation = VK_NULL_HANDLE; // Phase 2 physical memory (cross-frame reuse)
		VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
		void* mappedPtr = nullptr;
		// F16a: VmaAllocationInfo.offset of the physical allocation inside its
		// VkDeviceMemory block (NOT always 0) — bindings must use deviceMemory +
		// (blockOffset + virtAlloc.offset); mappedPtr already includes blockOffset.
		uint64_t blockOffset = 0;
		uint64_t physicalSize = 0;
		uint32_t memoryTypeIndex = 0;
		bool isBufferPool = true;
	};

	// Tracked virtual allocation within a VirtualBlock
	struct VirtualResourceAllocation
	{
		VmaVirtualAllocation virtualAlloc = VK_NULL_HANDLE;
		uint64_t offset = 0;
		uint64_t size = 0;
		uint32_t endBatch = 0;
		uint32_t blockIndex = 0;
	};

	class VulkanResourceAliasing : public VulkanSubobjectBase
	{
	public:
		VulkanResourceAliasing() = default;
		~VulkanResourceAliasing() = default;

		void Init();
		virtual void Release() override;

		// === Existing single-pool API (backward compat during refactor) ===
		void RegisterResource(uint64_t resourceId, ResourceLifetime const& lifetime);
	// Extend resource lifetime (called by MarkResourceUse)
	void ExtendResourceLifetime(uint64_t resourceId, uint32_t startBatch, uint32_t endBatch);
		void AnalyzeAndPlanAliasing();
		AliasedAllocation GetAliasedAllocation(uint64_t resourceId) const;
		bool AllocateAliasedPool(uint64_t totalSize, uint32_t memoryTypeBits = 0xFFFFFFFF);
		void FreeAliasedPool();
		void UpdateAliasedAllocationsMap();
		void ReplanWithRealAlignment(castl::unordered_map<uint64_t, VkMemoryRequirements> const& realMemReqs);
		void UpdateAliasedAllocationForLateResource(uint64_t resourceId, AliasedAllocation const& alloc, uint64_t alignedSize);

		// === Phase 1: Virtual allocation (VmaVirtualBlock) ===
		bool CreateVirtualBlocks(uint64_t blockSize = 256 * 1024 * 1024);
		void DestroyVirtualBlocks();
		bool AllocResource(uint64_t resourceId, uint64_t size, uint64_t alignment,
			uint32_t startBatch, uint32_t endBatch, bool isBuffer,
			uint64_t& outOffset, uint32_t& outBlockIndex);
		void FreeResourcesUpToBatch(uint32_t batchIndex);
		uint64_t GetPeakVirtualUsage(uint32_t memoryTypeIndex) const;

		// === Phase 2: Physical commit ===
		bool CommitVirtualAllocations();
		void ResetVirtualBlocks();

		// Query pool state
		bool IsPoolAllocated() const { return m_AliasedPoolAllocation != VK_NULL_HANDLE; }
		void* GetMappedPtr() const { return m_AliasedPoolMappedPtr; }
		VkDeviceMemory GetPoolDeviceMemory() const { return m_AliasedPoolDeviceMemory; }
		uint32_t GetPoolMemoryType() const { return m_AliasedPoolMemoryType; }
		// F16a: pool allocation's offset inside its VkDeviceMemory block (for bindings)
		uint64_t GetPoolBlockOffset() const { return m_AliasedPoolBlockOffset; }

		// Get total aliased memory size
		uint64_t GetTotalAliasedSize() const { return m_TotalAliasedSize; }

		// Get memory savings from aliasing
		float GetAliasingEfficiency() const;

		// Get physical allocation for a block (used by BindResourcesToPhysicalMemory)
		VirtualBlockPool const* GetBlockPool(uint32_t blockIndex) const;
		castl::unordered_map<uint64_t, VirtualResourceAllocation> const& GetActiveVirtualAllocs() const { return m_ActiveVirtualAllocs; }

		// Persistent (per-frame) record of EVERY resource's virtual allocation {offset,size,blockIndex}.
		// Survives FreeResourcesUpToBatch (which only prunes m_ActiveVirtualAllocs, so early-batch
		// resources are kept here). Used by BindResourcesToPhysicalMemory to bind ALL registered
		// resources (not just the last-batch survivors) and by CommitVirtualAllocations to compute
		// per-pool peak = max(offset+size) instead of live vmaGetVirtualBlockStatistics.allocationBytes.
		castl::unordered_map<uint64_t, VirtualResourceAllocation> const& GetPersistentVirtualAllocs() const { return m_PersistentVirtualAllocs; }

	private:
		bool LifetimesOverlap(ResourceLifetime const& a, ResourceLifetime const& b) const;
		void CalculateAliasingGroups();

		// === Existing single-pool state ===
		castl::unordered_map<uint64_t, ResourceLifetime> m_ResourceLifetimes;
		castl::unordered_map<uint64_t, AliasedAllocation> m_AliasedAllocations;

		VmaAllocation m_AliasedPoolAllocation = VK_NULL_HANDLE;
		VkDeviceMemory m_AliasedPoolDeviceMemory = VK_NULL_HANDLE;
		uint32_t m_AliasedPoolMemoryType = 0;
		void* m_AliasedPoolMappedPtr = nullptr;
		// VmaAllocationInfo.offset of the pool allocation within its VkDeviceMemory block.
		// NOT always 0 (only dedicated allocations start at 0) — resource bindings must use
		// deviceMemory + (blockOffset + plannedOffset), see UpdateAliasedAllocationsMap.
		uint64_t m_AliasedPoolBlockOffset = 0;
		uint64_t m_TotalAliasedSize = 0;
		uint64_t m_TotalUnaliasedSize = 0;

		// === Phase 1/2: VirtualBlock state ===
		castl::vector<VirtualBlockPool> m_BlockPools;                         // per memoryType
		castl::unordered_map<uint64_t, VirtualResourceAllocation> m_ActiveVirtualAllocs; // resourceId → virtual alloc (pruned per-batch)
		// resourceId → virtual alloc, surviving FreeResourcesUpToBatch. D-C: all resources that
		// ever got a vmaVirtualAllocate keep their {offset,size,blockIndex} here so bind-all and
		// per-pool peak work even after early-batch allocs are freed from m_ActiveVirtualAllocs.
		castl::unordered_map<uint64_t, VirtualResourceAllocation> m_PersistentVirtualAllocs;
		uint64_t m_DefaultBlockSize = 256 * 1024 * 1024;                       // 256 MB initial
	};
}
