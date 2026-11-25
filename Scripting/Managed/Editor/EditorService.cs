// editor-window-panel 任务 1.2 / 1.3 / 1.5 / 1.6：编辑器服务（D1 / D3 / D4 / D5 / D6 / D2）。
//
// 线程模型（D1）：
//   - **一个后台监听线程**：HttpListener 接收（静态文件 + /ws 升级）、WebSocket 收帧、JSON 形状校验、
//     入 ConcurrentQueue；发送也在这个线程（只发，不碰引擎）。该线程**不调用任何引擎绑定 API**。
//   - **引擎调用全部在引擎主线程的 Host.Tick**：drain 队列 → 执行命令（经生成绑定表面）→ 结果/事件写回
//     各客户端发送队列。窗口尺寸/关闭回调也在主线程触发，同样只改队列与服务自己的状态。
//   - **Host.Shutdown**：停监听、断客户端、join 监听线程、关闭 WebView2 控制器与窗口，全部先于
//     hostfxr_close（Host.Shutdown 里第一步调本类的 Shutdown）。
//
// 门控：CASCADED_EDITOR=1。默认不设时 Tick 直接返回：不建线程、不建窗口、不占端口、不打日志。
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.CsScript;

namespace Scripting.Managed.Editor
{
    /// <summary>一个已升级的 WebSocket 客户端。发送队列由监听线程消费；主线程只入队。</summary>
    internal sealed class EditorClient
    {
        internal int Id;
        internal WebSocket Socket;
        internal string Remote = string.Empty;
        internal readonly ConcurrentQueue<string> SendQueue = new ConcurrentQueue<string>();
        internal Task<WebSocketReceiveResult> ReceiveTask;
        internal Task SendTask;
        internal readonly byte[] Buffer = new byte[8192];
        internal readonly MemoryStream ReceiveBuffer = new MemoryStream();
        internal bool Handshaken;
        internal volatile bool CloseAfterSend;
        internal volatile bool Closed;
    }

    internal sealed class UndoRecord
    {
        internal string Command = string.Empty; // set-title / set-size
        internal string Title = string.Empty;
        internal int Width;
        internal int Height;
    }

    internal sealed class EditorObject
    {
        internal int Id;
        internal IWindow Window;
        internal UndoRecord Undo;
    }

    internal static class EditorService
    {
        internal const int DefaultPort = 18390;
        internal const int DefaultPanelWidth = 320;
        // 底部资产浏览器 dock 的默认高（design D9 / 照 DefaultPanelWidth 先例）：配置项默认值，
        // CASCADED_EDITOR_DOCK_HEIGHT 可配（范围 1..客户区高-1，上界在 ApplyLayout 校验并越界禁用）。
        // 240 的依据：够显资产表头 + 5 行预置资产（VulkanShaderLibrary.shLib / D3D12ShaderLibrary.shLib /
        // CSharpProbe/*.res 三行探针残留），且给场景视口留下 720-240=480 的高度。
        internal const int DefaultDockHeight = 240;
        // 窗口初始尺寸：布局默认值（不是配置项、不是上下限；够放面板与视口，测试与人工目检都用它）。
        private const int InitialWindowWidth = 1280;
        private const int InitialWindowHeight = 720;

        // 状态：0 未启动；1 监听已起（下一帧建窗）；2 运行；3 失败禁用；4 已关闭
        private static int s_state;
        private static bool s_started;
        private static int s_port;
        private static int s_panelWidth;
        private static int s_dockHeight;
        private static int s_tick;
        private static string s_error = string.Empty;
        private static string s_dataFolder = string.Empty;
        private static string s_readingsPath = string.Empty;

        private static IWindowSystem s_system;
        private static HttpListener s_listener;
        private static Thread s_listenerThread;
        private static volatile bool s_listenerStop;
        private static readonly List<EditorClient> s_clients = new List<EditorClient>();
        private static readonly object s_clientsLock = new object();
        private static int s_nextClientId = 1;

        private static readonly ConcurrentQueue<EditorInbound> s_inbound = new ConcurrentQueue<EditorInbound>();
        private static readonly Dictionary<int, EditorObject> s_objects = new Dictionary<int, EditorObject>();
        private static readonly HashSet<int> s_invalidated = new HashSet<int>();
        private static int s_nextObjectId = 1;
        private static int s_invalidatedCount;

        private static EditorWindow s_window;
        private static EditorSceneView s_scene;
        // 渲染原型窗（spike 模式专用；与三分区的 s_window 二选一）。
        private static SpikeWindow s_spikeWindow;
        // 抓窗钩子的进度游标（5.4 加固）：帧转发数/对账数一变化就主动补抓一张，避免只靠 0.5s 周期
        // 而抓到"导航后、第一帧到达前"的空 canvas（实测 Vulkan 侧两次抓窗都是空帧）。
        private static int s_spikeCaptureFrames;
        private static int s_spikeCaptureMatched;
        private static bool s_shellClosed;
        private static long s_windowSelf;
        private static int s_lastLayoutWidth = -1;
        private static int s_lastLayoutHeight = -1;

        // 原生调用计数：s_nativeCalls 是服务全部引擎调用；s_cmdCalls 只统计"最近一条命令执行期间"的调用，
        // 供 3.2 ③④ 断言"未知/失效编号零原生调用"（渲染每帧都会调用引擎，不能用总数做增量判据）。
        private static int s_nativeCalls;
        private static int s_cmdCalls;
        private static bool s_inCommand;
        private static int s_lastCommandRequest;
        private static string s_lastCommandName = string.Empty;

        private static bool s_staticErrorLogged;
        private static bool s_staticRootLogged;
        // dock 页面缺失只写一行日志（与上面的面板资源缺失分开记，两类缺失的原因与处置不同）。
        private static bool s_dockStaticErrorLogged;
        // 渲染原型页面缺失同样只写一行（editor-web-render-spike 1.2）。
        private static bool s_spikeStaticErrorLogged;

        // 资产浏览（editor-asset-browser）：上次成功枚举的条目路径快照（null = 还没成功枚举过）。
        // 每次成功枚举与它比对，有增删就广播 assets-changed（design D5：v1 不轮询，差异靠枚举发现）。
        private static HashSet<string> s_lastAssetPaths;

        internal static bool Enabled => Environment.GetEnvironmentVariable("CASCADED_EDITOR") == "1";
        internal static bool Running => s_state == 2;

        // ---- 渲染原型（editor-web-render-spike 2.2）----
        // 原型门控默认关：`CASCADED_EDITOR_SPIKE=web-render` 时服务起"全客户区单控制器"原型窗，
        // 加载 spike.html；不设时走既有三分区编辑器，行为一行不变。
        internal static bool SpikeEnabled =>
            Environment.GetEnvironmentVariable("CASCADED_EDITOR_SPIKE") == "web-render";
        internal static string SpikeSelftest =>
            Environment.GetEnvironmentVariable("CASCADED_EDITOR_SPIKE_SELFTEST") ?? string.Empty;
        // 原型独立端口 18393：避开 18390（默认）/18391（TestEditorWindowPanel）/18392（TestEditorAssetBrowser）。
        internal const int SpikeDefaultPort = 18393;

        /// <summary>引擎调用计数（EditWindow / 服务内部每个绑定表面调用后都记一次）。</summary>
        internal static void NoteNativeCall()
        {
            ++s_nativeCalls;
            if (s_inCommand)
            {
                ++s_cmdCalls;
            }
        }

        internal static void NoteCommandCall()
        {
            ++s_cmdCalls;
        }

        /// <summary>每帧由 Host.Tick（主线程）调用。绑定完成后的第一帧起监听，第二帧建窗。</summary>
        internal static void Tick(IWindowSystem system, ScriptRenderBackend backend, bool renderBound)
        {
            // M11：测试跑完主动停服务（不依赖清环境变量），隔离后续用例。放在门控判断之前 ——
            // 测试通常先清 CASCADED_EDITOR 再置 STOP=1，此时 Enabled 已经是 false。
            if (Environment.GetEnvironmentVariable("CASCADED_EDITOR_STOP") == "1")
            {
                Stop("测试请求停止（CASCADED_EDITOR_STOP=1）");
                return;
            }
            if (!Enabled || s_state == 3 || s_state == 4)
            {
                return;
            }
            s_system = system;
            ++s_tick;

            if (s_state == 0)
            {
                s_state = 1;
                StartService();
                return;
            }
            if (s_state == 1)
            {
                s_state = 2;
                CreateEditorWindow(backend, renderBound);
                return;
            }
            if (s_state == 2)
            {
                PumpWindow();
                DrainInbound();
                // 渲染原型：6000ms 没收到同帧回传 → E_WEBCANVAS_NOLINE（断链/缺回传判红）。
                if (SpikeEnabled)
                {
                    SpikeChannel.TickTimeout();
                }
                CheckObjectHealth();
                if (s_state == 2)
                {
                    RenderFrame();
                }
                EmitReadings();
            }
        }

        // ------------------------------------------------------------------------------------
        // 启动 / 关闭
        // ------------------------------------------------------------------------------------

        private static void StartService()
        {
            s_started = true;
            // 读数落盘（不依赖 --headless 的 stdout 重定向）：测试给一个文件路径，每帧追加一行。
            // 可见窗口模式（不带 --headless）也能读到读数行；未设置时只走宿主日志。
            s_readingsPath = Environment.GetEnvironmentVariable("CASCADED_EDITOR_READINGS_FILE") ?? string.Empty;
            // 渲染原型通道（PROTOCOL.md §7）：只有门控开着才识别那三类消息；关着时它们按未知类型回
            // E_BAD_MESSAGE，既有面板行为不变。每次启动清一次通道状态。
            EditorProtocol.SpikeMessagesEnabled = SpikeEnabled;
            SpikeChannel.Reset();
            s_spikeCaptureFrames = 0;
            s_spikeCaptureMatched = 0;
            // 1.11：记录程序集位置与 BaseDirectory，供"静态根为什么解析成这个绝对路径"核对
            //（hostfxr 宿主下 BaseDirectory 实测为空字符串，静态根改由程序集目录解析）。
            Host.LogFromEditor("[编辑器] 程序集位置：" + typeof(EditorService).Assembly.Location +
                               "；AppContext.BaseDirectory=\"" + AppContext.BaseDirectory + "\"");
            if (!TryReadPort(out string configError))
            {
                Disable("配置非法：" + configError);
                return;
            }
            if (!TryReadPanelWidth(out configError))
            {
                Disable("配置非法：" + configError);
                return;
            }
            if (!TryReadDockHeight(out configError))
            {
                Disable("配置非法：" + configError);
                return;
            }
            // editor-code-source-display D1：源显示的**生产注册点**（钉死真实 shLib 路径 + 注册 C# 显示资源）。
            // 门控 `CASCADED_EDITOR_SOURCE_DISPLAY` 默认 on；off 时这里什么都不注册（程序集落默认提供者仍显）。
            if (EditorSourceDisplay.EnsureRegistered())
            {
                Host.LogFromEditor("[编辑器] 源显示已注册：(shader-library, " +
                                   EditorSourceDisplay.VulkanShaderLibraryPath + ") / (shader-library, " +
                                   EditorSourceDisplay.D3D12ShaderLibraryPath + ") / (csharp-script, " +
                                   EditorSourceDisplay.CSharpAssemblyPath + ")");
            }
            else
            {
                Host.LogFromEditor("[编辑器] 源显示关闭（CASCADED_EDITOR_SOURCE_DISPLAY=off）：代码型程序集落默认提供者仍显");
            }
            s_dataFolder = Environment.GetEnvironmentVariable("CASCADED_EDITOR_WEBVIEW2_DATA");
            if (string.IsNullOrEmpty(s_dataFolder))
            {
                // 渲染原型用独立数据目录（D1：避开默认 %TEMP%\cascaded-editor-webview2，两个进程不抢锁）。
                s_dataFolder = Path.Combine(Path.GetTempPath(),
                    SpikeEnabled ? "cascaded-editor-webview2-spike" : "cascaded-editor-webview2");
            }
            try
            {
                Directory.CreateDirectory(s_dataFolder);
            }
            catch (Exception ex)
            {
                Disable("WebView2 用户数据目录创建失败：" + ex.Message);
                return;
            }
            try
            {
                s_listener = new HttpListener();
                // 只绑 localhost（D3：不对外网）。
                s_listener.Prefixes.Add("http://localhost:" + s_port + "/");
                s_listener.Start();
            }
            catch (Exception ex)
            {
                Disable("HttpListener 启动失败（端口 " + s_port + "）：" + ex.GetType().Name + ":" + ex.Message);
                return;
            }
            s_listenerThread = new Thread(ListenerLoop)
            {
                IsBackground = true,
                Name = "编辑器监听线程",
            };
            s_listenerThread.Start();
            Host.LogFromEditor("[编辑器] 监听线程已启动：http://localhost:" + s_port + "/（面板宽 " +
                               s_panelWidth + "，dock 高 " + s_dockHeight + "，仅本机；门控 CASCADED_EDITOR=1）");
        }

