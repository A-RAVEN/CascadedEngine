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
	};

	using FloatRect = cacore::Rect<float>;

	struct MonitorInfo
	{
		FloatRect m_MonitorRect;
		FloatRect m_WorkAreaRect;
		float m_DPIScale;
	};

	// 绑定：`IWindowSystem` 是第二个接到 C# 的接口（`csharp-window-creation` 任务 2.1）。
	// 16 个方法里 5 个进函数表；其余 11 条用 `CA_BIND_SKIP` 排除，理由一律写**本轮未实现** +
	// 缺的是哪条投影路径 —— 审计表对它们的结论仍是"可绑"，把"这轮没做"说成"永远绑不了"
	// 会让读表的人分不清两者（`check_bindable_audit.py` 会因此报错）。
	CA_BIND()
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

		// 以下 11 条本轮不进函数表（任务 2.1）。理由必须是"本轮未实现"+缺哪条投影路径 ——
		// 写成 ①–④ 会把"这轮没做"伪装成"形状本身绑不了"。
		CA_BIND_SKIP(SetWindowFocusCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetCursorEnterCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetCursorPosCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetMouseButtonCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetScrollCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetWindowCloseCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetWindowPosCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetWindowSizeCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetKeyCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(SetCharCallback, "本轮未实现：缺回调投影路径（保活表 + 注销句柄）")
		CA_BIND_SKIP(GetMonitor, "本轮未实现：成员是 cacore::Rect<float> 模板实例化类型，布局镜像只收成员全标量的结构体")
	};
}