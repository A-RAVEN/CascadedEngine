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
        // 渲染基础夹具没有引擎实例；这些委托只用于编译期签名检查，永远不执行。
        private static IBindFoundation NoFoundation => null;

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

            // ---- 渲染基础夹具（csharp-render-binding-foundations 任务 1.3）----
            // 记录 / 枚举 / 标志位 / 包装值 / 字节数组都必须形成真实的公开签名：
            // 把 `GetFlags` 的参数还成 ushort、把 `ConsumeHash` 还成指针、把 `MixBytes` 的
            // byte[] 还成 byte*，下面的委托赋值都会编译失败。
            Func<RenderDesc, RenderDesc> getDesc = desc => NoFoundation.GetDesc(desc);
            Func<RenderNested, RenderNested> getNested = nested => NoFoundation.GetNested(nested);
            Func<bool, bool> getBool = value => NoFoundation.GetBool(value);
            Func<ERenderMode, ERenderMode> getMode = mode => NoFoundation.GetMode(mode);
            Func<ERenderFlag, ushort> getFlags = flags => NoFoundation.GetFlags(flags);
            Action<string> setName = name => NoFoundation.SetName(name);
            Action<string> setPath = path => NoFoundation.SetPath(path);
            Action<RenderHashable> consumeHash = value => NoFoundation.ConsumeHash(value);
            Func<byte[], int, RenderDesc, string, ERenderMode, ERenderFlag, IntPtr, long> mixBytes =
                (data, count, desc, label, mode, flags, token) =>
                    NoFoundation.MixBytes(data, count, desc, label, mode, flags, token);

            // 把这些引用"用掉"，避免 CS0219 之类的噪音（也防止有人把上面的赋值删掉还看不出来）
            _ = new object[]
            {
                newWindow, windowCount, monitorCount, updateSystem, systemNative,
                closeWindow, shouldClose, showWindow, setPos, getPos, setSize, getSize, focus, hasFocus,
                minimized, setWindowName, getWindowName, setAlpha, dpiScale, nativeHandle, windowSystem,
                keyState, mouseX, mouseY, isKeyDown, isKeyTriggered, isMouseDown, isMouseUp,
                getDesc, getNested, getBool, getMode, getFlags, setName, setPath, consumeHash, mixBytes,
            };
        }

        /// <summary>
        /// 公开值内容与真实 CLR 布局的**运行时**检查（csharp-render-binding-foundations 任务 1.3）。
        /// 返回 null 表示通过；否则返回点名字段 / 期望值 / 实际值的文本，由 `Host.Boot` 拒绝启动。
        /// </summary>
        internal static string VerifyValueTypes()
        {
            // ---- 枚举：底层类型、实际值、[Flags] ----
            string error = BindingLayoutCheck.VerifyEnum(typeof(ERenderMode), typeof(ushort),
                ("None", 0), ("Fill", 1), ("Wire", 2));
            if (error != null)
            {
                return error;
            }
            error = BindingLayoutCheck.VerifyEnum(typeof(ERenderFlag), typeof(ushort),
                ("None", 0), ("Depth", 1), ("Blend", 2), ("Cull", 4));
            if (error != null)
            {
                return error;
            }
            if (!BindingLayoutCheck.HasFlagsAttribute(typeof(ERenderFlag)))
            {
                return "枚举自检：ERenderFlag 缺少 [Flags]（它用于 EnumFlags<T>）";
            }
            if (BindingLayoutCheck.HasFlagsAttribute(typeof(ERenderMode)))
            {
                return "枚举自检：ERenderMode 不是标志位枚举，却带了 [Flags]";
            }

            // ---- 记录：字段顺序、字段类型、真实 CLR 大小与偏移 ----
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderDesc), 16,
                ("visible", 0), ("id", 4), ("mode", 8), ("flags", 10), ("weight", 12));
            if (error != null)
            {
                return error;
            }
            if (typeof(RenderDesc).GetField("visible").FieldType != typeof(bool))
            {
                return "记录自检：RenderDesc.visible 不是 bool（bool 公开字段不得退化成指针或数值）";
            }
            if (typeof(RenderDesc).GetField("id").FieldType != typeof(uint))
            {
                return "记录自检：RenderDesc.id 不是 uint";
            }
            if (typeof(RenderDesc).GetField("mode").FieldType != typeof(ERenderMode))
            {
                return "记录自检：RenderDesc.mode 不是公开枚举 ERenderMode";
            }
            if (typeof(RenderDesc).GetField("flags").FieldType != typeof(ERenderFlag))
            {
                return "记录自检：RenderDesc.flags 不是公开枚举 ERenderFlag";
            }
            if (typeof(RenderDesc).GetField("weight").FieldType != typeof(ushort))
            {
                return "记录自检：RenderDesc.weight 不是 ushort";
            }
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderNested), 20, ("desc", 0), ("scale", 16));
            if (error != null)
            {
                return error;
            }
            if (typeof(RenderNested).GetField("desc").FieldType != typeof(RenderDesc))
            {
                return "记录自检：RenderNested.desc 不是公开记录 RenderDesc";
            }
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderHashable), 8, ("first", 0), ("second", 4));
            if (error != null)
            {
                return error;
            }
            return null;
        }
    }
}
