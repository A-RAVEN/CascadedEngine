#include "pch.h"
#include "CRenderBackend_Vulkan.h"
#include "WindowContext.h"

namespace graphics_backend
{
	void CRenderBackend_Vulkan::Initialize(castl::string const& appName, castl::string const& engineName)
	{
		m_Application.InitApp(appName, engineName);
	}

	void CRenderBackend_Vulkan::InitializeThreadContextCount(uint32_t threadCount)
	{
	}
	void CRenderBackend_Vulkan::ScheduleGPUFrame(CTaskGraph* taskGraph, GPUFrame const& gpuFrame)
	{
		m_Application.ScheduleGPUFrame(taskGraph, gpuFrame);
	}
	void CRenderBackend_Vulkan::Release()
	{
		m_Application.ReleaseApp();
	}
	castl::shared_ptr<WindowHandle> CRenderBackend_Vulkan::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
	{
		return m_Application.GetWindowHandle(window);
	}
	//castl::shared_ptr<WindowHandle> CRenderBackend_Vulkan::NewWindow(uint32_t width, uint32_t height, castl::string const& windowName
	//	, bool visible
	//	, bool focused
	//	, bool decorate
	//	, bool floating)
	//{
	//	return m_Application.CreateWindowContext(windowName, width, height
	//		, visible
	//		, focused
	//		, decorate
	//		, floating);
	//}

	bool CRenderBackend_Vulkan::AnyWindowRunning()
	{
		return m_Application.AnyWindowRunning();
	}

	void CRenderBackend_Vulkan::TickWindows()
	{
		m_Application.TickWindowContexts();
	}

	castl::shared_ptr<GPUBuffer> CRenderBackend_Vulkan::CreateGPUBuffer(EBufferUsageFlags usageFlags, uint64_t count, uint64_t stride)
	{
		return castl::shared_ptr<GPUBuffer>(m_Application.NewGPUBuffer(GPUBufferDescriptor::Create(usageFlags, count, stride)), [this](GPUBuffer* releaseBuffer)
			{
				m_Application.ReleaseGPUBuffer(releaseBuffer);
			});
	}
	castl::shared_ptr<GPUTexture> CRenderBackend_Vulkan::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor)
	{
		return castl::shared_ptr<GPUTexture>(m_Application.NewGPUTexture(inDescriptor)
			, [this](GPUTexture* releaseTex)
			{
				m_Application.ReleaseGPUTexture(releaseTex);
			});
	}
	/*uint32_t CRenderBackend_Vulkan::GetMonitorCount() const
	{
		return CWindowContext::GetMonitors().size();
	}
	MonitorHandle CRenderBackend_Vulkan::GetMonitorHandleAt(uint32_t monitorID) const
	{
		CA_ASSERT(monitorID < CWindowContext::GetMonitors().size(), "Invalid Monitor ID");
		return CWindowContext::GetMonitors()[monitorID];
	}

	void CRenderBackend_Vulkan::SetWindowFocusCallback(castl::function<void(WindowHandle*, bool)> callback)
	{
		glfwContext::s_Instance.m_WindowFocusCallback = callback;
	}

	void CRenderBackend_Vulkan::SetCursorEnterCallback(castl::function<void(WindowHandle*, bool)> callback)
	{
		glfwContext::s_Instance.m_CursorEnterCallback = callback;
	}

	void CRenderBackend_Vulkan::SetCursorPosCallback(castl::function<void(WindowHandle*, float, float)> callback)
	{
		glfwContext::s_Instance.m_CursorPosCallback = callback;
	}

	void CRenderBackend_Vulkan::SetMouseButtonCallback(castl::function<void(WindowHandle*, int, int, int)> callback)
	{
		glfwContext::s_Instance.m_MouseButtonCallback = callback;
	}

	void CRenderBackend_Vulkan::SetScrollCallback(castl::function<void(WindowHandle*, float, float)> callback)
	{
		glfwContext::s_Instance.m_ScrollCallback = callback;
	}

	void CRenderBackend_Vulkan::SetKeyCallback(castl::function<void(WindowHandle*, int, int, int, int)> callback)
	{
		glfwContext::s_Instance.m_KeyCallback = callback;
	}

	void CRenderBackend_Vulkan::SetCharCallback(castl::function<void(WindowHandle*, uint32_t)> callback)
	{
		glfwContext::s_Instance.m_CharCallback = callback;
	}

	void CRenderBackend_Vulkan::SetWindowCloseCallback(castl::function<void(WindowHandle*)> callback)
	{
		glfwContext::s_Instance.m_WindowCloseCallback = callback;
	}

	void CRenderBackend_Vulkan::SetWindowPosCallback(castl::function<void(WindowHandle*, float, float)> callback)
	{
		glfwContext::s_Instance.m_WindowPosCallback = callback;
	}

	void CRenderBackend_Vulkan::SetWindowSizeCallback(castl::function<void(WindowHandle*, float, float)> callback)
	{
		glfwContext::s_Instance.m_WindowSizeCallback = callback;
	}*/
}
