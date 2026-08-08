#include <VulkanObjects/VulkanWindowHandle.h>
#include <RenderBackend_Vulkan.h>
#include <VulkanQueue/QueueContext.h>
#include <CAWindow/WindowSystem.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	// Reverse of VulkanTexture::ConvertFormat for the formats a swapchain can present.
	// The backbuffer descriptor must use the ACTUAL swapchain format — hardcoding
	// B8G8R8A8 breaks when ChooseSurfaceFormat falls back to another format.
	static ETextureFormat ConvertFromVkFormat(vk::Format format)
	{
		switch (format)
		{
		case vk::Format::eB8G8R8A8Unorm:
		case vk::Format::eB8G8R8A8Srgb:
			return ETextureFormat::E_B8G8R8A8_UNORM;
		case vk::Format::eR8G8B8A8Unorm:
		case vk::Format::eR8G8B8A8Srgb:
			return ETextureFormat::E_R8G8B8A8_UNORM;
		case vk::Format::eR16G16B16A16Sfloat: return ETextureFormat::E_R16G16B16A16_SFLOAT;
		case vk::Format::eR32G32B32A32Sfloat: return ETextureFormat::E_R32G32B32A32_SFLOAT;
		default:
			// F8a: unknown swapchain format — warn instead of silently reporting a
			// wrong format; the descriptor keeps the closest approximation.
			CA_LOG_WARN("VulkanWindowHandle: Unmapped swapchain format {}, backbuffer descriptor approximated as B8G8R8A8_UNORM",
				vk::to_string(format));
			return ETextureFormat::E_B8G8R8A8_UNORM;
		}
	}

	void VulkanWindowHandle::Init(castl::shared_ptr<cawindow::IWindow> window)
	{
		m_Window = window;
		CreateSurface();
		CreateSwapchain();
		CreateImageViews();

		CA_LOG_INFO("VulkanWindowHandle initialized");
	}

	VulkanWindowHandle::~VulkanWindowHandle()
	{
		Release();
	}

	void VulkanWindowHandle::Release()
	{
		if (m_Released) return;
		m_Released = true;

		CleanupSwapchain();

		if (m_Surface)
		{
			GetInstance().destroySurfaceKHR(m_Surface);
			m_Surface = nullptr;
		}

		m_Window.reset();
		CA_LOG_INFO("VulkanWindowHandle released");
	}

	uint2 VulkanWindowHandle::GetSizeSafe() const
	{
		if (m_Window)
		{
			int width = 0, height = 0;
			m_Window->GetWindowSize(width, height);
			return uint2{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
		}
		return uint2{ 800, 600 };
	}

	void VulkanWindowHandle::CreateSurface()
	{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		vk::Win32SurfaceCreateInfoKHR surfaceInfo{};
		surfaceInfo.hinstance = *static_cast<HINSTANCE*>(m_Window->GetWindowSystem()->GetSystemNativeHandle());
		surfaceInfo.hwnd = *static_cast<HWND*>(m_Window->GetNativeWindowHandle());

		m_Surface = GetInstance().createWin32SurfaceKHR(surfaceInfo);

		if (!m_Surface)
		{
			CA_LOG_ERR("Failed to create Win32 Vulkan Surface");
		}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		// TODO: XCB surface creation
#endif
	}

	void VulkanWindowHandle::CreateSwapchain()
	{
		auto physicalDevice = GetPhysicalDevice();
		auto device = GetDevice();
		auto const& queueContext = GetQueueContext();

		// Get surface capabilities
		auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(m_Surface);

		// Choose format
		auto formats = physicalDevice.getSurfaceFormatsKHR(m_Surface);
		auto surfaceFormat = ChooseSurfaceFormat(formats);
		m_Format = surfaceFormat.format;
		m_ColorSpace = surfaceFormat.colorSpace;

		// Choose present mode
		auto presentModes = physicalDevice.getSurfacePresentModesKHR(m_Surface);
		auto presentMode = ChoosePresentMode(presentModes);

		// Choose extent
		m_Extent = ChooseSwapchainExtent(capabilities);

		// Image count
		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		{
			imageCount = capabilities.maxImageCount;
		}

		// Create swapchain
		vk::SwapchainCreateInfoKHR swapchainInfo{};
		swapchainInfo.surface = m_Surface;
		swapchainInfo.minImageCount = imageCount;
		swapchainInfo.imageFormat = m_Format;
		swapchainInfo.imageColorSpace = m_ColorSpace;
		swapchainInfo.imageExtent = m_Extent;
		swapchainInfo.imageArrayLayers = 1;
		// imageUsage must be a subset of supportedUsageFlags (VUID-VkSwapchainCreateInfoKHR-imageUsage-01275)
		vk::ImageUsageFlags desiredUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
		swapchainInfo.imageUsage = desiredUsage & capabilities.supportedUsageFlags;
		if (swapchainInfo.imageUsage != desiredUsage)
		{
			CA_LOG_WARN("VulkanWindowHandle: imageUsage reduced from {} to {} (surface supports {})",
				vk::to_string(desiredUsage), vk::to_string(swapchainInfo.imageUsage), vk::to_string(capabilities.supportedUsageFlags));
		}
		if (!swapchainInfo.imageUsage)
		{
			CA_LOG_ERR("VulkanWindowHandle: surface supports no usable image usage flags; falling back to eColorAttachment");
			swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment & capabilities.supportedUsageFlags;
		}

		// Queue family indices
		uint32_t graphicsFamily = queueContext.GetGraphicsQueueFamily();
		uint32_t presentFamily = queueContext.FindPresentQueueFamily(m_Surface);

		if (graphicsFamily != presentFamily)
		{
			uint32_t queueFamilyIndices[] = { graphicsFamily, presentFamily };
			swapchainInfo.imageSharingMode = vk::SharingMode::eConcurrent;
			swapchainInfo.queueFamilyIndexCount = 2;
			swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			swapchainInfo.imageSharingMode = vk::SharingMode::eExclusive;
		}

		swapchainInfo.preTransform = capabilities.currentTransform;
		// compositeAlpha must be a bit in supportedCompositeAlpha (VUID-VkSwapchainCreateInfoKHR-compositeAlpha-01281)
		if (capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)
		{
			swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		}
		else
		{
			// Fall back to the lowest supported bit — any single supported bit is legal.
			uint32_t raw = static_cast<uint32_t>(capabilities.supportedCompositeAlpha);
			swapchainInfo.compositeAlpha = static_cast<vk::CompositeAlphaFlagBitsKHR>(raw & (~raw + 1));
			CA_LOG_WARN("VulkanWindowHandle: eOpaque compositeAlpha unsupported, falling back to {}",
				vk::to_string(swapchainInfo.compositeAlpha));
		}
		swapchainInfo.presentMode = presentMode;
		swapchainInfo.clipped = VK_TRUE;
		swapchainInfo.oldSwapchain = nullptr;

		m_Swapchain = device.createSwapchainKHR(swapchainInfo);

		// Get swapchain images
		m_SwapchainImages = device.getSwapchainImagesKHR(m_Swapchain);

		// Initialize per-swapchain-image backbuffer resource states
		m_BackBufferResourceStates.resize(m_SwapchainImages.size(),
			{ vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eTopOfPipe,
			  vk::ImageLayout::eUndefined, EGPUQueueType::eDirect, true });

		// Update backbuffer descriptor — format MUST mirror the actual swapchain format
		// m_Format (ChooseSurfaceFormat may fall back to availableFormats[0]).
		m_BackbufferDescriptor = GPUTextureDescriptor::Create(
			m_Extent.width, m_Extent.height,
			ConvertFromVkFormat(m_Format),
			ETextureType::e2D, 1, 1, EMultiSampleCount::e1
		);

		CA_LOG_INFO("Swapchain created: {}x{}, {} images", m_Extent.width, m_Extent.height, m_SwapchainImages.size());
	}

	void VulkanWindowHandle::CreateImageViews()
	{
		m_SwapchainImageViews.clear();
		m_SwapchainImageViews.reserve(m_SwapchainImages.size());

		for (auto image : m_SwapchainImages)
		{
			vk::ImageViewCreateInfo viewInfo{};
			viewInfo.image = image;
			viewInfo.viewType = vk::ImageViewType::e2D;
			viewInfo.format = m_Format;
			viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			auto view = GetDevice().createImageView(viewInfo);
			m_SwapchainImageViews.push_back(view);
		}
	}

	void VulkanWindowHandle::CleanupSwapchain()
	{
		auto device = GetDevice();

		for (auto view : m_SwapchainImageViews)
		{
			if (view)
			{
				device.destroyImageView(view);
			}
		}
		m_SwapchainImageViews.clear();
		m_SwapchainImages.clear();

		if (m_Swapchain)
		{
			device.destroySwapchainKHR(m_Swapchain);
			m_Swapchain = nullptr;
		}
	}

	vk::SurfaceFormatKHR VulkanWindowHandle::ChooseSurfaceFormat(castl::vector<vk::SurfaceFormatKHR> const& availableFormats)
	{
		for (auto const& format : availableFormats)
		{
			if (format.format == vk::Format::eB8G8R8A8Unorm &&
				format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			{
				return format;
			}
		}
		return availableFormats[0];
	}

	vk::PresentModeKHR VulkanWindowHandle::ChoosePresentMode(castl::vector<vk::PresentModeKHR> const& availableModes)
	{
		for (auto const& mode : availableModes)
		{
			if (mode == vk::PresentModeKHR::eMailbox)
			{
				return mode;
			}
		}
		return vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanWindowHandle::ChooseSwapchainExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
	{
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}

		auto size = GetSizeSafe();
		vk::Extent2D actualExtent = { size.x, size.y };
		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		return actualExtent;
	}

	uint32_t VulkanWindowHandle::AcquireNextImage(vk::Semaphore semaphore, vk::Fence fence)
	{
		// throwing-mode vulkan.hpp: the result-check whitelist for acquireNextImageKHR is
		// {eSuccess, eTimeout, eNotReady, eSuboptimalKHR} — OUT_OF_DATE / SURFACE_LOST /
		// DEVICE_LOST throw instead of returning, so they must be caught here or the
		// swapchain-rebuild path would be unreachable dead code.
		try
		{
			auto result = GetDevice().acquireNextImageKHR(m_Swapchain, UINT64_MAX, semaphore, fence);
			m_CurrentImageIndex = result.value;
			m_AcquireFailed = false;
			return result.value;
		}
		catch (vk::OutOfDateKHRError const&)
		{
			// eSuboptimalKHR is in the throwing whitelist (returns normally) — only the
			// error codes below throw.
			// F3a: the acquire semaphore was NOT signaled — consumers must skip waiting
			// on it this frame (IsAcquireFailed) or the GPU would hang.
			m_SwapchainOutdated = true;
			m_AcquireFailed = true;
			return 0;
		}
		catch (vk::DeviceLostError const& e)
		{
			CA_LOG_ERR("VulkanWindowHandle: acquireNextImageKHR device lost: {}", e.what());
			m_SwapchainOutdated = true;
			m_AcquireFailed = true;
			return 0;
		}
		catch (vk::SurfaceLostKHRError const& e)
		{
			CA_LOG_ERR("VulkanWindowHandle: acquireNextImageKHR surface lost: {}", e.what());
			m_SwapchainOutdated = true;
			m_AcquireFailed = true;
			return 0;
		}
	}

	void VulkanWindowHandle::Present(vk::Queue queue, vk::Semaphore waitSemaphore)
	{
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_Swapchain;
		presentInfo.pImageIndices = &m_CurrentImageIndex;

		// throwing-mode vulkan.hpp: presentKHR's result-check whitelist is
		// {eSuccess, eSuboptimalKHR} — OUT_OF_DATE / SURFACE_LOST / DEVICE_LOST throw
		// instead of returning, so they must be caught here or the rebuild path is dead code.
		try
		{
			queue.presentKHR(presentInfo);
		}
		catch (vk::OutOfDateKHRError const&)
		{
			// eSuboptimalKHR is in the throwing whitelist (returns normally) — only the
			// error codes below throw.
			// 21.9 REVERTED (scope audit): m_AcquireFailed not set here — redundant;
			// the next frame's acquire fails the same way and sets it itself.
			m_SwapchainOutdated = true;
		}
		catch (vk::DeviceLostError const& e)
		{
			CA_LOG_ERR("VulkanWindowHandle: presentKHR device lost: {}", e.what());
			m_SwapchainOutdated = true;
		}
		catch (vk::SurfaceLostKHRError const& e)
		{
			CA_LOG_ERR("VulkanWindowHandle: presentKHR surface lost: {}", e.what());
			m_SwapchainOutdated = true;
		}
	}

	vk::Image VulkanWindowHandle::GetCurrentImage() const
	{
		if (m_CurrentImageIndex < m_SwapchainImages.size())
		{
			return m_SwapchainImages[m_CurrentImageIndex];
		}
		return nullptr;
	}

	vk::ImageView VulkanWindowHandle::GetCurrentImageView() const
	{
		if (m_CurrentImageIndex < m_SwapchainImageViews.size())
		{
			return m_SwapchainImageViews[m_CurrentImageIndex];
		}
		return nullptr;
	}

	void VulkanWindowHandle::RecreateSwapchain()
	{
		CleanupSwapchain();
		GetApp()->ClearFramebufferCache(); // old framebuffers reference destroyed swapchain imageViews
		CreateSwapchain();
		CreateImageViews();
		m_SwapchainOutdated = false;
		m_AcquireFailed = false; // R4-2: keep symmetric with m_SwapchainOutdated
		m_CurrentImageIndex = 0; // R5-12: a stale index would alias into the new image array
		m_Released = false;
		CA_LOG_INFO("Swapchain recreated");
	}

	void VulkanWindowHandle::ApplyCurrentBackBufferResourceState(VulkanResourceState const& state)
	{
		if (m_CurrentImageIndex < m_BackBufferResourceStates.size())
		{
			m_BackBufferResourceStates[m_CurrentImageIndex] = state;
		}
	}

	VulkanResourceState const& VulkanWindowHandle::GetCurrentBackBufferResourceState() const
	{
		if (m_CurrentImageIndex < m_BackBufferResourceStates.size())
			return m_BackBufferResourceStates[m_CurrentImageIndex];
		static VulkanResourceState s_Default = VulkanResourceState::InitializedImageState();
		return s_Default;
	}
}
