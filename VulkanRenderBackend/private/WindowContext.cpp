#include "pch.h"
#include "WindowContext.h"
#include "VulkanApplication.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{
	glfwContext glfwContext::s_Instance{};

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

	bool CWindowContext::GetKeyState(int keycode, int state) const
	{
		return glfwGetKey(m_Window, keycode) == state;
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
		glfwSetWindowTitle(m_Window, "Closing...");
		glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
	}

	void CWindowContext::ShowWindow()
	{
		glfwShowWindow(m_Window);
	}

	void CWindowContext::SetWindowPos(int x, int y)
	{
		CA_LOG_ERR("Set Window Pos " + m_WindowName + " " + castl::to_string(x) + " " + castl::to_string(y));
		//m_WindowPos = { x, y };
		//m_WindowPosDirty = true;
		glfwSetWindowPos(m_Window, x, y);
		CA_LOG_ERR("Set Window Pos End" + m_WindowName);
	}

	int2 CWindowContext::GetWindowPos() const
	{
		int x = 0, y = 0;
		glfwGetWindowPos(m_Window, &x, &y);
		return int2{ x, y };
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
		m_WindowName = name;
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
		glfwContext::s_Instance.UpdateMonitors();
	}

	castl::vector<MonitorHandle> const& CWindowContext::GetMonitors()
	{
		return glfwContext::s_Instance.m_Monitors;
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

	void CWindowContext::UpdatePos()
	{
		if (m_WindowPosDirty)
		{
			m_WindowPosDirty = false;
			glfwSetWindowPos(m_Window, m_WindowPos.x, m_WindowPos.y);
		}
	}

	void CWindowContext::Initialize(
		castl::string const& windowName
		, uint32_t initialWidth
		, uint32_t initialHeight
		, bool visible
		, bool focused
		, bool decorate
		, bool floating)
	{
		m_WindowName = windowName;
		m_Width = initialWidth;
		m_Height = initialHeight;
		assert(ValidContext());
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, visible);
		glfwWindowHint(GLFW_FOCUSED, focused);
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
		glfwWindowHint(GLFW_DECORATED, decorate);
		glfwWindowHint(GLFW_FLOATING, floating);
		
		m_Window = glfwCreateWindow(m_Width, m_Height, m_WindowName.c_str(), nullptr, nullptr);
		glfwSetWindowUserPointer(m_Window, this);
		VkSurfaceKHR surface;
		glfwCreateWindowSurface(static_cast<VkInstance>(GetInstance()), m_Window, nullptr, &surface);
		m_Surface = vk::SurfaceKHR(surface);
		m_PresentQueue = GetVulkanApplication().GetSubmitCounterContext().FindPresentQueue(m_Surface);

		m_SwapchainContext.Init(m_Width, m_Height, m_Surface, nullptr, m_PresentQueue.first);
		CA_LOG_ERR("Init Window");

		glfwContext::s_Instance.SetupWindowCallbacks(this);
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
		m_Swapchain = GetDevice().createSwapchainKHR(swapChainCreateInfo);
		auto images = GetDevice().getSwapchainImagesKHR(m_Swapchain);
		m_SwapchainImages.resize(images.size());
		for(size_t i = 0; i < images.size(); i++)
		{
			m_SwapchainImages[i].image = images[i];
		}
		m_WaitFrameDoneSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		m_CanPresentSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		WaitCurrentFrameBufferIndex();
	}
	void SwapchainContext::Release()
	{
		GetDevice().destroySemaphore(m_CanPresentSemaphore);
		m_CanPresentSemaphore = nullptr;
		GetDevice().destroySemaphore(m_WaitFrameDoneSemaphore);
		m_WaitFrameDoneSemaphore = nullptr;
		for (auto& imgData : m_SwapchainImages)
		{
			for (auto& viewPair : imgData.views)
			{
				GetDevice().destroyImageView(viewPair.second);
			}
		}
		m_SwapchainImages.clear();
		GetDevice().destroySwapchainKHR(m_Swapchain);
		m_Swapchain = nullptr;
	}
	vk::ImageView SwapchainContext::EnsureCurrentFrameImageView(GPUTextureView viewDesc)
	{
		auto& currentFrameImagePage = GetCurrentFrameImagePage();
		auto found = currentFrameImagePage.views.find(viewDesc);
		if(found == currentFrameImagePage.views.end())
		{
			viewDesc.Sanitize(m_TextureDesc);

			auto imageInfo = ETextureTypeToVulkanImageInfo(m_TextureDesc.textureType);
			vk::ImageViewCreateInfo createInfo({}
				, currentFrameImagePage.image
				, imageInfo.defaultImageViewType
				, ETextureFormatToVkFotmat(m_TextureDesc.format)
				, vk::ComponentMapping(
					vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity
					, vk::ComponentSwizzle::eIdentity)
				, vk::ImageSubresourceRange(
					ETextureAspectToVkImageAspectFlags(viewDesc.aspect, m_TextureDesc.format)
					, viewDesc.baseMip
					, viewDesc.mipCount
					, viewDesc.baseLayer
					, viewDesc.layerCount));
			auto view = GetDevice().createImageView(createInfo);
			found = currentFrameImagePage.views.insert(castl::make_pair(viewDesc, view)).first;
		}
		return found->second;
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
		//Semaphores
		m_WaitFrameDoneSemaphore = other.m_WaitFrameDoneSemaphore;
		m_CanPresentSemaphore = other.m_CanPresentSemaphore;
		//Meta data
		m_CurrentFrameUsageFlags = other.m_CurrentFrameUsageFlags;
		m_TextureDesc = other.m_TextureDesc;
		//Index
		m_CurrentBufferIndex = other.m_CurrentBufferIndex;
	}

