#include "Window_Impl.h"
#include "WindowSystem_Impl.h"

namespace cawindow
{
	void WindowImpl::Initialize(WindowSystem* windowSystem, castl::string const& windowName, int initialWidth, int initialHeight, bool visible, bool focused, bool decorate, bool floating)
	{
		m_WindowSystem = windowSystem;
		m_WindowName = windowName;
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, visible);
		glfwWindowHint(GLFW_FOCUSED, focused);
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
		glfwWindowHint(GLFW_DECORATED, decorate);
		glfwWindowHint(GLFW_FLOATING, floating);
		m_Window = glfwCreateWindow(initialWidth, initialHeight, m_WindowName.c_str(), nullptr, nullptr);
		glfwSetWindowUserPointer(m_Window, this);

#if defined(_WIN32) || defined(_WIN64)
		m_Win32Window = glfwGetWin32Window(m_Window);
#endif
	}

	void WindowImpl::Release()
	{
		// L1-idempotence / Design D2: a second Release() must not glfwDestroyWindow(nullptr).
		if (!m_Window)
			return;

		// Design D2: the d3d12 teardown UAF (Rax=0xDDD, freed std::function) is NOT caused by this
		// destroy call — vendored GLFW 3.4 already removes every callback via
		// memset(&window->callbacks, 0, ...) before _glfwDestroyWindowWin32 (src/window.c). The event
		// is delivered by a concurrent non-destroy window op (SetWindowPos/SetWindowSize -> WM_ACTIVATE
		// -> focus) on a window that outlives the WindowSystem, deref'ing the freed s_WindowSystem.
		// The load-bearing fixes are: ~WindowSystem() nulls s_WindowSystem, and every GLFW callback
		// guards `WindowSystem* ws = s_WindowSystem; if (ws && ws->m_Xxx)`. No event is dispatched to
		// a freed std::function member because ws is null there. Idempotence still guards the destroy.
		glfwDestroyWindow(m_Window);
		m_Window = nullptr;
	}

	void WindowImpl::CloseWindow()
	{
		SetWindowName("Closing...");
		glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
	}
	bool WindowImpl::WindowShouldClose() const
	{
		return glfwWindowShouldClose(m_Window) == GLFW_TRUE;
	}
	void WindowImpl::ShowWindow()
	{
		glfwShowWindow(m_Window);
	}
	void WindowImpl::SetWindowPos(int inX, int inY)
	{
		glfwSetWindowPos(m_Window, inX, inY);
	}
	void WindowImpl::GetWindowPos(int& outX, int& outY) const
	{
		glfwGetWindowPos(m_Window, &outX, &outY);
	}
	void WindowImpl::SetWindowSize(int width, int height)
	{
		glfwSetWindowSize(m_Window, width, height);
	}
	void WindowImpl::GetWindowSize(int& outX, int& outY) const
	{
		glfwGetWindowSize(m_Window, &outX, &outY);
	}
	void WindowImpl::Focus()
	{
		glfwFocusWindow(m_Window);
	}
	bool WindowImpl::GetWindowFocus() const
	{
		int focused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED);
		return focused != 0;
	}
	bool WindowImpl::GetWindowMinimized() const
	{
		return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) != 0;
	}
	void WindowImpl::SetWindowName(castl::string const& name)
	{
		m_WindowName = name;
		glfwSetWindowTitle(m_Window, m_WindowName.c_str());
	}
	castl::string_view WindowImpl::GetWindowName() const
	{
		return m_WindowName;
	}
	void WindowImpl::SetWindowAlpha(float alpha)
	{
		glfwSetWindowOpacity(m_Window, alpha);
	}
	float WindowImpl::GetDpiScale() const
	{
		return 0.0f;
	}
	IWindowSystem* WindowImpl::GetWindowSystem()
	{
		return m_WindowSystem;
	}

	bool WindowImpl::GetKeyState(int keycode, int state) const
	{
		return glfwGetKey(m_Window, keycode) == state;
	}

	float WindowImpl::GetMouseX() const
	{
		double x, y;
		glfwGetCursorPos(m_Window, &x, &y);
		return static_cast<float>(x);
	}

	float WindowImpl::GetMouseY() const
	{
		double x, y;
		glfwGetCursorPos(m_Window, &x, &y);
		return static_cast<float>(y);
	}

	bool WindowImpl::IsKeyDown(int keycode) const
	{
		auto state = glfwGetKey(m_Window, keycode);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool WindowImpl::IsKeyTriggered(int keycode) const
	{
		auto state = glfwGetKey(m_Window, keycode);
		return state == GLFW_PRESS;
	}

	bool WindowImpl::IsMouseDown(int mousecode) const
	{
		auto state = glfwGetMouseButton(m_Window, mousecode);
		return state == GLFW_PRESS;
	}

	bool WindowImpl::IsMouseUp(int mousecode) const
	{
		auto state = glfwGetMouseButton(m_Window, mousecode);
		return state == GLFW_RELEASE;
	}

	void* WindowImpl::GetNativeWindowHandle()
	{
#if defined(_WIN32) || defined(_WIN64)
		return &m_Win32Window;
#endif
		return nullptr;
	}
}