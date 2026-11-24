// editor-window-panel 任务 1.6 + editor-asset-browser 任务 3.2：单窗口三分区壳 + 两个 WebView2（D9）。
//
// 一个原生窗口（引擎 IWindowSystem.NewWindow 建）+ 三个分区：
//   ① 场景视口（左上）：宽 = 客户区宽 − 面板宽，高 = 客户区高 − dock 高；原生渲染画在这里。
//   ② 对象检查器（右上）：第一个 WebView2 控制器，加载 index.html。
//   ③ 资产浏览器 dock（底部通栏）：**第二个 WebView2 控制器**，加载 assets.html（独立面板单元）。
// 进程里只有这一个原生窗口（GetWindowCount 增量 1，MUST NOT 第二壳窗口）。
//
// 线程：两个控制器都在引擎主线程创建（0.2 探针两后端实测 mode=main-thread）；环境/控制器创建是异步
// 任务，每帧 Tick 轮询 IsCompleted 推进，**不阻塞主线程**；消息由引擎帧循环的 UpdateSystem
//（glfwPollEvents，线程级消息泵）派发。关闭用 `Close()`（CoreWebView2Controller 不实现 IDisposable，
// 构建期实测 CS1061）。
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.Web.WebView2.Core;
using Scripting.Generated.Bindings.Cawindow;

namespace Scripting.Managed.Editor
{
    internal sealed class EditorWindow
    {
        // WebView2 每一步的等待上限（测试内常量，不是产品配置）：600 帧 ≈ 10 秒（测试帧循环 16ms/帧）。
        // 依据：0.2 探针实测环境+控制器+导航共 17～22 帧完成；上限只把"卡住"变成可读的失败原因。
        private const int MaxWaitTicks = 600;

        // dock 抓窗落盘间隔（自检钩子内部常量，不是产品配置）：30 帧 × 16ms ≈ 0.5 秒，与 MaxWaitTicks
        // 的"600 帧 ≈ 10 秒"同一帧预算口径。抓窗要等 dock 页面把 list-assets 渲染出来，间隔太短没意义。
        private const int DockCaptureIntervalTicks = 30;

        internal IWindow Window;
        internal IntPtr Hwnd;
        internal int ClientWidth;
        internal int ClientHeight;
        internal int PanelWidth;
        internal int DockHeight;
        internal int ViewportWidth;
        internal int ViewportHeight;
        internal bool ControllerReady;
        internal bool NavigationDone;
        internal int NavigationHttpStatus;
        internal string Error = string.Empty;
        // 第二控制器（资产浏览器 dock）的独立就绪/错误/导航读数：任何一项失败都经 EditorService.PumpWindow
        // 触发禁用（不许第一控制器绿就整体当绿）。
        internal bool DockControllerReady;
        internal bool DockNavigationDone;
        internal int DockNavigationHttpStatus;
        internal string DockError = string.Empty;

        private readonly int m_ConfiguredPanelWidth;
        private readonly int m_ConfiguredDockHeight;
        private readonly string m_NavigationTarget;
        private readonly string m_DockNavigationTarget;
        private Task<CoreWebView2Environment> m_EnvironmentTask;
        private Task<CoreWebView2Controller> m_ControllerTask;
        private CoreWebView2Controller m_Controller;
        private int m_Step; // 1 = 等环境，2 = 等控制器，3 = 等导航，4 = 就绪
        private int m_WaitTicks;
        private bool m_NavigationFinished;
        private bool m_NavigationOk;
        private bool m_NavigationIsSuccess;
        private int m_NavigationHttpStatus;
        private string m_NavigationError = string.Empty;

        // 第二控制器（dock）自己的状态机。与第一个控制器共用同一个 WebView2 环境任务。
        private Task<CoreWebView2Controller> m_DockControllerTask;
        private CoreWebView2Controller m_DockController;
        private int m_DockStep; // 1 = 等环境，2 = 等控制器，3 = 等导航，4 = 就绪
        private int m_DockWaitTicks;
        private bool m_DockNavigationFinished;
        private bool m_DockNavigationOk;
        private bool m_DockNavigationIsSuccess;
        // dock 导航的 HTTP 状态码（与面板的 m_NavigationHttpStatus 同形；读数行另经公开字段 DockNavigationHttpStatus）。
        private int m_DockNavigationHttpStatus;
        private string m_DockNavigationError = string.Empty;

