// 任务 5.4 的测试夹具：`IWindowSystem` 的**第二份实现**。
//
// 为什么需要它：判据是"用两个不同实例名登记同一个接口的两份实现，对第二份上的窗口调
// `GetWindowSystem()`，返回值 MUST 来自第二份"。仓库里只有一个真实实现
// （`WindowSystem_GLFW` —— `CA_MODULE_INSTANCE(cawindow::IWindowSystem, cawindow::WindowSystem,
// WindowSystem_GLFW)` 只声明了一个实例名），所以第二份只能由测试自己给。
//
// 这份实现的 `Window::GetWindowSystem()` 回**自己**，所以"指针相等"就能证明取到的是哪一份；
// 它不碰 GLFW、不建真窗口，只回答尺寸与名字（本夹具不参与渲染）。
#pragma once
#include <CAWindow/WindowSystem.h>

#include <memory>
#include <vector>

namespace fake_window
{
	class System;

	class Window final : public cawindow::IWindow
	{
	public:
		explicit Window(System* owner) : m_owner(owner) {}

		void CloseWindow() override {}
		bool WindowShouldClose() const override { return false; }
		void ShowWindow() override {}
		void SetWindowPos(int, int) override {}
		void GetWindowPos(int& outX, int& outY) const override
		{
			outX = 0;
			outY = 0;
		}
		void SetWindowSize(int width, int height) override
		{
			m_width = width;
			m_height = height;
		}
		void GetWindowSize(int& outX, int& outY) const override
		{
			outX = m_width;
			outY = m_height;
		}
		void Focus() override {}
		bool GetWindowFocus() const override { return false; }
		bool GetWindowMinimized() const override { return false; }
		void SetWindowName(castl::string const& name) override { m_name = name; }
		castl::string_view GetWindowName() const override { return m_name; }
		void SetWindowAlpha(float) override {}
		float GetDpiScale() const override { return 1.0f; }
		void* GetNativeWindowHandle() override { return nullptr; }
		cawindow::IWindowSystem* GetWindowSystem() override;
		bool GetKeyState(int, int) const override { return false; }
		float GetMouseX() const override { return 0.0f; }
		float GetMouseY() const override { return 0.0f; }
		bool IsKeyDown(int) const override { return false; }
		bool IsKeyTriggered(int) const override { return false; }
		bool IsMouseDown(int) const override { return false; }
		bool IsMouseUp(int) const override { return false; }

	private:
		System* m_owner = nullptr;
		int m_width = 0;
		int m_height = 0;
		castl::string m_name = "Fake Window";
	};

	class System final : public cawindow::IWindowSystem
	{
	public:
		castl::weak_ptr<cawindow::IWindow> NewWindow(int width, int height,
		                                            castl::string_view const& windowName, bool, bool, bool,
		                                            bool) override
		{
			auto window = std::make_shared<Window>(this);
			window->SetWindowSize(width, height);
			window->SetWindowName(castl::string(windowName.data(), windowName.size()));
			m_windows.push_back(window);
			return window;
		}

		int GetWindowCount() const override { return static_cast<int>(m_windows.size()); }
		int GetMonitorCount() const override { return 1; }
		cawindow::MonitorInfo GetMonitor(int monitorId) const override;
		void UpdateSystem() override {}
		// 10 类回调（本 change 任务 4.1 / 5.2）：真实保存，测试用 `Trigger*` 确定性触发。
		void SetWindowFocusCallback(castl::function<void(cawindow::IWindow*, bool)> callback) override
		{
			m_windowFocus = callback;
		}
		void SetCursorEnterCallback(castl::function<void(cawindow::IWindow*, bool)> callback) override
		{
			m_cursorEnter = callback;
		}
		void SetCursorPosCallback(castl::function<void(cawindow::IWindow*, float, float)> callback) override
		{
			m_cursorPos = callback;
		}
		void SetMouseButtonCallback(castl::function<void(cawindow::IWindow*, int, int, int)> callback) override
		{
			m_mouseButton = callback;
		}
		void SetScrollCallback(castl::function<void(cawindow::IWindow*, float, float)> callback) override
		{
			m_scroll = callback;
		}
		void SetWindowCloseCallback(castl::function<void(cawindow::IWindow*)> callback) override
		{
			m_windowClose = callback;
		}
		void SetWindowPosCallback(castl::function<void(cawindow::IWindow*, float, float)> callback) override
		{
			m_windowPos = callback;
		}
		void SetWindowSizeCallback(castl::function<void(cawindow::IWindow*, float, float)> callback) override
		{
			m_windowSize = callback;
		}
		void SetKeyCallback(castl::function<void(cawindow::IWindow*, int, int, int, int)> callback) override
		{
			m_key = callback;
		}
		void SetCharCallback(castl::function<void(cawindow::IWindow*, uint32_t)> callback) override
		{
			m_char = callback;
		}

