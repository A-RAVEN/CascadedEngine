#include "GPUMemoryManager.h"
#include <VulkanDebug.h>
#include <InterfaceTranslator.h>

namespace graphics_backend
{
	GPUMemoryResourceManager::GPUMemoryResourceManager(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}
	GPUMemoryResourceManager::GPUMemoryResourceManager(GPUMemoryResourceManager&& other) noexcept : VKAppSubObjectBaseNoCopy(castl::move(other))
	{
		castl::lock_guard<castl::mutex> guard(other.m_Mutex);
		m_Allocator = castl::move(other.m_Allocator);
		m_ActiveAllocations = castl::move(other.m_ActiveAllocations);
	}
	void GPUMemoryResourceManager::Initialize()
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		VmaAllocatorCreateInfo vmaCreateInfo{};
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;
		vmaCreateInfo.instance = GetInstance();
		vmaCreateInfo.physicalDevice = GetPhysicalDevice();
		vmaCreateInfo.device = GetDevice();
		//vmaCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
		vmaCreateAllocator(&vmaCreateInfo, &m_Allocator);
	}
	void GPUMemoryResourceManager::Release()
	{
		FreeAllMemory();
		{
			castl::lock_guard<castl::mutex> guard(m_Mutex);
			vmaDestroyAllocator(m_Allocator);
		}
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
		VmaAllocation alloc = nullptr;
		{
			castl::lock_guard<castl::mutex> guard(m_Mutex);
			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.preferredFlags = static_cast<VkMemoryPropertyFlags>(memoryProperties);
			VkMemoryRequirements req = memoryReqs;
			VmaAllocationInfo allocationInfo{};
			VKResultCheck(vmaAllocateMemory(m_Allocator, &req, &allocCreateInfo, &alloc, &allocationInfo));
			m_ActiveAllocations.insert(alloc);
		}
		return alloc;
	}
	void* GPUMemoryResourceManager::MapMemory(VmaAllocation allocation)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		VkMemoryPropertyFlags props = 0;
		vmaGetAllocationMemoryProperties(m_Allocator, allocation, &props);
		CA_ASSERT(uenum::hasFlag(props, VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "Try To Map Host Invisible Memory");
		void* mappedMemory = nullptr;
		VKResultCheck(vmaMapMemory(m_Allocator, allocation, &mappedMemory), "Mapping Memory");
		return mappedMemory;
	}
	void GPUMemoryResourceManager::UnmapMemory(VmaAllocation allocation)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		VkMemoryPropertyFlags props = 0;
		vmaGetAllocationMemoryProperties(m_Allocator, allocation, &props);
		CA_ASSERT(uenum::hasFlag(props, VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "Try To Unmap Host Invisible Memory");
		vmaUnmapMemory(m_Allocator, allocation);
	}
	MapMemoryScope GPUMemoryResourceManager::ScopedMapMemory(VmaAllocation allocation)
	{
		return MapMemoryScope(MapMemory(allocation), allocation, this);
	}
	void GPUMemoryResourceManager::BindMemory(vk::Image image, VmaAllocation allocation)
	{
		VKResultCheck(vmaBindImageMemory(m_Allocator, allocation, image));
	}
	void GPUMemoryResourceManager::BindMemory(vk::Buffer buffer, VmaAllocation allocation)
	{
		VKResultCheck(vmaBindBufferMemory(m_Allocator, allocation, buffer));
	}
	void GPUMemoryResourceManager::FreeMemory(VmaAllocation const& allocation)
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		auto found = m_ActiveAllocations.find(allocation);
		if (found != m_ActiveAllocations.end())
		{
			m_ActiveAllocations.erase(found);
			vmaFreeMemory(m_Allocator, allocation);
		}
	}
	void GPUMemoryResourceManager::FreeAllMemory()
	{
		castl::lock_guard<castl::mutex> guard(m_Mutex);
		for (auto itrAllocation : m_ActiveAllocations)
		{
			vmaFreeMemory(m_Allocator, itrAllocation);
		}
		m_ActiveAllocations.clear();
	}
	MapMemoryScope::~MapMemoryScope()
	{
		p_Manager->UnmapMemory(m_Allocation);
	}
}