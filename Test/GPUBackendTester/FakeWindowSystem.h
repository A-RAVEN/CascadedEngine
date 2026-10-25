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
		cawindow::MonitorInfo GetMonitor(int) const override { return cawindow::MonitorInfo{}; }
		void UpdateSystem() override {}
		void SetWindowFocusCallback(castl::function<void(cawindow::IWindow*, bool)>) override {}
		void SetCursorEnterCallback(castl::function<void(cawindow::IWindow*, bool)>) override {}
		void SetCursorPosCallback(castl::function<void(cawindow::IWindow*, float, float)>) override {}
		void SetMouseButtonCallback(castl::function<void(cawindow::IWindow*, int, int, int)>) override {}
		void SetScrollCallback(castl::function<void(cawindow::IWindow*, float, float)>) override {}
		void SetWindowCloseCallback(castl::function<void(cawindow::IWindow*)>) override {}
		void SetWindowPosCallback(castl::function<void(cawindow::IWindow*, float, float)>) override {}
		void SetWindowSizeCallback(castl::function<void(cawindow::IWindow*, float, float)>) override {}
		void SetKeyCallback(castl::function<void(cawindow::IWindow*, int, int, int, int)>) override {}
		void SetCharCallback(castl::function<void(cawindow::IWindow*, uint32_t)>) override {}
		void* GetSystemNativeHandle() override { return nullptr; }

	private:
		std::vector<castl::shared_ptr<Window>> m_windows;
	};

	inline cawindow::IWindowSystem* Window::GetWindowSystem()
	{
		return m_owner;
	}
}
