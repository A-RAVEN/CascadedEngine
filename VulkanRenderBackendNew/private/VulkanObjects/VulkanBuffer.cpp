#include <VulkanObjects/VulkanBuffer.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <ResourceManagement/VulkanCommandListManager.h>
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
		if ((usageFlags & EBufferUsage::eVertexBuffer) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
		}
		if ((usageFlags & EBufferUsage::eIndexBuffer) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
		}
		if ((usageFlags & EBufferUsage::eConstantBuffer) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
		}
		if ((usageFlags & EBufferUsage::eStructuredBuffer) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
		}
		if ((usageFlags & EBufferUsage::eDataSrc) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eTransferSrc;
		}
		if ((usageFlags & EBufferUsage::eDataDst) != EBufferUsageFlags{})
		{
			vkUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;
		}
		if ((usageFlags & EBufferUsage::eIndirectBuffer) != EBufferUsageFlags{})
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
		if ((usageFlags & EBufferUsage::eCpuAccess) != EBufferUsageFlags{})
		{
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}

		auto& memoryManager = GetApp()->GetMemoryManager();
		vk::Buffer vkBuffer;
		m_Allocation = memoryManager.AllocateBuffer(bufferInfo, allocInfo, vkBuffer, &m_AllocationInfo);
		m_Buffer = vkBuffer;

		// Store mapped pointer if available
		if (m_AllocationInfo.pMappedData)
		{
			m_MappedPtr = m_AllocationInfo.pMappedData;
		}

		CA_LOG_INFO("VulkanBuffer created: size={}, name={}", descriptor.SizeInByte(), m_Name.c_str());
	}

	void VulkanBuffer::Release()
	{
		if (m_Buffer)
		{
			GetApp()->GetMemoryManager().FreeBuffer(m_Buffer, m_Allocation);

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

		m_MappedPtr = GetApp()->GetMemoryManager().MapMemory(m_Allocation);
		return m_MappedPtr;
	}

	void VulkanBuffer::Unmap()
	{
		if (m_MappedPtr && !m_AllocationInfo.pMappedData)
		{
			GetApp()->GetMemoryManager().UnmapMemory(m_Allocation);
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
			// Staging buffer upload for GPU-only (device-local) memory
			auto& memoryManager = GetApp()->GetMemoryManager();
			auto& cmdListManager = GetApp()->GetCommandListManager();
			auto& queueContext = const_cast<QueueContext&>(GetQueueContext());
			auto device = GetDevice();

			// Create staging buffer (HOST_VISIBLE + HOST_COHERENT)
			vk::BufferCreateInfo stagingInfo{};
			stagingInfo.size = size;
			stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			stagingInfo.sharingMode = vk::SharingMode::eExclusive;

			VmaAllocationCreateInfo stagingAllocInfo{};
			stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

			VmaAllocationInfo stagingAllocResult{};
			vk::Buffer stagingBuffer;
			VmaAllocation stagingAlloc = memoryManager.AllocateBuffer(
				stagingInfo, stagingAllocInfo, stagingBuffer, &stagingAllocResult);
			if (!stagingBuffer || stagingAlloc == VK_NULL_HANDLE) return;

			// Copy data to staging buffer
			if (stagingAllocResult.pMappedData)
			{
				memcpy(stagingAllocResult.pMappedData, pData, size);
			}

			// Record copy command
			vk::CommandBuffer cmdBuf = cmdListManager.AllocateCommandBuffer(cmdListManager.GetTransferPool());
			cmdListManager.BeginCommandBuffer(cmdBuf);

			vk::BufferCopy copyRegion{};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = offset;
			copyRegion.size = size;
			cmdBuf.copyBuffer(stagingBuffer, m_Buffer, copyRegion);

			// Pipeline barrier: TRANSFER_WRITE → target buffer usage
			vk::BufferMemoryBarrier barrier{};
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = m_AccessFlags;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = m_Buffer;
			barrier.offset = offset;
			barrier.size = size;

			cmdBuf.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				m_PipelineStageFlags,
				vk::DependencyFlags{},
				{}, barrier, {});

			cmdListManager.EndCommandBuffer(cmdBuf);

			// Submit and wait for completion (synchronous upload)
			vk::Fence fence;
			try { fence = device.createFence(vk::FenceCreateInfo{}); }
			catch (vk::SystemError const& e) {
				CA_LOG_ERR("VulkanBuffer: Failed to create upload fence: {}", e.what());
				cmdListManager.FreeCommandBuffer(cmdListManager.GetTransferPool(), cmdBuf);
				memoryManager.FreeBuffer(stagingBuffer, stagingAlloc);
				return;
			}
			queueContext.SubmitCommands(
				queueContext.GetTransferQueueFamily(), 0,
				cmdBuf,
				fence);

			vk::Result waitResult = device.waitForFences(fence, VK_TRUE, UINT64_MAX);
			if (waitResult != vk::Result::eSuccess)
				CA_LOG_ERR("VulkanBuffer: waitForFences failed: {}", vk::to_string(waitResult));
			device.destroyFence(fence);

			// Cleanup
			cmdListManager.FreeCommandBuffer(cmdListManager.GetTransferPool(), cmdBuf);
			memoryManager.FreeBuffer(stagingBuffer, stagingAlloc);
		}
	}
}