		int TriggerWindowFocus(cawindow::IWindow* window, bool value)
		{
			if (!m_windowFocus) { return 0; }
			m_windowFocus(window, value);
			return 1;
		}
		int TriggerCursorEnter(cawindow::IWindow* window, bool value)
		{
			if (!m_cursorEnter) { return 0; }
			m_cursorEnter(window, value);
			return 1;
		}
		int TriggerCursorPos(cawindow::IWindow* window, float x, float y)
		{
			if (!m_cursorPos) { return 0; }
			m_cursorPos(window, x, y);
			return 1;
		}
		int TriggerMouseButton(cawindow::IWindow* window, int button, int action, int mods)
		{
			if (!m_mouseButton) { return 0; }
			m_mouseButton(window, button, action, mods);
			return 1;
		}
		int TriggerScroll(cawindow::IWindow* window, float x, float y)
		{
			if (!m_scroll) { return 0; }
			m_scroll(window, x, y);
			return 1;
		}
		int TriggerWindowClose(cawindow::IWindow* window)
		{
			if (!m_windowClose) { return 0; }
			m_windowClose(window);
			return 1;
		}
		int TriggerWindowPos(cawindow::IWindow* window, float x, float y)
		{
			if (!m_windowPos) { return 0; }
			m_windowPos(window, x, y);
			return 1;
		}
		int TriggerWindowSize(cawindow::IWindow* window, float x, float y)
		{
			if (!m_windowSize) { return 0; }
			m_windowSize(window, x, y);
			return 1;
		}
		int TriggerKey(cawindow::IWindow* window, int key, int scanCode, int action, int mods)
		{
			if (!m_key) { return 0; }
			m_key(window, key, scanCode, action, mods);
			return 1;
		}
		int TriggerChar(cawindow::IWindow* window, uint32_t codepoint)
		{
			if (!m_char) { return 0; }
			m_char(window, codepoint);
			return 1;
		}
		void* GetSystemNativeHandle() override { return nullptr; }

	private:
		std::vector<castl::shared_ptr<Window>> m_windows;
		castl::function<void(cawindow::IWindow*, bool)> m_windowFocus;
		castl::function<void(cawindow::IWindow*, bool)> m_cursorEnter;
		castl::function<void(cawindow::IWindow*, float, float)> m_cursorPos;
		castl::function<void(cawindow::IWindow*, int, int, int)> m_mouseButton;
		castl::function<void(cawindow::IWindow*, float, float)> m_scroll;
		castl::function<void(cawindow::IWindow*)> m_windowClose;
		castl::function<void(cawindow::IWindow*, float, float)> m_windowPos;
		castl::function<void(cawindow::IWindow*, float, float)> m_windowSize;
		castl::function<void(cawindow::IWindow*, int, int, int, int)> m_key;
		castl::function<void(cawindow::IWindow*, uint32_t)> m_char;
	};

	// 固定非零显示器读数（本 change 任务 3.2）：每个字段都不同，任一字段被清零、错序或漏搬都能在
	// C++ 与托管的日志对拍里抓出来。原生实现的越界行为仍是 clamp（绑定层必须在此之前拦下，
	// 所以这里的 clamp 只作为"真实实现会返回另一个显示器"的对照）。
	inline cawindow::MonitorInfo System::GetMonitor(int monitorId) const
	{
		if (monitorId < 0 || monitorId >= GetMonitorCount())
		{
			monitorId = 0;
		}
		(void)monitorId;
		cawindow::MonitorInfo info{};
		info.m_MonitorRect.x = 10.5f;
		info.m_MonitorRect.y = 20.5f;
		info.m_MonitorRect.width = 1600.0f;
		info.m_MonitorRect.height = 900.0f;
		info.m_WorkAreaRect.x = 15.5f;
		info.m_WorkAreaRect.y = 25.5f;
		info.m_WorkAreaRect.width = 1580.0f;
		info.m_WorkAreaRect.height = 860.0f;
		info.m_DPIScale = 1.5f;
		return info;
	}

	inline cawindow::IWindowSystem* Window::GetWindowSystem()
	{
		return m_owner;
	}
}