        // 抓窗自检钩子（4.2 非前置抓窗）：CASCADED_EDITOR_SELFTEST=capture-dock=<png 路径>。
        // dock 控制器就绪后按固定间隔用 CapturePreviewAsync 把 dock 当前渲染内容覆盖落盘 —— 这是
        // WebView2 自己的渲染输出，不依赖窗口前台/不被遮挡，也不动用户桌面（禁止置前/最小化用户窗口）。
        private string m_DockCapturePath = string.Empty;
        private int m_DockCaptureCountdown;
        private Task m_DockCaptureTask;
        private MemoryStream m_DockCaptureStream;
        private int m_DockCaptureWrites;
        private int m_DockCaptureFailures;

        private EditorWindow(IWindow window, IntPtr hwnd, int panelWidth, int dockHeight, string navigationTarget,
            string dockNavigationTarget, string dockCapturePath)
        {
            Window = window;
            Hwnd = hwnd;
            m_ConfiguredPanelWidth = panelWidth;
            m_ConfiguredDockHeight = dockHeight;
            m_NavigationTarget = navigationTarget;
            m_DockNavigationTarget = dockNavigationTarget;
            m_DockCapturePath = dockCapturePath;
        }

        /// <summary>
        /// 建唯一的原生窗口并启动 WebView2 环境创建。失败返回 null 并给 error（调用方据此禁用编辑器，
        /// 进程不崩）。**所有引擎调用都在主线程**（本方法由 Tick 调用）。
        /// dockNavigationTarget 是底部资产浏览器页面的地址；dockCapturePath 非空时启用抓窗自检钩子。
        /// </summary>
        internal static EditorWindow Create(IWindowSystem system, int width, int height, int panelWidth,
            int dockHeight, string navigationTarget, string dockNavigationTarget, string dataFolder, out string error)
        {
            error = string.Empty;
#if DEBUG
            // M14（工件修订）：Debug 自检开关强制走"WebView2 运行时缺失"路径，供 TestEditorWindowPanel
            // 的 no-runtime 变体断言"明确禁用、进程不崩、错误文本可辨"。
            // 注意：这个开关在 NewWindow 之前返回，只覆盖"运行时缺失"；第二控制器失败由 no-dock-controller
            // 单独构造（见 PumpDock 的 ① 步）。
            if (Environment.GetEnvironmentVariable("CASCADED_EDITOR_SELFTEST") == "no-runtime")
            {
                error = "WebView2 运行时缺失（自检开关 CASCADED_EDITOR_SELFTEST=no-runtime 模拟）";
                return null;
            }
#endif
            IWindow window;
            try
            {
                window = system.NewWindow(width, height, "Cascaded 编辑器", true, true, true, false);
                EditorService.NoteNativeCall();
            }
            catch (Exception ex)
            {
                error = "NewWindow 失败：" + Describe(ex);
                return null;
            }
            if (window == null)
            {
                error = "NewWindow 返回空窗口";
                return null;
            }
            IntPtr handlePointer;
            try
            {
                handlePointer = window.GetNativeWindowHandle();
                EditorService.NoteNativeCall();
            }
            catch (Exception ex)
            {
                error = "GetNativeWindowHandle 失败：" + Describe(ex);
                return null;
            }
            if (handlePointer == IntPtr.Zero)
            {
                error = "GetNativeWindowHandle 返回空指针";
                return null;
            }
            // 原生返回的是 `&WindowImpl::m_Win32Window`（HWND*），必须再解引用一次（0.1① / 0.2 实测）。
            IntPtr hwnd = Marshal.ReadIntPtr(handlePointer);
            if (hwnd == IntPtr.Zero)
            {
                error = "解引用后 HWND 为空";
                return null;
            }
            int clientWidth;
            int clientHeight;
            try
            {
                var size = window.GetWindowSize();
                EditorService.NoteNativeCall();
                clientWidth = size.X;
                clientHeight = size.Y;
            }
            catch (Exception ex)
            {
                error = "GetWindowSize 失败：" + Describe(ex);
                return null;
            }
            var host = new EditorWindow(window, hwnd, panelWidth, dockHeight, navigationTarget, dockNavigationTarget,
                ReadDockCapturePath());
            host.ApplyLayout(clientWidth, clientHeight);
            if (host.Error.Length != 0)
            {
                error = host.Error;
                return null;
            }
            try
            {
                host.m_EnvironmentTask = CoreWebView2Environment.CreateAsync(null, dataFolder);
                host.m_Step = 1;
                host.m_DockStep = 1;
            }
            catch (Exception ex)
            {
                error = "WebView2 环境创建调用失败（运行时可能未安装）：" + Describe(ex);
                return null;
            }
            return host;
        }

