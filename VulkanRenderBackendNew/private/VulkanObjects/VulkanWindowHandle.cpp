#include <VulkanObjects/VulkanWindowHandle.h>
#include <RenderBackend_Vulkan.h>
#include <VulkanQueue/QueueContext.h>
#include <CAWindow/WindowSystem.h>
#include <Utils/VulkanDebug.h>

namespace graphics_backend
{
	void VulkanWindowHandle::Init(castl::shared_ptr<cawindow::IWindow> window)
	{
		m_Window = window;
		CreateSurface();
		CreateSwapchain();
		CreateImageViews();

		CA_LOG_INFO("VulkanWindowHandle initialized");
	}

	void VulkanWindowHandle::Release()
	{
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
			// Get size from window - assuming IWindow has GetWidth/GetHeight
			return uint2{ 800, 600 }; // Placeholder - should get from window
		}
		return uint2{ 800, 600 };
	}

	void VulkanWindowHandle::CreateSurface()
	{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		vk::Win32SurfaceCreateInfoKHR surfaceInfo{};
		surfaceInfo.hinstance = GetModuleHandle(nullptr);
		// Get HWND from window - this depends on IWindow interface
		// For now, use placeholder
		surfaceInfo.hwnd = nullptr; // TODO: Get from m_Window

		m_Surface = GetInstance().createWin32SurfaceKHR(surfaceInfo);
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
		swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

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
		swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
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

		// Update backbuffer descriptor
		m_BackbufferDescriptor = GPUTextureDescriptor::Create(
			m_Extent.width, m_Extent.height,
			ETextureFormat::E_B8G8R8A8_UNORM, // Map from vk::Format::eB8G8R8A8Unorm
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
		auto result = GetDevice().acquireNextImageKHR(m_Swapchain, UINT64_MAX, semaphore, fence);
		if (result.result == vk::Result::eErrorOutOfDateKHR || result.result == vk::Result::eSuboptimalKHR)
		{
			m_SwapchainOutdated = true;
			return 0;
		}
		m_CurrentImageIndex = result.value;
		return result.value;
	}

	void VulkanWindowHandle::Present(vk::Queue queue, vk::Semaphore waitSemaphore)
	{
		vk::PresentInfoKHR presentInfo{};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_Swapchain;
		presentInfo.pImageIndices = &m_CurrentImageIndex;

		auto result = queue.presentKHR(presentInfo);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		{
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
		CreateSwapchain();
		CreateImageViews();
		m_SwapchainOutdated = false;
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
		static VulkanResourceState s_Default = VulkanResourceState::InitializedState();
		return s_Default;
	}
}
