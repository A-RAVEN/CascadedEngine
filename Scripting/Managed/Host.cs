using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;
using Scripting.Generated.Bindings.CaBindFixture;
using Scripting.Generated.Bindings.Shadercompilerslang;

namespace Scripting.Managed
{
    /// <summary>
    /// 托管侧手写壳：被引擎调用的入口、ABI 自检、异常→错误码的唯一关口。
    /// 所有入口都是 [UnmanagedCallersOnly] 静态方法，且都被 try/catch 包住 ——
    /// 托管异常穿过 C ABI 边界是未定义行为。
    /// </summary>
    public static unsafe class Host
    {
        // ABI 版本 / 函数表大小 / 表版本 / 七个接口编号全部来自生成物（render-interface-merge：
        // 两侧同一次生成；手写 EngineApi.cs 与三个手写常量已退役）。

        private static EngineApiMirror* s_api;
        private static int s_tickCount;
        private static int s_lastEcho;
        private static string s_lastError = string.Empty;

        // 错误码（引擎侧按同一张表解释）
        private const int Ok = 0;
        private const int ErrAbiVersion = 1;
        private const int ErrAbiSize = 2;
        private const int ErrAbiNull = 3;
        private const int ErrException = 4;
        private const int ErrLayout = 5;

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

                var api = (EngineApiMirror*)apiPointer;

                if (api->AbiVersion != EngineApiBindings.AbiVersion)
                {
                    s_lastError = $"ABI 版本不符：引擎 {api->AbiVersion}，托管侧期望 {EngineApiBindings.AbiVersion}";
                    LogVia(api, s_lastError); // 失败也要让引擎侧看到"两边的版本号"
                    return ErrAbiVersion;
                }

                uint expectedSize = (uint)sizeof(EngineApiMirror);
                if (expectedSize != EngineApiBindings.Size)
                {
                    s_lastError = $"生成物自相矛盾：EngineApiMirror 是 {expectedSize} 字节，" +
                                  $"EngineApiBindings.Size = {EngineApiBindings.Size}";
                    LogVia(api, s_lastError);
                    return ErrAbiSize;
                }
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

                if (!VerifyBindingLayouts(api))
                {
                    LogVia(api, s_lastError);
                    return ErrLayout;
                }

