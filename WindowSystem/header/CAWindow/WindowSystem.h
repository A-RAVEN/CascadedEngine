#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAFunctional.h>
#include <Utils.h>
namespace cawindow
{
	class IWindowSystem;
	class IWindow
	{
	public:
		virtual void CloseWindow() = 0;
		virtual void ShowWindow() = 0;
		virtual void SetWindowPos(int inX, int inY) = 0;
		virtual void GetWindowPos(int& outX, int& outY) const = 0;
		virtual void SetWindowSize(int width, int height) = 0;
		virtual void GetWindowSize(int& outX, int& outY) const = 0;
		virtual void Focus() = 0;
		virtual bool GetWindowFocus() const = 0;
		virtual bool GetWindowMinimized() const = 0;
		virtual void SetWindowName(castl::string const& name) = 0;
		virtual castl::string_view GetWindowName() const = 0;
		virtual void SetWindowAlpha(float alpha) = 0;
		virtual float GetDpiScale() const = 0;
		virtual void* GetNativeWindowHandle() = 0;
		virtual IWindowSystem* GetWindowSystem() = 0;
	};

	using FloatRect = cacore::Rect<float>;

	struct MonitorInfo
	{
		FloatRect m_MonitorRect;
		FloatRect m_WorkAreaRect;
		float m_DPIScale;
	};

	class IWindowSystem
	{
	public:
		virtual castl::shared_ptr<IWindow> NewWindow(
			int width
			, int height
			, castl::string_view const& windowName = "Default Window"
			, bool visible = true
			, bool focused = true
			, bool decorate = true
			, bool floating = false) = 0;

		virtual int GetMonitorCount() const = 0;
		virtual MonitorInfo GetMonitor(int monitorID) const = 0;

		virtual void UpdateSystem() = 0;

		virtual void SetWindowFocusCallback(castl::function<void(IWindow*, bool)> callback) = 0;
		virtual void SetCursorEnterCallback(castl::function<void(IWindow*, bool)> callback) = 0;
		virtual void SetCursorPosCallback(castl::function<void(IWindow*, float, float)> callback) = 0;
		virtual void SetMouseButtonCallback(castl::function<void(IWindow*, int, int, int)> callback) = 0;
		virtual void SetScrollCallback(castl::function<void(IWindow*, float, float)> callback) = 0;
		virtual void SetWindowCloseCallback(castl::function<void(IWindow*)> callback) = 0;
		virtual void SetWindowPosCallback(castl::function<void(IWindow*, float, float)> callback) = 0;
		virtual void SetWindowSizeCallback(castl::function<void(IWindow*, float, float)> callback) = 0;

		virtual void SetKeyCallback(castl::function<void(IWindow*, int, int, int, int)> callback) = 0;
		virtual void SetCharCallback(castl::function<void(IWindow*, uint32_t)> callback) = 0;

		virtual void* GetSystemNativeHandle() = 0;
	};
}