// 场景运行时（csharp-scene-foundation 任务 2.2 / 2.3 / 3.1，design D5 / D6）。
//
// 职责只有一个：持一个**可选**的活动场景，并在宿主每帧调 `Tick(deltaSeconds)` 时
//   ① 有活动场景：先调 `UserScript.OnUpdate(scene, dt)`，再把场景翻译成图并提交（SceneRenderer）；
//   ② 没有活动场景：立即返回 —— 不调钩子、不建图、不提交、不打日志（默认关零行为）。
//
// 激活方式只有一条：脚本 / 测试显式调 `Register(scene)`（design D5）。注册时调一次
// `UserScript.OnStart(scene)`；重复注册**同一个**场景对象不再调（spec 的 OnStart 场景）。
// 想停掉场景渲染就调 `Unregister()`；此后 Tick 回到零行为。
//
// 后端与窗口系统由宿主（Host.Tick）经 `Attach` 交过来。场景渲染到**自己建的窗口**
// （`IWindowSystem.NewWindow`，尺寸取宿主绑定窗口的尺寸）并另建一张同尺寸离屏捕获目标（像素证据走这张）。
// 为什么建自己的窗口而不是直接画宿主那个：宿主手上的 `IWindow` 表面是「实例指针 + 句柄 0」形态
// （`Host.EnsureBound` 只拿到实例指针），拿它去 `CRenderBackend.GetWindowHandle` 会被判句柄失效；
// 自己 `NewWindow` 出来的表面带真实句柄（与四个渲染探针同一做法）。
// 窗口只建一次、**不关**（与脚本建的窗口同一弱持有约定：不 CloseWindow 就活到进程退出），
// 宿主换窗口 / 重绑后端时 `Attach` 会发现引用变了并释放旧表面与旧窗口。
//
// `Tick` 是 internal：只有 Host 该调它。用户脚本能用的入口是 Register / Unregister。
using System;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Scene
{
    /// <summary>场景运行时：可选活动场景 + 每帧 OnUpdate 与场景→图翻译。默认无活动场景（零行为）。</summary>
    public static class SceneRuntime
    {
        private static Scene s_Active;
        private static SceneRenderer s_Renderer;
        private static ScriptRenderBackend s_Backend;
        private static IWindowSystem s_Windows;
        private static IWindow s_HostWindow;
        private static IWindow s_SceneWindow;
        private static int s_RenderFrameCount;

        /// <summary>当前活动场景（没有注册时是 null）。</summary>
        public static Scene Active
        {
            get { return s_Active; }
        }

        /// <summary>已经提交并等待完成的场景帧数（读数口：无活动场景时恒不涨）。</summary>
        internal static int RenderFrameCount
        {
            get { return s_RenderFrameCount; }
        }

        /// <summary>最近一次渲染失败的原因（成功时为空串）。</summary>
        internal static string LastRenderError = string.Empty;

        // ---- Debug 自检开关（只在 Debug 生效；生产脚本不依赖）----
        // `TestCSharpScene` 的红态靠它构造，不必临时改源码：
        //   · `skip-onupdate` —— Tick 故意不调 `OnUpdate`：读数行 onUpdate/lastDtMicros 不涨，
        //                       用例必须以 `E_SCENE_DT` 判红（证明「每帧调钩子 + dt 真传下来」不是恒真）；
        //   · `bad-color`     —— 建顶点时把颜色换成蓝色：中心像素不再是纯红，
        //                       用例必须以 `E_SCENE_PIXEL` 判红（证明像素判据真读的是场景数据）。
        private static readonly string s_SelfTestMode = ReadSelfTestMode();

        internal static string SelfTestMode
        {
            get { return s_SelfTestMode; }
        }

        private static string ReadSelfTestMode()
        {
#if DEBUG
            return Environment.GetEnvironmentVariable("CASCADED_CSHARP_SCENE_SELFTEST") ?? string.Empty;
#else
            return string.Empty;
#endif
        }

        // ---- 读数口（测试用例按这些值核对；无活动场景时全为 0）----

        internal static ulong GraphSelf
        {
            get { return s_Renderer == null ? 0UL : s_Renderer.GraphSelf; }
        }

        internal static ulong GraphToken
        {
            get { return s_Renderer == null ? 0UL : s_Renderer.GraphToken; }
        }

        internal static ulong CaptureSelf
        {
            get { return s_Renderer == null ? 0UL : s_Renderer.CaptureSelf; }
        }

        internal static ulong CaptureToken
        {
            get { return s_Renderer == null ? 0UL : s_Renderer.CaptureToken; }
        }

        internal static uint CaptureWidth
        {
            get { return s_Renderer == null ? 0u : s_Renderer.CaptureWidth; }
        }

        internal static uint CaptureHeight
        {
            get { return s_Renderer == null ? 0u : s_Renderer.CaptureHeight; }
        }

        internal static uint PassCount
        {
            get { return s_Renderer == null ? 0u : s_Renderer.PassCount; }
        }

        internal static int PresentCount
        {
            get { return s_Renderer == null ? 0 : s_Renderer.PresentCount; }
        }

        internal static int RenderedEntities
        {
            get { return s_Renderer == null ? 0 : s_Renderer.RenderedEntities; }
        }

        /// <summary>最近一次翻译进图里的顶点总数（读数用）。</summary>
        internal static int RenderedVertices
        {
            get { return s_Renderer == null ? 0 : s_Renderer.RenderedVertices; }
        }

        /// <summary>最近一次翻译出来的着色器分组数（读数用；= 图里 pass 数的一半）。</summary>
        internal static int ShaderGroups
        {
            get { return s_Renderer == null ? 0 : s_Renderer.ShaderGroups; }
        }

        /// <summary>第一个被翻译顶点的位置分量（读数用；无活动场景 / 未建图时为 0）。</summary>
        internal static float FirstVertexX
        {
            get { return s_Renderer == null ? 0.0f : s_Renderer.FirstVertexX; }
        }

        internal static float FirstVertexY
        {
            get { return s_Renderer == null ? 0.0f : s_Renderer.FirstVertexY; }
        }

        internal static float FirstVertexZ
        {
            get { return s_Renderer == null ? 0.0f : s_Renderer.FirstVertexZ; }
        }

        // ---- 激活 / 注销 ----

        /// <summary>
        /// 注册活动场景：调一次 `UserScript.OnStart(scene)`。重复注册同一个场景对象是空操作
        /// （不重复调 OnStart）。注册另一个场景会替换活动场景并重新计一次 OnStart。
        /// </summary>
        public static void Register(Scene scene)
        {
            if (scene == null)
            {
                throw new ArgumentNullException(nameof(scene),
                    "注册场景：场景对象不能为空（要停掉场景渲染请调 SceneRuntime.Unregister）");
            }
            if (ReferenceEquals(scene, s_Active))
            {
                return;
            }
            s_Active = scene;
            // 场景换了：上一份图与目标是按旧场景的内容建的，不能继续用（下一帧按新内容重建）。
            ReleaseRenderer();
            UserScript.OnStart(scene);
        }

        /// <summary>注销活动场景：此后 Tick 立即返回（零行为），不再调 OnStart / OnUpdate。</summary>
        public static void Unregister()
        {
            if (s_Active == null)
            {
                return;
            }
            s_Active = null;
            ReleaseRenderer();
        }

        // ---- 宿主挂点 ----

        /// <summary>
        /// 宿主把当前的后端、窗口系统与它绑定的那个窗口（只用来读尺寸）交过来。引用一变就释放旧表面与旧窗口
        /// —— 旧后端或旧窗口上的表面在重绑之后已经失效（沿用 `Host.EnsureBound` 重绑时作废旧表面的既有纪律）。
        /// </summary>
        internal static void Attach(ScriptRenderBackend backend, IWindowSystem windows, IWindow hostWindow)
        {
            if (ReferenceEquals(backend, s_Backend) && ReferenceEquals(windows, s_Windows) &&
                ReferenceEquals(hostWindow, s_HostWindow))
            {
                return;
            }
            ReleaseRenderer();
            ReleaseSceneWindow();
            s_Backend = backend;
            s_Windows = windows;
            s_HostWindow = hostWindow;
        }

        /// <summary>
        /// 每帧入口（只有 Host 调）。有活动场景才做事；没有活动场景时零行为。
        /// 渲染失败抛 `SceneRenderException`，由宿主转成非零返回码 + 明确错误文本。
        /// </summary>
        internal static void Tick(double deltaSeconds)
        {
            if (s_Active == null)
            {
                return;
            }
            if (s_SelfTestMode.Length != 0 && s_SelfTestMode != "skip-onupdate" && s_SelfTestMode != "bad-color")
            {
                // 自检模式写错不许静默空转：宁可让 Tick 报错，也不要让红态构造悄悄失效。
                throw new SceneRenderException($"场景运行时：未知的自检模式 {s_SelfTestMode}");
            }
            // design D5：先调钩子（dt 就是引擎这一帧给的值），再翻译成图。
            if (s_SelfTestMode != "skip-onupdate")
            {
                UserScript.OnUpdate(s_Active, deltaSeconds);
            }
            if (s_Backend == null || s_Windows == null)
            {
                throw new SceneRenderException(
                    "场景渲染：没有可用的渲染后端或窗口系统（宿主没有把后端 / 窗口交过来，或渲染表没绑上）");
            }
            if (s_Renderer == null)
            {
                s_Renderer = new SceneRenderer(s_Backend, EnsureSceneWindow());
            }
            bool submitted;
            string error;
            if (!s_Renderer.Render(s_Active, out error, out submitted))
            {
                LastRenderError = error;
                throw new SceneRenderException(error);
            }
            LastRenderError = string.Empty;
            if (submitted)
            {
                ++s_RenderFrameCount;
            }
        }

        /// <summary>
        /// 场景窗口（只建一次）：尺寸取宿主绑定窗口的尺寸，可见、可聚焦、带边框、非浮动。
        /// 宿主没给窗口时明确报错（不拿魔数尺寸顶着）。
        /// </summary>
        private static IWindow EnsureSceneWindow()
        {
            if (s_SceneWindow != null)
            {
                return s_SceneWindow;
            }
            if (s_HostWindow == null)
            {
                throw new SceneRenderException("场景渲染：宿主没有绑定窗口，取不到场景窗口该用的尺寸");
            }
            var (width, height) = s_HostWindow.GetWindowSize();
            if (width <= 0 || height <= 0)
            {
                throw new SceneRenderException($"场景渲染：宿主窗口尺寸非法（{width}x{height}），无法建场景窗口");
            }
            IWindow created = s_Windows.NewWindow(width, height, "C# 场景窗口", true, true, true, false);
            if (created == null)
            {
                throw new SceneRenderException($"场景渲染：IWindowSystem.NewWindow 返回空（{width}x{height}）");
            }
            s_SceneWindow = created;
            return s_SceneWindow;
        }

        /// <summary>
        /// 释放场景运行时的全部原生表面（图 → 离屏目标 → 窗口目标）并关掉场景窗口。
        /// 宿主关闭 / 重绑时调：之后 Attach 会重新交后端与窗口系统，下一次 Tick 按需重建。
        /// </summary>
        internal static void ReleaseSurfaces()
        {
            ReleaseRenderer();
            ReleaseSceneWindow();
            s_Backend = null;
            s_Windows = null;
            s_HostWindow = null;
        }

        private static void ReleaseRenderer()
        {
            if (s_Renderer != null)
            {
                s_Renderer.Dispose();
                s_Renderer = null;
            }
        }

        private static void ReleaseSceneWindow()
        {
            if (s_SceneWindow == null)
            {
                return;
            }
            // 与既有探针的收尾同形：先请求关闭，再释放表面句柄；窗口本身由引擎在关闭且引用计数归 1 时回收。
            s_SceneWindow.CloseWindow();
            s_SceneWindow.Dispose();
            s_SceneWindow = null;
        }
    }
}
