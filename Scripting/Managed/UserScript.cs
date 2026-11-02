using System;
using Scripting.Generated.Bindings;

namespace Scripting.Managed
{
    /// <summary>
    /// 用户脚本入口。
    ///
    /// 宿主（`Host.cs`）在**编号 1、2 两张函数表都绑定成功之后**调用 `OnEngineReady` 一次，
    /// 整个进程生命周期内只调一次。要写自己的 C# 逻辑就改这个文件 —— 重新构建即生效，
    /// 不需要碰 `Host.cs`（那一份是引擎胶水，改坏了连 ABI 自检都会一起坏）。
    ///
    /// 注意两条边界：
    /// ① 传进来的 `windows` 只在"引擎换默认实例"之前一直有效；重绑之后它指向被替换掉的表面对象，
    ///    所以不要把它存进静态字段跨重绑使用。
    /// ② 建出来的窗口是**弱持有**：脚本不 `CloseWindow()`，它就一直活到进程退出（引擎的回收条件是
    ///    "关闭且引用计数归 1"），Dispose 只释放句柄、不销毁窗口。
    /// </summary>
    public static class UserScript
    {
        /// <summary>
        /// 被调用了几次。`Host.cs` 与测试用它验"只调一次"，在 `Tick` 之后读它即可。
        /// </summary>
        internal static int CalledCount;

        /// <summary>
        /// 建窗时读到的尺寸摘要（`宽 x 高`）。宿主拿它打 `[脚本] 建窗：` 那行 ——
        /// 托管侧自己的 `Console.Out` 在测试进程里会被改道到不同落点（实测同一条命令两次落点不同），
        /// 只有走宿主那份日志才稳定可见。
        /// </summary>
        internal static string LastCreatedSummary = string.Empty;

        /// <summary>
        /// 脚本建出来的那个窗口的表面对象（csharp-render-binding-foundations 任务 1.3 的目标释放探针
        /// 用它）：`Dispose` 只释放目标句柄，不关窗、不改变引擎的窗口计数。
        /// </summary>
        internal static IWindow LastCreated;

        /// <summary>
        /// 脚本入口：建一个保持可见的窗口（任务 2.3）。
        /// **不关窗、不 Dispose** —— 引擎的回收条件是"关闭且引用计数归 1"，所以它一直活到进程退出。
        /// </summary>
        public static void OnEngineReady(IWindowSystem windows)
        {
            CalledCount++;
            IWindow created = windows.NewWindow(320, 240, "脚本建的窗口", true, true, true, false);
            LastCreated = created;
            var (width, height) = created.GetWindowSize();
            LastCreatedSummary = $"{width} x {height}";
        }
    }

    /// <summary>
    /// 回调探针的委托与期望读数（本 change 任务 1.2 骨架；4.1–4.3 在 `Host.ProbeCallbacks` 里使用）。
    ///
    /// 全部是常量：`TestCSharpWindowCallbacks` 的假窗口系统按同一组数字触发事件，两边不靠"互相抄"，
    /// 而是各自对着同一份写死的夹具值比较 —— 一边清零或错序，比对就红。
    /// </summary>
    internal static class CallbackProbe
    {
        /// <summary>假窗口的固定尺寸（`FakeWindowSystem` 的 `TestCSharpWindowCallbacks` 用例按它建窗）。</summary>
        internal const int WindowWidth = 640;
        internal const int WindowHeight = 480;

        /// <summary>假窗口的固定名字：回调里读 `IWindow.GetWindowName()` 用它证明拿到的是同一个窗口。</summary>
        internal const string WindowName = "回调探针窗口";

        /// <summary>用户委托抛出的原始异常文本：下一次主线程调用必须原样读回。</summary>
        internal const string ThrowingExceptionText = "回调探针故意抛出的异常文本";

        // 十类回调的固定标量参数（引擎侧触发时用同一组）
        internal const int FocusValue = 1;
        internal const int CursorEnterValue = 1;
        internal const float CursorX = 12.5f;
        internal const float CursorY = 34.5f;
        internal const int MouseButton = 1;
        internal const int MouseAction = 0;
        internal const int MouseMods = 3;
        internal const float ScrollX = 1.5f;
        internal const float ScrollY = -2.5f;
        internal const float WindowPosX = 100.5f;
        internal const float WindowPosY = 200.5f;
        internal const float WindowSizeX = 800.5f;
        internal const float WindowSizeY = 600.5f;
        internal const int KeyCode = 65;
        internal const int KeyScanCode = 30;
        internal const int KeyAction = 1;
        internal const int KeyMods = 2;
        internal const uint CharCode = 65u;
    }
}
