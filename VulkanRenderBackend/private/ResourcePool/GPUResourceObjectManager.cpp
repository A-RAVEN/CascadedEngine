#include <InterfaceTranslator.h>
#include "GPUResourceObjectManager.h"

namespace graphics_backend
{
	GPUResourceObjectManager::GPUResourceObjectManager(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	GPUResourceObjectManager::GPUResourceObjectManager(GPUResourceObjectManager&& other) noexcept : VKAppSubObjectBaseNoCopy(std::move(other))
	{
		castl::lock_guard<castl::mutex> lock(other.m_Mutex);
		m_ActivateImages = std::move(other.m_ActivateImages);
		m_ActiveBuffers = std::move(other.m_ActiveBuffers);
	}

	void GPUResourceObjectManager::Release()
	{
		DestroyAll();
	}
	void GPUResourceObjectManager::DestroyImage(vk::Image image)
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		auto found = m_ActivateImages.find(image);
		if (found != m_ActivateImages.end())
		{
			for (auto& pair : found->second.views)
			{
				GetDevice().destroyImageView(pair.second);
			}
			GetDevice().destroyImage(image);
			m_ActivateImages.erase(found);
		}
	}
	void GPUResourceObjectManager::DestroyBuffer(vk::Buffer buffer)
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		auto found = m_ActiveBuffers.find(buffer);
		if (found != m_ActiveBuffers.end())
		{
			GetDevice().destroyBuffer(buffer);
			m_ActiveBuffers.erase(found);
		}
	}
	void GPUResourceObjectManager::DestroyAll()
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		for (auto& buffer : m_ActiveBuffers)
		{
			GetDevice().destroyBuffer(buffer);
		}
		m_ActiveBuffers.clear();
		for (auto& imageObjs : m_ActivateImages)
		{
			for (auto& pair : imageObjs.second.views)
			{
				GetDevice().destroyImageView(pair.second);
			}
			GetDevice().destroyImage(imageObjs.first);
		}
		m_ActivateImages.clear();
	}
	vk::Image GPUResourceObjectManager::CreateImage(GPUTextureDescriptor const& desc)
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
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
		auto newImage = GetDevice().createImage(imageCreateInfo);
		m_ActivateImages.insert(castl::make_pair(newImage, ActiveImageObjects{}));
		return newImage;
	}
	vk::Buffer GPUResourceObjectManager::CreateBuffer(GPUBufferDescriptor const& desc)
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo(
			{}, desc.count * desc.stride, EBufferUsageFlagsTranslate(desc.usageFlags), vk::SharingMode::eExclusive
		);
		vk::Buffer buffer = GetDevice().createBuffer(bufferCreateInfo);
		m_ActiveBuffers.insert(buffer);
		return buffer;
	}
	vk::ImageView GPUResourceObjectManager::EnsureImageView(vk::Image image
		, GPUTextureDescriptor const& desc
		, GPUTextureView viewDesc)
	{
		castl::lock_guard<castl::mutex> lock(m_Mutex);
		auto found = m_ActivateImages.find(image);
		CA_ASSERT(found != m_ActivateImages.end(), "Image not found");
		viewDesc.Sanitize(desc);
		auto foundView = found->second.views.find(viewDesc);
		if(foundView == found->second.views.end())
		{
			auto imageInfo = ETextureTypeToVulkanImageInfo(desc.textureType);
			vk::ImageViewCreateInfo createInfo({}
				, image
				, imageInfo.defaultImageViewType
				, ETextureFormatToVkFotmat(desc.format)
				, ETextureSwizzleToVkComponentMapping(viewDesc.swizzle)
				, vk::ImageSubresourceRange(
					ETextureAspectToVkImageAspectFlags(viewDesc.aspect, desc.format)
					, viewDesc.baseMip
					, viewDesc.mipCount
					, viewDesc.baseLayer
					, viewDesc.layerCount));
			auto newView = GetDevice().createImageView(createInfo);
			foundView = found->second.views.insert(castl::make_pair(viewDesc, newView)).first;
		}
		return foundView->second;
	}
	SemaphorePool::SemaphorePool(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	void SemaphorePool::Release()
	{
		for (auto& semaphore : m_Semaphores)
		{
			GetDevice().destroySemaphore(semaphore);
		}
		m_Semaphores.clear();
		m_SemaphoreIndex = 0;
	}

	void SemaphorePool::Reset()
	{
		m_SemaphoreIndex = 0;
	}

	vk::Semaphore SemaphorePool::AllocSemaphore()
	{
		if (m_SemaphoreIndex == m_Semaphores.size())
		{
			vk::SemaphoreCreateInfo createInfo;
			m_Semaphores.push_back(GetDevice().createSemaphore(createInfo));
		}
		return m_Semaphores[m_SemaphoreIndex++];
	}
	
}