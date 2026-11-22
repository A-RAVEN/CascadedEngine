using System;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;
using Scripting.Generated.Bindings.CaBindFixture;
using Scripting.Generated.Bindings.Shadercompilerslang;
// csharp-scene-foundation 2.1：场景类型在子命名空间 Scripting.Managed.Scene 里，
// 而本文件的命名空间是 Scripting.Managed —— 裸写 `Scene` 会解析成那个**子命名空间**（不是类型），
// 所以按别名引类型（签名里的类型仍是 Scripting.Managed.Scene.Scene）。
using SceneModel = Scripting.Managed.Scene.Scene;

namespace Scripting.Managed
{
    /// <summary>
    /// 用户脚本入口。
    ///
    /// 宿主（`Host.cs`）在**编号 1、2 两张函数表都绑定成功之后**调用 `OnEngineReady` 一次，
    /// 整个进程生命周期内只调一次。要写自己的 C# 逻辑就改这个文件 —— 重新构建即生效，
    /// 不需要碰 `Host.cs`（那一份是引擎胶水，改坏了连 ABI 自检都会一起坏）。
    ///
    /// 注意两条边界：
    /// ① 传进来的 `windows` 只在"引擎换默认实例"之前一直有效；重绑之后它指向被替换掉的表面对象，
    ///    所以不要把它存进静态字段跨重绑使用。
    /// ② 建出来的窗口是**弱持有**：脚本不 `CloseWindow()`，它就一直活到进程退出（引擎的回收条件是
    ///    "关闭且引用计数归 1"），Dispose 只释放句柄、不销毁窗口。
    ///
    /// 渲染探针（csharp-render-backend-triangle 任务 3.2）走独立入口 `OnRenderProbe`：它只在
    /// `CASCADED_CSHARP_RENDER_PROBE=1` 且三个渲染函数表都绑好时被调用一次，与上面那个
    /// "进程只调一次"的 `OnEngineReady` 互不消耗。
    /// </summary>
    public static class UserScript
    {
        /// <summary>
        /// 被调用了几次。`Host.cs` 与测试用它验"只调一次"，在 `Tick` 之后读它即可。
        /// </summary>
        internal static int CalledCount;

        /// <summary>
        /// 建窗时读到的尺寸摘要（`宽 x 高`）。宿主拿它打 `[脚本] 建窗：` 那行 ——
        /// 托管侧自己的 `Console.Out` 在测试进程里会被改道到不同落点（实测同一条命令两次落点不同），
        /// 只有走宿主那份日志才稳定可见。
        /// </summary>
        internal static string LastCreatedSummary = string.Empty;

        /// <summary>
        /// 脚本建出来的那个窗口的表面对象（csharp-render-binding-foundations 任务 1.3 的目标释放探针
        /// 用它）：`Dispose` 只释放目标句柄，不关窗、不改变引擎的窗口计数。
        /// </summary>
        internal static IWindow LastCreated;

        // ---- 渲染探针（csharp-native-direct-binding 4.1：原生直绑表面 + C# 糖层）----
        /// <summary>`OnRenderProbe` 被调用了几次；引擎侧按"独立调用标记"判它没被别的测试消耗。</summary>
        internal static int RenderProbeCount;
        /// <summary>最近一次渲染探针的摘要，由宿主打进日志（包含模式、图 pass 数、等待调用）。</summary>
        internal static string LastRenderSummary = string.Empty;
        /// <summary>机器可读读数行（C++ 侧用例按它核对托管计数与对象句柄）。</summary>
        internal static string LastRenderReadings = string.Empty;
        // 保留到托管 Shutdown 再释放：原生测试要用这些表面对象对应的原生资源做像素与结构核对。
        internal static IWindow RenderWindow;
        internal static ScriptRenderTarget RenderWindowTarget;
        internal static ScriptRenderTarget RenderCaptureTarget;
        internal static ScriptRenderGraph RenderGraph;

        /// <summary>
        /// 脚本入口：建一个保持可见的窗口（任务 2.3）。
        /// **不关窗、不 Dispose** —— 引擎的回收条件是"关闭且引用计数归 1"，所以它一直活到进程退出。
        /// </summary>
        public static void OnEngineReady(IWindowSystem windows)
        {
            CalledCount++;
            IWindow created = windows.NewWindow(320, 240, "脚本建的窗口", true, true, true, false);
            LastCreated = created;
            var (width, height) = created.GetWindowSize();
            LastCreatedSummary = $"{width} x {height}";
        }

        // ---- 场景生命周期钩子（csharp-scene-foundation 2.1，design D5，additive）----
        /// <summary>`OnStart` 被调用了几次（场景运行时注册活动场景时一次）。</summary>
        internal static int StartCount;
        /// <summary>`OnUpdate` 被调用了几次（有活动场景时每帧一次）。</summary>
        internal static int UpdateCount;
        /// <summary>最近一次 `OnStart` 收到的场景名（读数用）。</summary>
        internal static string LastStartSceneName = string.Empty;
        /// <summary>最近一次 `OnUpdate` 收到的场景名（读数用）。</summary>
        internal static string LastUpdateSceneName = string.Empty;
        /// <summary>最近一次 `OnUpdate` 收到的 dt（秒）—— 引擎这一帧传下来的值。</summary>
        internal static double LastUpdateDeltaSeconds;

        /// <summary>
        /// 场景注册钩子（design D5）：`SceneRuntime.Register(scene)` 时调一次。
        /// **默认实现不做事**，只累加计数并记下场景名（与 `OnEngineReady` 里的 `CalledCount` 同形态）——
        /// 既有的只实现 OnEngineReady 的脚本不受影响。
        /// </summary>
        public static void OnStart(SceneModel scene)
        {
            StartCount++;
            LastStartSceneName = scene == null ? string.Empty : (scene.Name ?? string.Empty);
        }

        /// <summary>
        /// 每帧钩子（design D5）：有活动场景时，`SceneRuntime.Tick` 每帧调一次。
        /// `deltaSeconds` 就是引擎这一帧传给宿主的值（此前只打日志、被丢弃）。
        /// **默认实现不做事**，只累加计数并记下收到的 dt 与场景名。
        /// </summary>
        public static void OnUpdate(SceneModel scene, double deltaSeconds)
        {
            UpdateCount++;
            LastUpdateSceneName = scene == null ? string.Empty : (scene.Name ?? string.Empty);
            LastUpdateDeltaSeconds = deltaSeconds;
        }

