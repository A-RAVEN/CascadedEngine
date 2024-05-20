#pragma once
#include <GPUTexture.h>
#include <GPUBuffer.h>
#include <VulkanIncludes.h>
#include <CASTL/CASet.h>
#include <VMA.h>
#include <VulkanApplicationSubobjectBase.h>

namespace graphics_backend
{
	class GPUMemoryResourceManager : public VKAppSubObjectBaseNoCopy
	{
	public:
		GPUMemoryResourceManager(CVulkanApplication& app);
		void Initialize();
		void Release();
		VmaAllocation AllocateMemory(vk::Image image, vk::MemoryPropertyFlags memoryProperties);
		VmaAllocation AllocateMemory(vk::Buffer buffer, vk::MemoryPropertyFlags memoryProperties);
		VmaAllocation AllocateMemory(vk::MemoryRequirements const& memoryReqs, vk::MemoryPropertyFlags memoryProperties);
		void BindMemory(vk::Image image, VmaAllocation allocation);
		void BindMemory(vk::Buffer buffer, VmaAllocation allocation);
		void FreeMemory(VmaAllocation const& allocation);
		void FreeAllMemory();
	private:
		VmaAllocator m_Allocator {nullptr};
		castl::set<VmaAllocation> m_ActiveAllocations;
	};
}
