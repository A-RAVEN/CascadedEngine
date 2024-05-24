#include "GPUMemoryManager.h"
#include <VulkanDebug.h>
#include <InterfaceTranslator.h>

namespace graphics_backend
{
	GPUMemoryResourceManager::GPUMemoryResourceManager(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	void GPUMemoryResourceManager::Initialize()
	{
		VmaAllocatorCreateInfo vmaCreateInfo{};
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;
		vmaCreateInfo.physicalDevice = static_cast<VkPhysicalDevice>(GetPhysicalDevice());
		vmaCreateInfo.device = GetDevice();
		vmaCreateInfo.instance = GetInstance();
		vmaCreateAllocator(&vmaCreateInfo, &m_Allocator);
	}
	void GPUMemoryResourceManager::Release()
	{
		FreeAllMemory();
		vmaDestroyAllocator(m_Allocator);
	}
	VmaAllocation GPUMemoryResourceManager::AllocateMemory(vk::Image image, vk::MemoryPropertyFlags memoryProperties)
	{
		auto requirements = GetDevice().getImageMemoryRequirements(image);
		return AllocateMemory(requirements, memoryProperties);
	}
	VmaAllocation GPUMemoryResourceManager::AllocateMemory(vk::Buffer buffer, vk::MemoryPropertyFlags memoryProperties)
	{
		auto requirements = GetDevice().getBufferMemoryRequirements(buffer);
		return AllocateMemory(requirements, memoryProperties);
	}
	VmaAllocation GPUMemoryResourceManager::AllocateMemory(vk::MemoryRequirements const& memoryReqs, vk::MemoryPropertyFlags memoryProperties)
	{
		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.preferredFlags = static_cast<VkMemoryPropertyFlags>(memoryProperties);
		VmaAllocation alloc;
		VkMemoryRequirements const& req = memoryReqs;
		vulkan_backend::VKResultCheck(vmaAllocateMemory(m_Allocator, &req, &allocCreateInfo, &alloc, nullptr));
		m_ActiveAllocations.insert(alloc);
		return alloc;
	}
	void GPUMemoryResourceManager::BindMemory(vk::Image image, VmaAllocation allocation)
	{
		vulkan_backend::VKResultCheck(vmaBindImageMemory(m_Allocator, allocation, image));
	}
	void GPUMemoryResourceManager::BindMemory(vk::Buffer buffer, VmaAllocation allocation)
	{
		vulkan_backend::VKResultCheck(vmaBindBufferMemory(m_Allocator, allocation, buffer));
	}
	void GPUMemoryResourceManager::FreeMemory(VmaAllocation const& allocation)
	{
		auto found = m_ActiveAllocations.find(allocation);
		if (found != m_ActiveAllocations.end())
		{
			m_ActiveAllocations.erase(found);
			vmaFreeMemory(m_Allocator, allocation);
		}
	}
	void GPUMemoryResourceManager::FreeAllMemory()
	{
		for (auto itrAllocation : m_ActiveAllocations)
		{
			vmaFreeMemory(m_Allocator, itrAllocation);
		}
		m_ActiveAllocations.clear();
	}
}