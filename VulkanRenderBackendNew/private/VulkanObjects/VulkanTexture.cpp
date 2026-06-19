#include <VulkanObjects/VulkanTexture.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	vk::Format VulkanTexture::ConvertFormat(ETextureFormat format)
	{
		switch (format)
		{
		case ETextureFormat::E_R8G8B8A8_UNORM: return vk::Format::eR8G8B8A8Unorm;
		case ETextureFormat::E_B8G8R8A8_UNORM: return vk::Format::eB8G8R8A8Unorm;
		case ETextureFormat::E_R16G16B16A16_SFLOAT: return vk::Format::eR16G16B16A16Sfloat;
		case ETextureFormat::E_R32G32B32A32_SFLOAT: return vk::Format::eR32G32B32A32Sfloat;
		case ETextureFormat::E_R8_UNORM: return vk::Format::eR8Unorm;
		case ETextureFormat::E_R8G8_UNORM: return vk::Format::eR8G8Unorm;
		case ETextureFormat::E_R16_SFLOAT: return vk::Format::eR16Sfloat;
		case ETextureFormat::E_R32_SFLOAT: return vk::Format::eR32Sfloat;
		case ETextureFormat::E_R16G16_SFLOAT: return vk::Format::eR16G16Sfloat;
		case ETextureFormat::E_R32G32_SFLOAT: return vk::Format::eR32G32Sfloat;
		case ETextureFormat::E_D24_UNORM_S8_UINT: return vk::Format::eD24UnormS8Uint;
		case ETextureFormat::E_D32_SFLOAT: return vk::Format::eD32Sfloat;
		case ETextureFormat::E_D16_UNORM: return vk::Format::eD16Unorm;
		default: return vk::Format::eR8G8B8A8Unorm;
		}
	}

	vk::ImageType VulkanTexture::ConvertTextureType(ETextureType type)
	{
		switch (type)
		{
		case ETextureType::e1D: return vk::ImageType::e1D;
		case ETextureType::e2D: return vk::ImageType::e2D;
		case ETextureType::e3D: return vk::ImageType::e3D;
		case ETextureType::eCubeMap: return vk::ImageType::e2D;
		default: return vk::ImageType::e2D;
		}
	}

	vk::SampleCountFlagBits VulkanTexture::ConvertSampleCount(EMultiSampleCount samples)
	{
		switch (samples)
		{
		case EMultiSampleCount::e1: return vk::SampleCountFlagBits::e1;
		case EMultiSampleCount::e2: return vk::SampleCountFlagBits::e2;
		case EMultiSampleCount::e4: return vk::SampleCountFlagBits::e4;
		case EMultiSampleCount::e8: return vk::SampleCountFlagBits::e8;
		case EMultiSampleCount::e16: return vk::SampleCountFlagBits::e16;
		default: return vk::SampleCountFlagBits::e1;
		}
	}

	vk::ImageAspectFlags VulkanTexture::GetImageAspect() const
	{
		switch (m_Descriptor.format)
		{
		case ETextureFormat::E_D24_UNORM_S8_UINT:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		case ETextureFormat::E_D32_SFLOAT:
		case ETextureFormat::E_D16_UNORM:
			return vk::ImageAspectFlagBits::eDepth;
		default:
			return vk::ImageAspectFlagBits::eColor;
		}
	}

	void VulkanTexture::Init(GPUTextureDescriptor const& descriptor, ETextureAccessTypeFlags accessType)
	{
		m_Descriptor = descriptor;
		m_AccessType = accessType;

		// Create image
		vk::ImageCreateInfo imageInfo{};
		imageInfo.imageType = ConvertTextureType(descriptor.textureType);
		imageInfo.format = ConvertFormat(descriptor.format);
		imageInfo.extent.width = descriptor.width;
		imageInfo.extent.height = descriptor.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = descriptor.mipLevels;
		imageInfo.arrayLayers = descriptor.layers;
		imageInfo.samples = ConvertSampleCount(descriptor.samples);
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

		// Add usage based on access type
		if ((accessType & ETextureAccessType::eRT) != ETextureAccessTypeFlags{})
		{
			imageInfo.usage |= vk::ImageUsageFlagBits::eColorAttachment;
		}
		if ((accessType & ETextureAccessType::eDepthStencil) != ETextureAccessTypeFlags{})
		{
			imageInfo.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
		}
		if ((accessType & ETextureAccessType::eUnorderedAccess) != ETextureAccessTypeFlags{})
		{
			imageInfo.usage |= vk::ImageUsageFlagBits::eStorage;
		}
		if ((accessType & ETextureAccessType::eSampled) != ETextureAccessTypeFlags{})
		{
			imageInfo.usage |= vk::ImageUsageFlagBits::eSampled;
		}

		// Allocation info
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		auto& memoryManager = GetApp()->GetMemoryManager();
		vk::Image vkImage;
		m_Allocation = memoryManager.AllocateImage(imageInfo, allocInfo, vkImage);
		m_Image = vkImage;

		// Create image view
		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = m_Image;
		viewInfo.viewType = descriptor.textureType == ETextureType::eCubeMap
			? vk::ImageViewType::eCube
			: (descriptor.layers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D);
		viewInfo.format = imageInfo.format;
		viewInfo.subresourceRange.aspectMask = GetImageAspect();
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = descriptor.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = descriptor.layers;

		m_ImageView = GetDevice().createImageView(viewInfo);

		m_CurrentLayout = vk::ImageLayout::eUndefined;
		CA_LOG_INFO("VulkanTexture created: {}x{}, format={}", descriptor.width, descriptor.height, (int)descriptor.format);
	}

	void VulkanTexture::Release()
	{
		auto device = GetDevice();

		if (m_ImageView)
		{
			device.destroyImageView(m_ImageView);
			m_ImageView = nullptr;
		}

		if (m_Image && m_Allocation)
		{
			GetApp()->GetMemoryManager().FreeImage(m_Image, m_Allocation);

			m_Image = nullptr;
			m_Allocation = VK_NULL_HANDLE;
		}
	}

	void VulkanTexture::SetName(castl::string const& name)
	{
		m_Name = name;
		SetVKObjectDebugName(GetDevice(), m_Image, name.c_str());
	}

	void VulkanTexture::TransitionLayout(vk::CommandBuffer cmdBuf, vk::ImageLayout newLayout
		, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage)
	{
		if (m_CurrentLayout == newLayout)
			return;

		vk::ImageMemoryBarrier barrier{};
		barrier.oldLayout = m_CurrentLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_Image;
		barrier.subresourceRange.aspectMask = GetImageAspect();
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = m_Descriptor.mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = m_Descriptor.layers;

		// Set access masks based on layout
		switch (m_CurrentLayout)
		{
		case vk::ImageLayout::eUndefined:
			barrier.srcAccessMask = vk::AccessFlagBits::eNone;
			break;
		case vk::ImageLayout::eTransferDstOptimal:
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			break;
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
			break;
		default:
			barrier.srcAccessMask = vk::AccessFlagBits::eNone;
			break;
		}

		switch (newLayout)
		{
		case vk::ImageLayout::eTransferDstOptimal:
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			break;
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			break;
		case vk::ImageLayout::eColorAttachmentOptimal:
			barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			break;
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			break;
		default:
			barrier.dstAccessMask = vk::AccessFlagBits::eNone;
			break;
		}

		cmdBuf.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags{}, {}, {}, barrier);
		m_CurrentLayout = newLayout;
	}

	void VulkanTexture::UploadData(void const* pData, uint64_t size)
	{
		// TODO: Implement staging buffer upload
		CA_LOG_WARN("VulkanTexture::UploadData - staging buffer upload not implemented yet");
	}
}