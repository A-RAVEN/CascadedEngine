#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAFunctional.h>
#include <CAUtils.h>
#include <CodeGenMeta.h>
namespace cawindow
{
	class IWindowSystem;
	// 绑定：`IWindow` 是第一个真正接到 C# 的接口（规矩见 Tools/CodeGen/README.md 第十三节）。
	CA_BIND()
	class IWindow
	{
	public:
		virtual void CloseWindow() = 0;
		virtual bool WindowShouldClose() const = 0;
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

		virtual bool GetKeyState(int keycode, int state) const = 0;
		virtual float GetMouseX() const = 0;
		virtual float GetMouseY() const = 0;
		virtual bool IsKeyDown(int keycode) const = 0;
		virtual bool IsKeyTriggered(int keycode) const = 0;
		virtual bool IsMouseDown(int mousecode) const = 0;
		virtual bool IsMouseUp(int mousecode) const = 0;

		// 排除必须写理由（② 所有权不明裸指针）。这行标记放在**所有方法声明之后** —— 夹在方法之间
		// 会把审计脚本"virtual … ;"的扫描切断（实测：对账从 60 条掉到 59 条，报"僵尸行"）。
		CA_BIND_SKIP(GetWindowSystem, "② 所有权不明裸指针：返回 IWindowSystem 实例且无所有权说明")
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
		virtual castl::weak_ptr<IWindow> NewWindow(
			int width
			, int height
			, castl::string_view const& windowName = "Default Window"
			, bool visible = true
			, bool focused = true
			, bool decorate = true
			, bool floating = false) = 0;
		virtual int GetWindowCount() const = 0;
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