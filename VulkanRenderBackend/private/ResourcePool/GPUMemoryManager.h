#pragma once
#include <GPUTexture.h>
#include <GPUBuffer.h>
#include <VulkanIncludes.h>
#include <CASTL/CASet.h>
#include <VMA.h>
#include <CASTL/CAMutex.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
	class GPUMemoryResourceManager;
	struct MapMemoryScope
	{
		MapMemoryScope(void* mappedMemory, VmaAllocation allocation, GPUMemoryResourceManager* pManager)
			: mappedMemory(mappedMemory)
			, m_Allocation(allocation)
			, p_Manager(pManager)
		{
		}
		~MapMemoryScope();
		void* const mappedMemory;
	private:
		VmaAllocation m_Allocation;
		GPUMemoryResourceManager* p_Manager;
	};
	class GPUMemoryResourceManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GPUMemoryResourceManager(CVulkanApplication& app);
		GPUMemoryResourceManager(GPUMemoryResourceManager&& other) noexcept;
		void Initialize();
		void Release();
		VmaAllocation AllocateMemory(vk::Image image, vk::MemoryPropertyFlags memoryProperties);
		VmaAllocation AllocateMemory(vk::Buffer buffer, vk::MemoryPropertyFlags memoryProperties);
		VmaAllocation AllocateMemory(vk::MemoryRequirements const& memoryReqs, vk::MemoryPropertyFlags memoryProperties);
		void* MapMemory(VmaAllocation allocation);
		void UnmapMemory(VmaAllocation allocation);
		MapMemoryScope ScopedMapMemory(VmaAllocation allocation);
		void BindMemory(vk::Image image, VmaAllocation allocation);
		void BindMemory(vk::Buffer buffer, VmaAllocation allocation);
		void FreeMemory(VmaAllocation const& allocation);
		void FreeAllMemory();
	private:
		castl::mutex m_Mutex;
		VmaAllocator m_Allocator {nullptr};
		castl::set<VmaAllocation> m_ActiveAllocations;
	};
}
