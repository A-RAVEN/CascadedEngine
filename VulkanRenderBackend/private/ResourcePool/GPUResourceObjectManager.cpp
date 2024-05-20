#include <InterfaceTranslator.h>
#include "GPUResourceObjectManager.h"

namespace graphics_backend
{
	GPUResourceObjectManager::GPUResourceObjectManager(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	void GPUResourceObjectManager::Release()
	{
		DestroyAll();
	}
	void GPUResourceObjectManager::DestroyImage(vk::Image image)
	{
		auto found = m_ActivateImages.find(image);
		if (found != m_ActivateImages.end())
		{
			GetDevice().destroyImage(image);
			m_ActivateImages.erase(found);
		}
	}
	void GPUResourceObjectManager::DestroyBuffer(vk::Buffer buffer)
	{
		auto found = m_ActiveBuffers.find(buffer);
		if (found != m_ActiveBuffers.end())
		{
			GetDevice().destroyBuffer(buffer);
			m_ActiveBuffers.erase(found);
		}
	}
	void GPUResourceObjectManager::DestroyAll()
	{
		for (auto& buffer : m_ActiveBuffers)
		{
			GetDevice().destroyBuffer(buffer);
		}
		m_ActiveBuffers.clear();
		for (auto& image : m_ActivateImages)
		{
			GetDevice().destroyImage(image);
		}
		m_ActivateImages.clear();
	}
	vk::Image GPUResourceObjectManager::CreateImage(GPUTextureDescriptor const& desc)
	{
		VulkanImageInfo imageInfo = ETextureTypeToVulkanImageInfo(desc.textureType);
		bool is3D = imageInfo.imageType == vk::ImageType::e3D;
		vk::ImageUsageFlags usages = ETextureAccessTypeToVulkanImageUsageFlags(desc.format, desc.accessType);
		VkImageCreateInfo imageCreateInfo = vk::ImageCreateInfo{ imageInfo.createFlags
			, imageInfo.imageType
			, ETextureFormatToVkFotmat(desc.format)
			, vk::Extent3D{
				desc.width
				, desc.height
				, is3D ? desc.layers : 1
			}
			, desc.mipLevels
			, is3D ? 1 : desc.layers
			, vk::SampleCountFlagBits::e1
				, vk::ImageTiling::eOptimal
				, usages
		};
		vk::Image image = GetDevice().createImage(imageCreateInfo);
		m_ActivateImages.insert(image);
		return image;
	}
	vk::Buffer GPUResourceObjectManager::CreateBuffer(GPUBufferDescriptor const& desc)
	{
		VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo(
			{}, desc.count * desc.stride, EBufferUsageFlagsTranslate(desc.usageFlags), vk::SharingMode::eExclusive
		);
		vk::Buffer buffer = GetDevice().createBuffer(bufferCreateInfo);
		m_ActiveBuffers.insert(buffer);
		return buffer;
	}
}