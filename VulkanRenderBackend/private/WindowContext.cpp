#include "pch.h"
#include "WindowContext.h"
#include "VulkanApplication.h"
#include "InterfaceTranslator.h"

namespace graphics_backend
{
	uint2 CWindowContext::GetSizeSafe() const
	{
		return uint2{ castl::max(1u, m_SwapchainContext.m_TextureDesc.width), castl::max(1u, m_SwapchainContext.m_TextureDesc.height), };
	}

	GPUTextureDescriptor const& CWindowContext::GetBackbufferDescriptor() const
	{
		return m_SwapchainContext.m_TextureDesc;
	}

	CWindowContext::CWindowContext(CVulkanApplication& inApp) : VKAppSubObjectBaseNoCopy(inApp)
		, m_SwapchainContext(inApp)
	{
	}


	void CWindowContext::WaitCurrentFrameBufferIndex()
	{
		m_SwapchainContext.WaitCurrentFrameBufferIndex();
	}

	void CWindowContext::MarkUsages(ResourceUsageFlags usages)
	{
		m_SwapchainContext.MarkUsages(usages);
	}

	bool CWindowContext::NeedPresent() const
	{
		return GetCurrentFrameUsageFlags() != ResourceUsage::eDontCare;
	}

	void CWindowContext::PresentFrame(FrameBoundResourcePool* pResourcePool)
	{
		m_SwapchainContext.Present(pResourcePool);
	}

	void CWindowContext::InitializeWindowHandle(castl::shared_ptr<cawindow::IWindow> windowHandle)
	{
		m_OwningWindow = windowHandle;
#if CA_PLATFORM_WINDOWS
		HWND winHandle = *static_cast<HWND*>(m_OwningWindow->GetNativeWindowHandle());
		HINSTANCE winInstHandle = *static_cast<HINSTANCE*>(m_OwningWindow->GetWindowSystem()->GetSystemNativeHandle());
		m_Surface = GetInstance().createWin32SurfaceKHR(vk::Win32SurfaceCreateInfoKHR({}, winInstHandle, winHandle));
#endif
		int width, height;
		m_OwningWindow->GetWindowSize(width, height);
		m_SwapchainContext.Init(width, height, m_Surface, nullptr);
	}

	bool CWindowContext::Invalid() const
	{
		return (m_OwningWindow == nullptr || m_OwningWindow->WindowShouldClose());
	}

	bool CWindowContext::NeedRecreateSwapchain() const
	{
		if (m_OwningWindow == nullptr)
		{
			int width, height;
			m_OwningWindow->GetWindowSize(width, height);
			if (m_SwapchainContext.GetWidth() != width || m_SwapchainContext.GetHeight() != height)
				return true;
		}
		return false;
	}

	void CWindowContext::CheckRecreateSwapchain()
	{
		if (Invalid())
			return;
		//Update Swapchain Size
		int width, height;
		m_OwningWindow->GetWindowSize(width, height);
		if (m_SwapchainContext.GetWidth() != width || m_SwapchainContext.GetHeight() != height)
		{
			SwapchainContext newContext(GetVulkanApplication());
			newContext.Init(width, height, m_Surface, m_SwapchainContext.GetSwapchain());
			GetVulkanApplication().GetGlobalResourecReleasingQueue().AddSwapchains(m_SwapchainContext);
			m_SwapchainContext.CopyFrom(newContext);
		}
	}

	void CWindowContext::ReleaseContext()
	{
		GetInstance().destroySurfaceKHR(m_Surface);
		m_OwningWindow = nullptr;
		m_SwapchainContext.Release();
	}

	SwapchainContext::SwapchainContext(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app)
	{
	}

	SwapchainContext::SwapchainContext(SwapchainContext const& other) : VKAppSubObjectBaseNoCopy(other.GetVulkanApplication())
	{
		CopyFrom(other);
	}

