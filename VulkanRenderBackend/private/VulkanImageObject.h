#pragma once
#include <RAII.h>
#include <GPUTexture.h>
#include "VulkanIncludes.h"
#include "VMA.h"

using namespace raii_utils;
namespace graphics_backend
{
	class VulkanImageObject_Internal
	{
	public:
		VulkanImageObject_Internal() = default;
		VulkanImageObject_Internal(
			GPUTextureDescriptor const& descriptor
			, vk::Image const& image
			, VmaAllocation const& allocation
			, VmaAllocationInfo const& allocationInfo
		);
		vk::Image const& GetImage() const { return m_Image; }
		VmaAllocation const& GetAllocation() const {return m_ImageAllocation;}
		VmaAllocationInfo const& GetAllocationInfo() const { return m_ImageAllocationInfo; }
		void* GetMappedPointer() const;
	private:
		vk::Image m_Image = nullptr;
		VmaAllocation m_ImageAllocation = nullptr;
		VmaAllocationInfo m_ImageAllocationInfo{};
		GPUTextureDescriptor m_Descriptor{};
	};

	using VulkanImageObject = TRAIIContainer<VulkanImageObject_Internal>;
}