        /// <summary>每帧推进两个 WebView2 控制器的创建/导航（主线程，不阻塞），并推进抓窗钩子。</summary>
        internal void Pump()
        {
            PumpPanel();
            PumpDock();
            PumpDockCapture();
        }

        private void PumpPanel()
        {
            if (Error.Length != 0 || m_Step == 0 || m_Step == 4)
            {
                return;
            }
            ++m_WaitTicks;
            try
            {
                if (m_Step == 1)
                {
                    if (m_EnvironmentTask == null)
                    {
                        Fail("WebView2 环境任务为空");
                        return;
                    }
                    if (m_EnvironmentTask.IsFaulted)
                    {
                        Fail("WebView2 环境创建失败（运行时可能未安装）：" + Describe(m_EnvironmentTask.Exception));
                        return;
                    }
                    if (!m_EnvironmentTask.IsCompleted)
                    {
                        if (m_WaitTicks > MaxWaitTicks)
                        {
                            Fail("WebView2 环境创建超过 " + MaxWaitTicks + " 帧未完成");
                        }
                        return;
                    }
                    m_ControllerTask = m_EnvironmentTask.Result.CreateCoreWebView2ControllerAsync(Hwnd);
                    m_Step = 2;
                    m_WaitTicks = 0;
                    return;
                }
                if (m_Step == 2)
                {
                    if (m_ControllerTask == null)
                    {
                        Fail("WebView2 控制器任务为空");
                        return;
                    }
                    if (m_ControllerTask.IsFaulted)
                    {
                        Fail("WebView2 控制器创建失败：" + Describe(m_ControllerTask.Exception));
                        return;
                    }
                    if (!m_ControllerTask.IsCompleted)
                    {
                        if (m_WaitTicks > MaxWaitTicks)
                        {
                            Fail("WebView2 控制器创建超过 " + MaxWaitTicks + " 帧未完成");
                        }
                        return;
                    }
                    m_Controller = m_ControllerTask.Result;
                    // 三分区：检查器在右上，高度 = 客户区高 − dock 高（底部那条留给资产浏览器）。
                    m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ViewportHeight);
                    m_Controller.CoreWebView2.NavigationCompleted += OnNavigationCompleted;
                    m_Controller.CoreWebView2.Navigate(m_NavigationTarget);
                    ControllerReady = true;
                    m_Step = 3;
                    m_WaitTicks = 0;
                    return;
                }
                if (m_Step == 3)
                {
                    if (m_NavigationFinished)
                    {
                        if (!m_NavigationOk)
                        {
                            Fail("面板页面导航失败：" + m_NavigationError);
                            return;
                        }
                        NavigationDone = true;
                        m_Step = 4;
                        // 面板资源的正常/缺失两种状态都记一行：缺失时这里显示的是服务端的说明错误页（HTTP 4xx/5xx），
                        // 不是网络故障 —— 2.x 把 EditorWeb 构建出来后自然变成真面板。
                        Host.LogFromEditor("[编辑器] 面板导航完成：IsSuccess=" + (m_NavigationIsSuccess ? 1 : 0) +
                                           " HttpStatus=" + m_NavigationHttpStatus + "（HTTP 错误页也算导航成功）");
                        return;
                    }
                    if (m_WaitTicks > MaxWaitTicks)
                    {
                        Fail("面板页面导航超过 " + MaxWaitTicks + " 帧未完成");
                    }
                }
            }
            catch (Exception ex)
            {
                Fail("WebView2 步骤异常：" + Describe(ex));
            }
        }