        /// <summary>
        /// 渲染探针（csharp-native-direct-binding 4.1）：用现有 `IWindowSystem.NewWindow` 建窗，取窗口目标
        /// 与同尺寸离屏目标，用自己的顶点字节、着色器路径、清除色建图、提交并等待。
        ///
        /// 正常模式：三个顶点都是红色、位置沿用现有简单三角形；窗口与离屏目标各一份**同逻辑**绘制
        /// （像素验证镜像，不是通用多 pass）。
        ///
        /// Debug 自检模式（`CASCADED_CSHARP_RENDER_SELFTEST`，由 `TestCSharpTriangle` 从环境变量读同一份值）：
        ///   · `skip-draw`   —— 在调用层省略窗口与离屏目标的两次 `AddTrianglePass`：图 pass 数为 0，
        ///                      托管前置校验拒绝空图提交；原生测试与正常模式的 2 个 pass 对比后判红；
        ///   · `bad-vertex`  —— 把一个顶点改成蓝色：中心不再是纯红，证明插值来自脚本数据；
        ///   · `skip-present`—— 省略窗口呈现；
        ///   · `skip-wait`   —— 省略 `WaitIdle`。
        /// 生产脚本不依赖这些开关。
        /// </summary>
        internal static void OnRenderProbe(IWindowSystem windows, ScriptRenderBackend backend)
        {
            RenderProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("渲染探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("渲染探针：原生渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_RENDER_SELFTEST") ?? string.Empty;
            bool skipDraw = mode == "skip-draw";
            bool badVertex = mode == "bad-vertex";
            bool skipPresent = mode == "skip-present";
            bool skipWait = mode == "skip-wait";
            if (mode.Length != 0 && !(skipDraw || badVertex || skipPresent || skipWait))
            {
                throw new InvalidOperationException($"渲染探针：未知的自检模式 {mode}");
            }
            int preExec = backend.ExecuteGraphCount;
            int preCapture = backend.CreateCaptureTargetCount;
            int preStruct = backend.CreateShaderStructCount;
            int preAddBuffer = backend.AddBufferCount;
            int preCreateTexture = backend.CreateTextureCount;
            int preUpload = backend.TextureUploadExecuteCount;
            int preImage = backend.SetImageCount;
            int preSampler = backend.SetSamplerCount;
            int preWait = backend.WaitIdleCount;
            int preSetFloat3 = backend.SetFloat3Count;
            int preSetStruct = backend.SetStructCount;
            int preSetPassParam = backend.SetPassParamCount;
            int preSetBuffer = backend.SetBufferCount;

            RenderWindow = windows.NewWindow(CSharpTriangleProbe.WindowWidth, CSharpTriangleProbe.WindowHeight,
                "C# 三角形", true, true, true, false);
            RenderWindowTarget = backend.GetWindowTarget(RenderWindow);
            if (RenderWindowTarget == null)
            {
                throw new InvalidOperationException($"渲染探针：GetWindowTarget 返回空：{backend.GetLastError()}");
            }
            RenderCaptureTarget = backend.CreateCaptureTarget(CSharpTriangleProbe.WindowWidth,
                CSharpTriangleProbe.WindowHeight);
            if (RenderCaptureTarget == null)
            {
                throw new InvalidOperationException($"渲染探针：CreateCaptureTarget 返回空：{backend.GetLastError()}");
            }
            RenderGraph = backend.CreateGraph();
            if (RenderGraph == null)
            {
                throw new InvalidOperationException($"渲染探针：CreateGraph 返回空：{backend.GetLastError()}");
            }

            // 顶点字节由脚本自己写：三个顶点，每个 24 字节（位置 float3 + 颜色 float3）。
            float[] values = CSharpTriangleProbe.BuildVertices(badVertex);
            byte[] vertexBytes = new byte[values.Length * sizeof(float)];
            Buffer.BlockCopy(values, 0, vertexBytes, 0, vertexBytes.Length);
            var descriptor = new GPUBufferDescriptor
            {
                count = CSharpTriangleProbe.VertexCount,
                stride = CSharpTriangleProbe.VertexStride,
            };
            CheckStatus(RenderGraph.AddBuffer(CSharpTriangleProbe.BufferName, descriptor, vertexBytes),
                RenderGraph, "AddBuffer");
            CheckStatus(RenderGraph.SetPositionColorStream(CSharpTriangleProbe.BufferName,
                CSharpTriangleProbe.VertexStride, CSharpTriangleProbe.PositionOffset,
                CSharpTriangleProbe.ColorOffset), RenderGraph, "SetPositionColorStream");

            // 同一份绘制描述：窗口一份、离屏目标一份（验证镜像）。
            // `skip-draw` 在**调用层**省略这两次 `AddTrianglePass`，图 pass 数为 0 ——
            // 原生测试与正常模式的 2 个 pass 对比判红（不造零面积三角形）。
            if (!skipDraw)
            {
                AddTrianglePass(RenderGraph, RenderWindowTarget);
                AddTrianglePass(RenderGraph, RenderCaptureTarget);
            }
            if (!skipPresent)
            {
                CheckStatus(RenderGraph.Present(RenderWindowTarget), RenderGraph, "Present");
            }
            int submitStatus = backend.SubmitGraph(RenderGraph);
            string submitError = backend.GetLastError();
            if (skipDraw)
            {
                // 空图本来就会被托管前置校验拒绝；这是负向模式期望的路径，不转成宿主错误。
                if (submitStatus == 0)
                {
                    throw new InvalidOperationException("渲染探针：skip-draw 的空图竟然提交成功");
                }
            }
            else
            {
                CheckStatus(submitStatus, backend, "SubmitGraph");
            }
            if (!skipWait)
            {
                backend.WaitIdle();
            }

            LastRenderSummary = $"mode={(mode.Length == 0 ? "normal" : mode)} passCount={RenderGraph.GetPassCount()} " +
                $"submitStatus={submitStatus} skipPresent={skipPresent} skipWait={skipWait}";
            LastRenderReadings = BuildReadings("triangle", mode, backend, RenderGraph, RenderCaptureTarget,
                RenderWindowTarget, submitStatus, submitError, preExec, preCapture, preStruct, preAddBuffer,
                preCreateTexture, preUpload, preImage, preSampler, preWait, preSetFloat3, preSetStruct,
                preSetPassParam, preSetBuffer, string.Empty);
        }

        // ---- 常量色探针（csharp-native-direct-binding 4.1）----
        /// <summary>`OnConstantColorProbe` 被调用了几次（与 `RenderProbeCount` 分开计数）。</summary>
        internal static int ConstantColorProbeCount;
        /// <summary>最近一次常量色探针的摘要：模式、pass 数、提交状态、拒绝文本、释放后失效读数。</summary>
        internal static string LastConstantColorSummary = string.Empty;
        /// <summary>机器可读读数行（C++ 侧用例按它核对托管计数与对象句柄）。</summary>
        internal static string LastConstantColorReadings = string.Empty;
        // 保留到原生核对完成：图、两个目标与参数结构都由原生侧读。
        internal static IWindow ConstantColorWindow;
        internal static ScriptRenderTarget ConstantColorWindowTarget;
        internal static ScriptRenderTarget ConstantColorCaptureTarget;
        internal static ScriptRenderGraph ConstantColorGraph;
        internal static ScriptShaderStruct ConstantColorParams;
        internal static ScriptShaderStruct[] ConstantColorSubStructs = new ScriptShaderStruct[3];

        /// <summary>
        /// 常量色探针（csharp-render-shader-constants 任务 3.2）：单 `POSITION` 顶点流 + `ConstantColor`
        /// 参数结构（3 个 `ColorSubStruct`），窗口与离屏各一个同逻辑 pass，两次 `SetPassParam`，
        /// 呈现、提交、等待。保留图、目标与参数结构到原生核对完成。
        ///
        /// Debug 自检（`CASCADED_CSHARP_CONSTCOLOR_SELFTEST`，C++ 侧读同一份值判定错误码）：
        ///   · `bad-struct-colors`：三槽颜色改成红/绿/蓝 —— 中心不再是纯红（`E_CONSTCOLOR_PIXEL`）；
        ///   · `skip-param`：完全不调 `SetPassParam` —— `SubmitGraph` 在进入后端执行前拒绝并点名
        ///     `constantColorBlock`（`E_CONSTCOLOR_PARAM`）；该状态记入摘要，不抛异常。
        /// </summary>
        internal static void OnConstantColorProbe(IWindowSystem windows, ScriptRenderBackend backend)
        {
            ConstantColorProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("常量色探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("常量色探针：原生渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_CONSTCOLOR_SELFTEST") ??
                          string.Empty;
            bool badStructColors = mode == "bad-struct-colors";
            bool skipParam = mode == "skip-param";
            if (mode.Length != 0 && !(badStructColors || skipParam))
            {
                throw new InvalidOperationException($"常量色探针：未知的自检模式 {mode}");
            }
            int preExec = backend.ExecuteGraphCount;
            int preCapture = backend.CreateCaptureTargetCount;
            int preStruct = backend.CreateShaderStructCount;
            int preAddBuffer = backend.AddBufferCount;
            int preCreateTexture = backend.CreateTextureCount;
            int preUpload = backend.TextureUploadExecuteCount;
            int preImage = backend.SetImageCount;
            int preSampler = backend.SetSamplerCount;
            int preWait = backend.WaitIdleCount;
            int preSetFloat3 = backend.SetFloat3Count;
            int preSetStruct = backend.SetStructCount;
            int preSetPassParam = backend.SetPassParamCount;
            int preSetBuffer = backend.SetBufferCount;

            ConstantColorWindow = windows.NewWindow(CSharpConstantColorProbe.WindowWidth,
                CSharpConstantColorProbe.WindowHeight, "C# 常量色", true, true, true, false);
            ConstantColorWindowTarget = backend.GetWindowTarget(ConstantColorWindow);
            if (ConstantColorWindowTarget == null)
            {
                throw new InvalidOperationException($"常量色探针：GetWindowTarget 返回空：{backend.GetLastError()}");
            }
            ConstantColorCaptureTarget = backend.CreateCaptureTarget(CSharpConstantColorProbe.WindowWidth,
                CSharpConstantColorProbe.WindowHeight);
            if (ConstantColorCaptureTarget == null)
            {
                throw new InvalidOperationException(
                    $"常量色探针：CreateCaptureTarget 返回空：{backend.GetLastError()}");
            }
            ConstantColorGraph = backend.CreateGraph();
            if (ConstantColorGraph == null)
            {
                throw new InvalidOperationException($"常量色探针：CreateGraph 返回空：{backend.GetLastError()}");
            }

            // 顶点字节由脚本自己写：三个顶点、每个只有位置 float3（步长 12）。
            float[] positions = CSharpConstantColorProbe.BuildPositions();
            byte[] vertexBytes = new byte[positions.Length * sizeof(float)];
            Buffer.BlockCopy(positions, 0, vertexBytes, 0, vertexBytes.Length);
            var descriptor = new GPUBufferDescriptor
            {
                count = CSharpConstantColorProbe.VertexCount,
                stride = CSharpConstantColorProbe.VertexStride,
            };
            CheckStatus(ConstantColorGraph.AddBuffer(CSharpConstantColorProbe.BufferName, descriptor,
                vertexBytes), ConstantColorGraph, "AddBuffer");
            CheckStatus(ConstantColorGraph.SetPositionStream(CSharpConstantColorProbe.BufferName,
                CSharpConstantColorProbe.VertexStride, CSharpConstantColorProbe.PositionOffset),
                ConstantColorGraph, "SetPositionStream");

            ConstantColorParams = backend.CreateShaderStruct(CSharpConstantColorProbe.StructTypeName);
            if (ConstantColorParams == null)
            {
                throw new InvalidOperationException(
                    $"常量色探针：CreateShaderStruct 返回空：{backend.GetLastError()}");
            }
            for (int slot = 0; slot < CSharpConstantColorProbe.SubStructCount; ++slot)
            {
                ScriptShaderStruct sub = backend.CreateShaderStruct(CSharpConstantColorProbe.SubStructTypeName);
                if (sub == null)
                {
                    throw new InvalidOperationException(
                        $"常量色探针：CreateShaderStruct(ColorSubStruct) 返回空：{backend.GetLastError()}");
                }
                ConstantColorSubStructs[slot] = sub;
                // 正常模式三槽同红；bad-struct-colors 改成红/绿/蓝（槽 0 红、槽 1 绿、槽 2 蓝）。
                float red = badStructColors ? (slot == 0 ? 1.0f : 0.0f) : 1.0f;
                float green = badStructColors && slot == 1 ? 1.0f : 0.0f;
                float blue = badStructColors && slot == 2 ? 1.0f : 0.0f;
                if (sub.SetFloat3(CSharpConstantColorProbe.ColorField, red, green, blue) != 0)
                {
                    throw new InvalidOperationException($"常量色探针：SetFloat3 被拒绝：{sub.GetLastError()}");
                }
                if (ConstantColorParams.SetStruct(CSharpConstantColorProbe.SubStructField, sub,
                        (uint)slot) != 0)
                {
                    throw new InvalidOperationException(
                        $"常量色探针：SetStruct 被拒绝：{ConstantColorParams.GetLastError()}");
                }
            }

            // 窗口与离屏各一个同逻辑 pass（像素验证镜像，不是通用多 pass）。
            AddConstantColorPass(ConstantColorWindowTarget);
            AddConstantColorPass(ConstantColorCaptureTarget);
            if (!skipParam)
            {
                CheckStatus(ConstantColorGraph.SetPassParam(0u, CSharpConstantColorProbe.BlockName,
                    ConstantColorParams), ConstantColorGraph, "SetPassParam(0)");
                CheckStatus(ConstantColorGraph.SetPassParam(1u, CSharpConstantColorProbe.BlockName,
                    ConstantColorParams), ConstantColorGraph, "SetPassParam(1)");
            }
            CheckStatus(ConstantColorGraph.Present(ConstantColorWindowTarget), ConstantColorGraph, "Present");

            int submitStatus = backend.SubmitGraph(ConstantColorGraph);
            string submitError = backend.GetLastError();
            bool disposedThrew = false;
            if (skipParam)
            {
                // 负向模式：提交前拒绝是期望结果，不抛异常（抛了会变成宿主错误码 4，判据分不清是哪一种）。
                if (submitStatus == 0)
                {
                    throw new InvalidOperationException("常量色探针：skip-param 的提交竟然成功");
                }
            }
            else
            {
                CheckStatus(submitStatus, backend, "SubmitGraph");
                backend.WaitIdle();
                // 提交并等待之后释放参数结构表面对象：旧表面再调用必须抛明确失效异常。
                ConstantColorParams.Dispose();
                try
                {
                    ConstantColorParams.SetFloat3(CSharpConstantColorProbe.ColorField, 1.0f, 0.0f, 0.0f);
                }
                catch (ObjectDisposedException)
                {
                    disposedThrew = true;
                }
                ConstantColorParams = null;
            }

            // 托管前置校验的负例（design D6，正常模式才跑）：
            //   ① 未知类型名 → 元数据查询处挡下，MUST NOT 进后端创建路径（创建计数不涨）；
            //   ② 未登记着色器路径 → 参数块校验点名路径拒绝。
            // 结果写进机器可读读数行，由 C++ 用例核对；负例自身不抛（负向模式另有分支）。
            string negatives = string.Empty;
            if (mode.Length == 0)
            {
                int structBeforeNegative = backend.CreateShaderStructCount;
                ScriptShaderStruct unknownStruct = backend.CreateShaderStruct("NoSuchStructForConstantColor");
                string unknownError = backend.GetLastError();
                bool unknownOk = unknownStruct == null &&
                                 unknownError.Contains("NoSuchStructForConstantColor") &&
                                 backend.CreateShaderStructCount == structBeforeNegative;
                ScriptRenderGraph negativeGraph = backend.CreateGraph();
                int negativeStatus = 1;
                string negativeError = string.Empty;
                if (negativeGraph != null)
                {
                    byte[] zeros = new byte[36];
                    int build = negativeGraph.AddBuffer("NegativeVertices",
                        new GPUBufferDescriptor { count = 3UL, stride = 12UL }, zeros);
                    build |= negativeGraph.SetPositionStream("NegativeStream", 12u, 0u);
                    build |= negativeGraph.AddTrianglePass(ConstantColorCaptureTarget,
                        "Shaders/Test/NoSuchShader", "NegativeStream", "NegativeVertices", 3u,
                        0.0f, 0.0f, 1.0f, 1.0f);
                    ScriptShaderStruct negativeParams = backend.CreateShaderStruct(
                        CSharpConstantColorProbe.StructTypeName);
                    build |= negativeGraph.SetPassParam(0u, CSharpConstantColorProbe.BlockName,
                        negativeParams);
                    negativeStatus = build;
                    negativeError = negativeGraph.GetLastError();
                    negativeGraph.Dispose();
                    negativeParams?.Dispose();
                }
                bool pathOk = negativeStatus != 0 && negativeError.Contains("NoSuchShader");
                negatives = $" negUnknown={(unknownOk ? 1 : 0)} negPath={(pathOk ? 1 : 0)}" +
                            $" negUnknownText={unknownError.Replace(' ', '_')}" +
                            $" negPathText={negativeError.Replace(' ', '_')}";
            }

            LastConstantColorSummary =
                $"mode={(mode.Length == 0 ? "normal" : mode)} passCount={ConstantColorGraph.GetPassCount()} " +
                $"submitStatus={submitStatus} skipParam={skipParam} disposedThrew={disposedThrew} " +
                $"submitError={submitError}";
            LastConstantColorReadings = BuildReadings("constantcolor", mode, backend, ConstantColorGraph,
                ConstantColorCaptureTarget, ConstantColorWindowTarget, submitStatus, submitError, preExec,
                preCapture, preStruct, preAddBuffer, preCreateTexture, preUpload, preImage, preSampler,
                preWait, preSetFloat3, preSetStruct, preSetPassParam, preSetBuffer, negatives);
        }

        private static void AddConstantColorPass(ScriptRenderTarget target)
        {
            CheckStatus(ConstantColorGraph.AddTrianglePass(target, CSharpConstantColorProbe.ShaderPath,
                CSharpConstantColorProbe.BufferName, CSharpConstantColorProbe.BufferName,
                CSharpConstantColorProbe.VertexCount, CSharpConstantColorProbe.ClearRed,
                CSharpConstantColorProbe.ClearGreen, CSharpConstantColorProbe.ClearBlue,
                CSharpConstantColorProbe.ClearAlpha), ConstantColorGraph, "AddTrianglePass");
        }

        // ---- 结构化缓冲探针（csharp-native-direct-binding 4.1）----
        /// <summary>`OnStructuredBufferProbe` 被调用了几次（与另外两个渲染探针分开计数）。</summary>
        internal static int StructuredBufferProbeCount;
        /// <summary>最近一次结构化缓冲探针的摘要：模式、pass 数、提交状态、拒绝文本。</summary>
        internal static string LastStructuredBufferSummary = string.Empty;
        /// <summary>机器可读读数行（C++ 侧用例按它核对托管计数与对象句柄）。</summary>
        internal static string LastStructuredBufferReadings = string.Empty;
        // 保留到原生核对完成：图、两个目标与参数结构都由原生侧读。
        internal static IWindow StructuredBufferWindow;
        internal static ScriptRenderTarget StructuredBufferWindowTarget;
        internal static ScriptRenderTarget StructuredBufferCaptureTarget;
        internal static ScriptRenderGraph StructuredBufferGraph;
        internal static ScriptShaderStruct StructuredBufferParams;

        /// <summary>
        /// 结构化缓冲探针（csharp-render-structured-buffer 任务 3.1）：单 `POSITION` 顶点流、
        /// `StructuredColor` 参数结构（一个 `StructuredBuffer<float3>` 字段）按名绑定图内登记的
        /// 结构化缓冲，窗口与离屏各一个同逻辑 pass、两次 `SetPassParam`，呈现、提交、等待。
        /// 保留图、目标与参数结构到原生核对完成。
        ///
        /// Debug 自检（`CASCADED_CSHARP_SB_SELFTEST`，C++ 侧读同一份值判定错误码）：
        ///   · `bad-buffer-color`：缓冲元素写成 (0,0,1) —— 中心精确变成 (0,0,255,255)（`E_SB_PIXEL`）；
        ///   · `unbound-field`：跳过 `SetBuffer` —— `SubmitGraph` 在进入后端执行前拒绝并点名
        ///     `colorStructuredBuffer`（`E_SB_BIND`），该状态记入摘要、不抛异常；
        ///   · `bad-buffer-name`：绑未登记的 `sbColorsMissing` —— 提交前拒绝并点名缓冲名（`E_SB_BIND`）。
        /// </summary>
        internal static void OnStructuredBufferProbe(IWindowSystem windows, ScriptRenderBackend backend)
        {
            StructuredBufferProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("结构化缓冲探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("结构化缓冲探针：原生渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_SB_SELFTEST") ?? string.Empty;
            bool badBufferColor = mode == "bad-buffer-color";
            bool unboundField = mode == "unbound-field";
            bool badBufferName = mode == "bad-buffer-name";
            if (mode.Length != 0 && !(badBufferColor || unboundField || badBufferName))
            {
                throw new InvalidOperationException($"结构化缓冲探针：未知的自检模式 {mode}");
            }
            int preExec = backend.ExecuteGraphCount;
            int preCapture = backend.CreateCaptureTargetCount;
            int preStruct = backend.CreateShaderStructCount;
            int preAddBuffer = backend.AddBufferCount;
            int preCreateTexture = backend.CreateTextureCount;
            int preUpload = backend.TextureUploadExecuteCount;
            int preImage = backend.SetImageCount;
            int preSampler = backend.SetSamplerCount;
            int preWait = backend.WaitIdleCount;
            int preSetFloat3 = backend.SetFloat3Count;
            int preSetStruct = backend.SetStructCount;
            int preSetPassParam = backend.SetPassParamCount;
            int preSetBuffer = backend.SetBufferCount;

            StructuredBufferWindow = windows.NewWindow(CSharpStructuredBufferProbe.WindowWidth,
                CSharpStructuredBufferProbe.WindowHeight, "C# 结构化缓冲", true, true, true, false);
            StructuredBufferWindowTarget = backend.GetWindowTarget(StructuredBufferWindow);
            if (StructuredBufferWindowTarget == null)
            {
                throw new InvalidOperationException(
                    $"结构化缓冲探针：GetWindowTarget 返回空：{backend.GetLastError()}");
            }
            StructuredBufferCaptureTarget = backend.CreateCaptureTarget(
                CSharpStructuredBufferProbe.WindowWidth, CSharpStructuredBufferProbe.WindowHeight);
            if (StructuredBufferCaptureTarget == null)
            {
                throw new InvalidOperationException(
                    $"结构化缓冲探针：CreateCaptureTarget 返回空：{backend.GetLastError()}");
            }
            StructuredBufferGraph = backend.CreateGraph();
            if (StructuredBufferGraph == null)
            {
                throw new InvalidOperationException(
                    $"结构化缓冲探针：CreateGraph 返回空：{backend.GetLastError()}");
            }

            // 顶点字节由脚本自己写：三个顶点、每个只有位置 float3（步长 12）。
            float[] positions = CSharpStructuredBufferProbe.BuildPositions();
            byte[] vertexBytes = new byte[positions.Length * sizeof(float)];
            Buffer.BlockCopy(positions, 0, vertexBytes, 0, vertexBytes.Length);
            var vertexDescriptor = new GPUBufferDescriptor
            {
                count = CSharpStructuredBufferProbe.VertexCount,
                stride = CSharpStructuredBufferProbe.VertexStride,
            };
            CheckStatus(StructuredBufferGraph.AddBuffer(CSharpStructuredBufferProbe.VertexBufferName,
                vertexDescriptor, vertexBytes), StructuredBufferGraph, "AddBuffer(顶点)");

            // 结构化缓冲：1 元素 × 12 字节。正常模式是 magenta (1,0,1)；
            // bad-buffer-color 写 (0,0,1)（既非清除色也非 magenta，供原生侧精确断言中心）。
            float[] color = CSharpStructuredBufferProbe.BuildBufferColor(badBufferColor);
            byte[] colorBytes = new byte[color.Length * sizeof(float)];
            Buffer.BlockCopy(color, 0, colorBytes, 0, colorBytes.Length);
            var colorDescriptor = new GPUBufferDescriptor
            {
                count = CSharpStructuredBufferProbe.BufferElementCount,
                stride = CSharpStructuredBufferProbe.BufferElementStride,
            };
            CheckStatus(StructuredBufferGraph.AddBuffer(CSharpStructuredBufferProbe.BufferName,
                colorDescriptor, colorBytes), StructuredBufferGraph, "AddBuffer(sbColors)");
            CheckStatus(StructuredBufferGraph.SetPositionStream(CSharpStructuredBufferProbe.VertexBufferName,
                CSharpStructuredBufferProbe.VertexStride, CSharpStructuredBufferProbe.PositionOffset),
                StructuredBufferGraph, "SetPositionStream");

            StructuredBufferParams = backend.CreateShaderStruct(CSharpStructuredBufferProbe.StructTypeName);
            if (StructuredBufferParams == null)
            {
                throw new InvalidOperationException(
                    $"结构化缓冲探针：CreateShaderStruct 返回空：{backend.GetLastError()}");
            }
            if (!unboundField)
            {
                // 正常与 bad-buffer-name 走这里；unbound-field 刻意不写，让提交检查点名缺失字段。
                string targetBufferName = badBufferName
                    ? CSharpStructuredBufferProbe.MissingBufferName
                    : CSharpStructuredBufferProbe.BufferName;
                if (StructuredBufferParams.SetBuffer(CSharpStructuredBufferProbe.FieldName,
                        targetBufferName) != 0)
                {
                    throw new InvalidOperationException(
                        $"结构化缓冲探针：SetBuffer 被拒绝：{StructuredBufferParams.GetLastError()}");
                }
            }

            AddStructuredBufferPass(StructuredBufferWindowTarget);
            AddStructuredBufferPass(StructuredBufferCaptureTarget);
            // 两种提交前拒绝形态也要走到两次 SetPassParam：拒绝发生在 SubmitGraph，不是绑定。
            CheckStatus(StructuredBufferGraph.SetPassParam(0u, CSharpStructuredBufferProbe.BlockName,
                StructuredBufferParams), StructuredBufferGraph, "SetPassParam(0)");
            CheckStatus(StructuredBufferGraph.SetPassParam(1u, CSharpStructuredBufferProbe.BlockName,
                StructuredBufferParams), StructuredBufferGraph, "SetPassParam(1)");
            CheckStatus(StructuredBufferGraph.Present(StructuredBufferWindowTarget), StructuredBufferGraph,
                "Present");

            int submitStatus = backend.SubmitGraph(StructuredBufferGraph);
            string submitError = backend.GetLastError();
            if (unboundField || badBufferName)
            {
                // 负向模式：提交前拒绝是期望结果，不抛异常（抛了会变成宿主错误码 4，判据分不清是哪一种）。
                if (submitStatus == 0)
                {
                    throw new InvalidOperationException(
                        $"结构化缓冲探针：{(unboundField ? "unbound-field" : "bad-buffer-name")} 的提交竟然成功");
                }
            }
            else
            {
                CheckStatus(submitStatus, backend, "SubmitGraph");
                backend.WaitIdle();
            }

            // 托管前置校验的负例（design D6，正常模式才跑）：缓冲表外的字段名 `SetBuffer` 必须被拒绝并
            // 点名字段，`SetBufferCount` 不涨（两后端对未命中字段是静默跳过，不能作为脚本面）。
            string negatives = string.Empty;
            if (mode.Length == 0)
            {
                int setBufferBeforeNegative = backend.SetBufferCount;
                int negativeStatus = StructuredBufferParams.SetBuffer("noSuchBufferField",
                    CSharpStructuredBufferProbe.BufferName);
                string negativeError = StructuredBufferParams.GetLastError();
                bool negativeOk = negativeStatus != 0 && negativeError.Contains("noSuchBufferField") &&
                                  backend.SetBufferCount == setBufferBeforeNegative;
                negatives = $" negField={(negativeOk ? 1 : 0)}" +
                            $" negFieldText={negativeError.Replace(' ', '_')}";
            }

            LastStructuredBufferSummary =
                $"mode={(mode.Length == 0 ? "normal" : mode)} " +
                $"passCount={StructuredBufferGraph.GetPassCount()} submitStatus={submitStatus} " +
                $"unboundField={unboundField} badBufferName={badBufferName} submitError={submitError}";
            string extras = string.Empty;
            if (StructuredBufferGraph != null)
            {
                string bound0 = StructuredBufferGraph.GetBoundBufferName(0,
                    CSharpStructuredBufferProbe.FieldName);
                string bound1 = StructuredBufferGraph.GetBoundBufferName(1,
                    CSharpStructuredBufferProbe.FieldName);
                extras = $" boundBuffer0={(bound0.Length == 0 ? "-" : bound0)}" +
                         $" boundBuffer1={(bound1.Length == 0 ? "-" : bound1)}";
            }
            extras += negatives;
            LastStructuredBufferReadings = BuildReadings("structuredbuffer", mode, backend,
                StructuredBufferGraph, StructuredBufferCaptureTarget, StructuredBufferWindowTarget,
                submitStatus, submitError, preExec, preCapture, preStruct, preAddBuffer, preCreateTexture,
                preUpload, preImage, preSampler, preWait, preSetFloat3, preSetStruct, preSetPassParam,
                preSetBuffer, extras);
            // 7.1 验收（正常模式）：后端直建缓冲跨两张图复用 —— 缓冲对象在 graph1.Dispose 之后仍能用；
            // 回退到"图内 AllocBuffer"时第二次绑定的就不是同一个后端对象，这个用例必败。
            // 放在读数行生成**之后**：复用探针的调用计数不污染主探针的计数断言，只追加自己的字段。
            if (mode.Length == 0)
            {
                LastStructuredBufferReadings += " " +
                    ProbeCreateBufferReuse(backend, colorDescriptor, colorBytes);
            }
        }

        /// <summary>
        /// 7.1 验收探针：后端 `CreateBuffer` 建一个缓冲，先后绑进两张独立的图并各自提交 —— 第一张图
        /// 负责把字节上传到**这个外部对象**，图 Dispose 后第二张图仍复用同一个对象。回退到图内
        /// `AllocBuffer` 时第二张图绑定的是另一个图内对象（且第一张图销毁后已不可达），本用例必败。
        /// </summary>
        private static string ProbeCreateBufferReuse(ScriptRenderBackend backend,
            GPUBufferDescriptor colorDescriptor, byte[] colorBytes)
        {
            // 红样开关（只给本用例的验收演练用）：走"图内 AllocBuffer"回退路径。第二张图没有本图
            // 分配的同名缓冲，提交前校验会拒绝 —— 证明跨图复用必须走后端直建对象。
            bool fallback = Environment.GetEnvironmentVariable("CASCADED_CSHARP_BUFFER_FALLBACK") == "1";
            string fallbackName = "sbColorsReuseFallback";
            ScriptBuffer buffer = null;
            ulong token = 0UL;
            if (!fallback)
            {
                buffer = backend.CreateBuffer(colorDescriptor,
                    EBufferUsage.eStructuredBuffer | EBufferUsage.eDataSrc | EBufferUsage.eDataDst);
                if (buffer == null)
                {
                    return "bufferReuse=0 bufferCreate=0 bufferError=" +
                           backend.GetLastError().Replace(' ', '_');
                }
                token = (ulong)(long)buffer.Native.Token;
            }
            float[] positions = CSharpStructuredBufferProbe.BuildPositions();
            byte[] vertexBytes = new byte[positions.Length * sizeof(float)];
            Buffer.BlockCopy(positions, 0, vertexBytes, 0, vertexBytes.Length);
            var vertexDescriptor = new GPUBufferDescriptor
            {
                count = CSharpStructuredBufferProbe.VertexCount,
                stride = CSharpStructuredBufferProbe.VertexStride,
            };
            int submits = 0;
            string error = string.Empty;
            for (int graphIndex = 0; graphIndex < 2 && error.Length == 0; ++graphIndex)
            {
                using (ScriptRenderGraph graph = backend.CreateGraph())
                {
                    if (graph == null)
                    {
                        error = "graph_null";
                        break;
                    }
                    if (graph.AddBuffer(CSharpStructuredBufferProbe.VertexBufferName,
                            vertexDescriptor, vertexBytes) != 0)
                    {
                        error = "vertex:" + graph.GetLastError();
                        break;
                    }
                    if (graph.SetPositionStream(CSharpStructuredBufferProbe.VertexBufferName,
                            CSharpStructuredBufferProbe.VertexStride,
                            CSharpStructuredBufferProbe.PositionOffset) != 0)
                    {
                        error = "stream:" + graph.GetLastError();
                        break;
                    }
                    if (fallback)
                    {
                        if (graphIndex == 0 &&
                            graph.AddBuffer(fallbackName, colorDescriptor, colorBytes) != 0)
                        {
                            error = "fallbackAdd:" + graph.GetLastError();
                            break;
                        }
                    }
                    else if (graphIndex == 0)
                    {
                        // 只在这里上传一次：数据写在**后端缓冲对象**上，不是图内分配。
                        graph.Native.ScheduleData(buffer.Handle, colorBytes, 0UL, true);
                    }
                    ScriptShaderStruct parameters = backend.CreateShaderStruct(
                        CSharpStructuredBufferProbe.StructTypeName);
                    if (parameters == null)
                    {
                        error = "struct:" + backend.GetLastError();
                        break;
                    }
                    int bindStatus = fallback
                        ? parameters.SetBuffer(CSharpStructuredBufferProbe.FieldName, fallbackName)
                        : parameters.SetBuffer(CSharpStructuredBufferProbe.FieldName, buffer);
                    if (bindStatus != 0)
                    {
                        error = "bind:" + parameters.GetLastError();
                        break;
                    }
                    if (graph.AddTrianglePass(StructuredBufferWindowTarget,
                            CSharpStructuredBufferProbe.ShaderPath,
                            CSharpStructuredBufferProbe.VertexBufferName,
                            CSharpStructuredBufferProbe.VertexBufferName,
                            CSharpStructuredBufferProbe.VertexCount,
                            CSharpStructuredBufferProbe.ClearRed, CSharpStructuredBufferProbe.ClearGreen,
                            CSharpStructuredBufferProbe.ClearBlue,
                            CSharpStructuredBufferProbe.ClearAlpha) != 0)
                    {
                        error = "pass:" + graph.GetLastError();
                        break;
                    }
                    if (graph.SetPassParam(0u, CSharpStructuredBufferProbe.BlockName, parameters) != 0)
                    {
                        error = "param:" + graph.GetLastError();
                        break;
                    }
                    if (graph.Present(StructuredBufferWindowTarget) != 0)
                    {
                        error = "present:" + graph.GetLastError();
                        break;
                    }
                    if (backend.SubmitGraph(graph) != 0)
                    {
                        error = "submit:" + backend.GetLastError();
                        break;
                    }
                    backend.WaitIdle();
                    ++submits;
                }
            }
            buffer?.Dispose();
            string suffix = error.Length == 0 ? string.Empty : " bufferError=" + error.Replace(' ', '_');
            return $"bufferReuse={(submits == 2 && !fallback ? 1 : 0)} bufferSubmits={submits} " +
                   $"bufferToken={token} bufferFallback={(fallback ? 1 : 0)} " +
                   $"bufferAfterDispose={(buffer != null && buffer.Disposed ? 1 : 0)} " +
                   $"bufferCreateCount={backend.CreateBufferCount}{suffix}";
        }

        private static void AddStructuredBufferPass(ScriptRenderTarget target)
        {
            CheckStatus(StructuredBufferGraph.AddTrianglePass(target,
                CSharpStructuredBufferProbe.ShaderPath, CSharpStructuredBufferProbe.VertexBufferName,
                CSharpStructuredBufferProbe.VertexBufferName, CSharpStructuredBufferProbe.VertexCount,
                CSharpStructuredBufferProbe.ClearRed, CSharpStructuredBufferProbe.ClearGreen,
                CSharpStructuredBufferProbe.ClearBlue, CSharpStructuredBufferProbe.ClearAlpha),
                StructuredBufferGraph, "AddTrianglePass");
        }

        // ---- 纹理采样探针（csharp-native-direct-binding 4.1）----
        /// <summary>`OnTextureProbe` 被调用了几次（与另外三个渲染探针分开计数）。</summary>
        internal static int TextureProbeCount;
        /// <summary>最近一次纹理探针的摘要：模式、pass 数、提交状态、拒绝文本、disposed 步。</summary>
        internal static string LastTextureSummary = string.Empty;
        /// <summary>机器可读读数行（C++ 侧用例按它核对托管计数与对象句柄）。</summary>
        internal static string LastTextureReadings = string.Empty;
        // 保留到原生核对与托管 Shutdown：图、两个目标、参数结构与纹理都由原生侧读。
        internal static IWindow TextureWindow;
        internal static ScriptRenderTarget TextureWindowTarget;
        internal static ScriptRenderTarget TextureCaptureTarget;
        internal static ScriptRenderGraph TextureGraph;
        internal static ScriptShaderStruct TextureParams;
        internal static ScriptTexture TextureProbeTexture;

        /// <summary>
        /// 纹理采样探针（csharp-render-texture-sampling 任务 3.2）：一次 `CreateTexture` 完成
        /// 创建 + 上传 + 转采样态；位置 + 纹理坐标顶点流；`TextureSampleBlock` 参数结构绑
        /// `testTexture` 与 `testSampler`；窗口与离屏各一个同逻辑 pass；呈现、提交、等待。
        /// 保留图、目标、参数结构与纹理到原生核对完成。
        ///
        /// Debug 自检（`CASCADED_CSHARP_TEX_SELFTEST`，C++ 侧读同一份值判定错误码）：
        ///   · `bad-texture-color`：四纹素改青 `(0,255,255,255)` —— 中心精确变 `(0,255,255,255)`（`E_TEX_PIXEL`）；
        ///   · `unbound-image`：跳过 `SetImage` —— `SubmitGraph` 在进入执行前拒绝并点名 `testTexture`（`E_TEX_BIND`）；
        ///   · `unbound-sampler`：跳过 `SetSampler` —— 同上，点名 `testSampler`。
        /// 探针末尾固定加一步：`TextureProbeTexture.Dispose()` 之后再用它调一次 `SetImage`，
        /// 捕获 `ObjectDisposedException` 并把结果写进摘要（"释放后使用"的防线在托管层，见 design 决策三）。
        /// </summary>
        internal static void OnTextureProbe(IWindowSystem windows, ScriptRenderBackend backend)
        {
            TextureProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("纹理探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("纹理探针：原生渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_TEX_SELFTEST") ?? string.Empty;
            bool badTextureColor = mode == "bad-texture-color";
            bool unboundImage = mode == "unbound-image";
            bool unboundSampler = mode == "unbound-sampler";
            if (mode.Length != 0 && !(badTextureColor || unboundImage || unboundSampler))
            {
                throw new InvalidOperationException($"纹理探针：未知的自检模式 {mode}");
            }
            int preExec = backend.ExecuteGraphCount;
            int preCapture = backend.CreateCaptureTargetCount;
            int preStruct = backend.CreateShaderStructCount;
            int preAddBuffer = backend.AddBufferCount;
            int preCreateTexture = backend.CreateTextureCount;
            int preUpload = backend.TextureUploadExecuteCount;
            int preImage = backend.SetImageCount;
            int preSampler = backend.SetSamplerCount;
            int preWait = backend.WaitIdleCount;
            int preSetFloat3 = backend.SetFloat3Count;
            int preSetStruct = backend.SetStructCount;
            int preSetPassParam = backend.SetPassParamCount;
            int preSetBuffer = backend.SetBufferCount;

            TextureWindow = windows.NewWindow(CSharpTextureProbe.WindowWidth,
                CSharpTextureProbe.WindowHeight, "C# 纹理采样", true, true, true, false);
            TextureWindowTarget = backend.GetWindowTarget(TextureWindow);
            if (TextureWindowTarget == null)
            {
                throw new InvalidOperationException(
                    $"纹理探针：GetWindowTarget 返回空：{backend.GetLastError()}");
            }
            TextureCaptureTarget = backend.CreateCaptureTarget(CSharpTextureProbe.WindowWidth,
                CSharpTextureProbe.WindowHeight);
            if (TextureCaptureTarget == null)
            {
                throw new InvalidOperationException(
                    $"纹理探针：CreateCaptureTarget 返回空：{backend.GetLastError()}");
            }
            TextureGraph = backend.CreateGraph();
            if (TextureGraph == null)
            {
                throw new InvalidOperationException(
                    $"纹理探针：CreateGraph 返回空：{backend.GetLastError()}");
            }

            // 2×2 纹理：正常全黄 (255,255,0,255)、bad-texture-color 全青 (0,255,255,255)。
            // 16 字节 = 2 × 2 × 4，必须等于 GetImageByteSize(2, 2, RGBA8)，否则适配层当场拒绝。
            byte[] texels = CSharpTextureProbe.BuildTexelBytes(badTextureColor);
            TextureProbeTexture = backend.CreateTexture(CSharpTextureProbe.TextureName,
                CSharpTextureProbe.TextureWidth, CSharpTextureProbe.TextureHeight, texels);
            if (TextureProbeTexture == null)
            {
                throw new InvalidOperationException(
                    $"纹理探针：CreateTexture 返回空：{backend.GetLastError()}");
            }
            // 尺寸读数必须在 disposed 步之前取：释放之后再问表面对象会抛失效异常。
            uint textureWidth = TextureProbeTexture.GetWidth();
            uint textureHeight = TextureProbeTexture.GetHeight();

            // 顶点字节由脚本自己写：三个顶点、每个 5 个 float（位置 xyz + UV，步长 20）。
            float[] vertices = CSharpTextureProbe.BuildVertices();
            byte[] vertexBytes = new byte[vertices.Length * sizeof(float)];
            Buffer.BlockCopy(vertices, 0, vertexBytes, 0, vertexBytes.Length);
            var vertexDescriptor = new GPUBufferDescriptor
            {
                count = CSharpTextureProbe.VertexCount,
                stride = CSharpTextureProbe.VertexStride,
            };
            CheckStatus(TextureGraph.AddBuffer(CSharpTextureProbe.VertexBufferName, vertexDescriptor,
                vertexBytes), TextureGraph, "AddBuffer(顶点)");
            CheckStatus(TextureGraph.SetPositionTextureStream(CSharpTextureProbe.VertexBufferName,
                CSharpTextureProbe.VertexStride, CSharpTextureProbe.PositionOffset,
                CSharpTextureProbe.TexcoordOffset), TextureGraph, "SetPositionTextureStream");

            TextureParams = backend.CreateShaderStruct(CSharpTextureProbe.StructTypeName);
            if (TextureParams == null)
            {
                throw new InvalidOperationException(
                    $"纹理探针：CreateShaderStruct 返回空：{backend.GetLastError()}");
            }
            if (!unboundImage)
            {
                // unbound-image 刻意不写，让提交检查点名缺失的 testTexture。
                if (TextureParams.SetImage(CSharpTextureProbe.TextureField, TextureProbeTexture) != 0)
                {
                    throw new InvalidOperationException(
                        $"纹理探针：SetImage 被拒绝：{TextureParams.GetLastError()}");
                }
            }
            if (!unboundSampler)
            {
                // unbound-sampler 刻意不写，让提交检查点名缺失的 testSampler。
                if (TextureParams.SetSampler(CSharpTextureProbe.SamplerField,
                    CSharpTextureProbe.LinearClamp()) != 0)
                {
                    throw new InvalidOperationException(
                        $"纹理探针：SetSampler 被拒绝：{TextureParams.GetLastError()}");
                }
            }

            AddTexturePass(TextureWindowTarget);
            AddTexturePass(TextureCaptureTarget);
            // 两种漏绑形态也要走到两次 SetPassParam：拒绝发生在 SubmitGraph，不是绑定。
            CheckStatus(TextureGraph.SetPassParam(0u, CSharpTextureProbe.BlockName, TextureParams),
                TextureGraph, "SetPassParam(0)");
            CheckStatus(TextureGraph.SetPassParam(1u, CSharpTextureProbe.BlockName, TextureParams),
                TextureGraph, "SetPassParam(1)");
            CheckStatus(TextureGraph.Present(TextureWindowTarget), TextureGraph, "Present");

            int submitStatus = backend.SubmitGraph(TextureGraph);
            string submitError = backend.GetLastError();
            if (unboundImage || unboundSampler)
            {
                // 负向模式：提交前拒绝是期望结果，不抛异常（抛了会变成宿主错误码 4，判据分不清是哪一种）。
                if (submitStatus == 0)
                {
                    throw new InvalidOperationException(
                        $"纹理探针：{(unboundImage ? "unbound-image" : "unbound-sampler")} 的提交竟然成功");
                }
            }
            else
            {
                CheckStatus(submitStatus, backend, "SubmitGraph");
                backend.WaitIdle();
            }

            // 对象句柄必须在 disposed 步之前取：释放后租约里的句柄归零。
            ulong textureToken = TextureProbeTexture.Token;
            string extras = $" texture={textureToken} textureSelf={TextureProbeTexture.NativeSelf}";
            if (TextureGraph != null)
            {
                extras += $" boundTexture0={TextureGraph.GetBoundImageToken(0, CSharpTextureProbe.TextureField)}" +
                          $" boundTexture1={TextureGraph.GetBoundImageToken(1, CSharpTextureProbe.TextureField)}";
            }

            // 托管前置校验的负例（design D6，正常模式才跑）：
            //   ① 字节数与 `width * height * 4` 不符 → 空句柄 + 文本同时点名期望值与实际值，
            //      `CreateTextureCount` 不涨（MUST NOT 进后端创建路径）；
            //   ② 纹理表外字段 `SetImage` → 拒绝并点名字段，`SetImageCount` 不涨。
            string negatives = string.Empty;
            if (mode.Length == 0)
            {
                int createBeforeNegative = backend.CreateTextureCount;
                ScriptTexture badTexture = backend.CreateTexture("texBadBytes", CSharpTextureProbe.TextureWidth,
                    CSharpTextureProbe.TextureHeight, new byte[12]);
                string badTextureError = backend.GetLastError();
                bool bytesOk = badTexture == null && badTextureError.Contains("16") &&
                               badTextureError.Contains("12") &&
                               backend.CreateTextureCount == createBeforeNegative;
                int imageBeforeNegative = backend.SetImageCount;
                int fieldStatus = TextureParams.SetImage("noSuchTextureField", TextureProbeTexture);
                string fieldError = TextureParams.GetLastError();
                bool fieldOk = fieldStatus != 0 && fieldError.Contains("noSuchTextureField") &&
                               backend.SetImageCount == imageBeforeNegative;
                negatives = $" negBytes={(bytesOk ? 1 : 0)} negField={(fieldOk ? 1 : 0)}" +
                            $" negBytesText={badTextureError.Replace(' ', '_')}" +
                            $" negFieldText={fieldError.Replace(' ', '_')}";
            }

            // 探针末尾的 disposed 步：释放纹理表面对象后再调一次 `SetImage`，必须在进入原生实现之前
            // 抛 `ObjectDisposedException`（原生 SetImage 调用 0）。
            string disposedStep;
            int disposedBefore = backend.SetImageCount;
            try
            {
                TextureProbeTexture.Dispose();
                TextureParams.SetImage(CSharpTextureProbe.TextureField, TextureProbeTexture);
                disposedStep = "disposed=none（释放后调用竟然成功，防线失效）";
            }
            catch (ObjectDisposedException ex)
            {
                disposedStep = backend.SetImageCount == disposedBefore
                    ? $"disposed={ex.GetType().Name} 原生 SetImage 调用 0"
                    : $"disposed={ex.GetType().Name} 但原生 SetImage 计数涨了";
            }

            LastTextureSummary =
                $"mode={(mode.Length == 0 ? "normal" : mode)} " +
                $"passCount={TextureGraph.GetPassCount()} submitStatus={submitStatus} " +
                $"unboundImage={unboundImage} unboundSampler={unboundSampler} " +
                $"texture=({textureWidth}x{textureHeight}) " +
                $"{disposedStep} submitError={submitError}";
            LastTextureReadings = BuildReadings("texture", mode, backend, TextureGraph, TextureCaptureTarget,
                TextureWindowTarget, submitStatus, submitError, preExec, preCapture, preStruct, preAddBuffer,
                preCreateTexture, preUpload, preImage, preSampler, preWait, preSetFloat3, preSetStruct,
                preSetPassParam, preSetBuffer, negatives + extras);
        }

        private static void AddTexturePass(ScriptRenderTarget target)
        {
            CheckStatus(TextureGraph.AddTrianglePass(target, CSharpTextureProbe.ShaderPath,
                CSharpTextureProbe.VertexBufferName, CSharpTextureProbe.VertexBufferName,
                CSharpTextureProbe.VertexCount, CSharpTextureProbe.ClearRed,
                CSharpTextureProbe.ClearGreen, CSharpTextureProbe.ClearBlue,
                CSharpTextureProbe.ClearAlpha), TextureGraph, "AddTrianglePass");
        }

        /// <summary>
        /// 关闭顺序的第 2 步（任务 3.1）：纹理表面对象排在参数结构之后、图之前释放 ——
        /// 参数结构持纹理引用，先放持有者。
        /// </summary>
        internal static void ReleaseTextureSurfaces()
        {
            if (TextureProbeTexture != null)
            {
                TextureProbeTexture.ReleaseNative();
                TextureProbeTexture.Dispose();
                TextureProbeTexture = null;
            }
        }

        /// <summary>
        /// 关闭顺序的第 1 步（任务 3.1）：参数结构表面对象（含三个子结构）必须先于图与目标释放。
        /// </summary>
        internal static void ReleaseShaderStructSurfaces()
        {
            if (ConstantColorParams != null)
            {
                ConstantColorParams.Dispose();
                ConstantColorParams = null;
            }
            for (int index = 0; index < ConstantColorSubStructs.Length; ++index)
            {
                if (ConstantColorSubStructs[index] != null)
                {
                    ConstantColorSubStructs[index].Dispose();
                    ConstantColorSubStructs[index] = null;
                }
            }
            // csharp-render-structured-buffer 任务 3.1：结构化缓冲探针的参数结构也按同一顺序先释放。
            if (StructuredBufferParams != null)
            {
                StructuredBufferParams.Dispose();
                StructuredBufferParams = null;
            }
            // csharp-render-texture-sampling 任务 3.1：纹理探针的参数结构同样排在第 1 步（它持纹理引用，
            // 必须先于第 2 步的纹理释放）。
            if (TextureParams != null)
            {
                TextureParams.Dispose();
                TextureParams = null;
            }
        }

        /// <summary>
        /// 关闭前释放脚本保留的渲染表面（本 change 任务 3.1）：先释放离屏目标、图与窗口目标，再让
        /// 宿主销毁原生适配器。释放后旧表面对象再次调用会抛 `ObjectDisposedException`。
        /// </summary>
        internal static void ReleaseRenderSurfaces()
        {
            if (RenderGraph != null)
            {
                RenderGraph.Dispose();
                RenderGraph = null;
            }
            if (ConstantColorGraph != null)
            {
                ConstantColorGraph.Dispose();
                ConstantColorGraph = null;
            }
            // csharp-render-structured-buffer 任务 3.1：结构化缓冲探针的图与两个目标。
            if (StructuredBufferGraph != null)
            {
                StructuredBufferGraph.Dispose();
                StructuredBufferGraph = null;
            }
            // csharp-render-texture-sampling 任务 3.1：纹理探针的图与两个目标。
            if (TextureGraph != null)
            {
                TextureGraph.Dispose();
                TextureGraph = null;
            }
            if (RenderCaptureTarget != null)
            {
                RenderCaptureTarget.Dispose();
                RenderCaptureTarget = null;
            }
            if (ConstantColorCaptureTarget != null)
            {
                ConstantColorCaptureTarget.Dispose();
                ConstantColorCaptureTarget = null;
            }
            if (StructuredBufferCaptureTarget != null)
            {
                StructuredBufferCaptureTarget.Dispose();
                StructuredBufferCaptureTarget = null;
            }
            if (TextureCaptureTarget != null)
            {
                TextureCaptureTarget.Dispose();
                TextureCaptureTarget = null;
            }
            if (RenderWindowTarget != null)
            {
                RenderWindowTarget.Dispose();
                RenderWindowTarget = null;
            }
            if (ConstantColorWindowTarget != null)
            {
                ConstantColorWindowTarget.Dispose();
                ConstantColorWindowTarget = null;
            }
            if (StructuredBufferWindowTarget != null)
            {
                StructuredBufferWindowTarget.Dispose();
                StructuredBufferWindowTarget = null;
            }
            if (TextureWindowTarget != null)
            {
                TextureWindowTarget.Dispose();
                TextureWindowTarget = null;
            }
            if (RenderWindow != null)
            {
                RenderWindow.Dispose();
                RenderWindow = null;
            }
            if (ConstantColorWindow != null)
            {
                ConstantColorWindow.Dispose();
                ConstantColorWindow = null;
            }
            if (StructuredBufferWindow != null)
            {
                StructuredBufferWindow.Dispose();
                StructuredBufferWindow = null;
            }
            if (TextureWindow != null)
            {
                TextureWindow.Dispose();
                TextureWindow = null;
            }
        }

        private static void AddTrianglePass(ScriptRenderGraph graph, ScriptRenderTarget target)
        {
            CheckStatus(graph.AddTrianglePass(target, CSharpTriangleProbe.ShaderPath,
                CSharpTriangleProbe.BufferName, CSharpTriangleProbe.BufferName,
                CSharpTriangleProbe.VertexCount, CSharpTriangleProbe.ClearRed,
                CSharpTriangleProbe.ClearGreen, CSharpTriangleProbe.ClearBlue,
                CSharpTriangleProbe.ClearAlpha), graph, "AddTrianglePass");
        }

        private static void CheckStatus(int status, ScriptRenderGraph graph, string step)
        {
            if (status == 0)
            {
                return;
            }
            throw new InvalidOperationException($"渲染探针：{step} 被拒绝（{status}）：{graph.GetLastError()}");
        }

        private static void CheckStatus(int status, ScriptRenderBackend backend, string step)
        {
            if (status == 0)
            {
                return;
            }
            throw new InvalidOperationException($"渲染探针：{step} 被拒绝（{status}）：{backend.GetLastError()}");
        }

        /// <summary>
        /// 一行**机器可读**读数（C++ 侧用例解析）：对象句柄供原生侧按绑定对象表解析出真实
        /// `GPUGraph` / `GPUTexture` / `WindowHandle`，计数供"拒绝时原生执行计数 0"这类断言。
        /// 文本里不含空格以外的分隔符；`reject` 永远在最后一段（以 ` | reject=` 起头）。
        /// </summary>
        private static string BuildReadings(string probe, string mode, ScriptRenderBackend backend,
            ScriptRenderGraph graph, ScriptRenderTarget capture, ScriptRenderTarget window,
            int submitStatus, string reject, int preExec, int preCapture, int preStruct, int preAddBuffer,
            int preCreateTexture, int preUpload, int preImage, int preSampler, int preWait,
            int preSetFloat3, int preSetStruct, int preSetPassParam, int preSetBuffer, string extra)
        {
            ulong graphToken = graph == null ? 0UL : graph.Token;
            ulong captureToken = capture == null ? 0UL : capture.CaptureTextureToken;
            ulong windowToken = window == null ? 0UL : window.WindowHandleToken;
            uint passCount = graph == null ? 0u : graph.GetPassCount();
            int presentCount = graph == null ? 0 : graph.PresentCount;
            string line = "[托管 读数]" +
                $" probe={probe}" +
                $" mode={(mode.Length == 0 ? "normal" : mode)}" +
                $" graph={graphToken}" +
                $" graphSelf={(graph == null ? 0UL : graph.NativeSelf)}" +
                $" capture={captureToken}" +
                $" captureSelf={(capture == null ? 0UL : capture.CaptureTextureSelf)}" +
                $" window={windowToken}" +
                $" size={(capture == null ? 0u : capture.GetWidth())}x{(capture == null ? 0u : capture.GetHeight())}" +
                $" pass={passCount}" +
                $" submit={submitStatus}" +
                $" present={presentCount}" +
                $" exec={backend.ExecuteGraphCount - preExec}" +
                $" wait={backend.WaitIdleCount - preWait}" +
                $" preExec={preExec}" +
                $" preCapture={preCapture}" +
                $" preStruct={preStruct}" +
                $" preAddBuffer={preAddBuffer}" +
                $" preCreateTexture={preCreateTexture}" +
                $" preUpload={preUpload}" +
                $" preImage={preImage}" +
                $" preSampler={preSampler}" +
                $" preWait={preWait}" +
                $" preSetFloat3={preSetFloat3}" +
                $" preSetStruct={preSetStruct}" +
                $" preSetPassParam={preSetPassParam}" +
                $" preSetBuffer={preSetBuffer}" +
                $" createStruct={backend.CreateShaderStructCount - preStruct}" +
                $" captureCount={backend.CreateCaptureTargetCount - preCapture}" +
                $" createTexture={backend.CreateTextureCount - preCreateTexture}" +
                $" upload={backend.TextureUploadExecuteCount - preUpload}" +
                $" addBuffer={backend.AddBufferCount - preAddBuffer}" +
                $" setFloat3={backend.SetFloat3Count - preSetFloat3}" +
                $" setStruct={backend.SetStructCount - preSetStruct}" +
                $" setPassParam={backend.SetPassParamCount - preSetPassParam}" +
                $" setBuffer={backend.SetBufferCount - preSetBuffer}" +
                $" setImage={backend.SetImageCount - preImage}" +
                $" setSampler={backend.SetSamplerCount - preSampler}";
            if (!string.IsNullOrEmpty(extra))
            {
                line += " " + extra;
            }
            if (!string.IsNullOrEmpty(reject))
            {
                line += " | reject=" + reject.Replace('\n', ' ').Replace('\r', ' ');
            }
            return line;
        }
    }

    /// <summary>
    /// 渲染探针的夹具值（本 change 任务 3.2 / 4.1）。C++ 侧 `TestCSharpTriangle` **另写一份**
    /// 独立的期望值（窗口尺寸、步长、偏移、像素颜色），两边不共享常量表 —— 一份改错不会两边一起绿。
    /// </summary>
    internal static class CSharpTriangleProbe
    {
        internal const int WindowWidth = 320;
        internal const int WindowHeight = 240;
        internal const uint VertexCount = 3;
        internal const uint VertexStride = 24;
        internal const uint PositionOffset = 0;
        internal const uint ColorOffset = 12;
        internal const string BufferName = "TestVerticesInput";
        internal const string ShaderPath = "Shaders/Test/TestSimpleTriangle";

        // 清除色 (0, 1, 0, 1) = 精确绿色背景（design 第六节）
        internal const float ClearRed = 0.0f;
        internal const float ClearGreen = 1.0f;
        internal const float ClearBlue = 0.0f;
        internal const float ClearAlpha = 1.0f;

        /// <summary>
        /// 三个顶点的位置与颜色（每个顶点 6 个 float：位置 xyz + 颜色 rgb）。
        /// 位置沿用现有简单三角形的平面坐标，深度统一为 0；正常模式的三个颜色都是红色。
        /// `skip-draw` 不经过这里 —— 它在调用层省略两次 `AddTrianglePass`，不靠改顶点数据伪造"跳过绘制"。
        /// </summary>
        internal static float[] BuildVertices(bool badVertex)
        {
            float[] values =
            {
                -0.25f, -0.25f, 0.0f, 1.0f, 0.0f, 0.0f,
                 0.25f, -0.25f, 0.0f, 1.0f, 0.0f, 0.0f,
                 0.00f,  0.50f, 0.0f, 1.0f, 0.0f, 0.0f,
            };
            if (badVertex)
            {
                // 第二个顶点的颜色改成蓝色：中心由红蓝插值得到，不再是纯红。
                values[1 * 6 + 3] = 0.0f;
                values[1 * 6 + 4] = 0.0f;
                values[1 * 6 + 5] = 1.0f;
            }
            return values;
        }
    }

    /// <summary>
    /// 常量色探针的夹具值（csharp-render-shader-constants 任务 3.2）。C++ 侧 `TestCSharpConstantColor`
    /// **另写一份**独立的期望值（尺寸、pass 数、背景与中心像素），两边不共享常量表。
    /// </summary>
    internal static class CSharpConstantColorProbe
    {
        internal const int WindowWidth = 320;
        internal const int WindowHeight = 240;
        internal const uint VertexCount = 3;
        internal const uint VertexStride = 12;
        internal const uint PositionOffset = 0;
        internal const string BufferName = "CSharpConstantColorVertices";
        internal const string ShaderPath = "Shaders/Test/TestTriangleWithConstantColor";
        internal const string StructTypeName = "ConstantColor";
        internal const string SubStructTypeName = "ColorSubStruct";
        internal const string SubStructField = "colorStruct";
        internal const string ColorField = "color";
        internal const string BlockName = "constantColorBlock";
        internal const int SubStructCount = 3;

        // 清除色 (0, 0, 1, 1) = 精确蓝色背景（design 第五节）
        internal const float ClearRed = 0.0f;
        internal const float ClearGreen = 0.0f;
        internal const float ClearBlue = 1.0f;
        internal const float ClearAlpha = 1.0f;

        /// <summary>
        /// 三个顶点的位置（每个顶点 3 个 float：xyz）。沿用常量色着色器既有测试的平面坐标与深度。
        /// </summary>
        internal static float[] BuildPositions()
        {
            return new float[]
            {
                -0.25f, -0.25f, 0.25f,
                 0.25f, -0.25f, 0.25f,
                 0.00f,  0.50f, 0.50f,
            };
        }
    }

    /// <summary>
    /// 结构化缓冲探针的夹具值（csharp-render-structured-buffer 任务 3.1）。C++ 侧
    /// `TestCSharpStructuredBuffer` **另写一份**独立的期望值（尺寸、pass 数、背景与中心像素、缓冲颜色、
    /// 清除色），两边不共享常量表。
    /// </summary>
    internal static class CSharpStructuredBufferProbe
    {
        internal const int WindowWidth = 320;
        internal const int WindowHeight = 240;
        internal const uint VertexCount = 3;
        internal const uint VertexStride = 12;
        internal const uint PositionOffset = 0;
        internal const string VertexBufferName = "CSharpStructuredBufferVertices";
        // 图内结构化缓冲的登记名：`SetBuffer` 与 `AddBuffer` 用同一个名字（先绑后建）。
        internal const string BufferName = "sbColors";
        internal const string MissingBufferName = "sbColorsMissing";
        internal const uint BufferElementCount = 1;
        internal const uint BufferElementStride = 12;
        internal const string ShaderPath = "Shaders/Test/TestTriangleWithStructuredBufferColor";
        internal const string StructTypeName = "StructuredColor";
        internal const string FieldName = "colorStructuredBuffer";
        internal const string BlockName = "structuredColorBlock";

        // 清除色 (0, 1, 0, 1) = 精确绿色背景
        internal const float ClearRed = 0.0f;
        internal const float ClearGreen = 1.0f;
        internal const float ClearBlue = 0.0f;
        internal const float ClearAlpha = 1.0f;

        /// <summary>
        /// 三个顶点的位置（每个顶点 3 个 float：xyz）。沿用结构化缓冲着色器既有测试的平面坐标。
        /// </summary>
        internal static float[] BuildPositions()
        {
            return new float[]
            {
                -0.25f, -0.25f, 0.25f,
                 0.25f, -0.25f, 0.25f,
                 0.00f,  0.50f, 0.50f,
            };
        }

        /// <summary>
        /// 结构化缓冲那一个元素的值（12 字节的 float3）。正常模式 magenta (1,0,1)；
        /// `bad-buffer-color` 用 (0,0,1) —— 既非清除色 (0,1,0) 也非 magenta。
        /// </summary>
        internal static float[] BuildBufferColor(bool badBufferColor)
        {
            return badBufferColor
                ? new float[] { 0.0f, 0.0f, 1.0f }
                : new float[] { 1.0f, 0.0f, 1.0f };
        }
    }

    /// <summary>
    /// 纹理采样探针的夹具值（csharp-render-texture-sampling 任务 3.2）。C++ 侧
    /// `TestCSharpTextureSampling` **另写一份**独立的期望值（尺寸、pass 数、背景与中心像素、
    /// 纹理字节、清除色），两边不共享常量表。
    /// </summary>
    internal static class CSharpTextureProbe
    {
        internal const int WindowWidth = 320;
        internal const int WindowHeight = 240;
        internal const uint VertexCount = 3;
        // 每个顶点 5 个 float：位置 xyz（12 字节）+ UV（8 字节）。
        internal const uint VertexStride = 20;
        internal const uint PositionOffset = 0;
        internal const uint TexcoordOffset = 12;
        internal const string VertexBufferName = "CSharpTextureVertices";
        internal const string TextureName = "texYellow2x2";
        internal const uint TextureWidth = 2;
        internal const uint TextureHeight = 2;
        internal const string ShaderPath = "Shaders/Test/TestCSharpTextureSampling";
        internal const string StructTypeName = "TextureSampleBlock";
        internal const string TextureField = "testTexture";
        internal const string SamplerField = "testSampler";
        internal const string BlockName = "textureBlock";

        // 清除色 (0, 0, 1, 1) = 精确蓝色背景
        internal const float ClearRed = 0.0f;
        internal const float ClearGreen = 0.0f;
        internal const float ClearBlue = 1.0f;
        internal const float ClearAlpha = 1.0f;

        /// <summary>
        /// 三个顶点的位置与 UV（每个顶点 5 个 float）。位置沿用常量色批的平面坐标；UV 三个值
        /// 只作流数据实例 —— 四纹素全同色，任何过滤 / 寻址 / UV 都返回同一颜色，判据不依赖插值。
        /// </summary>
        internal static float[] BuildVertices()
        {
            return new float[]
            {
                -0.25f, -0.25f, 0.25f, 0.0f, 0.0f,
                 0.25f, -0.25f, 0.25f, 1.0f, 0.0f,
                 0.00f,  0.50f, 0.50f, 0.5f, 1.0f,
            };
        }

        /// <summary>
        /// 2×2 纹理的 16 字节：正常四纹素全黄 (255,255,0,255)；
        /// `bad-texture-color` 全青 (0,255,255,255) —— 中心必须跟着精确变。
        /// </summary>
        internal static byte[] BuildTexelBytes(bool badTextureColor)
        {
            byte red = badTextureColor ? (byte)0 : (byte)255;
            byte green = 255;
            byte blue = badTextureColor ? (byte)255 : (byte)0;
            byte[] bytes = new byte[16];
            for (int texel = 0; texel < 4; ++texel)
            {
                bytes[texel * 4 + 0] = red;
                bytes[texel * 4 + 1] = green;
                bytes[texel * 4 + 2] = blue;
                bytes[texel * 4 + 3] = 255;
            }
            return bytes;
        }

        /// <summary>
        /// `LinearClamp` 采样器：与原生 `TextureSamplerDescriptor::LinearClamp()` 逐字段相同
        /// （三过滤 = 线性、三地址 = ClampToEdge、边界色默认、非整数格式）。
        /// 生成的公开记录没有工厂方法，这里按字段写。
        /// </summary>
        internal static TextureSamplerDescriptor LinearClamp()
        {
            var sampler = new TextureSamplerDescriptor
            {
                magFilterMode = ETextureSamplerFilterMode.eLinear,
                minFilterMode = ETextureSamplerFilterMode.eLinear,
                mipmapFilterMode = ETextureSamplerFilterMode.eLinear,
                addressModeU = ETextureSamplerAddressMode.eClampToEdge,
                addressModeV = ETextureSamplerAddressMode.eClampToEdge,
                addressModeW = ETextureSamplerAddressMode.eClampToEdge,
                boarderColor = ETextureSamplerBorderColor.eTransparentBlack,
                integerFormat = false,
            };
            return sampler;
        }
    }

    /// <summary>
    /// 回调探针的委托与期望读数（本 change 任务 1.2 骨架；4.1–4.3 在 `Host.ProbeCallbacks` 里使用）。
    ///
    /// 全部是常量：`TestCSharpWindowCallbacks` 的假窗口系统按同一组数字触发事件，两边不靠"互相抄"，
    /// 而是各自对着同一份写死的夹具值比较 —— 一边清零或错序，比对就红。
    /// </summary>
    internal static class CallbackProbe
    {
        /// <summary>假窗口的固定尺寸（`FakeWindowSystem` 的 `TestCSharpWindowCallbacks` 用例按它建窗）。</summary>
        internal const int WindowWidth = 640;
        internal const int WindowHeight = 480;

        /// <summary>假窗口的固定名字：回调里读 `IWindow.GetWindowName()` 用它证明拿到的是同一个窗口。</summary>
        internal const string WindowName = "回调探针窗口";

        /// <summary>用户委托抛出的原始异常文本：下一次主线程调用必须原样读回。</summary>
        internal const string ThrowingExceptionText = "回调探针故意抛出的异常文本";

        // 十类回调的固定标量参数（引擎侧触发时用同一组）
        internal const int FocusValue = 1;
        internal const int CursorEnterValue = 1;
        internal const float CursorX = 12.5f;
        internal const float CursorY = 34.5f;
        internal const int MouseButton = 1;
        internal const int MouseAction = 0;
        internal const int MouseMods = 3;
        internal const float ScrollX = 1.5f;
        internal const float ScrollY = -2.5f;
        internal const float WindowPosX = 100.5f;
        internal const float WindowPosY = 200.5f;
        internal const float WindowSizeX = 800.5f;
        internal const float WindowSizeY = 600.5f;
        internal const int KeyCode = 65;
        internal const int KeyScanCode = 30;
        internal const int KeyAction = 1;
        internal const int KeyMods = 2;
        internal const uint CharCode = 65u;
    }
}
