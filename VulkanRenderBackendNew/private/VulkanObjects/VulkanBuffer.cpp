#include <VulkanObjects/VulkanBuffer.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	void VulkanBuffer::Init(GPUBufferDescriptor const& descriptor, EBufferUsageFlags usageFlags)
	{
		m_Descriptor = descriptor;
		m_UsageFlags = usageFlags;

		// Create buffer create info
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = descriptor.SizeInByte();
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		// Convert usage flags to Vulkan buffer usage
		vk::BufferUsageFlags vkUsageFlags{};
		if ((usageFlags & EBufferUsageFlags::VertexBuffer) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
		}
		if ((usageFlags & EBufferUsageFlags::IndexBuffer) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
		}
		if ((usageFlags & EBufferUsageFlags::UniformBuffer) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
		}
		if ((usageFlags & EBufferUsageFlags::StorageBuffer) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
		}
		if ((usageFlags & EBufferUsageFlags::TransferSrc) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eTransferSrc;
		}
		if ((usageFlags & EBufferUsageFlags::TransferDst) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;
		}
		if ((usageFlags & EBufferUsageFlags::IndirectBuffer) != EBufferUsageFlags::None)
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
		}

		// Always add transfer destination for staging support
		vkUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;

		bufferInfo.usage = vkUsageFlags;

		// Setup allocation info
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		// CPU-accessible buffers
		if ((usageFlags & EBufferUsageFlags::CpuAccess) != EBufferUsageFlags::None)
		{
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}

		// Allocate through memory manager - we need to get it from the backend
		auto device = GetDevice();

		VmaAllocatorCreateInfo vmaCreateInfo = {};
		vmaCreateInfo.physicalDevice = GetPhysicalDevice();
		vmaCreateInfo.device = device;
		vmaCreateInfo.instance = GetInstance();
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;

		// Create temporary allocator for now (should be centralized later)
		VmaAllocator allocator;
		VkResult result = vmaCreateAllocator(&vmaCreateInfo, &allocator);
		VK_RESULT_CHECK(result);

		VkBuffer vkBuffer;
		result = vmaCreateBuffer(allocator
			, reinterpret_cast<VkBufferCreateInfo const*>(&bufferInfo)
			, &allocInfo
			, &vkBuffer
			, &m_Allocation
			, &m_AllocationInfo);

		VK_RESULT_CHECK(result);
		m_Buffer = vkBuffer;

		// Store mapped pointer if available
		if (m_AllocationInfo.pMappedData)
		{
			m_MappedPtr = m_AllocationInfo.pMappedData;
		}

		vmaDestroyAllocator(allocator);

		CA_LOG_INFO("VulkanBuffer created: size={}, name={}", descriptor.SizeInByte(), m_Name.c_str());
	}

	void VulkanBuffer::Release()
	{
		if (m_Buffer)
		{
			// Need allocator to free - for now use device directly
			// This should use VulkanMemoryManager
			VmaAllocatorCreateInfo vmaCreateInfo = {};
			vmaCreateInfo.physicalDevice = GetPhysicalDevice();
			vmaCreateInfo.device = GetDevice();
			vmaCreateInfo.instance = GetInstance();
			vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;

			VmaAllocator allocator;
			vmaCreateAllocator(&vmaCreateInfo, &allocator);

			vmaDestroyBuffer(allocator, m_Buffer, m_Allocation);
			vmaDestroyAllocator(allocator);

			m_Buffer = nullptr;
			m_Allocation = VK_NULL_HANDLE;
			m_MappedPtr = nullptr;
		}
	}

	void VulkanBuffer::SetName(castl::string const& name)
	{
		m_Name = name;
		SetVKObjectDebugName(GetDevice(), m_Buffer, name.c_str());
	}

	void* VulkanBuffer::Map()
	{
		if (m_MappedPtr)
		{
			return m_MappedPtr;
		}

		// Need to map manually
		VmaAllocatorCreateInfo vmaCreateInfo = {};
		vmaCreateInfo.physicalDevice = GetPhysicalDevice();
		vmaCreateInfo.device = GetDevice();
		vmaCreateInfo.instance = GetInstance();
		vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;

		VmaAllocator allocator;
		vmaCreateAllocator(&vmaCreateInfo, &allocator);

		void* pData = nullptr;
		VkResult result = vmaMapMemory(allocator, m_Allocation, &pData);
		VK_RESULT_CHECK(result);

		vmaDestroyAllocator(allocator);
		m_MappedPtr = pData;
		return pData;
	}

	void VulkanBuffer::Unmap()
	{
		if (m_MappedPtr && !m_AllocationInfo.pMappedData)
		{
			VmaAllocatorCreateInfo vmaCreateInfo = {};
			vmaCreateInfo.physicalDevice = GetPhysicalDevice();
			vmaCreateInfo.device = GetDevice();
			vmaCreateInfo.instance = GetInstance();
			vmaCreateInfo.vulkanApiVersion = VULKAN_API_VERSION_IN_USE;

			VmaAllocator allocator;
			vmaCreateAllocator(&vmaCreateInfo, &allocator);

			vmaUnmapMemory(allocator, m_Allocation);
			vmaDestroyAllocator(allocator);

			m_MappedPtr = nullptr;
		}
	}

	void VulkanBuffer::UploadData(void const* pData, uint64_t size, uint64_t offset)
	{
		void* pMapped = Map();
		if (pMapped)
		{
			memcpy(static_cast<char*>(pMapped) + offset, pData, size);
			Unmap();
		}
		else
		{
			// Need staging buffer for GPU-only memory
			// TODO: Implement staging buffer upload
			CA_LOG_WARN("VulkanBuffer::UploadData - staging buffer upload not implemented yet");
		}
	}
}
