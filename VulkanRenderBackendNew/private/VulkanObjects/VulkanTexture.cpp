#include <VulkanObjects/VulkanTexture.h>
#include <RenderBackend_Vulkan.h>
#include <ResourceManagement/VulkanMemoryManager.h>
#include <ResourceManagement/VulkanCommandListManager.h>
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
		auto& memoryManager = GetApp()->GetMemoryManager();
		auto& cmdListManager = GetApp()->GetCommandListManager();
		auto& queueContext = const_cast<QueueContext&>(GetQueueContext());
		auto device = GetDevice();

		// Calculate bytes per pixel for the format
		auto bytesPerPixel = [](ETextureFormat format) -> uint32_t {
			switch (format)
			{
			case ETextureFormat::E_R8_UNORM: return 1;
			case ETextureFormat::E_R8G8_UNORM:
			case ETextureFormat::E_R16_UNORM:
			case ETextureFormat::E_R16_SFLOAT: return 2;
			case ETextureFormat::E_R8G8B8A8_UNORM:
			case ETextureFormat::E_B8G8R8A8_UNORM:
			case ETextureFormat::E_R16G16_SFLOAT:
			case ETextureFormat::E_D16_UNORM: return 4;
			case ETextureFormat::E_R32_SFLOAT:
			case ETextureFormat::E_D24_UNORM_S8_UINT:
			case ETextureFormat::E_D32_SFLOAT: return 4;
			case ETextureFormat::E_R16G16B16A16_UNORM:
			case ETextureFormat::E_R16G16B16A16_SFLOAT:
			case ETextureFormat::E_R32G32_SFLOAT: return 8;
			case ETextureFormat::E_R32G32B32A32_SFLOAT: return 16;
			default: return 4;
			}
		};

		uint32_t bpp = bytesPerPixel(m_Descriptor.format);
		uint32_t width = m_Descriptor.width;
		uint32_t height = m_Descriptor.height;
		uint32_t mipLevels = m_Descriptor.mipLevels;
		uint32_t arrayLayers = m_Descriptor.layers;
		bool is3D = (m_Descriptor.textureType == ETextureType::e3D);

		// Build buffer image copy regions and calculate total staging size
		castl::vector<vk::BufferImageCopy> regions;
		uint64_t bufferOffset = 0;

		for (uint32_t layer = 0; layer < arrayLayers; ++layer)
		{
			uint32_t mipWidth = width;
			uint32_t mipHeight = height;
			uint32_t mipDepth = is3D ? m_Descriptor.layers : 1;

			for (uint32_t mip = 0; mip < mipLevels; ++mip)
			{
				vk::BufferImageCopy region{};
				region.bufferOffset = bufferOffset;
				region.bufferRowLength = 0;       // tightly packed
				region.bufferImageHeight = 0;     // tightly packed
				region.imageSubresource.aspectMask = GetImageAspect();
				region.imageSubresource.mipLevel = mip;
				region.imageSubresource.baseArrayLayer = layer;
				region.imageSubresource.layerCount = 1;
				region.imageOffset = vk::Offset3D{ 0, 0, 0 };
				region.imageExtent = vk::Extent3D(mipWidth, mipHeight, mipDepth);

				regions.push_back(region);

				bufferOffset += static_cast<uint64_t>(mipWidth) * mipHeight * mipDepth * bpp;
				mipWidth = std::max<uint32_t>(1u, mipWidth / 2);
				mipHeight = std::max<uint32_t>(1u, mipHeight / 2);
				if (is3D) mipDepth = std::max<uint32_t>(1u, mipDepth / 2);
			}
		}

		// Create staging buffer (HOST_VISIBLE + HOST_COHERENT)
		vk::BufferCreateInfo stagingInfo{};
		stagingInfo.size = bufferOffset;
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

		// Copy data to staging buffer
		if (stagingAllocResult.pMappedData)
		{
			memcpy(stagingAllocResult.pMappedData, pData, static_cast<size_t>(bufferOffset));
		}

		// Record copy command
		vk::CommandBuffer cmdBuf = cmdListManager.AllocateCommandBuffer(cmdListManager.GetTransferPool());
		cmdListManager.BeginCommandBuffer(cmdBuf);

		// Transition to TRANSFER_DST_OPTIMAL for copy destination
		vk::ImageLayout originalLayout = m_CurrentLayout;
		TransitionLayout(cmdBuf, vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eTransfer);

		// Copy buffer to image
		cmdBuf.copyBufferToImage(stagingBuffer, m_Image,
			vk::ImageLayout::eTransferDstOptimal,
			static_cast<uint32_t>(regions.size()), regions.data());

		// Transition back to original layout
		TransitionLayout(cmdBuf, originalLayout,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eBottomOfPipe);

		cmdListManager.EndCommandBuffer(cmdBuf);

		// Submit and wait for completion (synchronous upload)
		vk::Fence fence = device.createFence(vk::FenceCreateInfo{});
		queueContext.SubmitCommands(
			queueContext.GetTransferQueueFamily(), 0,
			cmdBuf,
			fence);

		vk::Result waitResult = device.waitForFences(fence, VK_TRUE, UINT64_MAX);
		(void)waitResult;
		device.destroyFence(fence);

		// Cleanup
		cmdListManager.FreeCommandBuffer(cmdListManager.GetTransferPool(), cmdBuf);
		memoryManager.FreeBuffer(stagingBuffer, stagingAlloc);
	}
}