        /// <summary>
        /// 第二控制器（资产浏览器 dock）的状态机：与第一个控制器共用环境，独立推进、独立报错。
        /// 4xx/5xx 导航一律当失败（served 的是"资源未构建"说明错误页时，dock 明确禁用而不是当成功）。
        /// </summary>
        private void PumpDock()
        {
            if (DockError.Length != 0 || m_DockStep == 0 || m_DockStep == 4)
            {
                return;
            }
            ++m_DockWaitTicks;
            try
            {
                if (m_DockStep == 1)
                {
                    if (m_EnvironmentTask == null)
                    {
                        DockFail("WebView2 环境任务为空（资产浏览器控制器无法创建）");
                        return;
                    }
                    if (m_EnvironmentTask.IsFaulted)
                    {
                        DockFail("WebView2 环境创建失败（运行时可能未安装）：" + Describe(m_EnvironmentTask.Exception));
                        return;
                    }
                    if (!m_EnvironmentTask.IsCompleted)
                    {
                        if (m_DockWaitTicks > MaxWaitTicks)
                        {
                            DockFail("WebView2 环境创建超过 " + MaxWaitTicks + " 帧未完成（资产浏览器控制器）");
                        }
                        return;
                    }
                    // 红态开关：运行时正常、第一控制器已建成，只在**第二控制器创建**这一步失败。
                    // （no-runtime 在 Create 里于 NewWindow 之前返回，覆盖不到这条分支，所以另设一个开关。）
                    if (Environment.GetEnvironmentVariable("CASCADED_EDITOR_SELFTEST") == "no-dock-controller")
                    {
                        // 等到第一控制器真建成再注入失败：工件场景是"运行时正常、第一控制器建成、第二控制器
                        // 创建失败"，否则红态读数里第一控制器也是 0，判据分不清是哪一步坏的。
                        if (!ControllerReady)
                        {
                            return;
                        }
                        DockFail("资产浏览器控制器创建失败（自检开关 CASCADED_EDITOR_SELFTEST=no-dock-controller 模拟）");
                        return;
                    }
                    m_DockControllerTask = m_EnvironmentTask.Result.CreateCoreWebView2ControllerAsync(Hwnd);
                    m_DockStep = 2;
                    m_DockWaitTicks = 0;
                    return;
                }
                if (m_DockStep == 2)
                {
                    if (m_DockControllerTask == null)
                    {
                        DockFail("资产浏览器 WebView2 控制器任务为空");
                        return;
                    }
                    if (m_DockControllerTask.IsFaulted)
                    {
                        DockFail("资产浏览器 WebView2 控制器创建失败：" + Describe(m_DockControllerTask.Exception));
                        return;
                    }
                    if (!m_DockControllerTask.IsCompleted)
                    {
                        if (m_DockWaitTicks > MaxWaitTicks)
                        {
                            DockFail("资产浏览器 WebView2 控制器创建超过 " + MaxWaitTicks + " 帧未完成");
                        }
                        return;
                    }
                    m_DockController = m_DockControllerTask.Result;
                    // 三分区：dock 在底部通栏，宽 = 客户区宽、高 = dock 高。
                    m_DockController.Bounds = new System.Drawing.Rectangle(0, ClientHeight - DockHeight, ClientWidth,
                                                                          DockHeight);
                    m_DockController.CoreWebView2.NavigationCompleted += OnDockNavigationCompleted;
                    m_DockController.CoreWebView2.Navigate(m_DockNavigationTarget);
                    DockControllerReady = true;
                    m_DockStep = 3;
                    m_DockWaitTicks = 0;
                    return;
                }
                if (m_DockStep == 3)
                {
                    if (m_DockNavigationFinished)
                    {
                        if (!m_DockNavigationOk)
                        {
                            DockFail("资产浏览器页面导航失败：" + m_DockNavigationError);
                            return;
                        }
                        DockNavigationDone = true;
                        m_DockStep = 4;
                        Host.LogFromEditor("[编辑器] 资产浏览器 dock 导航完成：IsSuccess=" +
                                           (m_DockNavigationIsSuccess ? 1 : 0) + " HttpStatus=" +
                                           m_DockNavigationHttpStatus);
                        return;
                    }
                    if (m_DockWaitTicks > MaxWaitTicks)
                    {
                        DockFail("资产浏览器页面导航超过 " + MaxWaitTicks + " 帧未完成");
                    }
                }
            }
            catch (Exception ex)
            {
                DockFail("资产浏览器 WebView2 步骤异常：" + Describe(ex));
            }
        }

