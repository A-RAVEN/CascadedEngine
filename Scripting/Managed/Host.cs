using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Scripting.Generated.Bindings;

namespace Scripting.Managed
{
    /// <summary>
    /// 托管侧手写壳：被引擎调用的入口、ABI 自检、异常→错误码的唯一关口。
    /// 所有入口都是 [UnmanagedCallersOnly] 静态方法，且都被 try/catch 包住 ——
    /// 托管异常穿过 C ABI 边界是未定义行为。
    /// </summary>
    public static unsafe class Host
    {
        // 与 C++ 侧必须逐字一致
        private const uint ExpectedAbiVersion = 4;

        private static EngineApi* s_api;
        private static int s_tickCount;
        private static int s_lastEcho;
        private static string s_lastError = string.Empty;

        // 错误码（引擎侧按同一张表解释）
        private const int Ok = 0;
        private const int ErrAbiVersion = 1;
        private const int ErrAbiSize = 2;
        private const int ErrAbiNull = 3;
        private const int ErrException = 4;

        [UnmanagedCallersOnly]
        public static int Boot(IntPtr apiPointer)
        {
            try
            {
                if (apiPointer == IntPtr.Zero)
                {
                    s_lastError = "引擎传进来的函数表指针是空的";
                    return ErrAbiNull;
                }

                var api = (EngineApi*)apiPointer;

                if (api->AbiVersion != ExpectedAbiVersion)
                {
                    s_lastError = $"ABI 版本不符：引擎 {api->AbiVersion}，托管侧期望 {ExpectedAbiVersion}";
                    LogVia(api, s_lastError); // 失败也要让引擎侧看到"两边的版本号"
                    return ErrAbiVersion;
                }

                uint expectedSize = (uint)sizeof(EngineApi);
                if (api->Size != expectedSize)
                {
                    s_lastError = $"函数表大小不符：引擎 {api->Size} 字节，托管侧 {expectedSize} 字节";
                    LogVia(api, s_lastError);
                    return ErrAbiSize;
                }

                if (api->Log == null || api->Echo == null || api->GetInterfaceApi == null)
                {
                    s_lastError = "函数表里有空指针（Log / Echo 至少有一个是空的）";
                    return ErrAbiNull;
                }

                s_api = api;
                s_lastError = string.Empty;

                LogLine($"托管侧已启动：ABI 版本 {api->AbiVersion}、函数表 {api->Size} 字节");
                LogLine($"托管侧看到的 get_interface_api 地址 = {((long)api->GetInterfaceApi):X}");
                s_lastEcho = api->Echo(42);
                LogLine($"回调引擎 Echo(42) 得到 {s_lastEcho}");
                return Ok;
            }
            catch (Exception ex)
            {
                s_lastError = "Boot 抛异常：" + ex.GetType().Name + ": " + ex.Message;
                return ErrException;
            }
        }

        [UnmanagedCallersOnly]
        public static int Tick(double deltaSeconds)
        {
            try
            {
                if (s_api == null)
                {
                    s_lastError = "Tick 在 Boot 之前被调用";
                    return ErrAbiNull;
                }

#if DEBUG
                // 自检开关（只在 Debug 生效）：让层 ③ 的失败路径测试不必临时改源码就能构造"托管抛异常"。
                if (s_tickCount == 0 &&
                    Environment.GetEnvironmentVariable("CASCADED_CSHARP_SELFTEST") == "throw")
                {
                    throw new InvalidOperationException("自检开关 CASCADED_CSHARP_SELFTEST=throw 触发的故意异常");
                }
#endif

                s_tickCount++;
                LogLine($"Tick #{s_tickCount}：dt = {deltaSeconds:F3} 秒");
                EnsureBound();
                ProbeWindowSize();
                ProbeExceptionMapping();
                // 关窗读数每帧都要读：第 1 帧窗口还开着（0），引擎侧关窗后再 tick 一次才是 1。
                // 放进 `s_tickCount == 1` 里就只剩一个 0 —— 实测过，那一版日志里没有第二次读数。
                // 一旦读到"该关了"，就说明引擎侧马上要把它回收掉：此后不再碰旧窗口的 self，
                // 下一次 Tick 改走"表面建窗"探针（`csharp-window-creation` 任务 5.5）。
                if (s_engineWindowClosed)
                {
                    ProbeSurfaceWindow();
                }
                else
                {
                    ProbeWindowClose();
                }
                return s_tickCount; // 回值就是本次是第几次 Tick，引擎侧据此逐字比对
            }
            catch (Exception ex)
            {
                s_lastError = "Tick 抛异常：" + ex.GetType().Name + ": " + ex.Message;
                LogLine(s_lastError); // 文本必须回到引擎侧，否则只剩一个光秃秃的错误码
                return ErrException;
            }
        }

