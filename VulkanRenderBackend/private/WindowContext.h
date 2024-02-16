#pragma once
#include <Common.h>
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

	class SwapchainContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		SwapchainContext(CVulkanApplication& app);
		SwapchainContext(SwapchainContext const& other);
		void Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain, uint32_t presentQueueID);
		void Release();
		vk::SwapchainKHR const& GetSwapchain() const { return m_Swapchain; }
		TIndex GetCurrentFrameBufferIndex() const { return m_CurrentBufferIndex; }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainImages[m_CurrentBufferIndex]; }
		vk::ImageView GetCurrentFrameImageView() const { return m_SwapchainImageViews[m_CurrentBufferIndex]; }
		vk::Semaphore GetWaitDoneSemaphore() const { return m_WaitFrameDoneSemaphore; }
		vk::Semaphore GetPresentWaitingSemaphore() const { return m_CanPresentSemaphore; }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_CurrentFrameUsageFlags; }
		void WaitCurrentFrameBufferIndex();
		void MarkUsages(ResourceUsageFlags usages);
		void CopyFrom(SwapchainContext const& other);
	private:
		//Swapchain
		vk::SwapchainKHR m_Swapchain = nullptr;
		castl::vector<vk::Image> m_SwapchainImages;
		castl::vector<vk::ImageView> m_SwapchainImageViews;
		//Semaphores
		vk::Semaphore m_WaitFrameDoneSemaphore = nullptr;
		vk::Semaphore m_CanPresentSemaphore = nullptr;
		//Meta data
		ResourceUsageFlags m_CurrentFrameUsageFlags = ResourceUsage::eDontCare;
		GPUTextureDescriptor m_TextureDesc;
		//Index
		TIndex m_CurrentBufferIndex = INVALID_INDEX;
		friend class CWindowContext;
	};

	class CWindowContext : public VKAppSubObjectBaseNoCopy, public WindowHandle
	{
	public:

		virtual castl::string GetName() const override;
		virtual uint2 GetSizeSafe() const override;
		virtual GPUTextureDescriptor const& GetBackbufferDescriptor() const override;
		virtual void RecreateContext() override;
		virtual bool IsKeyDown(int keycode) const override;
		virtual bool IsKeyTriggered(int keycode) const override;
		virtual bool IsMouseDown(int mousecode) const override;
		virtual bool IsMouseUp(int mousecode) const override;
		virtual float GetMouseX() const override;
		virtual float GetMouseY() const override;

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
		void Resize(FrameType resizeFrame);
		void TickReleaseResources(FrameType releasingFrame);
		void UpdateSize();
		void Initialize(castl::string const& windowName, uint32_t initialWidth, uint32_t initialHeight);
		void Release() override;
		bool NeedPresent() const;
		void PresentCurrentFrame();
		void PrepareForPresent(VulkanBarrierCollector& inoutBarrierCollector
			, castl::vector<vk::Semaphore>& inoutWaitSemaphores
			, castl::vector<vk::PipelineStageFlags>& inoutWaitStages
			, castl::vector<vk::Semaphore>& inoutSignalSemaphores);
	private:
		castl::string m_WindowName;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		GLFWwindow* m_Window = nullptr;
		vk::SurfaceKHR m_Surface = nullptr;
		castl::pair<uint32_t, vk::Queue> m_PresentQueue = castl::pair<uint32_t, vk::Queue>(INVALID_INDEX, nullptr);

		SwapchainContext m_SwapchainContext;
		castl::deque<castl::pair<FrameType, SwapchainContext>> m_PendingReleaseSwapchains;
		friend class CVulkanApplication;

		castl::function<void(GLFWwindow* window, int width, int height)> m_WindowCallback;
		bool m_Resized = false;

		castl::mutex m_Mutex;
	};
}