                // 公开值内容与真实 CLR 布局（csharp-render-binding-foundations 任务 1.3）：
                // 枚举实际值 / 底层类型 / [Flags]、记录字段顺序与 Marshal.SizeOf / Marshal.OffsetOf
                // 都在这里读，不靠生成器写出的两组常量自我印证。
                string valueError = SurfaceSignatureCheck.VerifyValueTypes();
                if (valueError != null)
                {
                    s_lastError = valueError;
                    LogVia(api, s_lastError);
                    return ErrLayout;
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

        /// <summary>
        /// 布局清单自检（本 change 任务 3.1）：把生成物里的托管清单与引擎侧运行期读数（真实
        /// `sizeof` / `offsetof`）逐类型、逐字段比对。不一致就拒绝启动并点名字段路径 ——
        /// 这条判据**不**依赖 C++ 的 `static_assert`，所以"生成器算错布局"或"程序集与引擎产物不同源"
        /// 都能在托管启动阶段被拦下。
        /// </summary>
        private static bool VerifyBindingLayouts(EngineApiMirror* api)
        {
            if (api->GetBindingLayoutType == null || api->GetBindingLayoutField == null)
            {
                s_lastError = "函数表里缺布局清单入口（v5 的两个字段是空指针）";
                return false;
            }
            if (!VerifyBindingLayout(api, api->WindowInterfaceId, IWindowBindings.LayoutManifest))
            {
                return false;
            }
            if (!VerifyBindingLayout(api, api->WindowSystemInterfaceId, IWindowSystemBindings.LayoutManifest))
            {
                return false;
            }
            // csharp-native-direct-binding 4.1：渲染侧换到**原生五类直绑表**（编号以生成常量为准），
            // 每张表的过界记录都按引擎侧运行期清单逐项核对。
            if (!VerifyBindingLayout(api, EngineApiBindings.GraphicsBackend__CRenderBackendInterfaceId,
                    CRenderBackendBindings.LayoutManifest))
            {
                return false;
            }
            if (!VerifyBindingLayout(api, EngineApiBindings.GraphicsBackend__GPUGraphInterfaceId,
                    GPUGraphBindings.LayoutManifest))
            {
                return false;
            }
            if (!VerifyBindingLayout(api, EngineApiBindings.GraphicsBackend__ShaderStructInterfaceId,
                    ShaderStructBindings.LayoutManifest))
            {
                return false;
            }
            if (!VerifyBindingLayout(api, EngineApiBindings.GraphicsBackend__GPUTextureInterfaceId,
                    GPUTextureBindings.LayoutManifest))
            {
                return false;
            }
            return VerifyBindingLayout(api, EngineApiBindings.GraphicsBackend__WindowHandleInterfaceId,
                WindowHandleBindings.LayoutManifest);
        }

        private static bool VerifyBindingLayout(EngineApiMirror* api, uint interfaceId, BindingLayoutType[] expected)
        {
            for (int typeIndex = 0; typeIndex < expected.Length; ++typeIndex)
            {
                BindingLayoutType type = expected[typeIndex];
                byte* typeName = stackalloc byte[128];
                ulong sizeBytes = 0;
                uint fieldCount = 0;
                uint found = api->GetBindingLayoutType(interfaceId, (uint)typeIndex, typeName, 128, &sizeBytes,
                                                       &fieldCount);
                if (found == 0)
                {
                    s_lastError = $"布局清单：引擎侧没有接口 {interfaceId} 的类型 #{typeIndex}（托管侧期望 {type.Name}）";
                    return false;
                }
                string engineType = Marshal.PtrToStringUTF8((IntPtr)typeName) ?? string.Empty;
                if (engineType != type.Name)
                {
                    s_lastError = $"布局清单不一致：类型 #{typeIndex} 托管叫 {type.Name}、引擎侧叫 {engineType}";
                    return false;
                }
                if ((long)sizeBytes != type.Size)
                {
                    s_lastError = $"布局清单不一致：{type.Name} 大小 托管 {type.Size} / 引擎 {sizeBytes}";
                    return false;
                }
                if (fieldCount != (uint)type.Fields.Length)
                {
                    s_lastError = $"布局清单不一致：{type.Name} 字段数 托管 {type.Fields.Length} / 引擎 {fieldCount}";
                    return false;
                }
                for (int fieldIndex = 0; fieldIndex < type.Fields.Length; ++fieldIndex)
                {
                    BindingLayoutField field = type.Fields[fieldIndex];
                    byte* fieldName = stackalloc byte[128];
                    byte* fieldType = stackalloc byte[128];
                    ulong offsetBytes = 0;
                    found = api->GetBindingLayoutField(interfaceId, (uint)typeIndex, (uint)fieldIndex, fieldName,
                                                       128, fieldType, 128, &offsetBytes);
                    if (found == 0)
                    {
                        s_lastError = $"布局清单：引擎侧读不到 {type.Name}.{field.Name}";
                        return false;
                    }
                    string engineField = Marshal.PtrToStringUTF8((IntPtr)fieldName) ?? string.Empty;
                    if (engineField != field.Name)
                    {
                        s_lastError = $"布局清单不一致：{type.Name} 的字段 #{fieldIndex} 托管叫 {field.Name}、引擎侧叫 {engineField}";
                        return false;
                    }
                    if ((long)offsetBytes != field.Offset)
                    {
                        s_lastError =
                            $"布局清单不一致：{type.Name}.{field.Name} 偏移 托管 {field.Offset} / 引擎 {offsetBytes}";
                        return false;
                    }
                }
            }
            return true;
        }

        /// <summary>
        /// 关闭入口（本 change 任务 4.1）：`CSharpHost::Shutdown` 在 `hostfxr_close` **之前**调用它。
        /// 释放窗口系统表面对象 —— 生成物的 `Dispose` 会逐项注销仍存活的回调并释放 `GCHandle`，
        /// 关闭路径不许只把旧引用置空。
        /// </summary>
        [UnmanagedCallersOnly]
        public static int Shutdown()
        {
            try
            {
                // editor-window-panel 1.2（D1）：编辑器服务必须在释放任何表面对象之前关闭 ——
                // 停监听、断客户端、join 监听线程、关 WebView2 控制器与窗口，全部先于 hostfxr_close。
                // 门控未开时是空操作（零日志、零副作用）。
                Editor.EditorService.Shutdown("Host.Shutdown");
                // 关闭顺序（csharp-render-texture-sampling 任务 3.1）：参数结构 → 纹理 → 图 → 离屏目标
                // → 后端表面。纹理排在参数结构之后：结构持纹理引用，先放持有者。
                LogShutdownStep("1/4 参数结构表面对象");
                UserScript.ReleaseShaderStructSurfaces();
                LogShutdownStep("2/4 纹理表面对象");
                UserScript.ReleaseTextureSurfaces();
                LogShutdownStep("3/4 图 → 离屏目标");
                UserScript.ReleaseRenderSurfaces();
                LogShutdownStep("4/4 后端表面对象");
                if (s_renderBackend != null)
                {
                    s_renderBackend.Dispose();
                    s_renderBackend = null;
                }
                LogShutdownStep("释放完成");
                s_renderTablesBound = false;
                s_renderBindError = string.Empty;
                s_renderProbeCalled = true;
                if (s_system != null)
                {
                    s_system.Dispose();
                    s_system = null;
                }
                if (s_window != null)
                {
                    s_window.Dispose();
                    s_window = null;
                }
                s_boundWindowInstance = null;
                s_callbackProbesRegistered = false;
                s_userScriptCalled = true;
                LogLine($"托管侧 Shutdown：已释放窗口系统表面对象（剩余保活项 {IWindowSystemCallbackSlot.LiveKeepAliveCount}）");
                s_api = null;
                return Ok;
            }
            catch (Exception ex)
            {
                s_lastError = "Shutdown 抛异常：" + ex.GetType().Name + ": " + ex.Message;
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
                // editor-window-panel 1.2：编辑器服务（CASCADED_EDITOR=1 门控；默认关时 Tick 直接返回、
                // 不建线程/窗口/端口）。必须在 EnsureBound 之后：建窗与命令执行都要走已绑定的生成表面；
                // 引擎调用只在这里（主线程）发生，监听线程只碰网络（D1）。
                if (s_system != null)
                {
                    Editor.EditorService.Tick(s_system, s_renderBackend, s_renderTablesBound);
                }
                // 渲染探针有独立的调用标记与开关（csharp-render-backend-triangle 任务 3.2）：
                // 它不依赖 `OnEngineReady` 那一次，也不会被更早的脚本调用消耗。
                ProbeRender();
                // 常量色探针同样独立（csharp-render-shader-constants 任务 3.2）。
                ProbeConstantColor();
                // 结构化缓冲探针独立（csharp-render-structured-buffer 任务 3.1）：三个渲染探针互不消耗。
                ProbeStructuredBuffer();
                // 纹理采样探针同样独立（csharp-render-texture-sampling 任务 3.2）：四个探针互不消耗。
                ProbeTexture();
                ProbeWindowSize();
                ProbeExceptionMapping();
                ProbeMonitor();
                ProbeTargetDispose();
                RunCallbackProbePhase();
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
        // `delegate*` 取出来自己转。表版本取自生成常量（生成接口表版本 2，本 change 不变）。
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
        // csharp-render-binding-foundations 任务 1.3 / 3.3：目标释放与释放后调用只探一次
        private static bool s_targetDisposeProbed;
        // 任务 2.2：脚本只调一次
        private static bool s_userScriptCalled;

        // ---- 回调探针（本 change 任务 1.2 骨架；4.1–4.3 填实）----
        // 只在 `CASCADED_CSHARP_CALLBACK_PROBE=1` 时启用：`TestCSharpWindowCallbacks` 用它走到
        // "注册 10 类回调 → 引擎侧确定性触发 → 校验参数 / 生命周期 / 异常"这条路；其它用例不注册任何回调。
        private static bool s_callbackProbesRegistered;
        private static string s_lastCallbackPhase = string.Empty;
        private static Action<IWindow, bool> s_focusDelegate;
        private static IWindow s_savedBorrowWindow;
        private static int s_reentrancyCode = -1;
        private static int s_cbScrollGood;
        private static int s_cbFocus;
        private static int s_cbFocusNew;
        private static int s_cbCursorEnter;
        private static int s_cbCursorPos;
        private static int s_cbMouseButton;
        private static int s_cbScroll;
        private static int s_cbWindowClose;
        private static int s_cbWindowPos;
        private static int s_cbWindowSize;
        private static int s_cbKey;
        private static int s_cbChar;

        // ---- GetMonitor 两侧对拍（本 change 任务 3.2）----
        // 每个绑定周期只读一次：负数与不小于数量的编号必须抛 1006，有效编号的每个字段都要与 C++ 侧一致。
        private static bool s_monitorProbed;

        private static bool CallbackProbesEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_CALLBACK_PROBE") == "1";

        private static string SelfTestMode =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_SELFTEST") ?? string.Empty;

        /// <summary>
        /// 绑定到 IWindowSystem 表面对象之后注册 10 类回调（本 change 任务 4.1）。
        /// 公开 setter 都是 `void SetX(Action<...>)`：注册号 / 函数指针 / 用户令牌只在生成物 internal 层。
        /// </summary>
        private static void ProbeCallbacks()
        {
            if (!CallbackProbesEnabled || s_system == null || s_callbackProbesRegistered)
            {
                return;
            }
            s_callbackProbesRegistered = true;
            // 焦点回调会被替换，重绑时注册的必须是**当前那一个**（否则重绑后的行为会退回到旧委托）。
            if (s_focusDelegate == null)
            {
                s_focusDelegate = (window, value) =>
                {
                    ++s_cbFocus;
                    LogCallback("SetWindowFocusCallback", window, value ? 1 : 0);
                    ValidateCallbackWindow(window);
                };
            }
            s_system.SetWindowFocusCallback(s_focusDelegate);
            s_system.SetCursorEnterCallback((window, value) =>
            {
                ++s_cbCursorEnter;
                LogCallback("SetCursorEnterCallback", window, value ? 1 : 0);
                ValidateCallbackWindow(window);
            });
            s_system.SetCursorPosCallback((window, x, y) =>
            {
                ++s_cbCursorPos;
                LogCallback("SetCursorPosCallback", window, x, y);
                ValidateCallbackWindow(window);
            });
            s_system.SetMouseButtonCallback((window, button, action, mods) =>
            {
                ++s_cbMouseButton;
                LogCallback("SetMouseButtonCallback", window, button, action, mods);
                ValidateCallbackWindow(window);
            });
            s_system.SetScrollCallback((window, x, y) =>
            {
                ++s_cbScroll;
                LogCallback("SetScrollCallback", window, x, y);
                ValidateCallbackWindow(window);
            });
            s_system.SetWindowCloseCallback((window) =>
            {
                ++s_cbWindowClose;
                LogCallback("SetWindowCloseCallback", window);
                ValidateCallbackWindow(window);
            });
            s_system.SetWindowPosCallback((window, x, y) =>
            {
                ++s_cbWindowPos;
                LogCallback("SetWindowPosCallback", window, x, y);
                ValidateCallbackWindow(window);
            });
            s_system.SetWindowSizeCallback((window, x, y) =>
            {
                ++s_cbWindowSize;
                LogCallback("SetWindowSizeCallback", window, x, y);
                ValidateCallbackWindow(window);
            });
            s_system.SetKeyCallback((window, key, scanCode, action, mods) =>
            {
                ++s_cbKey;
                LogCallback("SetKeyCallback", window, key, scanCode, action, mods);
                ValidateCallbackWindow(window);
            });
            s_system.SetCharCallback((window, codepoint) =>
            {
                ++s_cbChar;
                LogCallback("SetCharCallback", window, codepoint);
                ValidateCallbackWindow(window);
            });
            if (IWindowSystemCallbackSlot.LiveKeepAliveCount != 10)
            {
                s_lastError = $"注册 10 类回调之后保活项是 {IWindowSystemCallbackSlot.LiveKeepAliveCount}，期望 10";
            }
            LogLine($"[托管侧 回调] 已注册 10 类，保活项 {IWindowSystemCallbackSlot.LiveKeepAliveCount}");
        }

        private static string PointerText(IWindow window)
        {
            return window == null ? "0x0" : "0x" + ((long)window.NativeSelf).ToString("x");
        }

        private static void LogCallback(string name, IWindow window, params object[] values)
        {
            LogLine($"[托管侧回调] {name} window={PointerText(window)} args=({string.Join(",", values)})");
        }

        /// <summary>回调里的窗口必须是真实的 `IWindow` 表面对象（尺寸 / 名字都与夹具一致），不是空壳。</summary>
        private static void ValidateCallbackWindow(IWindow window)
        {
            if (window == null)
            {
                s_lastError = "回调收到的窗口是 null";
                return;
            }
            var (width, height) = window.GetWindowSize();
            if (width != CallbackProbe.WindowWidth || height != CallbackProbe.WindowHeight)
            {
                s_lastError = $"回调窗口尺寸读到 {width} x {height}，期望 {CallbackProbe.WindowWidth} x {CallbackProbe.WindowHeight}";
            }
        }

        /// <summary>
        /// 回调生命周期探针的阶段机（本 change 任务 4.1）：`TestCSharpWindowCallbacks` 在两次 Tick 之间
        /// 改环境变量 `CASCADED_CSHARP_CALLBACK_PHASE`，这里按阶段执行替换 / 置空 / 重绑后的断言。
        /// </summary>
        private static void RunCallbackProbePhase()
        {
            if (!CallbackProbesEnabled || s_system == null)
            {
                return;
            }
            string phase = Environment.GetEnvironmentVariable("CASCADED_CSHARP_CALLBACK_PHASE") ?? string.Empty;
            if (phase == s_lastCallbackPhase)
            {
                return;
            }
            s_lastCallbackPhase = phase;
            if (phase == "verify-all")
            {
                if (s_cbFocus != 1 || s_cbCursorEnter != 1 || s_cbCursorPos != 1 || s_cbMouseButton != 1 ||
                    s_cbScroll != 1 || s_cbWindowClose != 1 || s_cbWindowPos != 1 || s_cbWindowSize != 1 ||
                    s_cbKey != 1 || s_cbChar != 1)
                {
                    s_lastError = $"10 类回调的调用次数不符：focus={s_cbFocus} enter={s_cbCursorEnter} pos={s_cbCursorPos} " +
                                  $"mouse={s_cbMouseButton} scroll={s_cbScroll} close={s_cbWindowClose} " +
                                  $"winpos={s_cbWindowPos} winsize={s_cbWindowSize} key={s_cbKey} char={s_cbChar}";
                    return;
                }
                LogLine("[托管侧 回调] 10 类各执行 1 次");
                // 替换焦点回调：先装新的、再注销旧的（design D2）
                s_focusDelegate = (window, value) =>
                {
                    ++s_cbFocusNew;
                    // 任务 4.2：保存回调里收到的窗口对象，回调返回后在 verify-replace 里再调用它
                    s_savedBorrowWindow = window;
                    // 任务 4.3：回调执行期间替换同一个注册必须失败（1004），而不许静默销毁正在执行的回调
                    try
                    {
                        s_system.SetWindowFocusCallback((ignoredWindow, ignoredValue) => { });
                        s_reentrancyCode = 0;
                    }
                    catch (IWindowSystemException ex)
                    {
                        s_reentrancyCode = ex.Code;
                    }
                    LogCallback("SetWindowFocusCallback", window, value ? 1 : 0);
                    ValidateCallbackWindow(window);
                };
                s_system.SetWindowFocusCallback(s_focusDelegate);
                if (IWindowSystemCallbackSlot.LiveKeepAliveCount != 10)
                {
                    s_lastError = $"替换回调之后保活项是 {IWindowSystemCallbackSlot.LiveKeepAliveCount}，期望 10";
                }
                // 置空光标进入回调
                s_system.SetCursorEnterCallback(null);
                if (IWindowSystemCallbackSlot.LiveKeepAliveCount != 9)
                {
                    s_lastError = $"置空回调之后保活项是 {IWindowSystemCallbackSlot.LiveKeepAliveCount}，期望 9";
                }
            }
            else if (phase == "verify-replace")
            {
                if (s_cbFocus != 1 || s_cbFocusNew != 1 || s_cbCursorEnter != 1)
                {
                    s_lastError = $"替换/置空之后调用次数不符：旧 focus={s_cbFocus} 新 focus={s_cbFocusNew} enter={s_cbCursorEnter}";
                }
                LogLine("[托管侧 回调] 替换后只有新委托执行、置空后旧委托不执行");
                // 任务 4.2：回调返回后，保存下来的窗口对象再调用任意方法必须抛明确的失效异常（1001）。
                if (s_savedBorrowWindow == null)
                {
                    s_lastError = "回调里没有保存到窗口对象（4.2 的借用失效用例没有素材）";
                }
                else
                {
                    try
                    {
                        var (width, height) = s_savedBorrowWindow.GetWindowSize();
                        s_lastError = $"保存的借用窗口在回调外仍能调用（读到 {width} x {height}，期望抛 1001）";
                    }
                    catch (IWindowException ex)
                    {
                        if (ex.Code != 1001)
                        {
                            s_lastError = $"借用窗口失效的错误码是 {ex.Code}，期望 1001";
                        }
                        else
                        {
                            LogLine("[托管侧 回调] 保存的借用窗口在回调外抛 IWindowException(1001)");
                        }
                    }
                    s_savedBorrowWindow = null;
                }
                // 任务 4.3：回调内替换自身的错误码必须是 1004（生成桥用活动标记挡下，不销毁正在执行的回调）
                if (s_reentrancyCode != 1004)
                {
                    s_lastError = $"回调内替换自身没有拿到 1004（实际 {s_reentrancyCode}）";
                }
                else
                {
                    LogLine("[托管侧 回调] 重入替换被挡下：IWindowSystemException(1004)");
                }
                // 任务 4.3：注册一个抛异常的滚动回调；下一次 UpdateSystem 必须上报 1003 且保留原文
                s_system.SetScrollCallback((window, x, y) =>
                {
                    ++s_cbScroll;
                    LogCallback("SetScrollCallback", window, x, y);
                    throw new InvalidOperationException(CallbackProbe.ThrowingExceptionText);
                });
            }
            else if (phase == "verify-exception")
            {
                try
                {
                    s_system.UpdateSystem();
                    s_lastError = "回调抛异常之后 UpdateSystem 没有抛出";
                }
                catch (IWindowSystemException ex)
                {
                    if (ex.Code != 1003)
                    {
                        s_lastError = $"回调异常上报的错误码是 {ex.Code}，期望 1003";
                    }
                    else if (!ex.Message.Contains(CallbackProbe.ThrowingExceptionText))
                    {
                        s_lastError = $"回调异常上报时丢了原始文本：{ex.Message}";
                    }
                    else
                    {
                        LogLine("[托管侧 回调] 回调异常在下一次 UpdateSystem 上报：Code=1003，原始文本保留");
                    }
                }
                // 错误槽已经取走：紧接着的一次 UpdateSystem 不许再抛
                s_system.UpdateSystem();
                // 异常之后正常回调仍能执行：重新注册一个不抛的滚动回调
                s_system.SetScrollCallback((window, x, y) =>
                {
                    ++s_cbScrollGood;
                    LogCallback("SetScrollCallback", window, x, y);
                    ValidateCallbackWindow(window);
                });
                LogLine("[托管侧 回调] 异常上报之后的下一次调用正常，正常回调已重新注册");
            }
            else if (phase == "verify-thread")
            {
                try
                {
                    s_system.UpdateSystem();
                    s_lastError = "跨线程回调之后 UpdateSystem 没有抛出";
                }
                catch (IWindowSystemException ex)
                {
                    if (ex.Code != 1005)
                    {
                        s_lastError = $"跨线程回调的错误码是 {ex.Code}，期望 1005";
                    }
                    else
                    {
                        LogLine("[托管侧 回调] 跨线程回调被拒绝：IWindowSystemException(1005)");
                    }
                }
                s_system.UpdateSystem();
                LogLine("[托管侧 回调] 跨线程错误上报之后的下一次调用正常");
            }
            else if (phase == "verify-rebind")
            {
                if (IWindowSystemCallbackSlot.LiveKeepAliveCount != 10)
                {
                    s_lastError = $"重绑之后保活项是 {IWindowSystemCallbackSlot.LiveKeepAliveCount}，期望 10";
                }
                LogLine("[托管侧 回调] 重绑完成：新表面对象注册了 10 类（旧对象的保活项已释放）");
            }
            else if (phase == "verify-rebind-done")
            {
                if (s_cbFocus != 1 || s_cbFocusNew != 2)
                {
                    s_lastError = $"重绑之后调用次数不符：旧 focus={s_cbFocus} 新 focus={s_cbFocusNew}（期望 1 / 2）";
                }
                if (IWindowSystemCallbackSlot.LiveKeepAliveCount != 10)
                {
                    s_lastError = $"重绑之后保活项是 {IWindowSystemCallbackSlot.LiveKeepAliveCount}，期望 10";
                }
                LogLine("[托管侧 回调] 重绑后旧窗口系统不再收到事件、新表面对象收到事件");
            }
        }

        /// <summary>
        /// 目标释放与释放后调用（csharp-render-binding-foundations 任务 1.3 / 3.3）。
        ///
        /// 用脚本建出来的那个窗口（`IWindowSystem.NewWindow` 的**弱返回句柄** → `IWindow` 目标表面对象）：
        /// 解析与释放都必须走目标接口入口。`Dispose` 只释放目标句柄、不关窗（窗口计数不变）；
        /// 释放后再次调用 MUST 抛 `ObjectDisposedException`，且不得进入原生 thunk。
        /// </summary>
        private static void ProbeTargetDispose()
        {
            if (s_targetDisposeProbed || s_system == null)
            {
                return;
            }
            s_targetDisposeProbed = true;
            int ownerCountBefore = s_system.GetWindowCount();
            IWindow borrowed = UserScript.LastCreated;
            if (borrowed == null || borrowed.Disposed)
            {
                // 脚本窗口的租约已经被上一份绑定释放过：另建一个临时窗口，读完尺寸就关掉、释放租约，
                // 再 UpdateSystem 把它回收 —— 全程结束后窗口计数必须回到原值。
                try
                {
                    borrowed = s_system.NewWindow(120, 80, "目标释放探针", false, false, false, false);
                    borrowed.GetWindowSize();
                    borrowed.CloseWindow();
                    borrowed.Dispose();
                    s_system.UpdateSystem();
                }
                catch (Exception ex)
                {
                    s_lastError = "目标表面获取/释放失败：" + ex.GetType().Name + ": " + ex.Message;
                    LogLine("[托管侧 目标释放] " + s_lastError);
                    return;
                }
            }
            else
            {
                borrowed.GetWindowSize();
                try
                {
                    borrowed.Dispose();
                }
                catch (Exception ex)
                {
                    // 把合法释放错接成发行者入口时，这里会真的抛 `<接口>Exception`（1001）——
                    // 那正是本任务要证明"Dispose 检查错误通道"的失败路径。
                    s_lastError = "目标表面 Dispose 报错：" + ex.GetType().Name + ": " + ex.Message;
                    LogLine("[托管侧 目标释放] " + s_lastError);
                    return;
                }
            }
            bool disposedThrew = false;
            try
            {
                borrowed.GetWindowSize();
            }
            catch (ObjectDisposedException)
            {
                disposedThrew = true;
            }
            catch (Exception ex)
            {
                s_lastError = "释放后调用抛了 " + ex.GetType().Name + "，期望 ObjectDisposedException：" + ex.Message;
                LogLine("[托管侧 目标释放] " + s_lastError);
                return;
            }
            int ownerCount = s_system.GetWindowCount();
            if (!disposedThrew)
            {
                s_lastError = "目标表面 Dispose 之后再次调用没有抛 ObjectDisposedException";
                LogLine("[托管侧 目标释放] " + s_lastError);
                return;
            }
            LogLine($"[托管侧 目标释放] disposed=ObjectDisposedException borrowedCount={ownerCountBefore} ownerCount={ownerCount}");
        }

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
            uint windowSize = s_api->GetInterfaceApi(InterfaceIdWindow, null, EngineApiBindings.TableVersion, 0, null, null);
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
            uint windowWritten = s_api->GetInterfaceApi(InterfaceIdWindow, null, EngineApiBindings.TableVersion, windowSize,
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
            s_monitorProbed = false;
            // 目标释放探针按"每一份绑定"重跑一次：每个窗口测试各写自己的日志，脚本只建一次窗，
            // 所以第二份绑定起改用临时窗口（关掉并回收，计数回到原值）。
            s_targetDisposeProbed = false;
            // 任务 2.2：重绑开始就把旧的对象作废 —— 否则下面若取 IWindowSystem 表失败，`s_system`
            // 会保留上一次的旧对象，"两张表都绑好"这个条件被旧值骗过（脚本会拿到一个已失效的 self）。
            // 本 change 任务 4.1：作废前先 `Dispose` 旧表面对象，由它注销仍存活的回调、释放 GCHandle。
            // 渲染侧同理：旧窗口上的离屏目标 / 图 / 窗口目标不能跨重绑继续用。
            UserScript.ReleaseRenderSurfaces();
            s_renderTablesBound = false;
            if (s_renderBackend != null)
            {
                s_renderBackend.Dispose();
                s_renderBackend = null;
            }
            if (s_system != null)
            {
                s_system.Dispose();
            }
            s_system = null;
            s_lastCallbackPhase = string.Empty;

            uint systemSize = s_api->GetInterfaceApi(InterfaceIdSystem, null, EngineApiBindings.TableVersion, 0, null, null);
            if (systemSize == 0)
            {
                s_lastError = "取 IWindowSystem 函数表失败：探针调用返回 0（实例未登记）";
                return;
            }
            byte* systemTable = stackalloc byte[(int)systemSize];
            void* systemInstance = null;
            uint systemWritten = s_api->GetInterfaceApi(InterfaceIdSystem, null, EngineApiBindings.TableVersion, systemSize,
                                                        systemTable, &systemInstance);
            if (systemWritten != systemSize || systemInstance == null)
            {
                s_lastError = $"取 IWindowSystem 函数表失败：写入 {systemWritten} 字节、实例 {(long)systemInstance}";
                return;
            }
            IWindowSystemBindings.Bind(systemTable);
            s_system = new IWindowSystem(new IWindowSystemLease { Self = systemInstance });
            // 重绑 = 新的表面对象：回调探针按新对象重新注册（任务 4.1）
            s_callbackProbesRegistered = false;
            ProbeCallbacks();
            // 三张渲染表在同一份绑定里取一次（任务 3.1）；失败只记日志，探针启用时再明确报错。
            BindRenderInterfaces();

            // 任务 2.2：两张表都绑定成功之后调一次用户脚本，整个进程只调一次。
            // 先置标志再调用：脚本抛异常时由 `Tick` 的 catch 转成非零返回码 + `s_lastError`，
            // 且不会因为下一帧重试而反复抛。
            if (!s_userScriptCalled && s_system != null)
            {
                s_userScriptCalled = true;
                UserScript.OnEngineReady(s_system);
                // 调用次数与建窗尺寸都打到日志里：引擎侧只能看日志，用它判「只调一次」与「窗口建出来了」
                LogLine($"[脚本] OnEngineReady 调用 #{UserScript.CalledCount}");
                if (UserScript.LastCreatedSummary.Length != 0)
                {
                    LogLine($"[脚本] 建窗：脚本建的窗口 {UserScript.LastCreatedSummary}");
                }
            }
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

        /// <summary>
        /// GetMonitor 两侧对拍（本 change 任务 3.2）：读 0 号显示器并把两个矩形与 DPI 打成固定格式，
        /// 由 `Tools/check_window_system_bindings.py` 与 C++ 侧同一行逐字段比对；随后用 -1 与
        /// `GetMonitorCount()` 两次调用，断言绑定层在进原生 clamp 之前就抛 `IWindowSystemException`
        /// 且错误码是 1006。
        ///
        /// 自检开关（只在 Debug 生效，见 CSharpHost/README.md）：
        ///   `CASCADED_CSHARP_SELFTEST=monitor-mismatch`     打印时把 m_MonitorRect.width 加 1，
        ///                                                    用来证明脚本真的会报字段错配；
        ///   `CASCADED_CSHARP_SELFTEST=monitor-skip-invalid` 跳过两次无效编号调用，用来证明脚本
        ///                                                    真的会因"放过无效编号"变红。
        /// </summary>
        private static void ProbeMonitor()
        {
            if (s_system == null || s_monitorProbed)
            {
                return;
            }
            s_monitorProbed = true;
            int count = s_system.GetMonitorCount();
            LogLine($"[托管侧 GetMonitor] count={count}");
            if (count > 0)
            {
                MonitorInfo info = s_system.GetMonitor(0);
                float loggedWidth = info.m_MonitorRect.width;
                if (SelfTestMode == "monitor-mismatch")
                {
                    loggedWidth += 1.0f;
                }
                LogLine($"[托管侧 GetMonitor] probe=1 id=0 rect=({info.m_MonitorRect.x},{info.m_MonitorRect.y},{loggedWidth},{info.m_MonitorRect.height}) work=({info.m_WorkAreaRect.x},{info.m_WorkAreaRect.y},{info.m_WorkAreaRect.width},{info.m_WorkAreaRect.height}) dpi={info.m_DPIScale}");
            }
            if (SelfTestMode == "monitor-skip-invalid")
            {
                LogLine("[托管侧 无效显示器] 自检开关 monitor-skip-invalid：跳过两次无效编号调用");
                return;
            }
            int[] invalidIndexes = new int[] { -1, count };
            for (int index = 0; index < invalidIndexes.Length; ++index)
            {
                int invalid = invalidIndexes[index];
                try
                {
                    s_system.GetMonitor(invalid);
                    s_lastError = $"无效显示器编号 {invalid} 没有抛异常（原生 clamp 把它改成了另一个显示器）";
                    LogLine("[托管侧 无效显示器] " + s_lastError);
                }
                catch (IWindowSystemException ex)
                {
                    LogLine($"[托管侧 无效显示器] id={invalid} code={ex.Code}");
                    if (ex.Code != 1006)
                    {
                        s_lastError = $"无效显示器编号 {invalid} 的错误码是 {ex.Code}，期望 1006";
                    }
                }
            }
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

            int windowCount = s_system != null ? s_system.GetWindowCount() : -1;
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
            if (!s_windowProbed || s_surfaceWindowProbed || s_system == null)
            {
                // `s_system == null`：引擎只登记了编号 1（半绑定态）。这一帧没有表面可用，
                // 直接跳过，不许在这里吃空引用异常（任务 2.2 ④）。
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

        // ---- 脚本渲染直绑面（csharp-native-direct-binding 4.1）----
        // 后端表有真实实例（编号以生成常量为准）；图 / 参数结构 / 纹理 / 窗口句柄是目标类型表。
        private static ScriptRenderBackend s_renderBackend;
        private static bool s_renderTablesBound;
        /// <summary>
        /// 最近一次渲染表绑定失败的原因（`CRenderBackend` / 某张目标类型表没登记、写入字节数不符）。
        /// `BindRenderInterfaces` 会清空 `s_lastError`（未启用探针的窗口路径要保持原行为），所以这里
        /// 另记一份，供探针的失败文本点名"是哪张表"。
        /// </summary>
        private static string s_renderBindError = string.Empty;
        private static bool s_renderProbeCalled;
        // csharp-render-shader-constants 任务 3.2：常量色探针有自己的开关与调用标记，与渲染探针互不消耗。
        private static bool s_constantColorProbeCalled;
        // csharp-render-structured-buffer 任务 3.1：结构化缓冲探针同样独立（第三个渲染探针）。
        private static bool s_structuredBufferProbeCalled;
        // csharp-render-texture-sampling 任务 3.2：纹理探针有独立开关与调用标记（第四个渲染探针）。
        private static bool s_textureProbeCalled;

        private static bool RenderProbeEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_RENDER_PROBE") == "1";

        private static bool ConstantColorProbeEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_CONSTCOLOR_PROBE") == "1";

        private static bool StructuredBufferProbeEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_SB_PROBE") == "1";

        private static bool TextureProbeEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_CSHARP_TEX_PROBE") == "1";

        /// <summary>
        /// 绑定渲染表（csharp-native-direct-binding 4.1）：后端表单独取（编号 = 生成的
        /// `CRenderBackendInterfaceId`，要求真实后端实例）；图 / 参数结构 / 纹理 / 窗口句柄是
        /// **目标类型表**，按生成的登记清单循环绑定（实际对象由工厂或句柄解析产出）。
        /// 失败只记日志、不写 `s_lastError` —— 未启用渲染探针的既有窗口路径必须保持原行为；
        /// 真正启用探针时由探针报明确失败，失败文本必须点名是**哪个接口**。
        /// </summary>
        private static void BindRenderInterfaces()
        {
            if (s_renderTablesBound)
            {
                return;
            }
            if (s_renderBackend != null)
            {
                s_renderBackend.Dispose();
                s_renderBackend = null;
            }
            uint backendId = EngineApiBindings.GraphicsBackend__CRenderBackendInterfaceId;
            uint backendSize = s_api->GetInterfaceApi(backendId, null, EngineApiBindings.TableVersion, 0, null, null);
            if (backendSize == 0)
            {
                s_renderBindError = $"取 CRenderBackend 函数表失败：渲染后端未登记（编号 {backendId}）";
                LogLine("[托管侧 渲染] " + s_renderBindError + "（渲染探针不会执行）");
                return;
            }
            byte* backendTable = stackalloc byte[(int)backendSize];
            void* backendInstance = null;
            uint backendWritten = s_api->GetInterfaceApi(backendId, null, EngineApiBindings.TableVersion, backendSize,
                                                        backendTable, &backendInstance);
            if (backendWritten != backendSize || backendInstance == null)
            {
                s_renderBindError = $"取 CRenderBackend 函数表失败：写入 {backendWritten} 字节、实例 {(long)backendInstance}";
                LogLine("[托管侧 渲染] " + s_renderBindError);
                return;
            }
            CRenderBackendBindings.Bind(backendTable);
            s_renderBackend = new ScriptRenderBackend(
                new CRenderBackend(new CRenderBackendLease { Self = backendInstance }));

            // 目标类型表：遍历生成清单（窗口两表由 EnsureBound 自己绑定；后端表上面已处理，跳过）。
            foreach (var table in EngineApiBindings.Tables)
            {
                if (table.Id == s_api->WindowInterfaceId || table.Id == s_api->WindowSystemInterfaceId ||
                    table.Id == backendId)
                {
                    continue;
                }
                uint size = s_api->GetInterfaceApi(table.Id, null, EngineApiBindings.TableVersion, 0, null, null);
                if (size == 0)
                {
                    s_lastError = $"取 {table.Name} 函数表失败：目标类型表没有登记（编号 {table.Id}）";
                    s_renderBindError = s_lastError;
                    LogLine("[托管侧 渲染] " + s_lastError);
                    s_lastError = string.Empty;
                    return;
                }
                byte* tableBytes = stackalloc byte[(int)size];
                uint written = s_api->GetInterfaceApi(table.Id, null, EngineApiBindings.TableVersion, size, tableBytes,
                                                      null);
                if (written != size)
                {
                    s_lastError = $"取 {table.Name} 函数表失败：写入 {written} 字节、期望 {size} 字节";
                    s_renderBindError = s_lastError;
                    LogLine("[托管侧 渲染] " + s_lastError);
                    s_lastError = string.Empty;
                    return;
                }
                table.Bind(tableBytes);
            }
            s_renderTablesBound = true;
            s_renderBindError = string.Empty;
            LogLine($"[托管侧 渲染] 后端 + 五张目标类型表已绑定（后端实例 {(long)backendInstance}）");
        }

        /// <summary>
        /// 渲染探针（任务 3.2）：独立于 `OnEngineReady` 的调用标记，只在
        /// `CASCADED_CSHARP_RENDER_PROBE=1` 且表都绑好时执行一次。
        /// </summary>
        private static void ProbeRender()
        {
            if (s_renderProbeCalled || !RenderProbeEnabled)
            {
                return;
            }
            s_renderProbeCalled = true;
            if (!s_renderTablesBound)
            {
                BindRenderInterfaces();
            }
            if (!s_renderTablesBound || s_system == null || s_renderBackend == null)
            {
                s_lastError = "渲染探针：后端 + 五张目标类型表没有全部绑定（或 IWindowSystem 未绑定），拒绝执行";
                LogLine("[托管侧 渲染] " + s_lastError);
                return;
            }
            UserScript.OnRenderProbe(s_system, s_renderBackend);
            LogLine($"[脚本 渲染] OnRenderProbe 调用 #{UserScript.RenderProbeCount}：" +
                    UserScript.LastRenderSummary);
            LogLine(UserScript.LastRenderReadings);
        }

        /// <summary>
        /// 常量色探针（csharp-render-shader-constants 任务 3.2）：与 `ProbeRender` 同形但**独立**的
        /// 开关（`CASCADED_CSHARP_CONSTCOLOR_PROBE`）与调用标记 —— 两个探针在同一进程的全量顺序里
        /// 各自执行一次、互不消耗。未启用时不创建任何 GPU 工作。
        /// </summary>
        private static void ProbeConstantColor()
        {
            if (s_constantColorProbeCalled || !ConstantColorProbeEnabled)
            {
                return;
            }
            s_constantColorProbeCalled = true;
            if (!s_renderTablesBound)
            {
                BindRenderInterfaces();
            }
            if (!s_renderTablesBound || s_system == null || s_renderBackend == null)
            {
                s_lastError = "常量色探针：后端 + 五张目标类型表没有全部绑定（或 IWindowSystem 未绑定），拒绝执行";
                LogLine("[托管侧 常量色] " + s_lastError);
                return;
            }
            UserScript.OnConstantColorProbe(s_system, s_renderBackend);
            LogLine($"[脚本 常量色] OnConstantColorProbe 调用 #{UserScript.ConstantColorProbeCount}：" +
                    UserScript.LastConstantColorSummary);
            LogLine(UserScript.LastConstantColorReadings);
        }

        /// <summary>
        /// 结构化缓冲探针（csharp-render-structured-buffer 任务 3.1）：与 `ProbeConstantColor` 同形但
        /// **独立**的开关（`CASCADED_CSHARP_SB_PROBE`）与调用标记 —— 三个渲染探针在同一进程的全量顺序里
        /// 各自执行一次、互不消耗。未启用时不创建任何 GPU 工作。
        /// </summary>
        private static void ProbeStructuredBuffer()
        {
            if (s_structuredBufferProbeCalled || !StructuredBufferProbeEnabled)
            {
                return;
            }
            s_structuredBufferProbeCalled = true;
            if (!s_renderTablesBound)
            {
                BindRenderInterfaces();
            }
            if (!s_renderTablesBound || s_system == null || s_renderBackend == null)
            {
                s_lastError = "结构化缓冲探针：后端 + 五张目标类型表没有全部绑定（或 IWindowSystem 未绑定），拒绝执行";
                LogLine("[托管侧 结构化缓冲] " + s_lastError);
                return;
            }
            UserScript.OnStructuredBufferProbe(s_system, s_renderBackend);
            LogLine($"[脚本 结构化缓冲] OnStructuredBufferProbe 调用 #{UserScript.StructuredBufferProbeCount}：" +
                    UserScript.LastStructuredBufferSummary);
            LogLine(UserScript.LastStructuredBufferReadings);
        }

        /// <summary>
        /// 纹理采样探针（csharp-render-texture-sampling 任务 3.2）：与 `ProbeStructuredBuffer` 同形但
        /// **独立**的开关（`CASCADED_CSHARP_TEX_PROBE`）与调用标记 —— 四个渲染探针在同一进程的全量顺序里
        /// 各自执行一次、互不消耗。未启用时不创建任何 GPU 工作。
        /// </summary>
        private static void ProbeTexture()
        {
            if (s_textureProbeCalled || !TextureProbeEnabled)
            {
                return;
            }
            s_textureProbeCalled = true;
            if (!s_renderTablesBound)
            {
                BindRenderInterfaces();
            }
            if (!s_renderTablesBound || s_system == null || s_renderBackend == null)
            {
                s_lastError = "纹理采样探针：后端 + 五张目标类型表没有全部绑定（或 IWindowSystem 未绑定），拒绝执行" +
                              (s_renderBindError.Length == 0 ? string.Empty : "；绑定失败原因：" + s_renderBindError);
                LogLine("[托管侧 纹理] " + s_lastError);
                return;
            }
            UserScript.OnTextureProbe(s_system, s_renderBackend);
            LogLine($"[脚本 纹理] OnTextureProbe 调用 #{UserScript.TextureProbeCount}：" +
                    UserScript.LastTextureSummary);
            LogLine(UserScript.LastTextureReadings);
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

        private static void LogVia(EngineApiMirror* api, string text)
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

        /// <summary>
        /// 编辑器服务 / 探针的日志出口（editor-window-panel）：统一走宿主的托管日志桥，
        /// 这样 `[编辑器 读数]` / `[编辑器 探针]` 行与既有探针读数落在同一份测试日志里。
        /// </summary>
        internal static void LogFromEditor(string text)
        {
            LogLine(text);
        }

        /// <summary>
        /// 关闭顺序的逐步读数（任务 3.1）：同时走宿主日志与 stderr —— 关闭发生在各测试自己的日志文件
        /// 已关闭之后，只写宿主日志的话这一串顺序在进程输出里看不到。
        /// </summary>
        private static void LogShutdownStep(string step)
        {
            string text = "[托管侧 关闭顺序] " + step;
            LogLine(text);
            Console.Error.WriteLine(text);
            Console.Error.Flush();
        }
    }
}
