// 托管表面的**编译期签名断言**（`csharp-window-creation` 任务 5.2 / 5.6）。
//
// 为什么要有这个文件：表面方法是不是"逐个镜像了原始 C++ 声明"，只比名字抓不住 —— 把
// `NewWindow` 的四个 `bool` 写成一个 `int`，名字判据照样绿。这里把每个方法赋给一个
// `Func<…>`（参数与返回类型都写死），签名映射错就**编译不过**。
//
// 这些委托**从不调用**（`Never` 为 null，赋值只是让编译器检查签名），所以不会真去建窗口。
using System;
using Scripting.Generated.Bindings;

namespace Scripting.Managed
{
    internal static class SurfaceSignatureCheck
    {
        // 表面对象在编译期只需要"类型对不对"，不需要实例：用 lambda 包住一次调用即可。
        private static IWindow NoWindow => null;
        private static IWindowSystem NoSystem => null;

        internal static void Compile()
        {
            // ---- IWindowSystem：5 个未排除方法（任务 5.6）----
            // `castl::weak_ptr<IWindow>` → 同名接口 `IWindow`；四个 `bool` 不许退化成 int
            Func<int, int, string, bool, bool, bool, bool, IWindow> newWindow =
                (width, height, name, visible, focused, decorate, floating) =>
                    NoSystem.NewWindow(width, height, name, visible, focused, decorate, floating);
            Func<int> windowCount = () => NoSystem.GetWindowCount();
            Func<int> monitorCount = () => NoSystem.GetMonitorCount();
            Action updateSystem = () => NoSystem.UpdateSystem();
            Func<IntPtr> systemNative = () => NoSystem.GetSystemNativeHandle();

            // ---- IWindow：23 个方法（原声明 23 条纯虚，任务 5.3 之后一条都不排除）----
            Action closeWindow = () => NoWindow.CloseWindow();
            Func<bool> shouldClose = () => NoWindow.WindowShouldClose();
            Action showWindow = () => NoWindow.ShowWindow();
            Action<int, int> setPos = (x, y) => NoWindow.SetWindowPos(x, y);
            Func<(int X, int Y)> getPos = () => NoWindow.GetWindowPos();
            Action<int, int> setSize = (w, h) => NoWindow.SetWindowSize(w, h);
            Func<(int X, int Y)> getSize = () => NoWindow.GetWindowSize();
            Action focus = () => NoWindow.Focus();
            Func<bool> hasFocus = () => NoWindow.GetWindowFocus();
            Func<bool> minimized = () => NoWindow.GetWindowMinimized();
            Action<string> setWindowName = name => NoWindow.SetWindowName(name);
            Func<string> getWindowName = () => NoWindow.GetWindowName();
            Action<float> setAlpha = alpha => NoWindow.SetWindowAlpha(alpha);
            Func<float> dpiScale = () => NoWindow.GetDpiScale();
            Func<IntPtr> nativeHandle = () => NoWindow.GetNativeWindowHandle();
            // 借用句柄：`IWindowSystem*` → 同名接口 `IWindowSystem`（任务 5.3 / design D9）
            Func<IWindowSystem> windowSystem = () => NoWindow.GetWindowSystem();
            Func<int, int, bool> keyState = (code, state) => NoWindow.GetKeyState(code, state);
            Func<float> mouseX = () => NoWindow.GetMouseX();
            Func<float> mouseY = () => NoWindow.GetMouseY();
            Func<int, bool> isKeyDown = code => NoWindow.IsKeyDown(code);
            Func<int, bool> isKeyTriggered = code => NoWindow.IsKeyTriggered(code);
            Func<int, bool> isMouseDown = code => NoWindow.IsMouseDown(code);
            Func<int, bool> isMouseUp = code => NoWindow.IsMouseUp(code);

            // 把这些引用"用掉"，避免 CS0219 之类的噪音（也防止有人把上面的赋值删掉还看不出来）
            _ = new object[]
            {
                newWindow, windowCount, monitorCount, updateSystem, systemNative,
                closeWindow, shouldClose, showWindow, setPos, getPos, setSize, getSize, focus, hasFocus,
                minimized, setWindowName, getWindowName, setAlpha, dpiScale, nativeHandle, windowSystem,
                keyState, mouseX, mouseY, isKeyDown, isKeyTriggered, isMouseDown, isMouseUp,
            };
        }
    }
}