        // 托管侧只用**生成出来的表面**（`csharp-window-creation` 任务 5.5）：不再手抄偏移常量、不再把
        // `delegate*` 取出来自己转。表版本 = 2（任务 5.10 的错误通道两槽 + 5.2 的句柄解析/释放两槽都在表头）。
        private const int WindowApiTableVersion = 2;
        // 编号由引擎侧单向下发（任务 4.2），实例名传 null = 该接口默认那一份（任务 5.4）
        private static uint InterfaceIdWindow => s_api->WindowInterfaceId;
        private static uint InterfaceIdSystem => s_api->WindowSystemInterfaceId;
        private static bool s_windowProbed;
        private static IWindow s_window;
        private static IWindowSystem s_system;
        private static void* s_boundWindowInstance;
        private static bool s_surfaceWindowProbed;
        private static bool s_engineWindowClosed;
        private static bool s_exceptionProbed;

        /// <summary>
        /// 每帧读一次 `WindowShouldClose`（任务 2.8）。引擎侧在关窗前后各 tick 一次，
        /// 两个读数都必须为真 —— 少了这段，托管侧那条读数根本不存在。
        /// </summary>
        private static void ProbeWindowClose()
        {
            if (s_window == null)
            {
                return;
            }
            // 表面把 `int32_t` 还原成 `bool`（矩阵第 1 行）—— 这里拿到的就是 bool
            bool shouldClose = s_window.WindowShouldClose();
            LogLine($"[托管侧] WindowShouldClose = {(shouldClose ? 1 : 0)}");
            if (shouldClose)
            {
                // 引擎侧接下来会把它自己的窗口回收掉，那时 `s_window` 的 self 就悬空了 ——
                // 从此不再碰它（下一次 Tick 改走"表面建窗"探针）。
                s_engineWindowClosed = true;
            }
        }

        /// <summary>
        /// 每帧对一次"当前默认实例是谁"：引擎换窗口（或第一次登记）时在这里**重新绑定**。
        /// 为什么必须对：登记表是进程级 static，全量跑时前一个测试先登记了它自己的窗口，
        /// 托管侧一旦在那一帧绑死，后面就一直在读一个已经被回收的对象（实测：读数来自悬空 self）。
        /// </summary>
        private static void EnsureBound()
        {
            uint windowSize = s_api->GetInterfaceApi(InterfaceIdWindow, null, WindowApiTableVersion, 0, null, null);
            if (windowSize == 0)
            {
                if (!s_windowProbed && s_tickCount <= 1)
                {
                    LogLine("取 IWindow 函数表失败：探针调用返回 0（实例尚未登记，下一帧重试）");
                }
                return;
            }
            byte* windowTable = stackalloc byte[(int)windowSize];
            void* windowInstance = null;
            uint windowWritten = s_api->GetInterfaceApi(InterfaceIdWindow, null, WindowApiTableVersion, windowSize,
                                                       windowTable, &windowInstance);
            if (windowWritten != windowSize || windowInstance == null)
            {
                LogLine($"[托管侧] 取 IWindow 函数表失败：写入 {windowWritten} 字节、实例 {(long)windowInstance}");
                return;
            }
            if (windowInstance == s_boundWindowInstance && s_windowProbed)
            {
                return;
            }
            IWindowBindings.Bind(windowTable);
            s_window = new IWindow(new IWindowLease { Self = windowInstance });
            s_boundWindowInstance = windowInstance;
            // 绑到新实例 = 引擎又建了一个窗口：三个一次性标志一起复位
            s_windowProbed = false;
            s_engineWindowClosed = false;
            s_surfaceWindowProbed = false;

            uint systemSize = s_api->GetInterfaceApi(InterfaceIdSystem, null, WindowApiTableVersion, 0, null, null);
            if (systemSize == 0)
            {
                s_lastError = "取 IWindowSystem 函数表失败：探针调用返回 0（实例未登记）";
                return;
            }
            byte* systemTable = stackalloc byte[(int)systemSize];
            void* systemInstance = null;
            uint systemWritten = s_api->GetInterfaceApi(InterfaceIdSystem, null, WindowApiTableVersion, systemSize,
                                                        systemTable, &systemInstance);
            if (systemWritten != systemSize || systemInstance == null)
            {
                s_lastError = $"取 IWindowSystem 函数表失败：写入 {systemWritten} 字节、实例 {(long)systemInstance}";
                return;
            }
            IWindowSystemBindings.Bind(systemTable);
            s_system = new IWindowSystem(new IWindowSystemLease { Self = systemInstance });
        }