        /// <summary>
        /// 抓窗自检钩子：dock 就绪后每 DockCaptureIntervalTicks 帧用 CapturePreviewAsync 覆盖落盘一次。
        /// 只在 CASCADED_EDITOR_SELFTEST=capture-dock=&lt;png 路径&gt; 时启用；抓不到内容不算致命
        ///（记一行、按间隔重试），写盘失败则停止抓窗，都不影响编辑器本体。
        /// </summary>
        private void PumpDockCapture()
        {
            if (m_DockCapturePath.Length == 0 || m_DockController == null || DockError.Length != 0 ||
                !DockNavigationDone)
            {
                return;
            }
            if (m_DockCaptureTask != null)
            {
                if (!m_DockCaptureTask.IsCompleted)
                {
                    return;
                }
                bool ok = !m_DockCaptureTask.IsFaulted && !m_DockCaptureTask.IsCanceled;
                m_DockCaptureTask = null;
                MemoryStream stream = m_DockCaptureStream;
                m_DockCaptureStream = null;
                if (ok && stream != null && stream.Length != 0)
                {
                    try
                    {
                        File.WriteAllBytes(m_DockCapturePath, stream.ToArray());
                        ++m_DockCaptureWrites;
                        if (m_DockCaptureWrites == 1 || (m_DockCaptureWrites % 20) == 0)
                        {
                            Host.LogFromEditor("[编辑器] dock 抓窗落盘 #" + m_DockCaptureWrites + "：" +
                                               m_DockCapturePath + "（" + stream.Length + " 字节）");
                        }
                    }
                    catch (Exception ex)
                    {
                        Host.LogFromEditor("[编辑器] dock 抓窗写盘失败：" + Describe(ex) + "（已停止抓窗）");
                        m_DockCapturePath = string.Empty;
                    }
                }
                else
                {
                    // 抓窗失败不静默（4.2 要靠这个钩子拿画面）：记一行、继续按间隔重试，不无限刷日志。
                    ++m_DockCaptureFailures;
                    if (m_DockCaptureFailures == 1 || (m_DockCaptureFailures % 20) == 0)
                    {
                        Host.LogFromEditor("[编辑器] dock 抓窗失败 #" + m_DockCaptureFailures +
                                           "（CapturePreviewAsync 任务失败或内容为空）");
                    }
                }
                m_DockCaptureCountdown = DockCaptureIntervalTicks;
                return;
            }
            if (m_DockCaptureCountdown > 0)
            {
                --m_DockCaptureCountdown;
                return;
            }
            try
            {
                var stream = new MemoryStream();
                m_DockCaptureStream = stream;
                m_DockCaptureTask = m_DockController.CoreWebView2.CapturePreviewAsync(
                    CoreWebView2CapturePreviewImageFormat.Png, stream);
            }
            catch (Exception ex)
            {
                Host.LogFromEditor("[编辑器] dock 抓窗调用失败：" + Describe(ex) + "（已停止抓窗）");
                m_DockCapturePath = string.Empty;
            }
        }