        /// <summary>主动停服务（M11）：停监听、断客户端、join、关控制器与窗口；幂等。</summary>
        internal static void Stop(string reason)
        {
            Shutdown(reason);
        }

        /// <summary>Host.Shutdown 的第一步：停监听、断客户端、join 线程、关控制器与窗口（先于 hostfxr_close）。</summary>
        internal static void Shutdown(string reason)
        {
            if (!s_started || s_state == 4)
            {
                return; // 门控未开 / 已经关过：零行为、幂等
            }
            s_listenerStop = true;
            try
            {
                s_listener?.Stop();
                s_listener?.Close();
            }
            catch (Exception)
            {
                // 关闭路径的异常不影响后续清理；下面 join 仍会执行。
            }
            lock (s_clientsLock)
            {
                foreach (EditorClient client in s_clients)
                {
                    try
                    {
                        client.Socket.Abort();
                    }
                    catch (Exception)
                    {
                    }
                }
                s_clients.Clear();
            }
            if (s_listenerThread != null && s_listenerThread.IsAlive)
            {
                s_listenerThread.Join(2000);
            }
            if (s_scene != null)
            {
                s_scene.Dispose();
                s_scene = null;
            }
            if (s_window != null)
            {
                s_window.Close();
                try
                {
                    s_window.Window.CloseWindow();
                    s_window.Window.Dispose();
                }
                catch (Exception)
                {
                }
                s_window = null;
            }
            // 渲染原型窗：控制器关闭同样先于 hostfxr_close（Host.Shutdown 第一步调到这里）。
            if (s_spikeWindow != null)
            {
                s_spikeWindow.Close();
                try
                {
                    s_spikeWindow.Window.CloseWindow();
                    s_spikeWindow.Window.Dispose();
                }
                catch (Exception)
                {
                }
                s_spikeWindow = null;
            }
            s_shellClosed = false;
            s_objects.Clear();
            s_invalidated.Clear();
            s_lastAssetPaths = null;
            // 原型通道：服务关了就不再接受 spike 消息；读数状态清零（同一进程不会重复跑，但不留脏状态）。
            EditorProtocol.SpikeMessagesEnabled = false;
            SpikeChannel.Reset();
            s_state = 4;
            LogServiceMarker("[编辑器] 已关闭（" + reason + "）：监听已停、客户端已断、WebView2 控制器已关闭");
        }

        private static void Disable(string reason)
        {
            int stateBefore = s_state;
            s_error = reason;
            LogServiceMarker("[编辑器] 禁用：" + reason);
            Shutdown("失败禁用");
            s_state = 3;
            // 启动阶段的禁用（配置非法 / 监听失败 / 建窗失败）发生时，读数泵（state==2 才跑）还没跑过 ——
            // 用例与客户端只能在日志标记里看到原因，机器可读读数通道的 error 字段是空的。这里补发一行：
            // 判据要的是"原因点名"（如 CASCADED_EDITOR_DOCK_HEIGHT=0 非法 / 超出范围），不该取决于
            // 失败发生在哪一帧。state==2 的失败（dock 控制器/导航）那一帧本来就会发一行，不重复发。
            if (stateBefore < 2)
            {
                EmitReadings();
            }
        }

        /// <summary>
        /// 服务级标记行（入队/禁用/关闭）：除宿主日志外，门控开了读数文件时也追加进去 ——
        /// 这样可见窗口模式（不带 --headless、stdout 不重定向）下 C++ 用例仍能读到这些标记。
        /// </summary>
        internal static void LogServiceMarker(string text)
        {
            Host.LogFromEditor(text);
            if (s_readingsPath.Length != 0)
            {
                try
                {
                    File.AppendAllText(s_readingsPath, text + "\n");
                }
                catch (Exception)
                {
                }
            }
        }

