#pragma once
#include <CAWindow/WindowSystem.h>
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>
#include "GLFWInclude.h"

namespace cawindow
{
	class WindowSystem : public IWindowSystem
	{
	public:
		WindowSystem();
	public:
		// 通过 IWindowSystem 继承
		castl::shared_ptr<IWindow> NewWindow(int width, int height, castl::string_view const& windowName, bool visible, bool focused, bool decorate, bool floating) override;
		int GetMonitorCount() const override;
		void UpdateSystem() override;
		MonitorInfo GetMonitor(int monitorID) const override;
		void SetWindowFocusCallback(castl::function<void(IWindow*, bool)> callback) override;
		void SetCursorEnterCallback(castl::function<void(IWindow*, bool)> callback) override;
		void SetCursorPosCallback(castl::function<void(IWindow*, float, float)> callback) override;
		void SetMouseButtonCallback(castl::function<void(IWindow*, int, int, int)> callback) override;
		void SetScrollCallback(castl::function<void(IWindow*, float, float)> callback) override;
		void SetWindowCloseCallback(castl::function<void(IWindow*)> callback) override;
		void SetWindowPosCallback(castl::function<void(IWindow*, float, float)> callback) override;
		void SetWindowSizeCallback(castl::function<void(IWindow*, float, float)> callback) override;
		void SetKeyCallback(castl::function<void(IWindow*, int, int, int, int)> callback) override;
		void SetCharCallback(castl::function<void(IWindow*, uint32_t)> callback) override;
		void* GetSystemNativeHandle() override;
	private:
		void InitializeWindowCallbacks(GLFWwindow* window);
	private:
#if defined(_WIN32) || defined(_WIN64)
		HINSTANCE m_HInstance;
#endif
	public:
		castl::function<void(IWindow*, bool)> 					m_WindowFocusCallback = nullptr;
		castl::function<void(IWindow*, bool)> 					m_CursorEnterCallback = nullptr;
		castl::function<void(IWindow*, float, float)> 			m_CursorPosCallback = nullptr;
		castl::function<void(IWindow*, int, int, int)> 			m_MouseButtonCallback = nullptr;
		castl::function<void(IWindow*, float, float)> 			m_ScrollCallback = nullptr;
		castl::function<void(IWindow*, int, int, int, int)>		m_KeyCallback = nullptr;
		castl::function<void(IWindow*, int)> 					m_CharCallback = nullptr;
		castl::function<void(IWindow*)> 						m_WindowCloseCallback = nullptr;
		castl::function<void(IWindow*, float, float)> 			m_WindowPosCallback = nullptr;
		castl::function<void(IWindow*, float, float)> 			m_WindowSizeCallback = nullptr;
	};
}