#include "WindowSystem_Impl.h"

namespace cawindow
{
	WindowSystem::WindowSystem()
	{
		glfwInit();
		glfwSetErrorCallback([](int error, const char* msg)
			{
				std::cerr << "glfw Error: " << "(" << error << ")" << msg << std::endl;
			});
	}
	castl::shared_ptr<IWindow> WindowSystem::NewWindow(int width, int height, castl::string_view const& windowName, bool visible, bool focused, bool decorate, bool floating)
	{
		return castl::shared_ptr<IWindow>();
	}
	int WindowSystem::GetMonitorCount() const
	{
		return 0;
	}
	void WindowSystem::UpdateSystem()
	{
	}

	MonitorInfo WindowSystem::GetMonitor(int monitorID) const
	{
		return MonitorInfo();
	}

#pragma region Callbacks
	void WindowSystem::SetWindowFocusCallback(castl::function<void(IWindow*, bool)> callback)
	{
		m_WindowFocusCallback = callback;
	}
	void WindowSystem::SetCursorEnterCallback(castl::function<void(IWindow*, bool)> callback)
	{
		m_CursorEnterCallback = callback;
	}
	void WindowSystem::SetCursorPosCallback(castl::function<void(IWindow*, float, float)> callback)
	{
		m_CursorPosCallback = callback;
	}
	void WindowSystem::SetMouseButtonCallback(castl::function<void(IWindow*, int, int, int)> callback)
	{
		m_MouseButtonCallback = callback;
	}
	void WindowSystem::SetScrollCallback(castl::function<void(IWindow*, float, float)> callback)
	{
		m_ScrollCallback = callback;
	}
	void WindowSystem::SetWindowCloseCallback(castl::function<void(IWindow*)> callback)
	{
		m_WindowCloseCallback = callback;
	}
	void WindowSystem::SetWindowPosCallback(castl::function<void(IWindow*, float, float)> callback)
	{
		m_WindowPosCallback = callback;
	}
	void WindowSystem::SetWindowSizeCallback(castl::function<void(IWindow*, float, float)> callback)
	{
		m_WindowSizeCallback = callback;
	}
	void WindowSystem::SetKeyCallback(castl::function<void(IWindow*, int, int, int, int)> callback)
	{
		m_KeyCallback = callback;
	}
	void WindowSystem::SetCharCallback(castl::function<void(IWindow*, uint32_t)> callback)
	{
		m_CharCallback = callback;
	}
#pragma endregion
}