        /// <summary>按新的客户区尺寸重算三分区并同步两个控制器 Bounds（检查器右上、dock 底部通栏）。</summary>
        internal void ApplyLayout(int clientWidth, int clientHeight)
        {
            if (clientWidth <= 1 || clientHeight <= 1)
            {
                Fail("窗口客户区过小，无法分栏：" + clientWidth + "x" + clientHeight);
                return;
            }
            // B2（工件修订）：面板宽范围 1..客户区宽-1；越界是配置错误，明确禁用（不静默 clamp 成怪布局）。
            if (m_ConfiguredPanelWidth < 1 || m_ConfiguredPanelWidth >= clientWidth)
            {
                Fail("CASCADED_EDITOR_PANEL_WIDTH=" + m_ConfiguredPanelWidth + " 超出范围（1..客户区宽-1=" +
                     (clientWidth - 1) + "）");
                return;
            }
            // dock 高范围 1..客户区高-1：越界同样明确禁用（不 clamp），照面板宽先例的"上界在 ApplyLayout 校"。
            if (m_ConfiguredDockHeight < 1 || m_ConfiguredDockHeight >= clientHeight)
            {
                Fail("CASCADED_EDITOR_DOCK_HEIGHT=" + m_ConfiguredDockHeight + " 超出范围（1..客户区高-1=" +
                     (clientHeight - 1) + "）");
                return;
            }
            ClientWidth = clientWidth;
            ClientHeight = clientHeight;
            PanelWidth = m_ConfiguredPanelWidth;
            DockHeight = m_ConfiguredDockHeight;
            ViewportWidth = clientWidth - PanelWidth;
            ViewportHeight = clientHeight - DockHeight;
            if (m_Controller != null)
            {
                m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ViewportHeight);
            }
            if (m_DockController != null)
            {
                m_DockController.Bounds = new System.Drawing.Rectangle(0, ClientHeight - DockHeight, ClientWidth,
                                                                      DockHeight);
            }
        }

        /// <summary>关闭两个控制器（必须在创建它们的主线程调用）。</summary>
        internal void Close()
        {
            m_DockCapturePath = string.Empty;
            m_DockCaptureStream = null;
            try
            {
                if (m_DockController != null)
                {
                    m_DockController.Close();
                    m_DockController = null;
                }
            }
            catch (Exception)
            {
                // 关闭路径的异常不覆盖已经记录的失败原因；进程退出前 Edge 子进程由系统回收。
            }
            try
            {
                if (m_Controller != null)
                {
                    m_Controller.Close();
                    m_Controller = null;
                }
            }
            catch (Exception)
            {
                // 同上。
            }
        }

        private void OnNavigationCompleted(object sender, CoreWebView2NavigationCompletedEventArgs args)
        {
            m_NavigationFinished = true;
            m_NavigationIsSuccess = args.IsSuccess;
            m_NavigationHttpStatus = args.HttpStatusCode;
            // 读数行用的公开字段（navHttp）：与私有字段同步，避免"日志 503、读数 0"的自相矛盾。
            NavigationHttpStatus = args.HttpStatusCode;
            // 判据：**收到 HTTP 响应**就说明导航到达了本机服务（哪怕 404/503 的说明错误页）——WebView2 对
            // HTTP 错误页会给出 IsSuccess=false + WebErrorStatus=Unknown，但 HttpStatusCode 是真实状态码；
            // 只有 HttpStatusCode==0（无响应/连接失败/被拦截）才是真网络故障，按贴附失败禁用编辑器。
            m_NavigationOk = args.IsSuccess || args.HttpStatusCode >= 100;
            if (!m_NavigationOk)
            {
                m_NavigationError = args.WebErrorStatus + "（HttpStatus=" + args.HttpStatusCode + "）";
            }
        }

        /// <summary>
        /// dock 页面的导航判据比面板严格：**4xx/5xx 一律当失败**。原因：assets.html 单独缺失时服务端返回
        /// 的是"资源未构建"说明错误页（503），面板那边允许把它当"导航到了"（错误页可见），但 dock 是资产
        /// 浏览器的唯一入口 —— 拿到错误页说明资产浏览器根本不可用，必须明确禁用而不是显示一个空壳。
        /// </summary>
        private void OnDockNavigationCompleted(object sender, CoreWebView2NavigationCompletedEventArgs args)
        {
            m_DockNavigationFinished = true;
            m_DockNavigationIsSuccess = args.IsSuccess;
            m_DockNavigationHttpStatus = args.HttpStatusCode;
            DockNavigationHttpStatus = args.HttpStatusCode;
            bool httpOk = args.HttpStatusCode >= 100 && args.HttpStatusCode < 400;
            m_DockNavigationOk = args.IsSuccess || httpOk;
            if (!m_DockNavigationOk)
            {
                m_DockNavigationError = args.WebErrorStatus + "（HttpStatus=" + args.HttpStatusCode +
                                       "；资产浏览器页面取得 4xx/5xx 即失败）";
            }
        }

        private void Fail(string reason)
        {
            if (Error.Length == 0)
            {
                Error = reason;
            }
        }

        private void DockFail(string reason)
        {
            if (DockError.Length == 0)
            {
                DockError = reason;
            }
        }

        /// <summary>
        /// 解析抓窗钩子：`CASCADED_EDITOR_SELFTEST=capture-dock=&lt;png 路径&gt;` → 路径；其它值 → 空串（不抓窗）。
        /// 值必须逐字以 `capture-dock=` 开头，路径部分不许为空（否则等于开了个什么都不写的钩子）。
        /// </summary>
        private static string ReadDockCapturePath()
        {
            string value = Environment.GetEnvironmentVariable("CASCADED_EDITOR_SELFTEST") ?? string.Empty;
            const string prefix = "capture-dock=";
            if (!value.StartsWith(prefix, StringComparison.Ordinal))
            {
                return string.Empty;
            }
            string path = value.Substring(prefix.Length);
            return path.Length == 0 ? string.Empty : path;
        }

        private static string Describe(Exception exception)
        {
            if (exception == null)
            {
                return "未知异常";
            }
            Exception baseError = exception is AggregateException aggregate
                ? aggregate.GetBaseException()
                : exception;
            return baseError.GetType().Name + ":" + baseError.Message;
        }
    }
}
