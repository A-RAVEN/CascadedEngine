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
		glfwDestroyWindow(m_Window);
		m_Window = nullptr;
	}

	void WindowImpl::CloseWindow()
	{
		SetWindowName("Closing...");
		glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
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
		return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
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
	void* WindowImpl::GetNativeWindowHandle()
	{
#if defined(_WIN32) || defined(_WIN64)
		return &m_Win32Window;
#endif
		return nullptr;
	}
}