#include "pch.h"
#include "CVulkanBufferObject.h"
#include "CVulkanThreadContext.h"
#include "VulkanApplication.h"

namespace graphics_backend
{
	CVulkanBufferObject::CVulkanBufferObject(vk::Buffer const& buffer, VmaAllocation const& allocation, VmaAllocationInfo const& allocationInfo)
	{
		m_Buffer = buffer;
		m_BufferAllocation = allocation;
		m_BufferAllocationInfo = allocationInfo;
	}
	void* CVulkanBufferObject::GetMappedPointer() const
	{
		return m_BufferAllocationInfo.pMappedData;
	}
}