        private static bool TryReadPort(out string error)
        {
            error = string.Empty;
            string text = Environment.GetEnvironmentVariable("CASCADED_EDITOR_PORT");
            if (string.IsNullOrEmpty(text))
            {
                s_port = SpikeEnabled ? SpikeDefaultPort : DefaultPort;
                return true;
            }
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out s_port) ||
                s_port < 1 || s_port > 65535)
            {
                error = "CASCADED_EDITOR_PORT=\"" + text + "\" 非法（要求 1-65535 的整数）";
                return false;
            }
            return true;
        }

        private static bool TryReadPanelWidth(out string error)
        {
            error = string.Empty;
            string text = Environment.GetEnvironmentVariable("CASCADED_EDITOR_PANEL_WIDTH");
            if (string.IsNullOrEmpty(text))
            {
                s_panelWidth = DefaultPanelWidth;
                return true;
            }
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out s_panelWidth) ||
                s_panelWidth < 1)
            {
                error = "CASCADED_EDITOR_PANEL_WIDTH=\"" + text + "\" 非法（要求正整数像素）";
                return false;
            }
            return true;
        }

        /// <summary>
        /// 读 dock 高（design D9）：默认 DefaultDockHeight=240，CASCADED_EDITOR_DOCK_HEIGHT 可配。
        /// 这里只校"是不是正整数"（与 TryReadPanelWidth 同口径）；上界 1..客户区高-1 在
        /// EditorWindow.ApplyLayout 里按真实客户区校验并越界禁用（不 clamp）。
        /// </summary>
        private static bool TryReadDockHeight(out string error)
        {
            error = string.Empty;
            string text = Environment.GetEnvironmentVariable("CASCADED_EDITOR_DOCK_HEIGHT");
            if (string.IsNullOrEmpty(text))
            {
                s_dockHeight = DefaultDockHeight;
                return true;
            }
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out s_dockHeight) ||
                s_dockHeight < 1)
            {
                error = "CASCADED_EDITOR_DOCK_HEIGHT=\"" + text + "\" 非法（要求正整数像素）";
                return false;
            }
            return true;
        }

        // ------------------------------------------------------------------------------------
        // 建窗（1.6）
        // ------------------------------------------------------------------------------------

        private static void CreateEditorWindow(ScriptRenderBackend backend, bool renderBound)
        {
            if (s_system == null)
            {
                Disable("IWindowSystem 表面未绑定");
                return;
            }
            // 渲染原型（spike 模式）：单全窗 WebView2 控制器，不需要托管侧渲染后端（渲染在 C++ 侧）。
            if (SpikeEnabled)
            {
                CreateSpikeWindow();
                return;
            }
            if (!renderBound || backend == null)
            {
                // D11：渲染后端表面未绑定/不可用 ⇒ 明确报错并禁用，不静默降级、不造空壳 scene view。
                Disable("渲染后端表面未绑定（D11：编辑器禁用，不出现空壳 scene view）");
                return;
            }
            string navigationTarget = "http://localhost:" + s_port + "/";
            // 底部 dock 的独立页面（editor-asset-browser 3.1 / D6）：与右面板 index.html 分离。
            string dockNavigationTarget = "http://localhost:" + s_port + "/assets.html";
            EditorWindow window = EditorWindow.Create(s_system, InitialWindowWidth, InitialWindowHeight,
                s_panelWidth, s_dockHeight, navigationTarget, dockNavigationTarget, s_dataFolder, out string error);
            if (window == null)
            {
                Disable("创建编辑器窗口失败：" + error);
                return;
            }
            s_window = window;
            s_windowSelf = (long)window.Window.NativeSelf;
            RegisterWindowCallbacks();
            RegisterObject(window.Window);
            s_scene = new EditorSceneView(backend, window.Window);
            // D9：初始建图就按三分区的视口高（客户区高 − dock 高），不靠尺寸回调补。
            if (!s_scene.Build(window.ViewportWidth, window.ViewportHeight))
            {
                Disable("scene view 建图失败：" + s_scene.LastError);
                return;
            }
            s_lastLayoutWidth = window.ViewportWidth;
            s_lastLayoutHeight = window.ClientHeight;
            Host.LogFromEditor("[编辑器] 窗口已创建（单窗口三分区：视口 " + window.ViewportWidth + "x" +
                               window.ViewportHeight + " + 检查器 " + window.PanelWidth + "x" +
                               window.ViewportHeight + " + dock " + window.ClientWidth + "x" + window.DockHeight +
                               "，模式 main-thread，hwnd=0x" + ((long)window.Hwnd).ToString("X") +
                               "，windowSelf=0x" + s_windowSelf.ToString("X") + "）");
        }

        /// <summary>
        /// 渲染原型窗（spike 模式）：全客户区单 WebView2 控制器加载 spike.html。页面自检模式经 URL 查询串
        /// 传给网页（hide-canvas / local-red / no-line）；skip-native-render 是 C++ 侧开关，不传给网页。
        /// </summary>
        private static void CreateSpikeWindow()
        {
            string navigationTarget = "http://localhost:" + s_port + "/spike.html" + SpikeSelftestQuery();
            string capturePath = Environment.GetEnvironmentVariable("CASCADED_EDITOR_SPIKE_CAPTURE") ?? string.Empty;
            SpikeWindow window = SpikeWindow.Create(s_system, InitialWindowWidth, InitialWindowHeight,
                navigationTarget, s_dataFolder, capturePath, out string error);
            if (window == null)
            {
                Disable("创建渲染原型窗口失败：" + error);
                return;
            }
            s_spikeWindow = window;
            s_windowSelf = (long)window.Window.NativeSelf;
            Host.LogFromEditor("[渲染原型] 窗口已创建（全客户区单控制器 " + window.ClientWidth + "x" +
                               window.ClientHeight + "，hwnd=0x" + ((long)window.Hwnd).ToString("X") +
                               "，目标 " + navigationTarget + "，自检 " +
                               (SpikeSelftest.Length == 0 ? "-" : SpikeSelftest) + "）");
        }

        /// <summary>网页侧自检模式 → URL 查询串（只认三个网页侧模式；空/其它值不带查询串）。</summary>
        private static string SpikeSelftestQuery()
        {
            string mode = SpikeSelftest;
            if (mode == "hide-canvas" || mode == "local-red" || mode == "no-line")
            {
                return "?selftest=" + mode;
            }
            return string.Empty;
        }

        private static void RegisterWindowCallbacks()
        {
            if (s_system == null)
            {
                return;
            }
            try
            {
                s_system.SetWindowCloseCallback(OnWindowCloseCallback);
                NoteNativeCall();
                s_system.SetWindowSizeCallback(OnWindowSizeCallback);
                NoteNativeCall();
            }
            catch (Exception ex)
            {
                Disable("注册窗口回调失败：" + ex.GetType().Name + ":" + ex.Message);
            }
        }

        private static void OnWindowCloseCallback(IWindow window)
        {
            if (window == null || s_window == null || s_windowSelf == 0)
            {
                return;
            }
            if ((long)window.NativeSelf != s_windowSelf)
            {
                return;
            }
            EditorObject target = FindObjectByWindowSelf(s_windowSelf);
            if (target != null)
            {
                Invalidate(target, "目标窗口 close 回调");
            }
        }

        private static void OnWindowSizeCallback(IWindow window, float width, float height)
        {
            if (window == null || s_window == null || s_windowSelf == 0)
            {
                return;
            }
            if ((long)window.NativeSelf != s_windowSelf)
            {
                return;
            }
            SyncLayout((int)width, (int)height, "窗口尺寸回调");
        }

        private static void PumpWindow()
        {
            // 渲染原型：单控制器全窗（没有 dock 的第二控制器，也没有 scene view）。
            if (SpikeEnabled)
            {
                if (s_spikeWindow == null || s_shellClosed)
                {
                    return;
                }
                s_spikeWindow.Pump();
                if (s_spikeWindow.Error.Length != 0)
                {
                    Disable("渲染原型 WebView2 失败：" + s_spikeWindow.Error);
                }
                // 5.4 抓窗可靠性：不靠 0.5s 周期"撞"帧到达 —— 帧转发数与同源对账数只要推进，就主动排一次
                // 抓窗（SpikeWindow 内部延迟几帧再抓，保证合成器已经把 putImageData 的画面提上来）。
                // 覆盖写同一个 PNG：最后一次（三帧对账之后）留下的就是含特征图案的画面。
                if (s_spikeWindow.NavigationDone)
                {
                    if (SpikeChannel.FramesRelayed > s_spikeCaptureFrames)
                    {
                        s_spikeCaptureFrames = SpikeChannel.FramesRelayed;
                        s_spikeWindow.RequestCapture();
                    }
                    if (SpikeChannel.Matched > s_spikeCaptureMatched)
                    {
                        s_spikeCaptureMatched = SpikeChannel.Matched;
                        s_spikeWindow.RequestCapture();
                    }
                }
                return;
            }
            if (s_window == null || s_shellClosed)
            {
                return;
            }
            s_window.Pump();
            if (s_window.Error.Length != 0)
            {
                Disable("WebView2 贴附失败：" + s_window.Error);
                return;
            }
            // D9：第二控制器（资产浏览器 dock）有独立的错误通道 —— 第一控制器绿不等于整体绿。
            if (s_window.DockError.Length != 0)
            {
                Disable("资产浏览器 dock 失败：" + s_window.DockError);
            }
        }

        /// <summary>每帧提交 scene view（D11：编辑器开启期间每帧 SubmitGraph + WaitIdle）。</summary>
        private static void RenderFrame()
        {
            if (s_scene == null || s_shellClosed)
            {
                return;
            }
            if (!s_scene.RenderFrame())
            {
                Disable("scene view 渲染失败：" + s_scene.LastError);
            }
        }

        // ------------------------------------------------------------------------------------
        // 对象表（1.3 / 1.5）
        // ------------------------------------------------------------------------------------

        private static void RegisterObject(IWindow window)
        {
            int id = s_nextObjectId++;
            s_objects[id] = new EditorObject { Id = id, Window = window };
            Host.LogFromEditor("[编辑器] 登记对象 " + id + "（kind=window）");
        }

        private static EditorObject FindObjectByWindowSelf(long windowSelf)
        {
            foreach (EditorObject candidate in s_objects.Values)
            {
                try
                {
                    if ((long)candidate.Window.NativeSelf == windowSelf)
                    {
                        return candidate;
                    }
                }
                catch (Exception)
                {
                    // 表面已释放：交给 CheckObjectHealth 的失效路径处理。
                }
            }
            return null;
        }

        private static void CheckObjectHealth()
        {
            if (s_objects.Count == 0)
            {
                return;
            }
            var ids = new List<int>(s_objects.Keys);
            foreach (int id in ids)
            {
                if (!s_objects.TryGetValue(id, out EditorObject obj))
                {
                    continue;
                }
                bool shouldClose;
                try
                {
                    shouldClose = obj.Window.WindowShouldClose();
                    NoteNativeCall();
                }
                catch (ObjectDisposedException)
                {
                    Invalidate(obj, "表面已释放");
                    continue;
                }
                catch (IWindowException ex)
                {
                    Invalidate(obj, "绑定报告失效：错误码 " + ex.Code);
                    continue;
                }
                catch (Exception ex)
                {
                    Invalidate(obj, "读取关闭标志失败：" + ex.GetType().Name);
                    continue;
                }
                if (shouldClose)
                {
                    // 引擎 CloseWindow 不触发 GLFW close 回调（只置 should-close），所以这里每帧补一次检查；
                    // 用户点 X 的路径由 close 回调覆盖。两条路都走同一个失效函数。
                    Invalidate(obj, "目标窗口已请求关闭");
                }
            }
        }

        private static void Invalidate(EditorObject obj, string reason)
        {
            if (obj == null || !s_objects.Remove(obj.Id))
            {
                return;
            }
            s_invalidated.Add(obj.Id);
            ++s_invalidatedCount;
            if (s_window != null && obj.Window == s_window.Window)
            {
                // 单窗口单关闭路径（D2）：控制器与 scene view 随目标窗口一起结束。监听线程**继续运行** ——
                // 规范场景要求失效后客户端仍能拿到 E_OBJECT_INVALIDATED（不是连接被断开）。
                s_shellClosed = true;
                s_window.Close();
                if (s_scene != null)
                {
                    s_scene.Dispose();
                    s_scene = null;
                }
            }
            try
            {
                obj.Window.Dispose();
            }
            catch (Exception)
            {
            }
            Broadcast(EditorProtocol.BuildInvalidatedEvent(obj.Id));
            Host.LogFromEditor("[编辑器] 对象 " + obj.Id + " 已失效：" + Sanitize(reason));
        }

        private static void SyncLayout(int width, int height, string source)
        {
            if (s_window == null || width <= 0 || height <= 0)
            {
                return;
            }
            s_window.ApplyLayout(width, height);
            if (s_window.Error.Length != 0)
            {
                Disable("布局同步失败（" + source + "）：" + s_window.Error);
                return;
            }
            // D9：场景视口高 = 客户区高 − dock 高（ApplyLayout 已重算两个控制器的 Bounds）。
            if (s_scene != null && !s_shellClosed &&
                !s_scene.Resize(s_window.ViewportWidth, s_window.ViewportHeight))
            {
                Disable("scene view 改尺寸重建失败：" + s_scene.LastError);
                return;
            }
            if (width == s_lastLayoutWidth && height == s_lastLayoutHeight)
            {
                return;
            }
            s_lastLayoutWidth = width;
            s_lastLayoutHeight = height;
            // 外部改尺寸也要广播给面板（事件来源：窗口尺寸回调，D2）。
            EditorObject target = FindObjectByWindowSelf(s_windowSelf);
            if (target != null)
            {
                Broadcast(EditorProtocol.BuildChangedEvent(target.Id, new Dictionary<string, object>
                {
                    ["width"] = s_window.ClientWidth,
                    ["height"] = s_window.ClientHeight,
                }));
            }
        }

        // ------------------------------------------------------------------------------------
        // 命令执行（只在主线程）
        // ------------------------------------------------------------------------------------

        private static void DrainInbound()
        {
            while (s_inbound.TryDequeue(out EditorInbound inbound))
            {
                if (inbound.Kind == EditorInboundKind.Hello)
                {
                    SendWelcome(inbound.Client);
                }
                else if (inbound.Kind == EditorInboundKind.SpikeHello)
                {
                    // 原型页就绪：记录 spikePage=1，C++ 驱动据此开始推帧。
                    SpikeChannel.AcceptHello(inbound.Webgpu);
                }
                else if (inbound.Kind == EditorInboundKind.SpikeFrame)
                {
                    // C++ → C# → 原型页：登记待对账帧（含 6000ms 截止时间），再把原始 JSON 转发给其它客户端。
                    SpikeChannel.AcceptFrame(inbound.Frame);
                    BroadcastExcept(inbound.Client, inbound.Frame.RawJson);
                }
                else if (inbound.Kind == EditorInboundKind.SpikeReport)
                {
                    SpikeChannel.AcceptReport(inbound.Report);
                }
                else if (inbound.Command != null)
                {
                    ExecuteCommand(inbound.Client, inbound.Command);
                }
            }
        }

        private static void SendWelcome(EditorClient client)
        {
            var snapshots = new List<EditorObjectSnapshot>();
            foreach (EditorObject obj in s_objects.Values)
            {
                if (TrySnapshot(obj, out EditorObjectSnapshot snapshot, out _, out _))
                {
                    snapshots.Add(snapshot);
                }
            }
            Enqueue(client, EditorProtocol.BuildWelcome(snapshots));
        }

        private static void ExecuteCommand(EditorClient client, EditorCommand command)
        {
            s_cmdCalls = 0;
            s_lastCommandRequest = command.RequestId;
            s_lastCommandName = command.Name;
            s_inCommand = true;
            try
            {
                if (command.Name == "list-objects")
                {
                    var snapshots = new List<EditorObjectSnapshot>();
                    foreach (EditorObject obj in s_objects.Values)
                    {
                        if (TrySnapshot(obj, out EditorObjectSnapshot snapshot, out _, out _))
                        {
                            snapshots.Add(snapshot);
                        }
                    }
                    Enqueue(client, EditorProtocol.BuildObjectsResult(command.RequestId, snapshots));
                    return;
                }

                if (command.Name == "list-assets")
                {
                    // 非对象作用域命令（PROTOCOL.md 2.3）：不查对象表，直接在**主线程命令执行内**枚举
                    // 资产根（design D3）。监听线程绝不碰这些门面 / 文件系统调用。
                    ExecuteListAssets(client, command);
                    return;
                }

                if (!TryResolve(command.ObjectId, out EditorObject target, out string errorCode,
                        out string errorMessage))
                {
                    Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                    return;
                }

                switch (command.Name)
                {
                    case "get-object":
                    {
                        if (!TrySnapshot(target, out EditorObjectSnapshot snapshot, out errorCode, out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        Enqueue(client, EditorProtocol.BuildObjectResult(command.RequestId, snapshot));
                        return;
                    }
                    case "set-title":
                    {
                        if (!TryReadTitle(target, out string oldTitle, out errorCode, out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        SetTitleCore(target, command.Title);
                        target.Undo = new UndoRecord { Command = "set-title", Title = oldTitle };
                        if (!TrySnapshot(target, out EditorObjectSnapshot snapshot, out errorCode, out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        Enqueue(client, EditorProtocol.BuildObjectResult(command.RequestId, snapshot));
                        return;
                    }
                    case "set-size":
                    {
                        if (command.Width <= 0 || command.Height <= 0)
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                                EditorProtocol.ErrorInvalidArg, "set-size 的宽高必须 > 0"));
                            return;
                        }
                        // B2（工件修订）：宽度必须**大于面板宽**，否则左侧视口宽 ≤ 0（分栏没有渲染区）。
                        if (s_window != null && command.Width <= s_window.PanelWidth)
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                                EditorProtocol.ErrorInvalidArg, "set-size 的宽度必须大于面板宽 " +
                                                                s_window.PanelWidth + "（否则视口宽 ≤ 0）"));
                            return;
                        }
                        // D9：高度必须**大于 dock 高**，否则场景视口高 = 高 − dock 高 ≤ 0（三分区没有渲染区）。
                        // 这条判据在 EditorSceneView 之前拦下：视口非法会走 Disable（整窗禁用），不是参数错误。
                        if (s_window != null && command.Height <= s_window.DockHeight)
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                                EditorProtocol.ErrorInvalidArg, "set-size 的高度必须大于 dock 高 " +
                                                                s_window.DockHeight + "（否则视口高 ≤ 0）"));
                            return;
                        }
                        if (!TryReadSize(target, out int oldWidth, out int oldHeight, out errorCode,
                                out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        SetSizeCore(target, command.Width, command.Height);
                        target.Undo = new UndoRecord
                        {
                            Command = "set-size", Width = oldWidth, Height = oldHeight,
                        };
                        if (!TrySnapshot(target, out EditorObjectSnapshot snapshot, out errorCode, out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        Enqueue(client, EditorProtocol.BuildObjectResult(command.RequestId, snapshot));
                        return;
                    }
                    case "undo":
                    {
                        if (target.Undo == null)
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                                EditorProtocol.ErrorUndoEmpty, "对象 " + target.Id + " 的撤销槽为空"));
                            return;
                        }
                        UndoRecord record = target.Undo;
                        target.Undo = null;
                        if (record.Command == "set-title")
                        {
                            SetTitleCore(target, record.Title);
                        }
                        else if (record.Command == "set-size")
                        {
                            SetSizeCore(target, record.Width, record.Height);
                        }
                        if (!TrySnapshot(target, out EditorObjectSnapshot snapshot, out errorCode, out errorMessage))
                        {
                            Enqueue(client, EditorProtocol.BuildError(command.RequestId, errorCode, errorMessage));
                            return;
                        }
                        Enqueue(client, EditorProtocol.BuildObjectResult(command.RequestId, snapshot));
                        return;
                    }
                    default:
                    {
                        Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                            EditorProtocol.ErrorBadMessage, "未知命令：" + command.Name));
                        return;
                    }
                }
            }
            catch (ObjectDisposedException)
            {
                if (s_objects.TryGetValue(command.ObjectId, out EditorObject disposed))
                {
                    Invalidate(disposed, "命令执行时发现表面已释放");
                }
                Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                    EditorProtocol.ErrorObjectInvalidated, "对象 " + command.ObjectId + " 已失效"));
            }
            catch (IWindowException ex)
            {
                Enqueue(client, EditorProtocol.BuildError(command.RequestId,
                    EditorProtocol.ErrorObjectInvalidated, "对象 " + command.ObjectId + " 的绑定失效（错误码 " +
                    ex.Code + "）"));
            }
            finally
            {
                s_inCommand = false;
            }
        }

        // ------------------------------------------------------------------------------------
        // 资产浏览（editor-asset-browser 2.3 / 2.4）
        // ------------------------------------------------------------------------------------

        /// <summary>
        /// `list-assets`：门面取根 → 递归枚举 → 逐文件扩展名定种类 → 提供者成 {path, kind} 条目 →
        /// 与上次快照比对广播 → 回结果。**全部在主线程命令执行内**；门面调用之后 `NoteNativeCall()`
        ///（计入本命令的 s_cmdCalls，供线程边界运行期归因）。
        ///
        /// 异常面（design D3）：门面未绑 / 根空串 / 目录不存在 / 枚举 IO 错误 /
        /// IResourceRegistryException（门面已绑但底层抛，如注册表缺失 1014）/ ArgumentException（非法
        /// 字符）一律在本方法内转 `E_ASSET_ROOT` 回该 requestId —— **绝不让它穿到 Host.Tick 的外层
        /// catch**（那会把异常吞成 s_lastError，客户端收不到 result 而静默挂起）。
        /// </summary>
        private static void ExecuteListAssets(EditorClient client, EditorCommand command)
        {
            try
            {
                List<AssetEntry> assets = EnumerateAssets(out string errorMessage);
                if (assets == null)
                {
                    FailListAssets(client, command, errorMessage);
                    return;
                }
                BroadcastAssetsChangedIfNeeded(assets);
                Enqueue(client, EditorProtocol.BuildAssetsResult(command.RequestId, assets));
            }
            catch (DirectoryNotFoundException ex)
            {
                FailListAssets(client, command, "资产根目录不存在（枚举时）：" + ex.Message);
            }
            catch (UnauthorizedAccessException ex)
            {
                FailListAssets(client, command, "资产根没有访问权限：" + ex.Message);
            }
            catch (IOException ex)
            {
                FailListAssets(client, command, "枚举资产根时 IO 错误：" + ex.Message);
            }
            catch (IResourceRegistryException ex)
            {
                FailListAssets(client, command, "资源注册表门面报错（错误码 " + ex.Code + "）：" + ex.Message);
            }
            catch (ArgumentException ex)
            {
                FailListAssets(client, command, "资产根路径非法：" + ex.Message);
            }
            catch (NotSupportedException ex)
            {
                // 路径格式不受支持（含冒号位置非法等）：同样是"根不可用"，不许穿出去。
                FailListAssets(client, command, "资产根路径格式不受支持：" + ex.Message);
            }
            catch (ObjectDisposedException ex)
            {
                // 防御：门面表面被释放（正常路径不会发生，服务关闭前不释放门面）。
                FailListAssets(client, command, "资源注册表门面已释放：" + ex.Message);
            }
        }

        private static void FailListAssets(EditorClient client, EditorCommand command, string message)
        {
            Enqueue(client, EditorProtocol.BuildError(command.RequestId, EditorProtocol.ErrorAssetRoot, message));
            Host.LogFromEditor("[编辑器] list-assets 失败（requestId=" + command.RequestId + "）：" +
                               Sanitize(message));
        }

        /// <summary>
        /// 枚举资产根（editor-asset-browser 2.3 / editor-code-source-display D6）：① 资产根文件逐条
        /// `Resolve(kind, relative)`（精确 (kind,path) → 种类默认 → 默认提供者）；② 注册显示资源（C# 的 dll
        /// 在 bin/，不靠资产根枚举到）逐条引入；③ 按 path 去重（两个 shLib 的源路径重叠、同路径多来源）；
        /// ④ 按 path 序数排序。成功返回条目列表（可能为空数组 = 合法空态）；失败返回 null 并给原因文本
        ///（原因由调用方转 `E_ASSET_ROOT`，这里不抛协议错误）。
        ///
        /// 条目只有 `path` + `kind` 两个字段（2026-11-25 用户裁定）：这里**不为显示算任何固定元数据**
        ///（不算文件大小/时间、不查注册状态）；源显示的 IO 是例外但它只填提供者投影需要的输入 ——
        /// 读 shLib 字节提源路径、扫 Scripting 手写 `*.cs`、逐源路径查存在性，填 `AssetContext.SourceFiles`。
        /// 门控 `CASCADED_EDITOR_SOURCE_DISPLAY` 每次枚举重读：off 时一律走默认提供者、注册显示资源不引入
        ///（红态「未注册落默认」可在同一进程里翻开关观察）。
        /// </summary>
        private static List<AssetEntry> EnumerateAssets(out string errorMessage)
        {
            errorMessage = string.Empty;
            IResourceRegistry registry = Host.EnsureEditorResourceFacade();
            if (registry == null)
            {
                errorMessage = "资产根不可用：" + Host.EditorResourceFacadeError;
                return null;
            }
            // 根空串先判空、目录不存在先 Directory.Exists 判：两条都在把路径交给 Directory.EnumerateFiles
            // 之前分流（空串 / 部分非法字符会在枚举里抛 ArgumentException）。
            string root = registry.GetResourceRootPath();
            NoteNativeCall();
            if (string.IsNullOrEmpty(root))
            {
                errorMessage = "资产根不可用：资源根为空串（门面 GetResourceRootPath 返回空）";
                return null;
            }
            if (!Directory.Exists(root))
            {
                errorMessage = "资产根不可用：目录不存在（\"" + root + "\"）";
                return null;
            }

            // 源显示门控（每次枚举重读；启动时已注册过，这里再调一次覆盖「启动时 off、之后翻回 on」）。
            bool sourceDisplayEnabled = EditorSourceDisplay.Enabled;
            string projectRoot = string.Empty;
            if (sourceDisplayEnabled)
            {
                EditorSourceDisplay.EnsureRegistered();
                if (!TryResolveProjectRoot(root, out projectRoot, out errorMessage))
                {
                    return null;
                }
            }

            var entries = new List<AssetEntry>();
            foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(root, file).Replace('\\', '/');
                if (relative.Length == 0 || relative == ".")
                {
                    continue;
                }
                string kind = AssetDisplayProviders.KindOf(Path.GetExtension(relative));
                IDisplayProvider provider = sourceDisplayEnabled
                    ? AssetDisplayProviders.Resolve(kind, relative)
                    : DefaultFileDisplayProvider.Shared;
                var context = new AssetContext
                {
                    RelativePath = relative,
                    Kind = kind,
                };
                if (sourceDisplayEnabled && provider is ShaderSourceDisplayProvider &&
                    !TryCollectShaderSourceFiles(file, projectRoot, context.SourceFiles, out errorMessage))
                {
                    return null;
                }
                IReadOnlyList<AssetEntry> described = provider.Describe(context);
                if (described == null)
                {
                    continue;
                }
                entries.AddRange(described);
            }

            if (sourceDisplayEnabled)
            {
                // C# 源根与「生成绑定源取回目录」只在真有 C# 显示资源时才需要：按需解析（拆到循环里，
                // 免得只显 shader 源的环境被一个无关目录判失败）。
                string csharpSourceRoot = string.Empty;
                bool csharpSourceRootResolved = false;
                string codegenBindingsRelative = string.Empty;
                foreach (AssetDisplayProviders.DisplayResource resource in AssetDisplayProviders.DisplayResources)
                {
                    var context = new AssetContext
                    {
                        RelativePath = resource.Path,
                        Kind = resource.Kind,
                    };
                    if (resource.Provider is CSharpSourceDisplayProvider)
                    {
                        if (!csharpSourceRootResolved)
                        {
                            if (!TryResolveCSharpSourceRoot(projectRoot, out csharpSourceRoot, out errorMessage))
                            {
                                return null;
                            }
                            codegenBindingsRelative = ResolveCodegenBindingsRelativePath();
                            csharpSourceRootResolved = true;
                        }
                        if (!TryCollectCSharpSourceFiles(csharpSourceRoot, projectRoot, context.SourceFiles,
                                out errorMessage))
                        {
                            return null;
                        }
                        // 128 生成绑定源本批不显示，但明示略去 + 给取回目录（不静默隐藏）。
                        context.OmittedSourceDirectories.Add(codegenBindingsRelative);
                    }
                    else if (resource.Provider is ShaderSourceDisplayProvider)
                    {
                        string shaderLibraryFullPath = Path.Combine(root,
                            resource.Path.Replace('/', Path.DirectorySeparatorChar));
                        if (!TryCollectShaderSourceFiles(shaderLibraryFullPath, projectRoot, context.SourceFiles,
                                out errorMessage))
                        {
                            return null;
                        }
                    }
                    IReadOnlyList<AssetEntry> described = resource.Provider.Describe(context);
                    if (described != null)
                    {
                        entries.AddRange(described);
                    }
                }
            }

            // 输出顺序必须稳定（枚举顺序由文件系统决定）：先按 path 去重（`path` 同时是稳定标识；先出现者胜），
            // 再按 path 序数排序。
            var seenPaths = new HashSet<string>(StringComparer.Ordinal);
            var unique = new List<AssetEntry>(entries.Count);
            foreach (AssetEntry entry in entries)
            {
                if (seenPaths.Add(entry.Path))
                {
                    unique.Add(entry);
                }
            }
            unique.Sort((left, right) => string.CompareOrdinal(left.Path, right.Path));
            return unique;
        }

        // ------------------------------------------------------------------------------------
        // 源显示的主线程 IO（editor-code-source-display 2.1 / 2.2 / design D3）
        //
        // 契约：提供者不碰文件系统、不调门面 —— 下面这些活都在主线程命令执行里做完，结果填进
        // AssetContext（SourceFiles / OmittedSourceDirectories），提供者只投影。
        // ------------------------------------------------------------------------------------

        /// <summary>
        /// 工程根：env `CASCADED_EDITOR_PROJECT_ROOT` 优先（测试传临时根），否则资源根的上级。
        /// 不存在 / 路径非法 = 显式错误（不静默按空列表冒充成功，D5）。
        /// </summary>
        private static bool TryResolveProjectRoot(string assetRoot, out string projectRoot, out string errorMessage)
        {
            errorMessage = string.Empty;
            projectRoot = string.Empty;
            string configured = Environment.GetEnvironmentVariable("CASCADED_EDITOR_PROJECT_ROOT") ?? string.Empty;
            string candidate;
            if (configured.Length != 0)
            {
                candidate = configured;
            }
            else
            {
                string fullRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(assetRoot));
                candidate = Path.GetDirectoryName(fullRoot) ?? string.Empty;
                if (candidate.Length == 0)
                {
                    errorMessage = "工程根不可用：资源根 \"" + assetRoot + "\" 没有上级目录（源条目的 path 基准无从确定）";
                    return false;
                }
            }
            try
            {
                projectRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(candidate));
            }
            catch (Exception ex) when (ex is ArgumentException || ex is NotSupportedException ||
                                       ex is PathTooLongException)
            {
                errorMessage = "工程根不可用：路径非法（\"" + candidate + "\"）：" + ex.Message;
                return false;
            }
            if (!Directory.Exists(projectRoot))
            {
                errorMessage = "工程根不可用：目录不存在（\"" + projectRoot + "\"；来源 " +
                               (configured.Length != 0 ? "CASCADED_EDITOR_PROJECT_ROOT" : "资源根上级") + "）";
                return false;
            }
            return true;
        }

        /// <summary>C# 源根 = 工程根下的 `Scripting/`：不存在 / 不是目录 = 显式错误（D5）。</summary>
        private static bool TryResolveCSharpSourceRoot(string projectRoot, out string sourceRoot,
            out string errorMessage)
        {
            errorMessage = string.Empty;
            sourceRoot = Path.Combine(projectRoot, "Scripting");
            if (!Directory.Exists(sourceRoot))
            {
                errorMessage = "C# 源根不可用：目录不存在（\"" + sourceRoot + "\"）；源显示需要它在工程根下，" +
                               "或把 CASCADED_EDITOR_SOURCE_DISPLAY 设为 off";
                return false;
            }
            return true;
        }

        /// <summary>
        /// 读 shLib 字节、提取实际编译进库的源路径（D2 / 溯源-004）：`m_ShaderFiles` 是 ShaderLibrary 的
        /// 第一个字段 —— 文件开头是 map 条数（uint64），紧随第一条目的键（PathHash 序列化成
        /// 「uint64 长度 + ASCII 字节」，见 CACore/header/HashKeys.h 与 CodeGenSerialization.h 的字节契约）。
        /// 之后逐条扫「长度前缀 + 可打印 ASCII」的字符串，取与首条键**同目录前缀**的相对路径并去重
        ///（键 / 每个源文件的 path 字段 / 根结构表的键都按同一序列化写入，去重后就是键集合）。
        /// 提取条数与开头声明的 map 条数不符 ⇒ 序列化格式已变，显式报错（不静默少给/多给）。
        /// </summary>
        private static bool TryExtractShaderSourcePaths(byte[] bytes, out List<string> sourcePaths,
            out string errorMessage)
        {
            sourcePaths = new List<string>();
            errorMessage = string.Empty;
            const int lengthPrefixSize = sizeof(ulong);
            if (bytes == null || bytes.Length < lengthPrefixSize * 3)
            {
                errorMessage = "shLib 字节过短（" + (bytes == null ? 0 : bytes.Length) +
                               " 字节），读不出 m_ShaderFiles 条数与首条源路径";
                return false;
            }
            int position = 0;
            ulong declaredCount = 0;
            if (!TryReadUInt64(bytes, ref position, out declaredCount))
            {
                errorMessage = "shLib 开头不是 m_ShaderFiles 条数（格式可能已变）";
                return false;
            }
            if (declaredCount == 0)
            {
                return true; // 合法空库：0 条源路径
            }
            string firstPath;
            if (!TryReadLengthPrefixedAscii(bytes, ref position, out firstPath) || !IsRelativeSourcePath(firstPath))
            {
                errorMessage = "shLib 的 m_ShaderFiles 首条键不是合法的相对源路径（格式可能已变）";
                return false;
            }
            string prefix = DirectoryPrefixOf(firstPath);
            var seen = new HashSet<string>(StringComparer.Ordinal);
            var found = new List<string>();
            for (int offset = 0; offset + lengthPrefixSize <= bytes.Length; ++offset)
            {
                int probe = offset;
                if (!TryReadLengthPrefixedAscii(bytes, ref probe, out string candidate))
                {
                    continue;
                }
                if (!IsRelativeSourcePath(candidate))
                {
                    continue;
                }
                if (prefix.Length != 0 && !candidate.StartsWith(prefix, StringComparison.Ordinal))
                {
                    continue;
                }
                if (seen.Add(candidate))
                {
                    found.Add(candidate);
                }
            }
            if ((ulong)found.Count != declaredCount)
            {
                errorMessage = "shLib 源路径提取失败：m_ShaderFiles 声明 " + declaredCount + " 条、按字节提取 " +
                               found.Count + " 条（序列化格式可能已变，源映射需要同步）";
                return false;
            }
            found.Sort(StringComparer.Ordinal);
            sourcePaths = found;
            return true;
        }

        /// <summary>读一个 uint64（本机字节序，落盘端与运行时同机）。</summary>
        private static bool TryReadUInt64(byte[] bytes, ref int position, out ulong value)
        {
            value = 0;
            if (position < 0 || position + sizeof(ulong) > bytes.Length)
            {
                return false;
            }
            value = BitConverter.ToUInt64(bytes, position);
            position += sizeof(ulong);
            return true;
        }

        /// <summary>读一个「uint64 长度 + 可打印 ASCII 字节」的字符串（CodeGen 的字符串字节契约）。</summary>
        private static bool TryReadLengthPrefixedAscii(byte[] bytes, ref int position, out string text)
        {
            text = string.Empty;
            int start = position;
            if (!TryReadUInt64(bytes, ref position, out ulong length))
            {
                position = start;
                return false;
            }
            if (length == 0 || length > (ulong)(bytes.Length - position))
            {
                position = start;
                return false;
            }
            int charStart = position;
            int charCount = (int)length;
            for (int index = 0; index < charCount; ++index)
            {
                byte value = bytes[charStart + index];
                if (value < 0x20 || value > 0x7E)
                {
                    position = start;
                    return false;
                }
            }
            text = Encoding.ASCII.GetString(bytes, charStart, charCount);
            position = charStart + charCount;
            return true;
        }

        /// <summary>源路径形状：相对路径（无盘符 / 反斜杠 / 前导斜杠）、可打印 ASCII、无空段与 `.`/`..` 段。</summary>
        private static bool IsRelativeSourcePath(string text)
        {
            if (string.IsNullOrEmpty(text) || text[0] == '/' || text.IndexOf(':') >= 0 || text.IndexOf('\\') >= 0)
            {
                return false;
            }
            foreach (char ch in text)
            {
                if (ch < 0x20 || ch > 0x7E)
                {
                    return false;
                }
            }
            string[] segments = text.Split('/');
            foreach (string segment in segments)
            {
                if (segment.Length == 0 || segment == "." || segment == "..")
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>首条源路径的目录前缀（到最后一个 `/` 为止，含）；没有目录时为空串。</summary>
        private static string DirectoryPrefixOf(string path)
        {
            int slash = path.LastIndexOf('/');
            return slash < 0 ? string.Empty : path.Substring(0, slash + 1);
        }

        /// <summary>
        /// 读一个 shLib 文件 → 源路径列表 → `CAResources/&lt;源路径&gt;.slang` 的源文件信息（相对工程根 +
        /// 是否存在）。源文件缺失是合法结果（历史路径的 kind 用 missing 变体表达），不在这里失败。
        /// </summary>
        private static bool TryCollectShaderSourceFiles(string shaderLibraryFullPath, string projectRoot,
            List<AssetSourceFile> sourceFiles, out string errorMessage)
        {
            errorMessage = string.Empty;
            sourceFiles.Clear();
            byte[] bytes;
            try
            {
                bytes = File.ReadAllBytes(shaderLibraryFullPath);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException ||
                                       ex is NotSupportedException || ex is ArgumentException)
            {
                errorMessage = "读 shLib 字节失败（源映射需要它）：\"" + shaderLibraryFullPath + "\"：" + ex.Message;
                return false;
            }
            if (!TryExtractShaderSourcePaths(bytes, out List<string> sourcePaths, out errorMessage))
            {
                errorMessage = "shLib 源映射失败（\"" + shaderLibraryFullPath + "\"）：" + errorMessage;
                return false;
            }
            foreach (string sourcePath in sourcePaths)
            {
                sourceFiles.Add(BuildSourcePathInfo("CAResources/" + sourcePath + ".slang", projectRoot));
            }
            return true;
        }

        /// <summary>
        /// 扫 Scripting/ 下的**手写** `*.cs`：`obj/`（MSBuild 生成产物，含 AssemblyInfo / AssemblyAttributes）
        /// 与 `bin/` 一律排除。128 个生成绑定源在 `out/build/&lt;preset&gt;/codegen/bindings`，不在这个根下 ——
        /// 它们由 source-omitted 标记条目明示略去 + 给取回目录（见 CSharpSourceDisplayProvider）。
        /// </summary>
        private static bool TryCollectCSharpSourceFiles(string sourceRoot, string projectRoot,
            List<AssetSourceFile> sourceFiles, out string errorMessage)
        {
            errorMessage = string.Empty;
            sourceFiles.Clear();
            var handwritten = new List<string>();
            try
            {
                foreach (string file in Directory.EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories))
                {
                    // 通配符在 Windows 上对扩展名有历史怪癖（"*.abc" 也可能命中 ".abcd"）：再按扩展名精确收一次。
                    if (!Path.GetExtension(file).Equals(".cs", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }
                    string relativeFromRoot = Path.GetRelativePath(sourceRoot, file).Replace('\\', '/');
                    if (IsBuildArtifactPath(relativeFromRoot))
                    {
                        continue;
                    }
                    handwritten.Add("Scripting/" + relativeFromRoot);
                }
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException ||
                                       ex is NotSupportedException || ex is ArgumentException)
            {
                errorMessage = "扫 C# 源根失败：\"" + sourceRoot + "\"：" + ex.Message;
                return false;
            }
            handwritten.Sort(StringComparer.Ordinal);
            foreach (string sourcePath in handwritten)
            {
                sourceFiles.Add(BuildSourcePathInfo(sourcePath, projectRoot));
            }
            return true;
        }

        /// <summary>构建产物路径：任意目录段是 `obj` / `bin`（不区分大小写）即排除；最后一段是文件名。</summary>
        private static bool IsBuildArtifactPath(string relativePath)
        {
            string[] segments = relativePath.Split('/');
            for (int index = 0; index + 1 < segments.Length; ++index)
            {
                if (segments[index].Equals("obj", StringComparison.OrdinalIgnoreCase) ||
                    segments[index].Equals("bin", StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// 按源路径（相对工程根）填一条源文件信息：只算**源路径 + 是否存在**（提供者据此选 kind），
        /// 不读大小 / 修改时间，也不查注册状态（那些是用户没要求的固定元数据，D5 删）。
        /// </summary>
        private static AssetSourceFile BuildSourcePathInfo(string sourcePath, string projectRoot)
        {
            string fullPath = Path.Combine(projectRoot, sourcePath.Replace('/', Path.DirectorySeparatorChar));
            return new AssetSourceFile
            {
                Path = sourcePath,
                Exists = File.Exists(fullPath),
            };
        }

        /// <summary>
        /// 「生成绑定源」的取回目录（相对工程根）：`out/build/&lt;preset&gt;/codegen/bindings`。`&lt;preset&gt;`
        /// 从编辑器服务程序集位置推导（`&lt;repo&gt;/out/build/&lt;preset&gt;/bin/Scripting.Managed.dll`）；
        /// 布局不认识时退到 `out/build`（仍点名取回区，绝不把略去这件事静默隐藏）。
        /// </summary>
        private static string ResolveCodegenBindingsRelativePath()
        {
            string assemblyDirectory = Path.GetDirectoryName(typeof(EditorService).Assembly.Location) ??
                                       string.Empty;
            if (assemblyDirectory.Length != 0)
            {
                var bin = new DirectoryInfo(assemblyDirectory);
                DirectoryInfo preset = bin.Parent;
                DirectoryInfo build = preset?.Parent;
                DirectoryInfo outDirectory = build?.Parent;
                if (bin.Name.Equals("bin", StringComparison.OrdinalIgnoreCase) &&
                    preset != null && build != null && outDirectory != null &&
                    build.Name.Equals("build", StringComparison.OrdinalIgnoreCase) &&
                    outDirectory.Name.Equals("out", StringComparison.OrdinalIgnoreCase))
                {
                    return "out/build/" + preset.Name + "/codegen/bindings";
                }
            }
            return "out/build";
        }

        /// <summary>
        /// 与上次成功枚举的路径快照比对，有增删就广播 `assets-changed`（fields 只有 added / removed）。
        /// 首次成功枚举只记快照、不发事件（没有"上一次"可比）；其它客户端收到事件后重发 list-assets。
        /// </summary>
        private static void BroadcastAssetsChangedIfNeeded(List<AssetEntry> assets)
        {
            var current = new HashSet<string>(StringComparer.Ordinal);
            foreach (AssetEntry entry in assets)
            {
                current.Add(entry.Path);
            }
            if (s_lastAssetPaths != null)
            {
                int added = 0;
                int removed = 0;
                foreach (string path in current)
                {
                    if (!s_lastAssetPaths.Contains(path))
                    {
                        ++added;
                    }
                }
                foreach (string path in s_lastAssetPaths)
                {
                    if (!current.Contains(path))
                    {
                        ++removed;
                    }
                }
                if (added != 0 || removed != 0)
                {
                    Broadcast(EditorProtocol.BuildAssetsChangedEvent(added, removed));
                    Host.LogFromEditor("[编辑器] 资产变化：新增 " + added + "、移除 " + removed +
                                       " 条（已广播 assets-changed）");
                }
            }
            s_lastAssetPaths = current;
        }

        private static bool TryResolve(int objectId, out EditorObject target, out string errorCode,
            out string errorMessage)
        {
            target = null;
            errorCode = string.Empty;
            errorMessage = string.Empty;
            if (s_invalidated.Contains(objectId))
            {
                errorCode = EditorProtocol.ErrorObjectInvalidated;
                errorMessage = "对象 " + objectId + " 已失效";
                return false;
            }
            if (!s_objects.TryGetValue(objectId, out target))
            {
                errorCode = EditorProtocol.ErrorUnknownObject;
                errorMessage = "未知对象编号 " + objectId;
                return false;
            }
            return true;
        }

        private static bool TryReadTitle(EditorObject obj, out string title, out string errorCode,
            out string errorMessage)
        {
            title = string.Empty;
            errorCode = string.Empty;
            errorMessage = string.Empty;
            try
            {
                title = obj.Window.GetWindowName();
                NoteNativeCall();
                return true;
            }
            catch (ObjectDisposedException)
            {
                Invalidate(obj, "读标题时表面已释放");
                errorCode = EditorProtocol.ErrorObjectInvalidated;
                errorMessage = "对象 " + obj.Id + " 已失效";
                return false;
            }
            catch (IWindowException ex)
            {
                Invalidate(obj, "读标题时绑定失效：" + ex.Code);
                errorCode = EditorProtocol.ErrorObjectInvalidated;
                errorMessage = "对象 " + obj.Id + " 的绑定失效（错误码 " + ex.Code + "）";
                return false;
            }
        }

        private static bool TryReadSize(EditorObject obj, out int width, out int height, out string errorCode,
            out string errorMessage)
        {
            width = 0;
            height = 0;
            errorCode = string.Empty;
            errorMessage = string.Empty;
            try
            {
                var size = obj.Window.GetWindowSize();
                NoteNativeCall();
                width = size.X;
                height = size.Y;
                return true;
            }
            catch (ObjectDisposedException)
            {
                Invalidate(obj, "读尺寸时表面已释放");
                errorCode = EditorProtocol.ErrorObjectInvalidated;
                errorMessage = "对象 " + obj.Id + " 已失效";
                return false;
            }
            catch (IWindowException ex)
            {
                Invalidate(obj, "读尺寸时绑定失效：" + ex.Code);
                errorCode = EditorProtocol.ErrorObjectInvalidated;
                errorMessage = "对象 " + obj.Id + " 的绑定失效（错误码 " + ex.Code + "）";
                return false;
            }
        }

        private static bool TrySnapshot(EditorObject obj, out EditorObjectSnapshot snapshot, out string errorCode,
            out string errorMessage)
        {
            snapshot = null;
            if (!TryReadTitle(obj, out string title, out errorCode, out errorMessage))
            {
                return false;
            }
            if (!TryReadSize(obj, out int width, out int height, out errorCode, out errorMessage))
            {
                return false;
            }
            snapshot = new EditorObjectSnapshot { Id = obj.Id, Title = title, Width = width, Height = height };
            return true;
        }

        private static void SetTitleCore(EditorObject obj, string title)
        {
            obj.Window.SetWindowName(title);
            NoteNativeCall();
            Broadcast(EditorProtocol.BuildChangedEvent(obj.Id, new Dictionary<string, object>
            {
                ["title"] = title,
            }));
        }

        private static void SetSizeCore(EditorObject obj, int width, int height)
        {
            obj.Window.SetWindowSize(width, height);
            NoteNativeCall();
            // Windows 上尺寸回调在 SetWindowSize 内同步触发；这里仍读一次真实尺寸再同步布局（幂等），
            // 避免"回调没来"的平台上分栏与读数落后。结果值来自真实读回，不是请求值的乐观回显。
            var size = obj.Window.GetWindowSize();
            NoteNativeCall();
            SyncLayout(size.X, size.Y, "set-size");
        }

        // ------------------------------------------------------------------------------------
        // 监听线程（网络 I/O；绝不调用引擎）
        // ------------------------------------------------------------------------------------

        private static void ListenerLoop()
        {
            // 没有 `HttpListener.Pending`（BCL 不提供）：用 `GetContextAsync()` 的非阻塞轮询 —— 同一个
            // 监听线程既接新连接，也轮询已连接客户端的收发；停止由 `Shutdown` 里的 `Stop()` 让任务失败退出。
            Task<HttpListenerContext> pendingContext = null;
            try
            {
                if (s_listener != null)
                {
                    pendingContext = s_listener.GetContextAsync();
                }
            }
            catch (Exception ex)
            {
                Host.LogFromEditor("[编辑器] 监听线程启动失败：" + ex.GetType().Name + ":" + ex.Message);
                return;
            }
            while (!s_listenerStop)
            {
                try
                {
                    if (pendingContext != null && pendingContext.IsCompleted)
                    {
                        HttpListenerContext context = null;
                        if (pendingContext.Status == TaskStatus.RanToCompletion)
                        {
                            context = pendingContext.Result;
                        }
                        pendingContext = null;
                        if (context != null)
                        {
                            HandleContext(context);
                        }
                        if (!s_listenerStop && s_listener != null && s_listener.IsListening)
                        {
                            pendingContext = s_listener.GetContextAsync();
                        }
                    }
                }
                catch (HttpListenerException)
                {
                    break;
                }
                catch (ObjectDisposedException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Host.LogFromEditor("[编辑器] 监听线程异常：" + ex.GetType().Name + ":" + ex.Message);
                }

                EditorClient[] snapshot;
                lock (s_clientsLock)
                {
                    snapshot = s_clients.ToArray();
                }
                foreach (EditorClient client in snapshot)
                {
                    PollClient(client);
                }
                Thread.Sleep(2);
            }
            CloseAllClients("监听线程退出");
        }

        private static void HandleContext(HttpListenerContext context)
        {
            string path = context.Request.Url?.AbsolutePath ?? "/";
            if (path == "/ws")
            {
                if (!context.Request.IsWebSocketRequest)
                {
                    WriteResponse(context, 400, "text/plain; charset=utf-8",
                        Encoding.UTF8.GetBytes("WebSocket 握手请求无效：缺少 Upgrade 头"));
                    return;
                }
                AcceptWebSocket(context);
                return;
            }
            ServeStatic(context, path);
        }

        private static void AcceptWebSocket(HttpListenerContext context)
        {
            WebSocket socket;
            try
            {
                // AcceptWebSocketAsync 返回的是 HttpListenerWebSocketContext，WebSocket 在它的属性上。
                HttpListenerWebSocketContext webSocketContext =
                    context.AcceptWebSocketAsync(null).GetAwaiter().GetResult();
                socket = webSocketContext.WebSocket;
            }
            catch (Exception ex)
            {
                Host.LogFromEditor("[编辑器] WebSocket 升级失败：" + ex.GetType().Name + ":" + ex.Message);
                try
                {
                    context.Response.StatusCode = 500;
                    context.Response.Close();
                }
                catch (Exception)
                {
                }
                return;
            }
            var client = new EditorClient
            {
                Id = Interlocked.Increment(ref s_nextClientId),
                Socket = socket,
                Remote = context.Request.RemoteEndPoint?.ToString() ?? "?",
            };
            lock (s_clientsLock)
            {
                s_clients.Add(client);
            }
            StartReceive(client);
            Host.LogFromEditor("[编辑器] 客户端 #" + client.Id + " 已连接（" + client.Remote + "）");
        }

        /// <summary>
        /// 解析面板静态根目录（bin/editor-web）。**必须返回绝对路径**：
        /// hostfxr 宿主加载下 AppContext.BaseDirectory 可能是空字符串（实测错误日志里 root 打印成相对
        /// "editor-web"，相对 CWD 解析不到 bin/editor-web/，面板永远 503），此时改用程序集所在目录
        /// （hostfxr 下 Assembly.Location 是 bin/Scripting.Managed.dll）的 bin/ 拼 editor-web。
        /// 解析结果原样打日志，供视觉验收/HTTP 核对绝对路径。
        /// </summary>
        private static string ResolveEditorWebRoot()
        {
            string baseDirectory = AppContext.BaseDirectory;
            if (!string.IsNullOrEmpty(baseDirectory))
            {
                return Path.GetFullPath(Path.Combine(baseDirectory, "editor-web"));
            }
            string assemblyLocation = typeof(EditorService).Assembly.Location;
            if (!string.IsNullOrEmpty(assemblyLocation))
            {
                string assemblyDirectory = Path.GetDirectoryName(assemblyLocation);
                if (!string.IsNullOrEmpty(assemblyDirectory))
                {
                    return Path.GetFullPath(Path.Combine(assemblyDirectory, "editor-web"));
                }
            }
            // 最后兜底：进程可执行文件所在目录（tester 的 bin/）。仍然解析成绝对路径并照常打日志。
            string processPath = Environment.ProcessPath ?? string.Empty;
            string processDirectory = processPath.Length == 0 ? string.Empty : Path.GetDirectoryName(processPath) ?? string.Empty;
            if (processDirectory.Length != 0)
            {
                return Path.GetFullPath(Path.Combine(processDirectory, "editor-web"));
            }
            return Path.GetFullPath("editor-web");
        }

        private static void ServeStatic(HttpListenerContext context, string path)
        {
            string root = ResolveEditorWebRoot();
            if (!s_staticRootLogged)
            {
                s_staticRootLogged = true;
                // 走双通道（宿主日志 + 读数文件）：`TestEditorWindowPanel` 的静态页判据必须拿**服务实际用的
                // 这个绝对路径**当期望值。用测试端自己的 CWD 相对探测会和服务不一致 —— 1.11 把静态根从
                // CWD 相对改成绝对路径之后，cwd=仓库根时探测 false、服务却 200（那正是 editor 批遗留的
                // hasIndex bug）；读数文件在可见窗口模式（不带 --headless）下同样可读，所以放这条通道。
                LogServiceMarker("[编辑器] 面板静态根：" + root);
            }
            string index = Path.Combine(root, "index.html");
            if (!File.Exists(index))
            {
                const string message =
                    "<!doctype html><meta charset=\"utf-8\"><title>编辑器界面资源未构建</title>" +
                    "<h1>编辑器界面资源未构建</h1><p>原因：缺 node 或构建步被跳过（bin/editor-web/index.html 不存在）。</p>" +
                    "<p>按 EditorWeb/README.md 构建面板（npm ci &amp;&amp; npm run build，然后重新跑 python build.py）。</p>" +
                    "<p>协议端点 /ws 不受影响，可直接用 WebSocket 客户端调试。</p>";
                WriteResponse(context, 503, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(message));
                if (!s_staticErrorLogged)
                {
                    s_staticErrorLogged = true;
                    Host.LogFromEditor("[编辑器] 面板静态资源缺失：" + root +
                                       " 下没有 index.html —— 已返回说明原因的错误页（503），不是静默 404");
                }
                return;
            }
            if (path == "/")
            {
                path = "/index.html";
            }
            // dock 页面（editor-asset-browser 3.1）：assets.html 单独缺失时**不能**退回 404 纯文本 ——
            // 返回说明"资产浏览器页面未构建"的错误页并写日志。dock 控制器的导航判据把 4xx/5xx 当失败
            //（EditorWindow.OnDockNavigationCompleted），于是编辑器明确禁用而不是显示一个空壳 dock。
            if (path == "/assets.html" && !File.Exists(Path.Combine(root, "assets.html")))
            {
                const string dockMessage =
                    "<!doctype html><meta charset=\"utf-8\"><title>资产浏览器页面未构建</title>" +
                    "<h1>资产浏览器页面未构建</h1><p>原因：bin/editor-web/assets.html 不存在（缺 node 或构建步被跳过）。</p>" +
                    "<p>按 EditorWeb/README.md 构建面板（npm ci &amp;&amp; npm run build，然后重新跑 python build.py）。</p>";
                WriteResponse(context, 503, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(dockMessage));
                if (!s_dockStaticErrorLogged)
                {
                    s_dockStaticErrorLogged = true;
                    Host.LogFromEditor("[编辑器] 资产浏览器页面缺失：" + root +
                                       " 下没有 assets.html —— 已返回说明原因的错误页（503），不是静默 404");
                }
                return;
            }
            // 渲染原型页（editor-web-render-spike 1.2）：spike.html 单独缺失时返回说明原因的错误页并写日志，
            // 原型窗的导航判据把 4xx/5xx 当失败，于是明确禁用而不是显示一个空壳。
            if (path == "/spike.html" && !File.Exists(Path.Combine(root, "spike.html")))
            {
                const string spikeMessage =
                    "<!doctype html><meta charset=\"utf-8\"><title>渲染原型页面未构建</title>" +
                    "<h1>渲染原型页面未构建</h1><p>原因：bin/editor-web/spike.html 不存在（缺 node 或构建步被跳过）。</p>" +
                    "<p>按 EditorWeb/README.md 构建面板（npm ci &amp;&amp; npm run build，然后重新跑 python build.py）。</p>";
                WriteResponse(context, 503, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(spikeMessage));
                if (!s_spikeStaticErrorLogged)
                {
                    s_spikeStaticErrorLogged = true;
                    Host.LogFromEditor("[编辑器] 渲染原型页面缺失：" + root +
                                       " 下没有 spike.html —— 已返回说明原因的错误页（503），不是静默 404");
                }
                return;
            }
            string relative = path.TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
            string candidate;
            try
            {
                candidate = Path.GetFullPath(Path.Combine(root, relative));
            }
            catch (Exception)
            {
                WriteResponse(context, 400, "text/plain; charset=utf-8", Encoding.UTF8.GetBytes("路径非法"));
                return;
            }
            string rootPrefix = root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!candidate.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase) || !File.Exists(candidate))
            {
                WriteResponse(context, 404, "text/plain; charset=utf-8", Encoding.UTF8.GetBytes("找不到资源"));
                return;
            }
            byte[] body;
            try
            {
                body = File.ReadAllBytes(candidate);
            }
            catch (Exception ex)
            {
                WriteResponse(context, 500, "text/plain; charset=utf-8",
                    Encoding.UTF8.GetBytes("读取静态文件失败：" + ex.Message));
                return;
            }
            WriteResponse(context, 200, ContentType(candidate), body);
        }

        private static void WriteResponse(HttpListenerContext context, int status, string contentType, byte[] body)
        {
            try
            {
                context.Response.StatusCode = status;
                context.Response.ContentType = contentType;
                context.Response.ContentLength64 = body.Length;
                context.Response.OutputStream.Write(body, 0, body.Length);
                context.Response.OutputStream.Close();
            }
            catch (Exception)
            {
                // 客户端提前断开：只影响这一个请求。
            }
        }

        private static string ContentType(string file)
        {
            switch (Path.GetExtension(file).ToLowerInvariant())
            {
                case ".html":
                case ".htm":
                    return "text/html; charset=utf-8";
                case ".js":
                case ".mjs":
                    return "text/javascript; charset=utf-8";
                case ".css":
                    return "text/css; charset=utf-8";
                case ".json":
                case ".map":
                    return "application/json; charset=utf-8";
                case ".svg":
                    return "image/svg+xml";
                case ".png":
                    return "image/png";
                case ".ico":
                    return "image/x-icon";
                default:
                    return "application/octet-stream";
            }
        }

        // ---- 客户端收发（只在监听线程）----

        private static void StartReceive(EditorClient client)
        {
            try
            {
                client.ReceiveTask = client.Socket.ReceiveAsync(new ArraySegment<byte>(client.Buffer),
                    CancellationToken.None);
            }
            catch (Exception)
            {
                client.CloseAfterSend = true;
            }
        }

        private static void PollClient(EditorClient client)
        {
            if (client.Closed)
            {
                return;
            }
            if (client.ReceiveTask != null && client.ReceiveTask.IsCompleted)
            {
                Task<WebSocketReceiveResult> task = client.ReceiveTask;
                client.ReceiveTask = null;
                if (task.IsFaulted || task.IsCanceled)
                {
                    CloseClient(client, "接收失败");
                    return;
                }
                WebSocketReceiveResult result = task.Result;
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    client.CloseAfterSend = true;
                }
                else if (result.MessageType == WebSocketMessageType.Binary)
                {
                    Enqueue(client, EditorProtocol.BuildError(0, EditorProtocol.ErrorBadMessage,
                        "不支持二进制帧，本协议只收 UTF-8 文本帧"));
                    client.CloseAfterSend = true;
                }
                else
                {
                    if (result.Count > 0)
                    {
                        client.ReceiveBuffer.Write(client.Buffer, 0, result.Count);
                    }
                    if (!result.EndOfMessage)
                    {
                        StartReceive(client);
                    }
                    else
                    {
                        string text = Encoding.UTF8.GetString(client.ReceiveBuffer.ToArray());
                        client.ReceiveBuffer.SetLength(0);
                        HandleClientText(client, text);
                        if (!client.CloseAfterSend)
                        {
                            StartReceive(client);
                        }
                    }
                }
            }

            if (client.SendTask != null && client.SendTask.IsCompleted)
            {
                if (client.SendTask.IsFaulted || client.SendTask.IsCanceled)
                {
                    CloseClient(client, "发送失败");
                    return;
                }
                client.SendTask = null;
            }
            // CloseAfterSend 的客户端也要先把队列里的消息发完（例如 E_PROTOCOL_VERSION 的拒绝结果），
            // 队列清空、发送任务结束后才关连接；早先这里多了 `!CloseAfterSend` 条件，导致拒绝结果
            // 永远发不出去、连接也不关（⑧ 超时的根因）。
            if (client.SendTask == null && client.SendQueue.TryDequeue(out string message))
            {
                try
                {
                    byte[] bytes = Encoding.UTF8.GetBytes(message);
                    client.SendTask = client.Socket.SendAsync(new ArraySegment<byte>(bytes),
                        WebSocketMessageType.Text, true, CancellationToken.None);
                }
                catch (Exception)
                {
                    CloseClient(client, "发送异常");
                    return;
                }
            }
            if (client.CloseAfterSend && client.SendQueue.IsEmpty && client.SendTask == null)
            {
                CloseClient(client, "关闭连接");
            }
        }

        private static void HandleClientText(EditorClient client, string text)
        {
            if (!EditorProtocol.TryParse(text, out EditorInbound inbound, out int requestId, out string errorCode,
                    out string errorMessage))
            {
                Enqueue(client, EditorProtocol.BuildError(requestId, errorCode, errorMessage));
                return;
            }
            if (inbound.Kind == EditorInboundKind.Hello)
            {
                if (client.Handshaken)
                {
                    Enqueue(client, EditorProtocol.BuildError(0, EditorProtocol.ErrorBadMessage,
                        "重复的 hello（本连接已经握手）"));
                    return;
                }
                if (inbound.ProtocolVersion != EditorProtocol.Version)
                {
                    Enqueue(client, EditorProtocol.BuildError(0, EditorProtocol.ErrorProtocolVersion,
                        "协议版本不符：客户端 " + inbound.ProtocolVersion + "，服务端 " + EditorProtocol.Version));
                    client.CloseAfterSend = true;
                    return;
                }
                client.Handshaken = true;
                inbound.Client = client;
                s_inbound.Enqueue(inbound);
                return;
            }
            if (!client.Handshaken)
            {
                Enqueue(client, EditorProtocol.BuildError(requestId, EditorProtocol.ErrorBadMessage,
                    "hello 之前不能发命令"));
                client.CloseAfterSend = true;
                return;
            }
            inbound.Client = client;
            // M3（工件修订）：入队即打一行（监听线程）。C++ 用例在"不 Tick"的窗口期先看到这行，
            // 再断言标题未变 —— 排除"命令还没到"的假阴性。
            // 渲染原型的三类消息（PROTOCOL.md §7）没有 `command` 字段，`inbound.Command` 为 null：
            // 这里按 kind 分别打，**不许无条件解引用 Command**（否则页面第一条 spike-hello 就让监听线程
            // 抛 NullReferenceException，进程 0xC0000005 崩掉）。
            if (inbound.Command != null)
            {
                LogServiceMarker("[编辑器] 命令已入队（监听线程）：requestId=" + requestId + " command=" +
                                 inbound.Command.Name);
            }
            else
            {
                LogServiceMarker("[编辑器] 渲染原型消息已入队（监听线程）：kind=" + inbound.Kind);
            }
            s_inbound.Enqueue(inbound);
        }

        private static void Enqueue(EditorClient client, string message)
        {
            if (client == null)
            {
                return;
            }
            client.SendQueue.Enqueue(message);
        }

        private static void Broadcast(string message)
        {
            EditorClient[] snapshot;
            lock (s_clientsLock)
            {
                snapshot = s_clients.ToArray();
            }
            foreach (EditorClient client in snapshot)
            {
                client.SendQueue.Enqueue(message);
            }
        }

        /// <summary>发给除发送者以外的所有客户端（渲染原型的帧转发：C++ 驱动发进来、原型页收到）。</summary>
        private static void BroadcastExcept(EditorClient sender, string message)
        {
            EditorClient[] snapshot;
            lock (s_clientsLock)
            {
                snapshot = s_clients.ToArray();
            }
            foreach (EditorClient client in snapshot)
            {
                if (ReferenceEquals(client, sender))
                {
                    continue;
                }
                client.SendQueue.Enqueue(message);
            }
        }

        private static void CloseClient(EditorClient client, string reason)
        {
            if (client.Closed)
            {
                return;
            }
            client.Closed = true;
            lock (s_clientsLock)
            {
                s_clients.Remove(client);
            }
            try
            {
                if (client.Socket.State == WebSocketState.Open ||
                    client.Socket.State == WebSocketState.CloseReceived)
                {
                    client.Socket.CloseAsync(WebSocketCloseStatus.NormalClosure, string.Empty,
                        CancellationToken.None).Wait(200);
                }
            }
            catch (Exception)
            {
            }
            try
            {
                client.Socket.Dispose();
            }
            catch (Exception)
            {
            }
            Host.LogFromEditor("[编辑器] 客户端 #" + client.Id + " 断开（" + reason + "）");
        }

        private static void CloseAllClients(string reason)
        {
            EditorClient[] snapshot;
            lock (s_clientsLock)
            {
                snapshot = s_clients.ToArray();
                s_clients.Clear();
            }
            foreach (EditorClient client in snapshot)
            {
                try
                {
                    client.Socket.Abort();
                }
                catch (Exception)
                {
                }
            }
            if (snapshot.Length != 0)
            {
                Host.LogFromEditor("[编辑器] " + reason + "：断开 " + snapshot.Length + " 个客户端");
            }
        }

        // ------------------------------------------------------------------------------------
        // 读数行（D9 / D11 的数据源）
        // ------------------------------------------------------------------------------------

        private static void EmitReadings()
        {
            int clientCount;
            lock (s_clientsLock)
            {
                clientCount = s_clients.Count;
            }
            string line = "[编辑器 读数] probe=editor" +
                " tick=" + s_tick +
                " state=" + (s_state == 2 && (s_window != null || s_spikeWindow != null) ? "running" : "starting") +
                " mode=main-thread" +
                " port=" + s_port +
                " clients=" + clientCount +
                " objects=" + s_objects.Count +
                " invalidated=" + s_invalidatedCount +
                " shellClosed=" + (s_shellClosed ? 1 : 0) +
                " windowSelf=" + s_windowSelf +
                " client=" + (s_window == null ? "0x0" : s_window.ClientWidth + "x" + s_window.ClientHeight) +
                // D9：三分区 —— 检查器右上（高 = 客户区高 − dock 高）、视口左上（同高）、dock 底部通栏。
                " panel=" + (s_window == null ? "-" : s_window.ViewportWidth + ",0," + s_window.PanelWidth +
                                                "," + s_window.ViewportHeight) +
                " viewport=" + (s_window == null ? "-" : "0,0," + s_window.ViewportWidth + "," +
                                                  s_window.ViewportHeight) +
                " dock=" + (s_window == null ? "-" : "0," + (s_window.ClientHeight - s_window.DockHeight) + "," +
                                               s_window.ClientWidth + "," + s_window.DockHeight) +
                " controller=" + (s_window != null && s_window.ControllerReady ? 1 : 0) +
                " nav=" + (s_window != null && s_window.NavigationDone ? 1 : 0) +
                " navHttp=" + (s_window == null ? 0 : s_window.NavigationHttpStatus) +
                " dockController=" + (s_window != null && s_window.DockControllerReady ? 1 : 0) +
                " dockNav=" + (s_window != null && s_window.DockNavigationDone ? 1 : 0) +
                " dockNavHttp=" + (s_window == null ? 0 : s_window.DockNavigationHttpStatus) +
                " exec=" + (s_scene == null ? 0 : s_scene.ExecuteFrames) +
                " capture=" + (s_scene == null ? "0x0" : s_scene.ViewportWidth + "x" + s_scene.ViewportHeight) +
                " graphSelf=" + (s_scene == null ? 0UL : s_scene.GraphSelf) +
                " captureSelf=" + (s_scene == null ? 0UL : s_scene.CaptureSelf) +
                " wtarget=" + (s_scene == null ? "0x0" : s_scene.WindowTargetWidth + "x" + s_scene.WindowTargetHeight) +
                " passes=" + (s_scene == null ? 0u : s_scene.PassCount) +
                " presents=" + (s_scene == null ? 0 : s_scene.PresentCount) +
                " nativeCalls=" + s_nativeCalls +
                " cmdRequest=" + s_lastCommandRequest +
                " cmdCalls=" + s_cmdCalls +
                " lastCmd=" + (s_lastCommandName.Length == 0 ? "-" : s_lastCommandName) +
                " error=" + (s_error.Length == 0 ? "-" : Sanitize(s_error));
            if (SpikeEnabled)
            {
                // 渲染原型读数（editor-web-render-spike 2.2 / 2.3 / 3.1）：控制器/导航/页面就绪/帧计数/
                // 路径（path=cpu-readback 的读数来源）/WebView2 版本/失败码。**只在门控开着时追加**，
                // 既有编辑器用例的读数行逐字不变。
                line += " spikeController=" + (s_spikeWindow != null && s_spikeWindow.ControllerReady ? 1 : 0) +
                        " spikeNav=" + (s_spikeWindow != null && s_spikeWindow.NavigationDone ? 1 : 0) +
                        " spikeNavHttp=" + (s_spikeWindow == null ? 0 : s_spikeWindow.NavigationHttpStatus) +
                        // 运行读数里的 path= 字段（spec 要求 2）：本批固定 cpu-readback，零拷贝是 Non-goal。
                        " path=" + Sanitize(SpikeChannel.Path) +
                        " webview2Version=" + (s_spikeWindow == null || s_spikeWindow.WebView2Version.Length == 0
                            ? "-"
                            : Sanitize(s_spikeWindow.WebView2Version));
                var spikeBuilder = new StringBuilder();
                SpikeChannel.AppendReadings(spikeBuilder);
                line += spikeBuilder.ToString();
            }
            Host.LogFromEditor(line);
            if (s_readingsPath.Length != 0)
            {
                try
                {
                    File.AppendAllText(s_readingsPath, line + "\n");
                }
                catch (Exception)
                {
                    // 读数落盘失败不影响服务本体（宿主日志里仍有同一行）。
                }
            }
        }

        internal static string Sanitize(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                return "-";
            }
            char[] buffer = text.ToCharArray();
            for (int index = 0; index < buffer.Length; ++index)
            {
                if (char.IsWhiteSpace(buffer[index]))
                {
                    buffer[index] = '_';
                }
            }
            return new string(buffer);
        }
    }
}
