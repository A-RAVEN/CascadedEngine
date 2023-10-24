#pragma once
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include <ExternalLib/VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <RenderInterface/header/Common.h>
#include <SharedTools/header/RAII.h>

namespace graphics_backend
{
	struct CVulkanBufferObject : public BaseApplicationSubobject
	{
	public:
		CVulkanBufferObject(CVulkanApplication & owner);
		CVulkanBufferObject(CVulkanBufferObject&& other) = default;
		CVulkanBufferObject& operator=(CVulkanBufferObject&& other) = default;
		CVulkanBufferObject(CVulkanBufferObject const& other) = default;
		CVulkanBufferObject& operator=(CVulkanBufferObject const& other) = default;

		void Initialize(vk::Buffer const& buffer, VmaAllocation const& allocation, VmaAllocationInfo const& allocationInfo);
		virtual void Release() override;
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