#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()
#define IM_ARRAYSIZE(_ARR)          ((int)(sizeof(_ARR) / sizeof(*(_ARR))))     // Size of a static C-style array. Don't use on pointers!

	static int ImGui_ImplGlfw_TranslateUntranslatedKey(int key, int scancode)
	{
#if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
		// GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
		// (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
		// See https://github.com/glfw/glfw/issues/1502 for details.
		// Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
		// This won't cover edge cases but this is at least going to cover common cases.
		if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
			return key;
		GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
		const char* key_name = glfwGetKeyName(key, scancode);
		glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__) // Eat errors (see #5908)
		(void)glfwGetError(nullptr);
#endif
		if (key_name && key_name[0] != 0 && key_name[1] == 0)
		{
			const char char_names[] = "`-=[]\\,;\'./";
			const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
			CA_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys), "In Compatible Char Size");
			if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
			else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
			else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
			else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
		}
		// if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
		IM_UNUSED(scancode);
#endif
		return key;
	}


	void ImGui_ImplGlfw_KeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_KeyCallback)
		{
			keycode = ImGui_ImplGlfw_TranslateUntranslatedKey(keycode, scancode);
			glfwContext::s_Instance.m_KeyCallback(windowHandle, keycode, scancode, action, mods);
		}
	}

	void ImGui_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_WindowFocusCallback)
		{
			glfwContext::s_Instance.m_WindowFocusCallback(windowHandle, focused);
		}
	}

	void ImGui_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_CursorPosCallback)
		{
			glfwContext::s_Instance.m_CursorPosCallback(windowHandle, x, y);
		}
	}

	// Workaround: X11 seems to send spurious Leave/Enter events which would make us lose our position,
	// so we back it up and restore on Leave/Enter (see https://github.com/ocornut/imgui/issues/4984)
	void ImGui_ImplGlfw_CursorEnterCallback(GLFWwindow* window, int entered)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_CursorEnterCallback)
		{
			glfwContext::s_Instance.m_CursorEnterCallback(windowHandle, entered);
		}
	}

	void ImGui_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int c)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_CharCallback)
		{
			glfwContext::s_Instance.m_CharCallback(windowHandle, c);
		}
	}

	void ImGui_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_MouseButtonCallback)
		{
			glfwContext::s_Instance.m_MouseButtonCallback(windowHandle, button, action, mods);
		}
	}

	void ImGui_ImplGlfw_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_ScrollCallback)
		{
			glfwContext::s_Instance.m_ScrollCallback(windowHandle, xoffset, yoffset);
		}
	}


	static void ImGui_ImplGlfw_WindowCloseCallback(GLFWwindow* window)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_WindowCloseCallback)
		{
			glfwContext::s_Instance.m_WindowCloseCallback(windowHandle);
		}
	}

	// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
	// However: depending on the platform the callback may be invoked at different time:
	// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
	// - on Linux it is queued and invoked during glfwPollEvents()
	// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
	// ignore recent glfwSetWindowXXX() calls.
	static void ImGui_ImplGlfw_WindowPosCallback(GLFWwindow* window, int x, int y)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_WindowPosCallback)
		{
			glfwContext::s_Instance.m_WindowPosCallback(windowHandle, x, y);
		}
	}

	static void ImGui_ImplGlfw_WindowSizeCallback(GLFWwindow* window, int width, int height)
	{
		CWindowContext* windowHandle = reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(window));
		if (glfwContext::s_Instance.m_WindowSizeCallback)
		{
			glfwContext::s_Instance.m_WindowSizeCallback(windowHandle, width, height);
		}
	}


	void glfwContext::SetupWindowCallbacks(CWindowContext* windowHandle)
	{
		glfwSetWindowFocusCallback(windowHandle->m_Window, ImGui_ImplGlfw_WindowFocusCallback);
		glfwSetCursorEnterCallback(windowHandle->m_Window, ImGui_ImplGlfw_CursorEnterCallback);
		glfwSetCursorPosCallback(windowHandle->m_Window, ImGui_ImplGlfw_CursorPosCallback);
		glfwSetMouseButtonCallback(windowHandle->m_Window, ImGui_ImplGlfw_MouseButtonCallback);
		glfwSetScrollCallback(windowHandle->m_Window, ImGui_ImplGlfw_ScrollCallback);
		glfwSetKeyCallback(windowHandle->m_Window, ImGui_ImplGlfw_KeyCallback);
		glfwSetCharCallback(windowHandle->m_Window, ImGui_ImplGlfw_CharCallback);
		glfwSetWindowCloseCallback(windowHandle->m_Window, ImGui_ImplGlfw_WindowCloseCallback);
		glfwSetWindowPosCallback(windowHandle->m_Window, ImGui_ImplGlfw_WindowPosCallback);
		glfwSetWindowSizeCallback(windowHandle->m_Window, ImGui_ImplGlfw_WindowSizeCallback);
	}
}
