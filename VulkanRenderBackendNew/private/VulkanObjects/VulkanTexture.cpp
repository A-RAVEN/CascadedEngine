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
		case ETextureFormat::E_D32_SFLOAT_S8_UINT: return vk::Format::eD32SfloatS8Uint;
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
		case ETextureFormat::E_D32_SFLOAT_S8_UINT:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		case ETextureFormat::E_D32_SFLOAT:
		case ETextureFormat::E_D16_UNORM:
			return vk::ImageAspectFlagBits::eDepth;
		default:
			return vk::ImageAspectFlagBits::eColor;
		}
	}

	// R4-3: aspect for the IMAGE VIEW — single bit for depth-stencil formats because the
	// view is bound directly into sampled/storage descriptors
	// (VUID-VkDescriptorImageInfo-imageView-01976: depth/stencil views used in descriptors
	// must include exactly one aspect bit). Barriers keep using GetImageAspect() (the
	// DEPTH|STENCIL combination is legal there, VUID-VkImageMemoryBarrier-image-03320).
	vk::ImageAspectFlags VulkanTexture::GetImageViewAspect() const
	{
		if ((GetImageAspect() & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{})
			return vk::ImageAspectFlagBits::eDepth;
		return GetImageAspect();
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
		// A CUBE image view requires VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT on the image
		// (VUID-VkImageViewCreateInfo-image-01003).
		if (descriptor.textureType == ETextureType::eCubeMap)
		{
			// F10a/R3-4: VUID-VkImageCreateInfo-flags-08866 (arrayLayers >= 6) and
			// VUID-VkImageCreateInfo-flags-08865 (square dimensions) apply to cube images,
			// and VUID-VkImageViewCreateInfo-viewType-02960 requires a CUBE view's
			// layerCount to be exactly 6 (this path creates a plain CUBE view, not
			// CUBE_ARRAY) — refuse to create the invalid image instead of failing
			// validation later.
			if (descriptor.layers != 6 || descriptor.width != descriptor.height)
			{
				CA_LOG_ERR("VulkanTexture: eCubeMap requires layers==6 and square dimensions "
					"(got layers={}, {}x{}) — refusing to create image",
					descriptor.layers, descriptor.width, descriptor.height);
				return;
			}
			imageInfo.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
		}
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
		// add-render-readback: eTransferSrc lets this texture be read back via
		// vkCmdCopyImageToBuffer (an offscreen RT must carry TRANSFER_SRC, else the copy is
		// invalid — VUID-vkCmdCopyImageToBuffer-srcImage-00186). Matches
		// VulkanGraphLocalResourceManager::GetTextureImageUsage. The enum bit already exists
		// (Common.h); only this usage wiring was missing on the external CreateGPUTexture path.
		if ((accessType & ETextureAccessType::eTransferSrc) != ETextureAccessTypeFlags{})
		{
			imageInfo.usage |= vk::ImageUsageFlagBits::eTransferSrc;
		}

		// Allocation info
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		auto& memoryManager = GetApp()->GetMemoryManager();
		vk::Image vkImage;
		// owner=this: register the allocation with this object as owner, so the L3a teardown
		// sweep can call owner->Release() and clear this object's m_Allocation (design D5).
		m_Allocation = memoryManager.AllocateImage(imageInfo, allocInfo, vkImage, nullptr, this);
		m_Image = vkImage;

		// Create image view
		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = m_Image;
		viewInfo.viewType = descriptor.textureType == ETextureType::eCubeMap
			? vk::ImageViewType::eCube
			: (descriptor.layers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D);
		viewInfo.format = imageInfo.format;
		// R4-3: view aspect must be single-bit for depth-stencil formats (the view is
		// bound into sampled/storage descriptors, VUID-VkDescriptorImageInfo-imageView-01976).
		viewInfo.subresourceRange.aspectMask = GetImageViewAspect();
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = descriptor.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = descriptor.layers;

		try
		{
			m_ImageView = GetDevice().createImageView(viewInfo);
		}
		catch (vk::SystemError const& e)
		{
			CA_LOG_ERR("VulkanTexture: Failed to create image view: {}", e.what());
			GetApp()->GetMemoryManager().FreeImage(m_Image, m_Allocation);
			m_Image = nullptr;
			m_Allocation = VK_NULL_HANDLE;
			return;
		}

		m_CurrentLayout = vk::ImageLayout::eUndefined;
		CA_LOG_INFO("VulkanTexture created: {}x{}, format={}", descriptor.width, descriptor.height, (int)descriptor.format);
	}

	void VulkanTexture::Release()
	{
		// L1 idempotence: m_Allocation is the authoritative "is this alive" flag (design D5).
		// Early-return when the L3a sweep already released this allocation and cleared it — this
		// avoids double-free AND avoids deref'ing a GetDevice()/GetApp() whose pApp may already
		// be torn down (the sweep runs before device destroy; a later object destruction must be
		// a no-op). Image + allocation are created together and nulled together, so consistency holds.
		if (m_Allocation == VK_NULL_HANDLE)
			return;

		if (m_ImageView)
		{
			// GetDevice() only when there is actually a view to destroy (design D5: avoid
			// unconditional GetDevice() that derefs a potentially-destroyed pApp, cpp:183).
			GetDevice().destroyImageView(m_ImageView);
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

		// R4-12/13: when the caller passes the default stages (TOP_OF_PIPE /
		// BOTTOM_OF_PIPE) together with layout-derived access masks, the combination is
		// illegal — those stages support NO access flags (access-support table). Derive
		// the stage from the layout so srcStage/srcAccess and dstStage/dstAccess match.
		if (srcStage == vk::PipelineStageFlagBits::eTopOfPipe)
		{
			switch (m_CurrentLayout)
			{
			case vk::ImageLayout::eTransferDstOptimal: srcStage = vk::PipelineStageFlagBits::eTransfer; break;
			case vk::ImageLayout::eShaderReadOnlyOptimal: srcStage = vk::PipelineStageFlagBits::eVertexShader
				| vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader; break;
			case vk::ImageLayout::eColorAttachmentOptimal: srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput; break;
			case vk::ImageLayout::eDepthStencilAttachmentOptimal: srcStage = vk::PipelineStageFlagBits::eEarlyFragmentTests
				| vk::PipelineStageFlagBits::eLateFragmentTests; break;
			default: break;
			}
		}
		if (dstStage == vk::PipelineStageFlagBits::eBottomOfPipe)
		{
			switch (newLayout)
			{
			case vk::ImageLayout::eTransferDstOptimal: dstStage = vk::PipelineStageFlagBits::eTransfer; break;
			case vk::ImageLayout::eShaderReadOnlyOptimal: dstStage = vk::PipelineStageFlagBits::eVertexShader
				| vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader; break;
			case vk::ImageLayout::eColorAttachmentOptimal: dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput; break;
			case vk::ImageLayout::eDepthStencilAttachmentOptimal: dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests
				| vk::PipelineStageFlagBits::eLateFragmentTests; break;
			default: break;
			}
		}

		// R5-3/4: when this barrier is recorded on the TRANSFER command buffer and the
		// transfer family is a dedicated transfer-only family, graphics stages are
		// unsupported (VUID-vkCmdPipelineBarrier-srcStageMask-06461 / dstStageMask-06462)
		// and shader-access bits are not supported by TRANSFER. Degrade to TRANSFER stage
		// + transfer-only access bits (cross-queue visibility is provided by the caller's
		// host-side waitForFences serialization).
		bool transferOnlyFamily = (GetQueueContext().GetTransferQueueFamily() != GetQueueContext().GetGraphicsQueueFamily());
		if (transferOnlyFamily)
		{
			srcStage = vk::PipelineStageFlagBits::eTransfer;
			dstStage = vk::PipelineStageFlagBits::eTransfer;
		}

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
			barrier.srcAccessMask = transferOnlyFamily ? vk::AccessFlagBits::eNone : vk::AccessFlagBits::eShaderRead;
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
			barrier.dstAccessMask = transferOnlyFamily ? vk::AccessFlagBits::eNone : vk::AccessFlagBits::eShaderRead;
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
			case ETextureFormat::E_R16_SFLOAT:
			case ETextureFormat::E_D16_UNORM: return 2; // VK_FORMAT_D16_UNORM is 16-bit (2 bytes)
			case ETextureFormat::E_R8G8B8A8_UNORM:
			case ETextureFormat::E_B8G8R8A8_UNORM:
			case ETextureFormat::E_R16G16_SFLOAT:
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

		// VUID-VkBufferImageCopy-aspectMask-09103: aspectMask must be a SINGLE bit.
		// depth-stencil formats (e.g. D24S8) must copy depth and stencil separately;
		// this upload path copies the depth aspect only (stencil stays zeroed).
		vk::ImageAspectFlags copyAspect = GetImageAspect();
		if ((copyAspect & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{})
		{
			copyAspect = vk::ImageAspectFlagBits::eDepth;
			CA_LOG_WARN("VulkanTexture: UploadData copies depth aspect only for depth-stencil format {} (stencil not uploaded)",
				static_cast<int>(m_Descriptor.format));
		}

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
				region.imageSubresource.aspectMask = copyAspect;
				region.imageSubresource.mipLevel = mip;
				region.imageSubresource.baseArrayLayer = layer;
				region.imageSubresource.layerCount = 1;
				region.imageOffset = vk::Offset3D{ 0, 0, 0 };
				region.imageExtent = vk::Extent3D(mipWidth, mipHeight, mipDepth);

				regions.push_back(region);

				// F13a: VUID-vkCmdCopyBufferToImage-dstImage-07978 requires each region's
				// bufferOffset to be a multiple of 4 for depth/stencil formats. The staging
				// layout stays tightly packed (matching the caller's pData layout — padding
				// here would desync the memcpy below and over-read pData); instead, reject
				// uploads whose next offset would violate the alignment (2-byte texel
				// formats like D16 with odd texel counts).
				if (copyAspect != vk::ImageAspectFlagBits::eColor && (bufferOffset % 4) != 0)
				{
					CA_LOG_ERR("VulkanTexture: UploadData would produce a non-4-byte-aligned bufferOffset {} "
						"for a depth/stencil copy (VUID-vkCmdCopyBufferToImage-dstImage-07978); aborting upload",
						bufferOffset);
					return;
				}
				bufferOffset += static_cast<uint64_t>(mipWidth) * mipHeight * mipDepth * bpp;
				mipWidth = std::max<uint32_t>(1u, mipWidth / 2);
				mipHeight = std::max<uint32_t>(1u, mipHeight / 2);
				if (is3D) mipDepth = std::max<uint32_t>(1u, mipDepth / 2);
			}
		}

		// F14a: VUID-vkCmdCopyBufferToImage-commandBuffer-07739 — copy of a non-color
		// aspect (depth / depth-stencil) requires the command pool's queue family to
		// support VK_QUEUE_GRAPHICS_BIT. Checked BEFORE allocating the staging buffer to
		// avoid leaking it on the failure path. Single-universal-family devices fall back
		// transfer → graphics (QueueContext), which passes; a dedicated transfer family
		// would not.
		if (copyAspect != vk::ImageAspectFlagBits::eColor)
		{
			auto queueFamilyProperties = GetPhysicalDevice().getQueueFamilyProperties();
			int transferFamily = queueContext.GetTransferQueueFamily();
			bool graphicsCapable = (transferFamily >= 0 && transferFamily < static_cast<int>(queueFamilyProperties.size()))
				&& ((queueFamilyProperties[transferFamily].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags{});
			if (!graphicsCapable)
			{
				CA_LOG_ERR("VulkanTexture: UploadData of non-color aspect requires a graphics-capable queue family, "
					"but transfer family {} does not support graphics; skipping upload", transferFamily);
				return;
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
		if (!stagingBuffer || stagingAlloc == VK_NULL_HANDLE) return;

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

		// Transition back to original layout. A freshly created texture's original layout
		// is UNDEFINED — transitioning TO UNDEFINED is illegal
		// (VUID-VkImageMemoryBarrier-newLayout-01198); leave such textures in
		// SHADER_READ_ONLY_OPTIMAL (the default sampled layout) instead.
		vk::ImageLayout targetLayout = (originalLayout == vk::ImageLayout::eUndefined)
			? vk::ImageLayout::eShaderReadOnlyOptimal
			: originalLayout;
		TransitionLayout(cmdBuf, targetLayout,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eBottomOfPipe); // R5-4: default triggers the layout-derived stage (R4-13) / transfer-only degradation (R5-3)

		cmdListManager.EndCommandBuffer(cmdBuf);

		// Submit and wait for completion (synchronous upload).
		// R3-7: the copy runs on the transfer queue family — a barrier recorded here
		// only synchronizes work WITHIN this queue; cross-queue visibility would
		// require a semaphore signal/wait pair or queue-family ownership transfer.
		// This upload is safe because waitForFences below serializes host-side before
		// any consuming queue submits.
		vk::Fence fence;
		try { fence = device.createFence(vk::FenceCreateInfo{}); }
		catch (vk::SystemError const& e) {
			CA_LOG_ERR("VulkanTexture: Failed to create upload fence: {}", e.what());
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
			CA_LOG_ERR("VulkanTexture: waitForFences failed: {}", vk::to_string(waitResult));
		device.destroyFence(fence);

		// Cleanup
		cmdListManager.FreeCommandBuffer(cmdListManager.GetTransferPool(), cmdBuf);
		memoryManager.FreeBuffer(stagingBuffer, stagingAlloc);
	}
}