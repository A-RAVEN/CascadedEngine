#pragma once
#include <Common.h>
#include <CAWindow/WindowSystem.h>
//#include <MonitorHandle.h>
#include <WindowHandle.h>
#include <GLFW/glfw3.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAFunctional.h>
#include <CASTL/CADeque.h>
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include "ResourceUsageInfo.h"
#include "VulkanBarrierCollector.h"
#include "RenderBackendSettings.h"

namespace graphics_backend
{
	class CWindowContext;
	class FrameBoundResourcePool;

	struct SwapchainImagePage
	{
		vk::Image image;
		ResourceUsageFlags lastUsages;
		int lastQueueFamilyIndex;
		castl::map<GPUTextureView, vk::ImageView> views;
		void Init(CVulkanApplication& application, vk::Image image)
		{
			this->image = image;
			lastUsages = ResourceUsage::eDontCare;
			lastQueueFamilyIndex = -1;
			views.clear();
		}
		void Release(CVulkanApplication& application);
	};

	class SwapchainContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		SwapchainContext(CVulkanApplication& app);
		SwapchainContext(SwapchainContext const& other);
		void Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain);
		void Release();
		vk::SwapchainKHR const& GetSwapchain() const { return m_Swapchain; }
		TIndex GetCurrentFrameBufferIndex() const { return m_CurrentBufferIndex; }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainImages[m_CurrentBufferIndex].image; }
		SwapchainImagePage& GetCurrentFrameImagePage() { return m_SwapchainImages[m_CurrentBufferIndex]; }
		vk::ImageView EnsureCurrentFrameImageView(GPUTextureView view);
		void WaitCurrentFrameBufferIndex();
		void MarkUsages(ResourceUsageFlags usages, int queueFamily = -1);
		ResourceUsageFlags GetCurrentFrameUsageFlags() const;
		int GetCurrentFrameQueueFamily() const;
		void CopyFrom(SwapchainContext const& other);
		void Present(FrameBoundResourcePool* resourcePool);
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		vk::Fence GetWaitDoneFence() const{return m_WaitingDoneFence;}
	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		vk::SurfaceKHR m_Surface = nullptr;
		//Swapchain
		vk::SwapchainKHR m_Swapchain = nullptr;
		castl::vector<SwapchainImagePage> m_SwapchainImages;
		vk::Fence m_WaitingDoneFence = nullptr;
		//Meta data
		GPUTextureDescriptor m_TextureDesc;
		//Index
		TIndex m_CurrentBufferIndex = INVALID_INDEX;
		friend class CWindowContext;
	};

	class CWindowContext : public VKAppSubObjectBaseNoCopy, public WindowHandle
	{
	public:
		virtual uint2 GetSizeSafe() const override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override;
		CWindowContext(CVulkanApplication& inOwner);
		void WaitCurrentFrameBufferIndex();
		vk::SwapchainKHR const& GetSwapchain() const { return m_SwapchainContext.GetSwapchain(); }
		TIndex GetCurrentFrameBufferIndex() const { return m_SwapchainContext.GetCurrentFrameBufferIndex(); }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainContext.GetCurrentFrameImage(); }
		vk::ImageView EnsureCurrentFrameImageView(GPUTextureView const& viewDesc) { return m_SwapchainContext.EnsureCurrentFrameImageView(viewDesc); }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_SwapchainContext.GetCurrentFrameUsageFlags(); }
		constexpr SwapchainContext& GetSwapchainContext() noexcept { return m_SwapchainContext; }
		constexpr SwapchainContext const& GetSwapchainContext() const noexcept { return m_SwapchainContext; }
		void MarkUsages(ResourceUsageFlags usages);
		
		bool NeedPresent() const;
		void PresentFrame(FrameBoundResourcePool* pResourcePool);

		//New Window API
		void InitializeWindowHandle(castl::shared_ptr<cawindow::IWindow> windowHandle);
		bool Invalid() const;
		bool NeedRecreateSwapchain() const;
		void CheckRecreateSwapchain();
		void ReleaseContext();

	private:
		castl::shared_ptr<cawindow::IWindow> m_OwningWindow = nullptr;
		vk::SurfaceKHR m_Surface = nullptr;
		SwapchainContext m_SwapchainContext;
		friend class CVulkanApplication;
	};
}
