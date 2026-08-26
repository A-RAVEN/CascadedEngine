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
			// L3a (design D5, [AUDIT-R5-1]): sweep remaining allocations BEFORE destroying the
			// allocator — vmaDestroyAllocator asserts if any allocation is still outstanding.
			// Maintain the table as the single "is this freed?" authority at every step:
			//  (a) snapshot owners (collect only — owner->Release() erases table entries, so an
			//      iterator over m_Allocations would be invalidated mid-sweep).
			//  (b) owner-live (count still >0, dragged to teardown) → virtual Release(): clears the
			//      object's m_Allocation, then the idempotent Free* erases the table entry. The
			//      object's later destruction then sees m_Allocation==null → no-op, no double-free,
			//      no deref of a pApp/device already torn down.
			//  (c) remaining ownerless (aliased pool physical block / virtual-block commit / raw /
			//      graph-local) → idempotent Free* per kind.
			if (!m_Allocations.empty())
			{
				castl::vector<VulkanSubobjectBase*> owners;
				owners.reserve(m_Allocations.size());
				for (auto& [alloc, rec] : m_Allocations)
				{
					if (rec.owner) owners.push_back(rec.owner);
				}
				for (auto* owner : owners)
				{
					owner->Release();
				}

				castl::vector<VmaAllocation> stale;
				stale.reserve(m_Allocations.size());
				for (auto& [alloc, rec] : m_Allocations)
				{
					stale.push_back(alloc);
				}
				for (auto alloc : stale)
				{
					auto it = m_Allocations.find(alloc);
					if (it == m_Allocations.end()) continue;
					switch (it->second.kind)
					{
					case Kind::Buffer: FreeBuffer(it->second.buffer, alloc); break;
					case Kind::Image: FreeImage(it->second.image, alloc); break;
					case Kind::Memory: FreeMemory(alloc); break;
					}
				}
			}

			vmaDestroyAllocator(m_Allocator);
			m_Allocator = VK_NULL_HANDLE;
			CA_LOG_INFO("VulkanMemoryManager released");
		}
	}

	VmaAllocation VulkanMemoryManager::AllocateBuffer(vk::BufferCreateInfo const& bufferInfo
		, VmaAllocationCreateInfo const& allocInfo
		, vk::Buffer& outBuffer
		, VmaAllocationInfo* pAllocationInfo
		, VulkanSubobjectBase* owner)
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
		outBuffer = vk::Buffer{};
			return VK_NULL_HANDLE;
		}

		outBuffer = buffer;
		m_Allocations[allocation] = { Kind::Buffer, buffer, vk::Image{}, owner };
		return allocation;
	}

	VmaAllocation VulkanMemoryManager::AllocateImage(vk::ImageCreateInfo const& imageInfo
		, VmaAllocationCreateInfo const& allocInfo
		, vk::Image& outImage
		, VmaAllocationInfo* pAllocationInfo
		, VulkanSubobjectBase* owner)
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
		outImage = vk::Image{};
			return VK_NULL_HANDLE;
		}

		outImage = image;
		m_Allocations[allocation] = { Kind::Image, vk::Buffer{}, image, owner };
		return allocation;
	}

	VmaAllocation VulkanMemoryManager::AllocateMemory(VkMemoryRequirements const& memReq
		, VmaAllocationCreateInfo const& allocInfo
		, VmaAllocationInfo* pAllocationInfo
		, VulkanSubobjectBase* owner)
	{
		VmaAllocation allocation;
		VkResult result = vmaAllocateMemory(m_Allocator, &memReq, &allocInfo, &allocation, pAllocationInfo);
		if (result != VK_SUCCESS)
		{
			CA_LOG_ERR("Failed to allocate raw Vulkan memory: {}", (int)result);
			return VK_NULL_HANDLE;
		}
		m_Allocations[allocation] = { Kind::Memory, vk::Buffer{}, vk::Image{}, owner };
		return allocation;
	}

	void VulkanMemoryManager::FreeMemory(VmaAllocation allocation)
	{
		auto it = m_Allocations.find(allocation);
		if (it == m_Allocations.end()) return;
		vmaFreeMemory(m_Allocator, allocation);
		m_Allocations.erase(it);
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
		auto it = m_Allocations.find(allocation);
		if (it == m_Allocations.end()) return;
		if (buffer && allocation)
		{
			vmaDestroyBuffer(m_Allocator, buffer, allocation);
		}
		m_Allocations.erase(it);
	}

	void VulkanMemoryManager::FreeImage(vk::Image image, VmaAllocation allocation)
	{
		auto it = m_Allocations.find(allocation);
		if (it == m_Allocations.end()) return;
		if (image && allocation)
		{
			vmaDestroyImage(m_Allocator, image, allocation);
		}
		m_Allocations.erase(it);
	}
}
