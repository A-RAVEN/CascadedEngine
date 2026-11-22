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
        // 窗口初始尺寸：布局默认值（不是配置项、不是上下限；够放面板与视口，测试与人工目检都用它）。
        private const int InitialWindowWidth = 1280;
        private const int InitialWindowHeight = 720;

        // 状态：0 未启动；1 监听已起（下一帧建窗）；2 运行；3 失败禁用；4 已关闭
        private static int s_state;
        private static bool s_started;
        private static int s_port;
        private static int s_panelWidth;
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

        internal static bool Enabled => Environment.GetEnvironmentVariable("CASCADED_EDITOR") == "1";
        internal static bool Running => s_state == 2;

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
            s_dataFolder = Environment.GetEnvironmentVariable("CASCADED_EDITOR_WEBVIEW2_DATA");
            if (string.IsNullOrEmpty(s_dataFolder))
            {
                s_dataFolder = Path.Combine(Path.GetTempPath(), "cascaded-editor-webview2");
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
                               s_panelWidth + "，仅本机；门控 CASCADED_EDITOR=1）");
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
            s_shellClosed = false;
            s_objects.Clear();
            s_invalidated.Clear();
            s_state = 4;
            LogServiceMarker("[编辑器] 已关闭（" + reason + "）：监听已停、客户端已断、WebView2 控制器已关闭");
        }

        private static void Disable(string reason)
        {
            s_error = reason;
            LogServiceMarker("[编辑器] 禁用：" + reason);
            Shutdown("失败禁用");
            s_state = 3;
        }

        /// <summary>
        /// 服务级标记行（入队/禁用/关闭）：除宿主日志外，门控开了读数文件时也追加进去 ——
        /// 这样可见窗口模式（不带 --headless、stdout 不重定向）下 C++ 用例仍能读到这些标记。
        /// </summary>
        private static void LogServiceMarker(string text)
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
                s_port = DefaultPort;
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
            if (!renderBound || backend == null)
            {
                // D11：渲染后端表面未绑定/不可用 ⇒ 明确报错并禁用，不静默降级、不造空壳 scene view。
                Disable("渲染后端表面未绑定（D11：编辑器禁用，不出现空壳 scene view）");
                return;
            }
            string navigationTarget = "http://localhost:" + s_port + "/";
            EditorWindow window = EditorWindow.Create(s_system, InitialWindowWidth, InitialWindowHeight,
                s_panelWidth, navigationTarget, s_dataFolder, out string error);
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
            if (!s_scene.Build(window.ViewportWidth, window.ClientHeight))
            {
                Disable("scene view 建图失败：" + s_scene.LastError);
                return;
            }
            s_lastLayoutWidth = window.ViewportWidth;
            s_lastLayoutHeight = window.ClientHeight;
            Host.LogFromEditor("[编辑器] 窗口已创建（单窗口分栏：视口 " + window.ViewportWidth + "x" +
                               window.ClientHeight + " + 面板 " + window.PanelWidth + "x" + window.ClientHeight +
                               "，模式 main-thread，hwnd=0x" + ((long)window.Hwnd).ToString("X") +
                               "，windowSelf=0x" + s_windowSelf.ToString("X") + "）");
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
            if (s_window == null || s_shellClosed)
            {
                return;
            }
            s_window.Pump();
            if (s_window.Error.Length != 0)
            {
                Disable("WebView2 贴附失败：" + s_window.Error);
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
            if (s_scene != null && !s_shellClosed &&
                !s_scene.Resize(s_window.ViewportWidth, s_window.ClientHeight))
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
            LogServiceMarker("[编辑器] 命令已入队（监听线程）：requestId=" + requestId + " command=" +
                             inbound.Command.Name);
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
                " state=" + (s_state == 2 && s_window != null ? "running" : "starting") +
                " mode=main-thread" +
                " port=" + s_port +
                " clients=" + clientCount +
                " objects=" + s_objects.Count +
                " invalidated=" + s_invalidatedCount +
                " shellClosed=" + (s_shellClosed ? 1 : 0) +
                " windowSelf=" + s_windowSelf +
                " client=" + (s_window == null ? "0x0" : s_window.ClientWidth + "x" + s_window.ClientHeight) +
                " panel=" + (s_window == null ? "-" : s_window.ViewportWidth + ",0," + s_window.PanelWidth +
                                                "," + s_window.ClientHeight) +
                " viewport=" + (s_window == null ? "-" : "0,0," + s_window.ViewportWidth + "," +
                                                  s_window.ClientHeight) +
                " controller=" + (s_window != null && s_window.ControllerReady ? 1 : 0) +
                " nav=" + (s_window != null && s_window.NavigationDone ? 1 : 0) +
                " navHttp=" + (s_window == null ? 0 : s_window.NavigationHttpStatus) +
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

        private static string Sanitize(string text)
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
