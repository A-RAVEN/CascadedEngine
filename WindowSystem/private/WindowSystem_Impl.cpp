#include <LibraryExportCommon.h>
#include "WindowSystem_Impl.h"
#include "Window_Impl.h"

namespace cawindow
{
#pragma region GLFW Callbacks
	static WindowSystem* s_WindowSystem = nullptr;

#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()
#define IM_ARRAYSIZE(_ARR)          ((int)(sizeof(_ARR) / sizeof(*(_ARR))))     // Size of a static C-style array. Don't use on pointers!

	static int WindowSystem_ImplGlfw_TranslateUntranslatedKey(int key, int scancode)
	{
#if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
		// GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
		// (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
		// See https://github.com/glfw/glfw/issues/1502 for details.
		// Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
		// This won't cover edge cases but this is at least going to cover common cases.
		if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
			return key;
		GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
		const char* key_name = glfwGetKeyName(key, scancode);
		glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__) // Eat errors (see #5908)
		(void)glfwGetError(nullptr);
#endif
		if (key_name && key_name[0] != 0 && key_name[1] == 0)
		{
			const char char_names[] = "`-=[]\\,;\'./";
			const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
			CA_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys), "In Compatible Char Size");
			if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
			else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
			else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
			else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
		}
		// if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
		IM_UNUSED(scancode);
#endif
		return key;
	}


	void WindowSystem_ImplGlfw_KeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_KeyCallback)
		{
			keycode = WindowSystem_ImplGlfw_TranslateUntranslatedKey(keycode, scancode);
			s_WindowSystem->m_KeyCallback(windowHandle, keycode, scancode, action, mods);
		}
	}

	void WindowSystem_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_WindowFocusCallback)
		{
			s_WindowSystem->m_WindowFocusCallback(windowHandle, focused);
		}
	}

	void WindowSystem_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_CursorPosCallback)
		{
			s_WindowSystem->m_CursorPosCallback(windowHandle, x, y);
		}
	}

	// Workaround: X11 seems to send spurious Leave/Enter events which would make us lose our position,
	// so we back it up and restore on Leave/Enter (see https://github.com/ocornut/imgui/issues/4984)
	void WindowSystem_ImplGlfw_CursorEnterCallback(GLFWwindow* window, int entered)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_CursorEnterCallback)
		{
			s_WindowSystem->m_CursorEnterCallback(windowHandle, entered);
		}
	}

	void WindowSystem_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int c)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_CharCallback)
		{
			s_WindowSystem->m_CharCallback(windowHandle, c);
		}
	}

	void WindowSystem_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_MouseButtonCallback)
		{
			s_WindowSystem->m_MouseButtonCallback(windowHandle, button, action, mods);
		}
	}

	void WindowSystem_ImplGlfw_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_ScrollCallback)
		{
			s_WindowSystem->m_ScrollCallback(windowHandle, xoffset, yoffset);
		}
	}


	static void WindowSystem_ImplGlfw_WindowCloseCallback(GLFWwindow* window)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_WindowCloseCallback)
		{
			s_WindowSystem->m_WindowCloseCallback(windowHandle);
		}
	}

	// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
	// However: depending on the platform the callback may be invoked at different time:
	// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
	// - on Linux it is queued and invoked during glfwPollEvents()
	// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
	// ignore recent glfwSetWindowXXX() calls.
	void WindowSystem_ImplGlfw_WindowPosCallback(GLFWwindow* window, int x, int y)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_WindowPosCallback)
		{
			s_WindowSystem->m_WindowPosCallback(windowHandle, x, y);
		}
	}

	void WindowSystem_ImplGlfw_WindowSizeCallback(GLFWwindow* window, int width, int height)
	{
		WindowImpl* windowHandle = reinterpret_cast<WindowImpl*>(glfwGetWindowUserPointer(window));
		if (s_WindowSystem->m_WindowSizeCallback)
		{
			s_WindowSystem->m_WindowSizeCallback(windowHandle, width, height);
		}
	}

