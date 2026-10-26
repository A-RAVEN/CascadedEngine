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
        /// 脚本入口：建一个保持可见的窗口（任务 2.3）。
        /// **不关窗、不 Dispose** —— 引擎的回收条件是"关闭且引用计数归 1"，所以它一直活到进程退出。
        /// </summary>
        public static void OnEngineReady(IWindowSystem windows)
        {
            CalledCount++;
            IWindow created = windows.NewWindow(320, 240, "脚本建的窗口", true, true, true, false);
            var (width, height) = created.GetWindowSize();
            LastCreatedSummary = $"{width} x {height}";
        }
    }
}
