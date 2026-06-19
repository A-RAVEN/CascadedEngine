#include <ResourceManagement/VulkanMemoryManager.h>
#include <RenderBackend_Vulkan.h>
#include <Utils/VulkanDebug.h>
#include <Utils/VulkanVMAUtils.h>

namespace graphics_backend
{
	void VulkanMemoryManager::Init()
	{
		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = GetPhysicalDevice();
		allocatorInfo.device = GetDevice();
		allocatorInfo.instance = GetInstance();
		allocatorInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;

		VmaVulkanFunctions vmaFuncs{};
		FillVmaVulkanFunctions(vmaFuncs);
		allocatorInfo.pVulkanFunctions = &vmaFuncs;

		VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
		VK_RESULT_CHECK(result);
		CA_LOG_INFO("VulkanMemoryManager initialized successfully");
	}

	void VulkanMemoryManager::Release()
	{
		if (m_Allocator != VK_NULL_HANDLE)
		{
			vmaDestroyAllocator(m_Allocator);
			m_Allocator = VK_NULL_HANDLE;
			CA_LOG_INFO("VulkanMemoryManager released");
		}
	}

	VmaAllocation VulkanMemoryManager::AllocateBuffer(vk::BufferCreateInfo const& bufferInfo
		, VmaAllocationCreateInfo const& allocInfo
		, vk::Buffer& outBuffer
		, VmaAllocationInfo* pAllocationInfo)
	{
		VkBuffer buffer;
		VmaAllocation allocation;

		VkResult result = vmaCreateBuffer(m_Allocator
			, reinterpret_cast<VkBufferCreateInfo const*>(&bufferInfo)
			, &allocInfo
			, &buffer
			, &allocation
			, pAllocationInfo);

		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Failed to allocate Vulkan buffer: {}", (int)result);
			return VK_NULL_HANDLE;
		}

		outBuffer = buffer;
		return allocation;
	}

	VmaAllocation VulkanMemoryManager::AllocateImage(vk::ImageCreateInfo const& imageInfo
		, VmaAllocationCreateInfo const& allocInfo
		, vk::Image& outImage
		, VmaAllocationInfo* pAllocationInfo)
	{
		VkImage image;
		VmaAllocation allocation;

		VkResult result = vmaCreateImage(m_Allocator
			, reinterpret_cast<VkImageCreateInfo const*>(&imageInfo)
			, &allocInfo
			, &image
			, &allocation
			, pAllocationInfo);

		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Failed to allocate Vulkan image: {}", (int)result);
			return VK_NULL_HANDLE;
		}

		outImage = image;
		return allocation;
	}

	VmaAllocation VulkanMemoryManager::AllocateMemory(VkMemoryRequirements const& memReq
		, VmaAllocationCreateInfo const& allocInfo
		, VmaAllocationInfo* pAllocationInfo)
	{
		VmaAllocation allocation;
		VkResult result = vmaAllocateMemory(m_Allocator, &memReq, &allocInfo, &allocation, pAllocationInfo);
		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Failed to allocate raw Vulkan memory: {}", (int)result);
			return VK_NULL_HANDLE;
		}
		return allocation;
	}

	void VulkanMemoryManager::FreeMemory(VmaAllocation allocation)
	{
		if (allocation)
		{
			vmaFreeMemory(m_Allocator, allocation);
		}
	}

	void* VulkanMemoryManager::MapMemory(VmaAllocation allocation)
	{
		void* pData = nullptr;
		VkResult result = vmaMapMemory(m_Allocator, allocation, &pData);
		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Failed to map Vulkan memory: {}", (int)result);
			return nullptr;
		}
		return pData;
	}

	void VulkanMemoryManager::UnmapMemory(VmaAllocation allocation)
	{
		vmaUnmapMemory(m_Allocator, allocation);
	}

	void VulkanMemoryManager::FreeBuffer(vk::Buffer buffer, VmaAllocation allocation)
	{
		if (buffer && allocation)
		{
			vmaDestroyBuffer(m_Allocator, buffer, allocation);
		}
	}

	void VulkanMemoryManager::FreeImage(vk::Image image, VmaAllocation allocation)
	{
		if (image && allocation)
		{
			vmaDestroyImage(m_Allocator, image, allocation);
		}
	}
}