	void SwapchainContext::Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain)
	{
		m_Width = width;
		m_Height = height;
		m_Surface = surface;
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

		m_TextureDesc = GPUTextureDescriptor::Create(swapchainExtent.width
			, swapchainExtent.height
			, VkFotmatToETextureFormat(format)
			, (ETextureAccessType::eRT | ETextureAccessType::eTransferDst)
			, ETextureType::e2D
			, 1
			, 1
			, EMultiSampleCount::e1
		);

		swapChainCreateInfo.oldSwapchain = oldSwapchain;
		m_Swapchain = GetDevice().createSwapchainKHR(swapChainCreateInfo);
		auto images = GetDevice().getSwapchainImagesKHR(m_Swapchain);
		m_SwapchainImages.resize(images.size());
		for(size_t i = 0; i < images.size(); i++)
		{
			m_SwapchainImages[i].image = images[i];
		}
		m_WaitingDoneFence = GetDevice().createFence({});
		WaitCurrentFrameBufferIndex();
	}
	void SwapchainContext::Release()
	{
		GetDevice().destroyFence(m_WaitingDoneFence);
		m_WaitingDoneFence = nullptr;
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
		if (m_CurrentBufferIndex != INVALID_INDEX && GetCurrentFrameUsageFlags() != ResourceUsage::ePresent)
			return;
		vk::ResultValue<uint32_t> currentBuffer = GetDevice().acquireNextImageKHR(
			m_Swapchain
			, castl::numeric_limits<uint64_t>::max()
			, nullptr, m_WaitingDoneFence);

		CA_ASSERT(currentBuffer.result == vk::Result::eSuccess, "Aquire Next Swapchain Image Failed!");
		if (currentBuffer.result == vk::Result::eSuccess)
		{
			GetDevice().waitForFences(m_WaitingDoneFence, true, castl::numeric_limits<uint64_t>::max());
			GetDevice().resetFences(m_WaitingDoneFence);
			m_CurrentBufferIndex = currentBuffer.value;
			MarkUsages(ResourceUsage::eDontCare);
		}
	}
	void SwapchainContext::MarkUsages(ResourceUsageFlags usages, int queueFamily)
	{
		m_SwapchainImages[m_CurrentBufferIndex].lastUsages = usages;
		m_SwapchainImages[m_CurrentBufferIndex].lastQueueFamilyIndex = queueFamily;
	}
	ResourceUsageFlags SwapchainContext::GetCurrentFrameUsageFlags() const
	{
		if (m_CurrentBufferIndex == INVALID_INDEX)
			return ResourceUsage::eDontCare;
		return m_SwapchainImages[m_CurrentBufferIndex].lastUsages;
	}
	int SwapchainContext::GetCurrentFrameQueueFamily() const
	{
		if (m_CurrentBufferIndex == INVALID_INDEX)
			return -1;
		return m_SwapchainImages[m_CurrentBufferIndex].lastQueueFamilyIndex;
	}
	void SwapchainContext::CopyFrom(SwapchainContext const& other)
	{
		m_Width = other.m_Width;
		m_Height = other.m_Height;
		m_Surface = other.m_Surface;
		//Swapchain
		m_Swapchain = other.m_Swapchain;
		m_SwapchainImages = other.m_SwapchainImages;
		//Meta data
		m_TextureDesc = other.m_TextureDesc;
		//Index
		m_CurrentBufferIndex = other.m_CurrentBufferIndex;
		m_WaitingDoneFence = other.m_WaitingDoneFence;
	}

	void SwapchainContext::Present(FrameBoundResourcePool* resourcePool)
	{
		//if frame image is not available or used, we don't need to present
		if (m_CurrentBufferIndex == INVALID_INDEX || GetCurrentFrameUsageFlags() == ResourceUsage::eDontCare)
			return;

		ResourceUsageFlags lastUsage = m_SwapchainImages[m_CurrentBufferIndex].lastUsages;
		int lastIndex =  m_SwapchainImages[m_CurrentBufferIndex].lastQueueFamilyIndex;
		vk::Image pendingImage = m_SwapchainImages[m_CurrentBufferIndex].image;

		if (lastUsage != ResourceUsage::ePresent)
		{
			auto commandBufferPool = resourcePool->commandBufferThreadPool.AquireCommandBufferPool();

			if (GetQueueContext().QueueFamilySupportsPresent(m_Surface, lastIndex))
			{
				auto stageMask = GetQueueContext().QueueFamilyIndexToPipelineStageMask(lastIndex);
				VulkanBarrierCollector barrierCollector(stageMask, lastIndex);
				barrierCollector.PushImageBarrier(pendingImage, m_TextureDesc.format, lastUsage, ResourceUsage::ePresent);
				auto command = commandBufferPool->AllocCommand(lastIndex, "PresentBarrier");
				barrierCollector.ExecuteBarrier(command);
				command.end();

				vk::Semaphore transferDoneSemaphore = resourcePool->semaphorePool.AllocSemaphore();
				vk::Semaphore transferDoneSemaphore1 = resourcePool->semaphorePool.AllocSemaphore();
				castl::array<vk::Semaphore, 2> semaphores = { transferDoneSemaphore, transferDoneSemaphore1 };
				GetQueueContext().SubmitCommands(lastIndex, 0, command, {}, {}, {}, semaphores);
				resourcePool->AddLeafSempahores(transferDoneSemaphore1);
				vk::PresentInfoKHR presenttInfo(
					transferDoneSemaphore
					, m_Swapchain
					, m_CurrentBufferIndex
				);
				GetDevice().getQueue(lastIndex, 0).presentKHR(presenttInfo);
				MarkUsages(ResourceUsage::ePresent, lastIndex);
			}
			else
			{
				int presentFamilyIndex = GetQueueContext().FindPresentQueueFamily(m_Surface);

				auto srcStageMask = GetQueueContext().QueueFamilyIndexToPipelineStageMask(lastIndex);
				VulkanBarrierCollector releaseBarrierCollector(srcStageMask, lastIndex);
				releaseBarrierCollector.PushImageReleaseBarrier(presentFamilyIndex, pendingImage, m_TextureDesc.format, lastUsage, ResourceUsage::ePresent);
		
				auto dstStageMask = GetQueueContext().QueueFamilyIndexToPipelineStageMask(presentFamilyIndex);
				VulkanBarrierCollector aquireBarrierCollector(dstStageMask, presentFamilyIndex);
				aquireBarrierCollector.PushImageAquireBarrier(lastIndex, pendingImage, m_TextureDesc.format, lastUsage, ResourceUsage::ePresent);
		
				auto releaseCommand = commandBufferPool->AllocCommand(lastIndex, "PresentReleaseBarrier");
				releaseBarrierCollector.ExecuteReleaseBarrier(releaseCommand);
				releaseCommand.end();

				auto aquireCommand = commandBufferPool->AllocCommand(presentFamilyIndex, "PresentAquireBarrier");
				aquireBarrierCollector.ExecuteBarrier(aquireCommand);
				aquireCommand.end();

				vk::Semaphore ownershipTransferSemaphore = resourcePool->semaphorePool.AllocSemaphore();
				vk::Semaphore transferDoneSemaphore = resourcePool->semaphorePool.AllocSemaphore();
				vk::Semaphore transferDoneSemaphore1 = resourcePool->semaphorePool.AllocSemaphore();
				castl::array<vk::Semaphore, 2> semaphores = { transferDoneSemaphore, transferDoneSemaphore1 };
				auto aquireStageMask = aquireBarrierCollector.GetAquireStageMask();
				GetQueueContext().SubmitCommands(lastIndex, 0, releaseCommand, {}, {}, {}, ownershipTransferSemaphore);
				GetQueueContext().SubmitCommands(presentFamilyIndex, 0, aquireCommand, {}, ownershipTransferSemaphore, aquireStageMask, semaphores);
				resourcePool->AddLeafSempahores(transferDoneSemaphore1);

				vk::PresentInfoKHR presenttInfo(
					transferDoneSemaphore
					, m_Swapchain
					, m_CurrentBufferIndex
				);
				GetDevice().getQueue(presentFamilyIndex, 0).presentKHR(presenttInfo);
				MarkUsages(ResourceUsage::ePresent, presentFamilyIndex);
			}
		}
	}

}
