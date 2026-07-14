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
	};

	class VulkanResourceAliasing : public VulkanSubobjectBase
	{
	public:
		VulkanResourceAliasing() = default;
		~VulkanResourceAliasing() = default;

		void Init();
		virtual void Release() override;

		// Register a resource with its lifetime
		void RegisterResource(uint64_t resourceId, ResourceLifetime const& lifetime);

		// Analyze lifetimes and create aliasing plan
		void AnalyzeAndPlanAliasing();

		// Get aliased memory for a resource
		AliasedAllocation GetAliasedAllocation(uint64_t resourceId) const;

		// Allocate aliased memory pool
		bool AllocateAliasedPool(uint64_t totalSize);

		// Free aliased memory pool
		void FreeAliasedPool();

		// Update aliased allocation map after pool allocation
		void UpdateAliasedAllocationsMap();

		// Get total aliased memory size
		uint64_t GetTotalAliasedSize() const { return m_TotalAliasedSize; }

		// Get memory savings from aliasing
		float GetAliasingEfficiency() const;

	private:
		// Check if two lifetimes overlap
		bool LifetimesOverlap(ResourceLifetime const& a, ResourceLifetime const& b) const;

		// Calculate non-overlapping groups
		void CalculateAliasingGroups();

		castl::unordered_map<uint64_t, ResourceLifetime> m_ResourceLifetimes;
		castl::unordered_map<uint64_t, AliasedAllocation> m_AliasedAllocations;

		VmaAllocation m_AliasedPoolAllocation = VK_NULL_HANDLE;
		void* m_AliasedPoolMappedPtr = nullptr;
		uint64_t m_TotalAliasedSize = 0;
		uint64_t m_TotalUnaliasedSize = 0;
	};
}
