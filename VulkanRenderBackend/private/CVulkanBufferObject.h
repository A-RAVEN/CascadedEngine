#pragma once
#include <Common.h>
#include <RAII.h>
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include "VMA.h"

namespace graphics_backend
{
	struct CVulkanBufferObject
	{
	public:
		CVulkanBufferObject() = default;
		CVulkanBufferObject(CVulkanBufferObject&& other) = default;
		CVulkanBufferObject& operator=(CVulkanBufferObject&& other) = default;
		CVulkanBufferObject(CVulkanBufferObject const& other) = default;
		CVulkanBufferObject& operator=(CVulkanBufferObject const& other) = default;
		CVulkanBufferObject(vk::Buffer const& buffer, VmaAllocation const& allocation, VmaAllocationInfo const& allocationInfo);
		
		vk::Buffer GetBuffer() const { return m_Buffer; }
		VmaAllocationInfo const& GetAllocationInfo() const { return m_BufferAllocationInfo; }
		void* GetMappedPointer() const;
		const bool operator==(CVulkanBufferObject const& rhs) const { return m_Buffer == rhs.m_Buffer; }
	private:
		vk::Buffer m_Buffer = nullptr;
		VmaAllocation m_BufferAllocation = nullptr;
		VmaAllocationInfo m_BufferAllocationInfo {};
		TIndex m_OwningFrameBoundPoolId = INVALID_INDEX;
		friend class CVulkanMemoryManager;
		friend class CFrameBoundMemoryPool;
		friend class CGlobalMemoryPool;
		friend class GPUBuffer_Impl;
	};

	using VulkanBufferHandle = raii_utils::TRAIIContainer<CVulkanBufferObject>;
}
