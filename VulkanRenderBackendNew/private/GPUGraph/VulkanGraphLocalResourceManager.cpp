#include <GPUGraph/VulkanGraphLocalResourceManager.h>
#include <RenderBackend_Vulkan.h>
#include <VulkanObjects/VulkanBuffer.h>
#include <VulkanObjects/VulkanTexture.h>
#include <VulkanObjects/VulkanWindowHandle.h>

namespace graphics_backend
{
	void VulkanGraphLocalResourceManager::Init()
	{
		m_LocalResources.clear();
		m_Resources.clear();
		m_AliasingManager.Init();
		m_NextResourceId = 1;
		m_TotalMemoryUsed = 0;
		CA_LOG_INFO("VulkanGraphLocalResourceManager initialized");
	}

	void VulkanGraphLocalResourceManager::Release()
	{
		ReleaseAllResources();
		m_AliasingManager.Release();
		CA_LOG_INFO("VulkanGraphLocalResourceManager released");
	}

	uint64_t VulkanGraphLocalResourceManager::RegisterTemporaryBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex)
	{
		uint64_t id = m_NextResourceId++;
		GraphLocalResource& resource = m_LocalResources[id];
		resource.type = GraphLocalResource::Type::Buffer;
		resource.bufferDesc = desc;
		resource.bufferUsage = usage;
		resource.firstUseBatch = batchIndex;
		resource.lastUseBatch = batchIndex;
		resource.resourceId = id;

		m_AliasingManager.RegisterResource(id, {
			batchIndex, batchIndex, desc.SizeInByte(), 256, true
		});

		return id;
	}

	uint64_t VulkanGraphLocalResourceManager::RegisterTemporaryTexture(GPUTextureDescriptor const& desc, ETextureAccessTypeFlags access, uint32_t batchIndex)
	{
		uint64_t id = m_NextResourceId++;
		GraphLocalResource& resource = m_LocalResources[id];
		resource.type = GraphLocalResource::Type::Texture;
		resource.textureDesc = desc;
		resource.textureAccess = access;
		resource.firstUseBatch = batchIndex;
		resource.lastUseBatch = batchIndex;
		resource.resourceId = id;

		// Estimate texture size (simplified)
		uint64_t texSize = desc.width * desc.height * 4 * desc.mipLevels * desc.layers;

		m_AliasingManager.RegisterResource(id, {
			batchIndex, batchIndex, texSize, 256, false
		});

		return id;
	}

	void VulkanGraphLocalResourceManager::MarkResourceUse(uint64_t resourceId, uint32_t batchIndex)
	{
		auto it = m_LocalResources.find(resourceId);
		if (it != m_LocalResources.end())
		{
			it->second.firstUseBatch = castl::min(it->second.firstUseBatch, batchIndex);
			it->second.lastUseBatch = castl::max(it->second.lastUseBatch, batchIndex);
		}
	}

	bool VulkanGraphLocalResourceManager::AllocateAliasedResources()
	{
		// Analyze aliasing opportunities
		m_AliasingManager.AnalyzeAndPlanAliasing();

		// Allocate pool for aliased resources
		if (!m_AliasingManager.AllocateAliasedPool(m_AliasingManager.GetTotalAliasedSize()))
		{
			CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to allocate aliased pool");
			return false;
		}

		// Create actual resources
		for (auto const& [id, localResource] : m_LocalResources)
		{
			ManagedGPUResource managed{};
			managed.localResource = &localResource;

			auto aliasedAlloc = m_AliasingManager.GetAliasedAllocation(id);

			if (localResource.type == GraphLocalResource::Type::Buffer)
			{
				// Create buffer
				vk::BufferCreateInfo bufferInfo{};
				bufferInfo.size = localResource.bufferDesc.SizeInByte();
				bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;

				auto bufferResult = GetDevice().createBuffer(bufferInfo);
				if (bufferResult.result != vk::Result::eSuccess)
				{
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create buffer");
					return false;
				}
				managed.buffer = bufferResult.value;

				// Bind to aliased memory (simplified - would use VMA with custom pool)
				m_TotalMemoryUsed += bufferInfo.size;
			}
			else
			{
				// Create image
				vk::ImageCreateInfo imageInfo{};
				imageInfo.imageType = vk::ImageType::e2D;
				imageInfo.format = vk::Format::eR8G8B8A8Unorm;
				imageInfo.extent.width = localResource.textureDesc.width;
				imageInfo.extent.height = localResource.textureDesc.height;
				imageInfo.extent.depth = 1;
				imageInfo.mipLevels = localResource.textureDesc.mipLevels;
				imageInfo.arrayLayers = localResource.textureDesc.layers;
				imageInfo.samples = vk::SampleCountFlagBits::e1;
				imageInfo.tiling = vk::ImageTiling::eOptimal;
				imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
				imageInfo.initialLayout = vk::ImageLayout::eUndefined;

				auto imageResult = GetDevice().createImage(imageInfo);
				if (imageResult.result != vk::Result::eSuccess)
				{
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image");
					return false;
				}
				managed.image = imageResult.value;

				// Create image view
				vk::ImageViewCreateInfo viewInfo{};
				viewInfo.image = managed.image;
				viewInfo.viewType = vk::ImageViewType::e2D;
				viewInfo.format = imageInfo.format;
				viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
				viewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

				auto viewResult = GetDevice().createImageView(viewInfo);
				if (viewResult.result != vk::Result::eSuccess)
				{
					CA_LOG_ERR("VulkanGraphLocalResourceManager: Failed to create image view");
					return false;
				}
				managed.imageView = viewResult.value;

				m_TotalMemoryUsed += imageInfo.extent.width * imageInfo.extent.height * 4;
			}

			m_Resources[id] = managed;
		}

		CA_LOG_INFO("VulkanGraphLocalResourceManager: Allocated {} resources, total memory: {}"
			, m_Resources.size(), m_TotalMemoryUsed);
		return true;
	}

	ManagedGPUResource const* VulkanGraphLocalResourceManager::GetResource(uint64_t resourceId) const
	{
		auto it = m_Resources.find(resourceId);
		if (it != m_Resources.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	vk::Buffer VulkanGraphLocalResourceManager::GetBuffer(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->buffer : nullptr;
	}

	vk::Image VulkanGraphLocalResourceManager::GetTexture(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->image : nullptr;
	}

	vk::ImageView VulkanGraphLocalResourceManager::GetTextureView(uint64_t resourceId) const
	{
		auto* resource = GetResource(resourceId);
		return resource ? resource->imageView : nullptr;
	}

	void VulkanGraphLocalResourceManager::ReleaseAllResources()
	{
		auto device = GetDevice();

		for (auto& [id, resource] : m_Resources)
		{
			if (resource.buffer)
			{
				device.destroyBuffer(resource.buffer);
			}
			if (resource.imageView)
			{
				device.destroyImageView(resource.imageView);
			}
			if (resource.image)
			{
				device.destroyImage(resource.image);
			}
		}

		m_Resources.clear();
		m_LocalResources.clear();
		m_BufferHandleToResource.clear();
		m_TextureHandleToResource.clear();
		m_AliasingManager.FreeAliasedPool();
		m_TotalMemoryUsed = 0;

		CA_LOG_INFO("VulkanGraphLocalResourceManager: Released all resources");
	}

	void VulkanGraphLocalResourceManager::RegisterBufferHandle(BufferHandle const& handle, uint64_t resourceId)
	{
		if (handle.IsValid())
		{
			m_BufferHandleToResource[handle] = resourceId;
		}
	}

	void VulkanGraphLocalResourceManager::RegisterTextureHandle(ImageHandle const& handle, uint64_t resourceId)
	{
		if (handle.IsValid())
		{
			m_TextureHandleToResource[handle] = resourceId;
		}
	}

	vk::Buffer VulkanGraphLocalResourceManager::GetBuffer(BufferHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_BufferHandleToResource.find(handle);
		if (it != m_BufferHandleToResource.end())
		{
			return GetBuffer(it->second);
		}

		// Fall back to external buffer
		switch (handle.GetType())
		{
		case BufferHandle::BufferType::External:
		{
			VulkanBuffer const* pBuffer = handle.GetBufferPtr<VulkanBuffer>();
			if (pBuffer)
			{
				return pBuffer->GetBuffer();
			}
			break;
		}
		case BufferHandle::BufferType::Internal:
			// Internal buffers should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal buffer not registered");
			break;
		}

		return nullptr;
	}

	vk::Image VulkanGraphLocalResourceManager::GetTexture(ImageHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_TextureHandleToResource.find(handle);
		if (it != m_TextureHandleToResource.end())
		{
			return GetTexture(it->second);
		}

		// Fall back to external texture or backbuffer
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::External:
		{
			VulkanTexture const* pTexture = handle.GetTexturePtr<VulkanTexture>();
			if (pTexture)
			{
				return pTexture->GetImage();
			}
			break;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			VulkanWindowHandle const* pWindow = handle.GetWindowPtr<VulkanWindowHandle>();
			if (pWindow)
			{
				return pWindow->GetCurrentImage();
			}
			break;
		}
		case ImageHandle::ImageType::Internal:
			// Internal textures should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal texture not registered");
			break;
		}

		return nullptr;
	}

	vk::ImageView VulkanGraphLocalResourceManager::GetTextureView(ImageHandle const& handle) const
	{
		if (!handle.IsValid())
			return nullptr;

		// First check if we have a registered local resource
		auto it = m_TextureHandleToResource.find(handle);
		if (it != m_TextureHandleToResource.end())
		{
			return GetTextureView(it->second);
		}

		// Fall back to external texture or backbuffer
		switch (handle.GetType())
		{
		case ImageHandle::ImageType::External:
		{
			VulkanTexture const* pTexture = handle.GetTexturePtr<VulkanTexture>();
			if (pTexture)
			{
				return pTexture->GetImageView();
			}
			break;
		}
		case ImageHandle::ImageType::Backbuffer:
		{
			VulkanWindowHandle const* pWindow = handle.GetWindowPtr<VulkanWindowHandle>();
			if (pWindow)
			{
				return pWindow->GetCurrentImageView();
			}
			break;
		}
		case ImageHandle::ImageType::Internal:
			// Internal textures should be registered
			CA_LOG_WARN("VulkanGraphLocalResourceManager: Internal texture view not registered");
			break;
		}

		return nullptr;
	}
}
