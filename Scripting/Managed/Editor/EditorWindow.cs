// editor-window-panel 任务 1.6：单窗口分栏壳 + WebView2（D2，UI 线程模式按 0.2 实测 = 主线程）。
//
// 一个原生窗口（引擎 IWindowSystem.NewWindow 建）+ 右侧面板条里的 WebView2 子 HWND；左侧是原生
// 渲染视口区域（1.7 的 scene view 画在这里）。进程里只有这一个原生窗口（GetWindowCount 增量 1）。
//
// 线程：控制器在引擎主线程创建（0.2 探针两后端实测 mode=main-thread）；环境/控制器创建是异步任务，
// 每帧 Tick 轮询 IsCompleted 推进，**不阻塞主线程**；消息由引擎帧循环的 UpdateSystem
//（glfwPollEvents，线程级消息泵）派发。关闭用 `Close()`（CoreWebView2Controller 不实现 IDisposable，
// 构建期实测 CS1061）。
using System;
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

        internal IWindow Window;
        internal IntPtr Hwnd;
        internal int ClientWidth;
        internal int ClientHeight;
        internal int PanelWidth;
        internal int ViewportWidth;
        internal bool ControllerReady;
        internal bool NavigationDone;
        internal int NavigationHttpStatus;
        internal string Error = string.Empty;

        private readonly int m_ConfiguredPanelWidth;
        private readonly string m_NavigationTarget;
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

        private EditorWindow(IWindow window, IntPtr hwnd, int panelWidth, string navigationTarget)
        {
            Window = window;
            Hwnd = hwnd;
            m_ConfiguredPanelWidth = panelWidth;
            m_NavigationTarget = navigationTarget;
        }

        /// <summary>
        /// 建唯一的原生窗口并启动 WebView2 环境创建。失败返回 null 并给 error（调用方据此禁用编辑器，
        /// 进程不崩）。**所有引擎调用都在主线程**（本方法由 Tick 调用）。
        /// </summary>
        internal static EditorWindow Create(IWindowSystem system, int width, int height, int panelWidth,
            string navigationTarget, string dataFolder, out string error)
        {
            error = string.Empty;
#if DEBUG
            // M14（工件修订）：Debug 自检开关强制走"WebView2 运行时缺失"路径，供 TestEditorWindowPanel
            // 的 no-runtime 变体断言"明确禁用、进程不崩、错误文本可辨"。
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
            var host = new EditorWindow(window, hwnd, panelWidth, navigationTarget);
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
            }
            catch (Exception ex)
            {
                error = "WebView2 环境创建调用失败（运行时可能未安装）：" + Describe(ex);
                return null;
            }
            return host;
        }

        /// <summary>每帧推进 WebView2 创建/导航（主线程，不阻塞）。</summary>
        internal void Pump()
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
                    m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ClientHeight);
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

        /// <summary>按新的客户区尺寸重算分栏并同步控制器 Bounds（面板在右，视口在左）。</summary>
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
            int panel = m_ConfiguredPanelWidth;
            ClientWidth = clientWidth;
            ClientHeight = clientHeight;
            PanelWidth = panel;
            ViewportWidth = clientWidth - panel;
            if (m_Controller != null)
            {
                m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ClientHeight);
            }
        }

        /// <summary>关闭控制器（必须在创建它的主线程调用）。</summary>
        internal void Close()
        {
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
                // 关闭路径的异常不覆盖已经记录的失败原因；进程退出前 Edge 子进程由系统回收。
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

        private void Fail(string reason)
        {
            if (Error.Length == 0)
            {
                Error = reason;
            }
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