        /// <summary>
        /// 异常映射（任务 5.8）：造一次失败 → 表面 MUST 抛 `<接口>Exception` 且 `Code` 等于码表里的值；
        /// 紧接着一次必成功的调用 MUST NOT 抛（每个入口先清错误码，D11）。
        /// 触发手段用表里的 `ReleaseHandle` 槽：拿一个从没发出去的句柄去释放，它按码表写 1001。
        /// </summary>
        private static void ProbeExceptionMapping()
        {
            if (!s_windowProbed || s_exceptionProbed)
            {
                return;
            }
            s_exceptionProbed = true;
            IWindowBindings.ReleaseHandle(s_boundWindowInstance, 0xDEADBEEFUL);
            try
            {
                IWindowBindings.ThrowIfFailed(s_boundWindowInstance);
                s_lastError = "一次失败调用之后，表面没有抛异常";
            }
            catch (IWindowException ex)
            {
                LogLine($"[托管侧] 异常码读数：{ex.GetType().Name} 的 Code = {ex.Code}（码表里 1001 = 句柄不可用）");
                if (ex.Code != 1001)
                {
                    s_lastError = $"抛出的异常码是 {ex.Code}，码表里应当是 1001";
                }
            }
            // 反向用例：上一次失败之后紧接着一次成功调用 —— 错误码按 self 清零，这里 MUST NOT 抛
            var (width, height) = s_window.GetWindowSize();
            LogLine($"[托管侧] 失败之后紧接着成功调用：{width} x {height}（没有抛）");
        }

        private static void ProbeWindowSize()
        {
            if (s_windowProbed || s_window == null)
            {
                return;
            }
            s_windowProbed = true;

            // 读数全走表面（任务 5.5）：出参 → 元组（矩阵第 2 行）、`int` 仍是 `int`
            var (width, height) = s_window.GetWindowSize();
            LogLine($"[托管侧] 窗口尺寸 = {width} x {height}");

            int windowCount = s_system.GetWindowCount();
            LogLine($"[托管侧] IWindowSystem::GetWindowCount = {windowCount}");
            if (windowCount <= 0)
            {
                s_lastError = $"IWindowSystem::GetWindowCount 回值 {windowCount}，期望至少 1（引擎侧已建窗）";
            }
        }

        /// <summary>
        /// 表面建的窗口走一整圈（任务 5.5）：`NewWindow`（弱引用 → 表面对象）→ 读尺寸 → 关窗 → `Dispose`。
        /// 引擎侧在第三次 Tick 之前把自己的窗口收掉，所以这一圈不会打乱前面那三读数。
        /// </summary>
        private static void ProbeSurfaceWindow()
        {
            if (!s_windowProbed || s_surfaceWindowProbed)
            {
                return;
            }
            s_surfaceWindowProbed = true;
            IWindow created = s_system.NewWindow(320, 240, "托管侧建的窗口", true, true, true, false);
            var (width, height) = created.GetWindowSize();
            LogLine($"[托管侧] 表面建窗读数 = {width} x {height}（期望 320 x 240）");
            if (width != 320 || height != 240)
            {
                s_lastError = $"表面建的窗口尺寸读到 {width} x {height}，期望 320 x 240";
            }
            created.CloseWindow();
            created.Dispose();
        }

        /// <summary>最近一次失败原因（供引擎侧在诊断里带上）；成功时为空串。</summary>
        public static string LastError => s_lastError;

        /// <summary>
        /// 把最近一次失败原因拷给引擎（`csharp-window-creation` 任务 2.7）。返回写入的字节数，
        /// 0 表示没有错误。没有这个导出，引擎侧的 LastError() 永远读到空串 ——
        /// 所有「托管侧报错 ⇒ 测试变红」的判据都会恒绿（实测过）。
        /// </summary>
        [System.Runtime.InteropServices.UnmanagedCallersOnly]
        public static int GetLastError(byte* dst, int cap)
        {
            if (dst == null || cap <= 0 || s_lastError.Length == 0)
            {
                return 0;
            }
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(s_lastError);
            int count = bytes.Length < cap - 1 ? bytes.Length : cap - 1;
            for (int index = 0; index < count; ++index)
            {
                dst[index] = bytes[index];
            }
            dst[count] = 0;
            return count;
        }

        private static void LogVia(EngineApi* api, string text)
        {
            if (api == null || api->Log == null)
            {
                return;
            }

            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = utf8)
            {
                api->Log(2, pointer, (ulong)utf8.Length);
            }
        }

        private static void LogLine(string text)
        {
            if (s_api == null || s_api->Log == null)
            {
                return;
            }

            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = utf8)
            {
                s_api->Log(0, pointer, (ulong)utf8.Length);
            }
        }
    }
}
