#include "pch.h"
#include "WindowContext.h"
#include <math.h>
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
	};

	static glfwContext s_GLFWContext = glfwContext();

	std::string CWindowContext::GetName() const
	{ return m_WindowName; }

	uint2 const& CWindowContext::GetSizeSafe() const
	{
		return uint2{ std::max(1u, m_SwapchainContext.m_TextureDesc.width), std::max(1u, m_SwapchainContext.m_TextureDesc.height), };
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
				m_PendingReleaseSwapchains.push_back(std::make_pair(resizeFrame, m_SwapchainContext));
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
		std::string const& windowName
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
		m_PresentQueue = std::pair<uint32_t, vk::Queue>(INVALID_INDEX, nullptr);
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
		//std::cout << "Present Frame: " << m_SwapchainContext.GetCurrentFrameBufferIndex() << std::endl;
		std::array<vk::Semaphore, 1> waitSemaphores = { m_SwapchainContext.GetPresentWaitingSemaphore() };
		std::array<uint32_t, 1> swapchainIndices = { m_SwapchainContext.GetCurrentFrameBufferIndex() };
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
		, std::vector<vk::Semaphore>& inoutWaitSemaphores
		, std::vector<vk::PipelineStageFlags>& inoutWaitStages
		, std::vector<vk::Semaphore>& inoutSignalSemaphores)
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
		if (surfaceCapabilities.currentExtent.width == std::numeric_limits<uint32_t>::max())
		{
			// If the surface size is undefined, the size is set to the size of the images requested.
			swapchainExtent.width = std::min(std::max(width, surfaceCapabilities.minImageExtent.width), surfaceCapabilities.maxImageExtent.width);
			swapchainExtent.height = std::min(std::max(height, surfaceCapabilities.minImageExtent.height), surfaceCapabilities.maxImageExtent.height);
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
		m_SwapchainImages = GetDevice().getSwapchainImagesKHR(m_Swapchain);
		GetVulkanApplication().CreateImageViews2D(format, m_SwapchainImages, m_SwapchainImageViews);
		m_WaitFrameDoneSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		m_CanPresentSemaphore = GetDevice().createSemaphore(vk::SemaphoreCreateInfo());
		std::cout << "Wait Buffer Index" <<std::endl;
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
			, std::numeric_limits<uint64_t>::max()
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
