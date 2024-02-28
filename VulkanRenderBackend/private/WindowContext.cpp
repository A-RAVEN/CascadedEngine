#include "pch.h"
#include "WindowContext.h"
#include "VulkanApplication.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{
	class glfwContext
	{
	public:
		glfwContext()
		{
			glfwInit();
			glfwSetErrorCallback([](int error, const char* msg)
				{
					std::cerr << "glfw Error: " << "(" << error << ")" << msg << std::endl;
				});
		}

		~glfwContext()
		{
			glfwTerminate();
		}

		void UpdateMonitors()
		{
			int monitors_count = 0;
			GLFWmonitor** glfw_monitors = glfwGetMonitors(&monitors_count);
			m_Monitors.clear();
			m_Monitors.reserve(monitors_count);

			for (int n = 0; n < monitors_count; n++)
			{
				int x, y;
				glfwGetMonitorPos(glfw_monitors[n], &x, &y);
				const GLFWvidmode* vid_mode = glfwGetVideoMode(glfw_monitors[n]);
				if (vid_mode == nullptr)
					continue; // Failed to get Video mode (e.g. Emscripten does not support this function)
				
				MonitorHandle monitorHandle{};
				monitorHandle.m_MonitorRect.x = x;
				monitorHandle.m_MonitorRect.y = y;
				monitorHandle.m_MonitorRect.width = vid_mode->width;
				monitorHandle.m_MonitorRect.height = vid_mode->height;
				int w, h;
				glfwGetMonitorWorkarea(glfw_monitors[n], &x, &y, &w, &h);
				if (w > 0 && h > 0) // Workaround a small GLFW issue reporting zero on monitor changes: https://github.com/glfw/glfw/pull/1761
				{
					monitorHandle.m_WorkAreaRect.x = x;
					monitorHandle.m_WorkAreaRect.y = y;
					monitorHandle.m_WorkAreaRect.width = w;
					monitorHandle.m_WorkAreaRect.height = h;
				}
				// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
				float x_scale, y_scale;
				glfwGetMonitorContentScale(glfw_monitors[n], &x_scale, &y_scale);
				monitorHandle.m_DPIScale = x_scale;
				m_Monitors.push_back(monitorHandle);
			}
		}

		castl::vector<MonitorHandle> m_Monitors;
	};

	static glfwContext s_GLFWContext = glfwContext();

	castl::string CWindowContext::GetName() const
	{ return m_WindowName; }

	uint2 CWindowContext::GetSizeSafe() const
	{
		return uint2{ castl::max(1u, m_SwapchainContext.m_TextureDesc.width), castl::max(1u, m_SwapchainContext.m_TextureDesc.height), };
	}

	GPUTextureDescriptor const& CWindowContext::GetBackbufferDescriptor() const
	{
		return m_SwapchainContext.m_TextureDesc;
	}

	void CWindowContext::RecreateContext()
	{
		m_Resized = true;
	}

	float CWindowContext::GetMouseX() const
	{
		double x, y;
		glfwGetCursorPos(m_Window, &x, &y);
		return static_cast<float>(x);
	}

	float CWindowContext::GetMouseY() const
	{
		double x, y;
		glfwGetCursorPos(m_Window, &x, &y);
		return static_cast<float>(y);
	}


	void CWindowContext::CloseWindow()
	{
		glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
	}

	void CWindowContext::ShowWindow()
	{
		glfwShowWindow(m_Window);
	}

	void CWindowContext::SetWindowPos(uint32_t x, uint32_t y)
	{
		glfwSetWindowPos(m_Window, x, y);
	}

	uint2 CWindowContext::GetWindowPos() const
	{
		int x = 0, y = 0;
		glfwGetWindowPos(m_Window, &x, &y);
		return uint2{ static_cast<uint32_t>(x), static_cast<uint32_t>(y) };
	}

	void CWindowContext::SetWindowSize(uint32_t width, uint32_t height)
	{
		glfwSetWindowSize(m_Window, width, height);
	}

	uint2 CWindowContext::GetWindowSize() const
	{
		int w = 0, h = 0;
		glfwGetWindowSize(m_Window, &w, &h);
		return uint2{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
	}

	void CWindowContext::Focus()
	{
		glfwFocusWindow(m_Window);
	}

	bool CWindowContext::GetWindowFocus() const
	{
		return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
	}

	bool CWindowContext::GetWindowMinimized() const
	{
		return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) != 0;
	}

	void CWindowContext::SetWindowName(castl::string_view const& name)
	{
		glfwSetWindowTitle(m_Window, name.data());
	}

	void CWindowContext::SetWindowAlpha(float alpha)
	{
		glfwSetWindowOpacity(m_Window, alpha);
	}

	float CWindowContext::GetDpiScale() const
	{
		return 0.0f;
	}

	void CWindowContext::UpdateMonitors()
	{
		s_GLFWContext.UpdateMonitors();
	}

	castl::vector<MonitorHandle> const& CWindowContext::GetMonitors()
	{
		return s_GLFWContext.m_Monitors;
	}

	bool CWindowContext::IsKeyDown(int keycode) const
	{
		auto state = glfwGetKey(m_Window, keycode);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool CWindowContext::IsKeyTriggered(int keycode) const
	{
		auto state = glfwGetKey(m_Window, keycode);
		return state == GLFW_PRESS;
	}

	bool CWindowContext::IsMouseDown(int mousecode) const
	{
		auto state = glfwGetMouseButton(m_Window, mousecode);
		return state == GLFW_PRESS;
	}

	bool CWindowContext::IsMouseUp(int mousecode) const
	{
		auto state = glfwGetMouseButton(m_Window, mousecode);
		return state == GLFW_RELEASE;
	}

	CWindowContext::CWindowContext(CVulkanApplication& inApp) : VKAppSubObjectBaseNoCopy(inApp)
		, m_SwapchainContext(inApp)
	{
	}

	bool CWindowContext::NeedClose() const
	{
		//assert(ValidContext());
		if(m_Window != nullptr)
		{
			return glfwWindowShouldClose(m_Window);
		}
		return false;
	}

	bool CWindowContext::Resized() const
	{
		return m_Resized;
	}

	void CWindowContext::WaitCurrentFrameBufferIndex()
	{
		m_SwapchainContext.WaitCurrentFrameBufferIndex();
	}

	void CWindowContext::MarkUsages(ResourceUsageFlags usages)
	{
		m_SwapchainContext.MarkUsages(usages);
	}

	void CWindowContext::Resize(FrameType resizeFrame)
	{
		if (m_Resized)
		{
			m_Resized = false;
			if (ValidContext())
			{
				std::cout << "Recreate Swapchain " << resizeFrame << ":" << m_Width << "x" << m_Height << std::endl;
				SwapchainContext newContext(GetVulkanApplication());
				newContext.Init(m_Width, m_Height, m_Surface, m_SwapchainContext.GetSwapchain(), m_PresentQueue.first);
				m_PendingReleaseSwapchains.push_back(castl::make_pair(resizeFrame, m_SwapchainContext));
				m_SwapchainContext.CopyFrom(newContext);
			}
			else
			{
				CA_LOG_ERR("Invlaid Swapchain Context Default Creation");
			}
		}
	}

	void CWindowContext::TickReleaseResources(FrameType releasingFrame)
	{
		while (!m_PendingReleaseSwapchains.empty() && m_PendingReleaseSwapchains.front().first <= releasingFrame)
		{
			m_PendingReleaseSwapchains.front().second.Release();
			m_PendingReleaseSwapchains.pop_front();
		}
	}

	void CWindowContext::UpdateSize()
	{
		int extractedWidth = 0;
		int extractedHeight = 0;
		glfwGetFramebufferSize(m_Window, &extractedWidth, &extractedHeight);
		if (extractedWidth != m_Width || extractedHeight != m_Height)
		{
			m_Width = extractedWidth;
			m_Height = extractedHeight;
			m_Resized = true;
		}
	}

	void CWindowContext::Initialize(
		castl::string const& windowName
		, uint32_t initialWidth
		, uint32_t initialHeight)
	{
		m_WindowName = windowName;
		m_Width = initialWidth;
		m_Height = initialHeight;
		assert(ValidContext());
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_Window = glfwCreateWindow(m_Width, m_Height, m_WindowName.c_str(), nullptr, nullptr);
		glfwSetWindowUserPointer(m_Window, this);
		VkSurfaceKHR surface;
		glfwCreateWindowSurface(static_cast<VkInstance>(GetInstance()), m_Window, nullptr, &surface);
		m_Surface = vk::SurfaceKHR(surface);
		m_PresentQueue = GetVulkanApplication().GetSubmitCounterContext().FindPresentQueue(m_Surface);

		m_SwapchainContext.Init(m_Width, m_Height, m_Surface, nullptr, m_PresentQueue.first);
		CA_LOG_ERR("Init Window");

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				CWindowContext* windowContext = static_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
			});
	}

	void CWindowContext::Release()
	{
		for(auto& pair : m_PendingReleaseSwapchains)
		{
			pair.second.Release();
		}
		m_PendingReleaseSwapchains.clear();
		m_SwapchainContext.Release();
		m_PresentQueue = castl::pair<uint32_t, vk::Queue>(INVALID_INDEX, nullptr);
		if (m_Surface != vk::SurfaceKHR(nullptr))
		{
			GetInstance().destroySurfaceKHR(m_Surface);
			m_Surface = nullptr;
		}
		if (m_Window != nullptr)
		{
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
		}
	}

	bool CWindowContext::NeedPresent() const
	{
		return ValidContext() && GetCurrentFrameUsageFlags() != ResourceUsage::eDontCare;
	}

	void CWindowContext::PresentCurrentFrame()
	{
		if (!NeedPresent())
			return;
		//castl::cout << "Present Frame: " << m_SwapchainContext.GetCurrentFrameBufferIndex() << castl::endl;
		castl::array<vk::Semaphore, 1> waitSemaphores = { m_SwapchainContext.GetPresentWaitingSemaphore() };
		castl::array<uint32_t, 1> swapchainIndices = { m_SwapchainContext.GetCurrentFrameBufferIndex() };
		vk::PresentInfoKHR presenttInfo(
			waitSemaphores
			, GetSwapchain()
			, swapchainIndices
		);
		auto result = m_PresentQueue.second.presentKHR(presenttInfo);
		MarkUsages(ResourceUsage::ePresent);
	}

	void CWindowContext::PrepareForPresent(
		VulkanBarrierCollector& inoutBarrierCollector
		, castl::vector<vk::Semaphore>& inoutWaitSemaphores
		, castl::vector<vk::PipelineStageFlags>& inoutWaitStages
		, castl::vector<vk::Semaphore>& inoutSignalSemaphores)
	{
		if (!NeedPresent())
			return;
		inoutWaitSemaphores.push_back(GetWaitDoneSemaphore());
		inoutWaitStages.push_back(vk::PipelineStageFlagBits::eTransfer);
		inoutSignalSemaphores.push_back(GetPresentWaitingSemaphore());
		inoutBarrierCollector.PushImageBarrier(GetCurrentFrameImage(), m_SwapchainContext.m_TextureDesc.format
			, GetCurrentFrameUsageFlags(), ResourceUsage::ePresent);
	}

	SwapchainContext::SwapchainContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	SwapchainContext::SwapchainContext(SwapchainContext const& other) : VKAppSubObjectBaseNoCopy(other.GetVulkanApplication())
	{
		CopyFrom(other);
	}

	void SwapchainContext::Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain, uint32_t presentQueueID)
	{
		std::vector<vk::SurfaceFormatKHR> formats = GetPhysicalDevice().getSurfaceFormatsKHR(surface);
		assert(!formats.empty());
		vk::Format format = (formats[0].format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : formats[0].format;
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface);
		vk::Extent2D               swapchainExtent;
		if (surfaceCapabilities.currentExtent.width == castl::numeric_limits<uint32_t>::max())
		{
			// If the surface size is undefined, the size is set to the size of the images requested.
			swapchainExtent.width = castl::min(castl::max(width, surfaceCapabilities.minImageExtent.width), surfaceCapabilities.maxImageExtent.width);
			swapchainExtent.height = castl::min(castl::max(height, surfaceCapabilities.minImageExtent.height), surfaceCapabilities.maxImageExtent.height);
		}
		else
		{
			// If the surface size is defined, the swap chain size must match
			swapchainExtent = surfaceCapabilities.currentExtent;
		}

		std::cout << "Swapchain Extent: " << swapchainExtent.width << "x" << swapchainExtent.height << std::endl;
		std::cout << "Window Extent: " << width << "x" << height << std::endl;

		// The FIFO present mode is guaranteed by the spec to be supported
		vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;

		vk::SurfaceTransformFlagBitsKHR preTransform = (surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
			? vk::SurfaceTransformFlagBitsKHR::eIdentity
			: surfaceCapabilities.currentTransform;

		vk::CompositeAlphaFlagBitsKHR compositeAlpha =
			(surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied) ? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
			: (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied) ? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
			: (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit) ? vk::CompositeAlphaFlagBitsKHR::eInherit
			: vk::CompositeAlphaFlagBitsKHR::eOpaque;

		vk::SwapchainCreateInfoKHR swapChainCreateInfo(vk::SwapchainCreateFlagsKHR(),
			surface,
			surfaceCapabilities.minImageCount,
			format,
			vk::ColorSpaceKHR::eSrgbNonlinear,
			swapchainExtent,
			1,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
			vk::SharingMode::eExclusive,
			{},
			preTransform,
			compositeAlpha,
			swapchainPresentMode,
			true,
			nullptr);

		m_TextureDesc.width = swapchainExtent.width;
		m_TextureDesc.height = swapchainExtent.height;
		m_TextureDesc.layers = 1;
		m_TextureDesc.mipLevels = 1;
		m_TextureDesc.accessType = (ETextureAccessType::eRT | ETextureAccessType::eTransferDst);
		m_TextureDesc.format = VkFotmatToETextureFormat(format);

		uint32_t graphicsFamily = GetVulkanApplication().GetSubmitCounterContext().GetGraphicsQueueRef().first;

		uint32_t queueFamilyIndices[2] = { graphicsFamily, presentQueueID };
		if (graphicsFamily != presentQueueID)
		{
			// If the graphics and present queues are from different queue families, we either have to explicitly transfer
			// ownership of images between the queues, or we have to create the swapchain with imageSharingMode as
			// VK_SHARING_MODE_CONCURRENT
			swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
			swapChainCreateInfo.queueFamilyIndexCount = 2;
			swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		swapChainCreateInfo.oldSwapchain = oldSwapchain;

		std::cout << "Create Swapchain" << std::endl;
		m_Swapchain = GetDevice().createSwapchainKHR(swapChainCreateInfo);
		std::cout << "Create Swapchain Done" << std::endl;
		m_SwapchainImages = castl::to_ca(GetDevice().getSwapchainImagesKHR(m_Swapchain));
		GetVulkanApplication().CreateImageViews2D(format, m_SwapchainImages, m_SwapchainImageViews);
		m_WaitFrameDoneSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		m_CanPresentSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		std::cout << "Wait Buffer Index" << std::endl;
		WaitCurrentFrameBufferIndex();
	}
	void SwapchainContext::Release()
	{
		GetDevice().destroySemaphore(m_CanPresentSemaphore);
		m_CanPresentSemaphore = nullptr;
		GetDevice().destroySemaphore(m_WaitFrameDoneSemaphore);
		m_WaitFrameDoneSemaphore = nullptr;
		for (auto& imgView : m_SwapchainImageViews)
		{
			GetDevice().destroyImageView(imgView);
		}
		m_SwapchainImageViews.clear();
		m_SwapchainImages.clear();
		GetDevice().destroySwapchainKHR(m_Swapchain);
		m_Swapchain = nullptr;
	}
	void SwapchainContext::WaitCurrentFrameBufferIndex()
	{
		//if frame image is not used, we don't need to wait
		if (m_CurrentBufferIndex != INVALID_INDEX && m_CurrentFrameUsageFlags == ResourceUsage::eDontCare)
			return;
		vk::ResultValue<uint32_t> currentBuffer = GetDevice().acquireNextImageKHR(
			m_Swapchain
			, castl::numeric_limits<uint64_t>::max()
			, m_WaitFrameDoneSemaphore, nullptr);

		CA_ASSERT(currentBuffer.result == vk::Result::eSuccess, "Aquire Next Swapchain Image Failed!");
		if (currentBuffer.result == vk::Result::eSuccess)
		{
			m_CurrentBufferIndex = currentBuffer.value;
			MarkUsages(ResourceUsage::eDontCare);
		}
	}
	void SwapchainContext::MarkUsages(ResourceUsageFlags usages)
	{
		m_CurrentFrameUsageFlags = usages;
	}
	void SwapchainContext::CopyFrom(SwapchainContext const& other)
	{
		//Swapchain
		m_Swapchain = other.m_Swapchain;
		m_SwapchainImages = other.m_SwapchainImages;
		m_SwapchainImageViews = other.m_SwapchainImageViews;
		//Semaphores
		m_WaitFrameDoneSemaphore = other.m_WaitFrameDoneSemaphore;
		m_CanPresentSemaphore = other.m_CanPresentSemaphore;
		//Meta data
		m_CurrentFrameUsageFlags = other.m_CurrentFrameUsageFlags;
		m_TextureDesc = other.m_TextureDesc;
		//Index
		m_CurrentBufferIndex = other.m_CurrentBufferIndex;
	}
}
