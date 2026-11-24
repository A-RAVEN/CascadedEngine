// editor-web-render-spike 2.2：全客户区单 WebView2 控制器（渲染原型窗）。
//
// 与 EditorWindow（三分区壳）的关系：**复用同一套 WebView2 宿主设施**（引擎主线程建环境/控制器、
// 每帧 Tick 轮询异步任务推进、关闭先于 hostfxr_close），但布局是"一个控制器覆盖整个客户区"，
// 加载 spike.html 而不是 index.html + assets.html。独立成类是为了让既有三分区编辑器的行为一行不变
// （design D1：原型门控默认关，现有用例零影响）。
//
// 线程：环境/控制器创建都在引擎主线程，异步任务由 Pump 每帧轮询（不阻塞主线程）。关闭用 Close()
// （CoreWebView2Controller 不实现 IDisposable）。
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.Web.WebView2.Core;
using Scripting.Generated.Bindings.Cawindow;

namespace Scripting.Managed.Editor
{
    internal sealed class SpikeWindow
    {
        // WebView2 每一步的等待上限（测试内常量，不是产品配置）：600 帧 ≈ 10 秒（测试帧循环 16ms/帧）。
        // 与 EditorWindow.MaxWaitTicks 同一帧预算口径（0.2 探针实测环境+控制器+导航共 17～22 帧完成）。
        private const int MaxWaitTicks = 600;

        // 抓窗落盘间隔（自检钩子内部常量）：30 帧 × 16ms ≈ 0.5 秒，与 EditorWindow 的 dock 抓窗同口径。
        private const int CaptureIntervalTicks = 30;

        internal IWindow Window;
        internal IntPtr Hwnd;
        internal int ClientWidth;
        internal int ClientHeight;
        internal bool ControllerReady;
        internal bool NavigationDone;
        internal int NavigationHttpStatus;
        internal string Error = string.Empty;
        // 环境创建后由 WebView2 自己报的浏览器版本（CoreWebView2Environment.BrowserVersionString）。
        // 1.1 研究要求的"版本三段"运行时来源；读数行每次都会带上它。
        internal string WebView2Version = string.Empty;

        private readonly string m_NavigationTarget;
        // 非 readonly：抓窗写盘失败后置空以停止抓窗（与 EditorWindow 的 dock 抓窗同一处置）。
        private string m_CapturePath;
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

        // 抓窗自检钩子（CASCADED_EDITOR_SPIKE_CAPTURE=<png 路径>）：导航完成后按固定间隔用
        // CapturePreviewAsync 把控制器当前渲染内容覆盖落盘。这是 WebView2 自己的渲染输出，不依赖
        // 窗口前台/不被遮挡，也不动用户桌面（禁止置前/最小化用户窗口）。
        // 5.4 加固：**不能只靠 0.5 秒周期** —— 实测 Vulkan 侧周期抓窗只抓到"导航后、第一帧到达前"的空
        // canvas（PNG 两次跑字节数相同、画面是"尚未收到帧"）。所以帧到达/对账推进时由 EditorService 调
        // RequestCapture() 主动排一次抓窗，延迟几帧确保 putImageData 已经被合成上屏。
        private const int RequestedCaptureDelayTicks = 3;
        private int m_CaptureCountdown;
        private int m_RequestedCaptureDelay;
        private Task m_CaptureTask;
        private MemoryStream m_CaptureStream;
        private int m_CaptureWrites;
        private int m_CaptureFailures;

        private SpikeWindow(IWindow window, IntPtr hwnd, string navigationTarget, string capturePath)
        {
            Window = window;
            Hwnd = hwnd;
            m_NavigationTarget = navigationTarget;
            m_CapturePath = capturePath;
        }

