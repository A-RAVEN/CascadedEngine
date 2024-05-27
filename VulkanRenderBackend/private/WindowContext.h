#pragma once
#include <Common.h>
#include <MonitorHandle.h>
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
			UpdateMonitors();
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

		void SetupWindowCallbacks(CWindowContext* windowHandle);
		CWindowContext* ContextFromHandle(GLFWwindow* windowHandle)
		{
			return reinterpret_cast<CWindowContext*>(glfwGetWindowUserPointer(windowHandle));
		}

		castl::vector<MonitorHandle> m_Monitors;

		castl::function<void(WindowHandle*, bool)> 					m_WindowFocusCallback = nullptr;
		castl::function<void(WindowHandle*, bool)> 					m_CursorEnterCallback = nullptr;
		castl::function<void(WindowHandle*, float, float)> 			m_CursorPosCallback = nullptr;
		castl::function<void(WindowHandle*, int, int, int)> 		m_MouseButtonCallback = nullptr;
		castl::function<void(WindowHandle*, float, float)> 			m_ScrollCallback = nullptr;
		castl::function<void(WindowHandle*, int, int, int, int)>	m_KeyCallback = nullptr;
		castl::function<void(WindowHandle*, uint32_t)> 				m_CharCallback = nullptr;
		castl::function<void(WindowHandle*)> 						m_WindowCloseCallback = nullptr;
		castl::function<void(WindowHandle*, float, float)> 			m_WindowPosCallback = nullptr;
		castl::function<void(WindowHandle*, float, float)> 			m_WindowSizeCallback = nullptr;

		static glfwContext s_Instance;
	};

	struct SwapchainImagePage
	{
		vk::Image image;
		castl::map<GPUTextureView, vk::ImageView> views;
	};

	class SwapchainContext : public VKAppSubObjectBaseNoCopy
	{
	public:
		SwapchainContext(CVulkanApplication& app);
		SwapchainContext(SwapchainContext const& other);
		void Init(uint32_t width, uint32_t height, vk::SurfaceKHR surface, vk::SwapchainKHR oldSwapchain, uint32_t presentQueueID);
		void Release();
		vk::SwapchainKHR const& GetSwapchain() const { return m_Swapchain; }
		TIndex GetCurrentFrameBufferIndex() const { return m_CurrentBufferIndex; }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainImages[m_CurrentBufferIndex].image; }
		SwapchainImagePage& GetCurrentFrameImagePage() { return m_SwapchainImages[m_CurrentBufferIndex]; }
		vk::ImageView EnsureCurrentFrameImageView(GPUTextureView view);
		vk::Semaphore GetWaitDoneSemaphore() const { return m_WaitFrameDoneSemaphore; }
		vk::Semaphore GetPresentWaitingSemaphore() const { return m_CanPresentSemaphore; }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_CurrentFrameUsageFlags; }
		void WaitCurrentFrameBufferIndex();
		void MarkUsages(ResourceUsageFlags usages);
		void CopyFrom(SwapchainContext const& other);
	private:
		//Swapchain
		vk::SwapchainKHR m_Swapchain = nullptr;
		castl::vector<SwapchainImagePage> m_SwapchainImages;
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
		virtual bool GetKeyState(int keycode, int state) const override;
		virtual bool IsKeyDown(int keycode) const override;
		virtual bool IsKeyTriggered(int keycode) const override;
		virtual bool IsMouseDown(int mousecode) const override;
		virtual bool IsMouseUp(int mousecode) const override;
		virtual float GetMouseX() const override;
		virtual float GetMouseY() const override;

		virtual void CloseWindow() override;
		virtual void ShowWindow() override;
		virtual void SetWindowPos(int x, int y) override;
		virtual int2 GetWindowPos() const override;
		virtual void SetWindowSize(uint32_t width, uint32_t height) override;
		virtual uint2 GetWindowSize() const override;
		virtual void Focus() override;
		virtual bool GetWindowFocus() const override;
		virtual bool GetWindowMinimized() const override;
		virtual void SetWindowName(castl::string_view const& name) override;
		virtual void SetWindowAlpha(float alpha) override;
		virtual float GetDpiScale() const override;

		static void UpdateMonitors();
		static castl::vector<MonitorHandle> const& GetMonitors();
		inline bool ValidContext() const { return m_Width > 0 && m_Height > 0; }
		CWindowContext(CVulkanApplication& inOwner);
		bool NeedClose() const;
		bool Resized() const;
		void WaitCurrentFrameBufferIndex();
		vk::SwapchainKHR const& GetSwapchain() const { return m_SwapchainContext.GetSwapchain(); }
		TIndex GetCurrentFrameBufferIndex() const { return m_SwapchainContext.GetCurrentFrameBufferIndex(); }
		vk::Image GetCurrentFrameImage() const { return m_SwapchainContext.GetCurrentFrameImage(); }
		vk::ImageView EnsureCurrentFrameImageView(GPUTextureView const& viewDesc) { return m_SwapchainContext.EnsureCurrentFrameImageView(viewDesc); }
		vk::Semaphore GetWaitDoneSemaphore() const { return m_SwapchainContext.GetWaitDoneSemaphore(); }
		vk::Semaphore GetPresentWaitingSemaphore() const { return m_SwapchainContext.GetPresentWaitingSemaphore(); }
		ResourceUsageFlags GetCurrentFrameUsageFlags() const { return m_SwapchainContext.GetCurrentFrameUsageFlags(); }
		void MarkUsages(ResourceUsageFlags usages);
		void Resize(FrameType resizeFrame);
		void TickReleaseResources(FrameType releasingFrame);
		void UpdateSize();
		void UpdatePos();
		void Initialize(castl::string const& windowName
			, uint32_t initialWidth
			, uint32_t initialHeight
			, bool visible
			, bool focused
			, bool decorate
			, bool floating);
		void Release();
		bool NeedPresent() const;
		void PresentCurrentFrame();
		void PrepareForPresent(VulkanBarrierCollector& inoutBarrierCollector
			, castl::vector<vk::Semaphore>& inoutWaitSemaphores
			, castl::vector<vk::PipelineStageFlags>& inoutWaitStages
			, castl::vector<vk::Semaphore>& inoutSignalSemaphores);
	private:
		friend class glfwContext;
		castl::string m_WindowName;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		bool m_WindowPosDirty = false;
		int2 m_WindowPos = { 0, 0 };

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
