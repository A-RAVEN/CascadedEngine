#pragma once
#include "VulkanIncludes.h"
#include "VulkanApplicationSubobjectBase.h"
#include "ResourceUsageInfo.h"
#include <RenderInterface/header/Common.h>
#include <RenderInterface/header/WindowHandle.h>
#include <ExternalLib/glfw/include/GLFW/glfw3.h>
#include <functional>
#include "VulkanBarrierCollector.h"

namespace graphics_backend
{

	class SwapchainContext : public BaseApplicationSubobject
	{
	public:
		SwapchainContext(CVulkanApplication& app);
		void Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain, uint32_t presentQueueID);
		void Release();
		vk::SwapchainKHR const& GetSwapchain() const { return m_Swapchain; }
		TIndex GetCurrentFrameBufferIndex() const { return m_CurrentBufferIndex; }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainImages[m_CurrentBufferIndex]; }
		vk::ImageView GetCurrentFrameImageView() const { return m_SwapchainImageViews[m_CurrentBufferIndex]; }
		vk::Semaphore GetWaitDoneSemaphore() const { return m_WaitNextFrameSemaphore; }
		vk::Semaphore GetPresentWaitingSemaphore() const { return m_CanPresentSemaphore; }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_CurrentFrameUsageFlags; }
		void WaitCurrentFrameBufferIndex();
		void MarkUsages(ResourceUsageFlags usages);
		void CopyFrom(SwapchainContext const& other);
	private:
		//Swapchain
		vk::SwapchainKHR m_Swapchain = nullptr;
		std::vector<vk::Image> m_SwapchainImages;
		std::vector<vk::ImageView> m_SwapchainImageViews;
		//Semaphores
		vk::Semaphore m_WaitNextFrameSemaphore = nullptr;
		vk::Semaphore m_CanPresentSemaphore = nullptr;
		//Meta data
		ResourceUsageFlags m_CurrentFrameUsageFlags = ResourceUsage::eDontCare;
		GPUTextureDescriptor m_TextureDesc;
		//Index
		TIndex m_CurrentBufferIndex = INVALID_INDEX;
		friend class CWindowContext;
	};

	class CWindowContext : public BaseApplicationSubobject, public WindowHandle
	{
	public:

		virtual std::string GetName() const override;
		virtual uint2 const& GetSizeSafe() override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override;

		inline bool ValidContext() const { return m_Width > 0 && m_Height > 0; }
		CWindowContext(CVulkanApplication& inOwner);
		bool NeedClose() const;
		bool Resized() const;
		void WaitCurrentFrameBufferIndex();
		vk::SwapchainKHR const& GetSwapchain() const { return m_SwapchainContext.GetSwapchain(); }
		TIndex GetCurrentFrameBufferIndex() const { return m_SwapchainContext.GetCurrentFrameBufferIndex(); }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainContext.GetCurrentFrameImage(); }
		vk::ImageView GetCurrentFrameImageView() const { return m_SwapchainContext.GetCurrentFrameImageView(); }
		vk::Semaphore GetWaitDoneSemaphore() const { return m_SwapchainContext.GetWaitDoneSemaphore(); }
		vk::Semaphore GetPresentWaitingSemaphore() const { return m_SwapchainContext.GetPresentWaitingSemaphore(); }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_SwapchainContext.GetCurrentFrameUsageFlags(); }
		void MarkUsages(ResourceUsageFlags usages);
		void Resize();
		void UpdateSize();
		void Initialize(std::string const& windowName, uint32_t initialWidth, uint32_t initialHeight);
		void Release() override;
		bool NeedPresent() const;
		void PresentCurrentFrame();
		void PrepareForPresent(VulkanBarrierCollector& inoutBarrierCollector
			, std::vector<vk::Semaphore>& inoutWaitSemaphores
			, std::vector<vk::PipelineStageFlags>& inoutWaitStages
			, std::vector<vk::Semaphore>& inoutSignalSemaphores);
	private:
		std::string m_WindowName;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		GLFWwindow* m_Window = nullptr;
		vk::SurfaceKHR m_Surface = nullptr;
		std::pair<uint32_t, vk::Queue> m_PresentQueue = std::pair<uint32_t, vk::Queue>(INVALID_INDEX, nullptr);

		SwapchainContext m_SwapchainContext;
		friend class CVulkanApplication;

		std::function<void(GLFWwindow* window, int width, int height)> m_WindowCallback;
		bool m_Resized = false;
	};
}