        /// <summary>
        /// 建唯一的原生窗口 + 全客户区 WebView2 控制器。失败返回 null 并给 error（调用方据此禁用，
        /// 进程不崩）。**所有引擎调用都在主线程**（本方法由 Tick 调用）。
        /// </summary>
        internal static SpikeWindow Create(IWindowSystem system, int width, int height, string navigationTarget,
            string dataFolder, string capturePath, out string error)
        {
            error = string.Empty;
            IWindow window;
            try
            {
                window = system.NewWindow(width, height, "Cascaded 渲染原型", true, true, true, false);
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
            if (clientWidth <= 1 || clientHeight <= 1)
            {
                error = "窗口客户区过小，无法铺满控制器：" + clientWidth + "x" + clientHeight;
                return null;
            }
            var host = new SpikeWindow(window, hwnd, navigationTarget, capturePath);
            host.ClientWidth = clientWidth;
            host.ClientHeight = clientHeight;
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

        /// <summary>每帧推进控制器创建/导航，并推进抓窗钩子（主线程，不阻塞）。</summary>
        internal void Pump()
        {
            PumpController();
            PumpCapture();
        }

        private void PumpController()
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
                    CoreWebView2Environment environment = m_EnvironmentTask.Result;
                    // 1.1：WebView2 版本由运行时自己报（不读注册表、不读文件版本）。
                    try
                    {
                        WebView2Version = environment.BrowserVersionString;
                    }
                    catch (Exception ex)
                    {
                        WebView2Version = "取版本失败：" + Describe(ex);
                    }
                    m_ControllerTask = environment.CreateCoreWebView2ControllerAsync(Hwnd);
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
                    // 全客户区：一个控制器盖住整个客户区（对比三分区：检查器右上 + dock 底部）。
                    m_Controller.Bounds = new System.Drawing.Rectangle(0, 0, ClientWidth, ClientHeight);
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
                            Fail("原型页面导航失败：" + m_NavigationError);
                            return;
                        }
                        NavigationDone = true;
                        m_Step = 4;
                        Host.LogFromEditor("[渲染原型] 导航完成：IsSuccess=" + (m_NavigationIsSuccess ? 1 : 0) +
                                           " HttpStatus=" + m_NavigationHttpStatus + " webview2Version=" +
                                           WebView2Version + " target=" + m_NavigationTarget);
                        return;
                    }
                    if (m_WaitTicks > MaxWaitTicks)
                    {
                        Fail("原型页面导航超过 " + MaxWaitTicks + " 帧未完成");
                    }
                }
            }
            catch (Exception ex)
            {
                Fail("WebView2 步骤异常：" + Describe(ex));
            }
        }

        /// <summary>
        /// 抓窗自检钩子：导航完成后每 CaptureIntervalTicks 帧用 CapturePreviewAsync 覆盖落盘一次。
        /// 只在 CASCADED_EDITOR_SPIKE_CAPTURE 非空时启用；抓不到内容不算致命（记一行、按间隔重试），
        /// 写盘失败则停止抓窗，都不影响原型本体。
        /// </summary>
        private void PumpCapture()
        {
            if (m_CapturePath.Length == 0 || m_Controller == null || Error.Length != 0 || !NavigationDone)
            {
                return;
            }
            if (m_CaptureTask != null)
            {
                if (!m_CaptureTask.IsCompleted)
                {
                    return;
                }
                bool ok = !m_CaptureTask.IsFaulted && !m_CaptureTask.IsCanceled;
                m_CaptureTask = null;
                MemoryStream stream = m_CaptureStream;
                m_CaptureStream = null;
                if (ok && stream != null && stream.Length != 0)
                {
                    try
                    {
                        File.WriteAllBytes(m_CapturePath, stream.ToArray());
                        ++m_CaptureWrites;
                        if (m_CaptureWrites == 1 || (m_CaptureWrites % 20) == 0)
                        {
                            Host.LogFromEditor("[渲染原型] 抓窗落盘 #" + m_CaptureWrites + "：" + m_CapturePath + "（" +
                                               stream.Length + " 字节）");
                        }
                    }
                    catch (Exception ex)
                    {
                        Host.LogFromEditor("[渲染原型] 抓窗写盘失败：" + Describe(ex) + "（已停止抓窗）");
                        m_CapturePath = string.Empty;
                    }
                }
                else
                {
                    ++m_CaptureFailures;
                    if (m_CaptureFailures == 1 || (m_CaptureFailures % 20) == 0)
                    {
                        Host.LogFromEditor("[渲染原型] 抓窗失败 #" + m_CaptureFailures +
                                           "（CapturePreviewAsync 任务失败或内容为空）");
                    }
                }
                m_CaptureCountdown = CaptureIntervalTicks;
                return;
            }
            if (m_RequestedCaptureDelay > 0)
            {
                // 主动请求的抓窗：先等 RequestedCaptureDelayTicks 帧（给合成器把 putImageData 的结果提上来），
                // 到期后立刻抓一张（覆盖写，最后一张就是含帧画面）。
                --m_RequestedCaptureDelay;
                if (m_RequestedCaptureDelay > 0)
                {
                    return;
                }
            }
            else if (m_CaptureCountdown > 0)
            {
                --m_CaptureCountdown;
                return;
            }
            try
            {
                var stream = new MemoryStream();
                m_CaptureStream = stream;
                m_CaptureTask = m_Controller.CoreWebView2.CapturePreviewAsync(
                    CoreWebView2CapturePreviewImageFormat.Png, stream);
            }
            catch (Exception ex)
            {
                Host.LogFromEditor("[渲染原型] 抓窗调用失败：" + Describe(ex) + "（已停止抓窗）");
                m_CapturePath = string.Empty;
            }
        }

        /// <summary>
        /// 主动请求一次抓窗（5.4 加固）：帧到达 / 同源对账推进时由 EditorService 调用。延迟几帧再抓，
        /// 保证 CapturePreviewAsync 拿到的是已经合成上屏的**含帧**画面，而不是导航后那张空 canvas。
        /// </summary>
        internal void RequestCapture()
        {
            if (m_CapturePath.Length == 0)
            {
                return;
            }
            m_RequestedCaptureDelay = RequestedCaptureDelayTicks;
        }

        /// <summary>按新的客户区尺寸同步控制器 Bounds（全客户区）。</summary>
        internal void ApplyLayout(int clientWidth, int clientHeight)
        {
            if (clientWidth <= 1 || clientHeight <= 1)
            {
                Fail("窗口客户区过小，无法铺满控制器：" + clientWidth + "x" + clientHeight);
                return;
            }
            ClientWidth = clientWidth;
            ClientHeight = clientHeight;
            if (m_Controller != null)
            {
                m_Controller.Bounds = new System.Drawing.Rectangle(0, 0, ClientWidth, ClientHeight);
            }
        }

        /// <summary>关闭控制器（必须在创建它的主线程调用）。</summary>
        internal void Close()
        {
            m_CapturePath = string.Empty;
            m_CaptureStream = null;
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
            NavigationHttpStatus = args.HttpStatusCode;
            // 原型页必须真的存在：4xx/5xx（含"资源未构建"说明错误页）一律当失败，不显示空壳。
            bool httpOk = args.HttpStatusCode >= 100 && args.HttpStatusCode < 400;
            m_NavigationOk = args.IsSuccess || httpOk;
            if (!m_NavigationOk)
            {
                m_NavigationError = args.WebErrorStatus + "（HttpStatus=" + args.HttpStatusCode +
                                    "；原型页面取得 4xx/5xx 即失败）";
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