#pragma endregion


	WindowSystem::WindowSystem()
	{
		glfwInit();
		glfwSetErrorCallback([](int error, const char* msg)
			{
				std::cerr << "glfw Error: " << "(" << error << ")" << msg << std::endl;
			});
		s_WindowSystem = this;

#if defined(_WIN32) || defined(_WIN64)
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)glfwInit,
			(HMODULE*)&m_HInstance))
		{
			CA_LOG_ERR("Win32 Instance Not Exist!");
		}
#endif
	}
	castl::weak_ptr<IWindow> WindowSystem::NewWindow(int width, int height, castl::string_view const& windowName, bool visible, bool focused, bool decorate, bool floating)
	{
		WindowImpl* window = new WindowImpl();
		window->Initialize(this, castl::string{ windowName }, width, height, visible, focused, decorate, floating);
		InitializeWindowCallbacks(window->m_Window);
		castl::shared_ptr<IWindow> result = castl::shared_ptr<IWindow>(window, [this](IWindow* releasedWindow)
			{
				auto pwindowImpl = static_cast<WindowImpl*>(releasedWindow);
				pwindowImpl->Release();
				delete pwindowImpl;
			});
		m_Windows.push_back(result);
		return m_Windows.back();
	}
	int WindowSystem::GetWindowCount() const
	{
		return m_Windows.size();
	}
	int WindowSystem::GetMonitorCount() const
	{
		return m_Monitors.size();
	}
	void WindowSystem::UpdateSystem()
	{
		glfwPollEvents();
		UpdateMonitors();
		UpdateWindows();
	}

	MonitorInfo WindowSystem::GetMonitor(int monitorID) const
	{
		if (m_Monitors.empty())
			return {};
		return m_Monitors[castl::clamp(monitorID, 0, (int)m_Monitors.size() - 1)];
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
	void* WindowSystem::GetSystemNativeHandle()
	{
#if defined(_WIN32) || defined(_WIN64)
		return &m_HInstance;
#endif
		return nullptr;
	}
	void WindowSystem::InitializeWindowCallbacks(GLFWwindow* window)
	{
		glfwSetWindowFocusCallback(window, WindowSystem_ImplGlfw_WindowFocusCallback);
		glfwSetCursorEnterCallback(window, WindowSystem_ImplGlfw_CursorEnterCallback);
		glfwSetCursorPosCallback(window, WindowSystem_ImplGlfw_CursorPosCallback);
		glfwSetMouseButtonCallback(window, WindowSystem_ImplGlfw_MouseButtonCallback);
		glfwSetScrollCallback(window, WindowSystem_ImplGlfw_ScrollCallback);
		glfwSetKeyCallback(window, WindowSystem_ImplGlfw_KeyCallback);
		glfwSetCharCallback(window, WindowSystem_ImplGlfw_CharCallback);
		glfwSetWindowCloseCallback(window, WindowSystem_ImplGlfw_WindowCloseCallback);
		glfwSetWindowPosCallback(window, WindowSystem_ImplGlfw_WindowPosCallback);
		glfwSetWindowSizeCallback(window, WindowSystem_ImplGlfw_WindowSizeCallback);
	}
	void WindowSystem::UpdateMonitors()
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

			MonitorInfo monitorHandle{};
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
	void WindowSystem::UpdateWindows()
	{
		size_t currentIndex = 0;
		while (currentIndex < m_Windows.size())
		{
			if (m_Windows[currentIndex].use_count() == 1 && m_Windows[currentIndex]->WindowShouldClose())
			{
				castl::swap(m_Windows[currentIndex], m_Windows.back());
				m_Windows.pop_back();
			}
			else
			{
				currentIndex++;
			}
		}
	}
#pragma endregion

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IWindowSystem, WindowSystem)
}

