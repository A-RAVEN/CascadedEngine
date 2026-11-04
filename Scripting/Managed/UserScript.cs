using System;
using Scripting.Generated.Bindings;

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

        // ---- 渲染探针（csharp-render-backend-triangle 任务 3.2）----
        /// <summary>`OnRenderProbe` 被调用了几次；引擎侧按"独立调用标记"判它没被别的测试消耗。</summary>
        internal static int RenderProbeCount;
        /// <summary>最近一次渲染探针的摘要，由宿主打进日志（包含模式、图 pass 数、等待调用）。</summary>
        internal static string LastRenderSummary = string.Empty;
        // 保留到托管 Shutdown 再释放：原生测试要用这些表面对象对应的原生资源做像素与结构核对。
        internal static IWindow RenderWindow;
        internal static IScriptRenderTarget RenderWindowTarget;
        internal static IScriptRenderTarget RenderCaptureTarget;
        internal static IScriptRenderGraph RenderGraph;

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

        /// <summary>
        /// 渲染探针（本 change 任务 3.2）：用现有 `IWindowSystem.NewWindow` 建窗，取窗口目标与同尺寸
        /// 离屏目标，用自己的顶点字节、着色器路径、清除色建图、提交并等待。
        ///
        /// 正常模式：三个顶点都是红色、位置沿用现有简单三角形；窗口与离屏目标各一份**同逻辑**绘制
        /// （像素验证镜像，不是通用多 pass）。
        ///
        /// Debug 自检模式（`CASCADED_CSHARP_RENDER_SELFTEST`，由 `TestCSharpTriangle` 从环境变量读同一份值）：
        ///   · `skip-draw`   —— 在调用层省略窗口与离屏目标的两次 `AddTrianglePass`：图 pass 数为 0，
        ///                      原生测试与正常模式的 2 个 pass 对比后以 `E_TRIANGLE_DRAW` 判红；
        ///   · `bad-vertex`  —— 把一个顶点改成蓝色：中心不再是纯红，证明插值来自脚本数据；
        ///   · `skip-present`—— 省略窗口呈现；
        ///   · `skip-wait`   —— 省略 `WaitIdle`。
        /// 生产脚本不依赖这些开关。
        /// </summary>
        public static void OnRenderProbe(IWindowSystem windows, IScriptRenderBackend backend)
        {
            RenderProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("渲染探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("渲染探针：脚本渲染后端表没有绑定");
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
            // 原生测试与正常模式的 2 个 pass 对比，以 `E_TRIANGLE_DRAW` 判红（不造零面积三角形）。
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
            if (skipDraw)
            {
                // 空图本来就会被适配器拒绝；这是负向模式期望的路径，不转成宿主错误。
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
        }

        // ---- 常量色探针（csharp-render-shader-constants 任务 3.2）----
        /// <summary>`OnConstantColorProbe` 被调用了几次（与 `RenderProbeCount` 分开计数）。</summary>
        internal static int ConstantColorProbeCount;
        /// <summary>最近一次常量色探针的摘要：模式、pass 数、提交状态、拒绝文本、释放后失效读数。</summary>
        internal static string LastConstantColorSummary = string.Empty;
        // 保留到原生核对完成：图、两个目标与参数结构都由原生侧读。
        internal static IWindow ConstantColorWindow;
        internal static IScriptRenderTarget ConstantColorWindowTarget;
        internal static IScriptRenderTarget ConstantColorCaptureTarget;
        internal static IScriptRenderGraph ConstantColorGraph;
        internal static IScriptShaderStruct ConstantColorParams;
        internal static IScriptShaderStruct[] ConstantColorSubStructs = new IScriptShaderStruct[3];

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
        public static void OnConstantColorProbe(IWindowSystem windows, IScriptRenderBackend backend)
        {
            ConstantColorProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("常量色探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("常量色探针：脚本渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_CONSTCOLOR_SELFTEST") ??
                          string.Empty;
            bool badStructColors = mode == "bad-struct-colors";
            bool skipParam = mode == "skip-param";
            if (mode.Length != 0 && !(badStructColors || skipParam))
            {
                throw new InvalidOperationException($"常量色探针：未知的自检模式 {mode}");
            }

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
                IScriptShaderStruct sub = backend.CreateShaderStruct(CSharpConstantColorProbe.SubStructTypeName);
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

            LastConstantColorSummary =
                $"mode={(mode.Length == 0 ? "normal" : mode)} passCount={ConstantColorGraph.GetPassCount()} " +
                $"submitStatus={submitStatus} skipParam={skipParam} disposedThrew={disposedThrew} " +
                $"submitError={submitError}";
        }

        private static void AddConstantColorPass(IScriptRenderTarget target)
        {
            CheckStatus(ConstantColorGraph.AddTrianglePass(target, CSharpConstantColorProbe.ShaderPath,
                CSharpConstantColorProbe.BufferName, CSharpConstantColorProbe.BufferName,
                CSharpConstantColorProbe.VertexCount, CSharpConstantColorProbe.ClearRed,
                CSharpConstantColorProbe.ClearGreen, CSharpConstantColorProbe.ClearBlue,
                CSharpConstantColorProbe.ClearAlpha), ConstantColorGraph, "AddTrianglePass");
        }

        // ---- 结构化缓冲探针（csharp-render-structured-buffer 任务 3.1）----
        /// <summary>`OnStructuredBufferProbe` 被调用了几次（与另外两个渲染探针分开计数）。</summary>
        internal static int StructuredBufferProbeCount;
        /// <summary>最近一次结构化缓冲探针的摘要：模式、pass 数、提交状态、拒绝文本。</summary>
        internal static string LastStructuredBufferSummary = string.Empty;
        // 保留到原生核对完成：图、两个目标与参数结构都由原生侧读。
        internal static IWindow StructuredBufferWindow;
        internal static IScriptRenderTarget StructuredBufferWindowTarget;
        internal static IScriptRenderTarget StructuredBufferCaptureTarget;
        internal static IScriptRenderGraph StructuredBufferGraph;
        internal static IScriptShaderStruct StructuredBufferParams;

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
        public static void OnStructuredBufferProbe(IWindowSystem windows, IScriptRenderBackend backend)
        {
            StructuredBufferProbeCount++;
            if (windows == null)
            {
                throw new InvalidOperationException("结构化缓冲探针：窗口系统表没有绑定");
            }
            if (backend == null)
            {
                throw new InvalidOperationException("结构化缓冲探针：脚本渲染后端表没有绑定");
            }

            string mode = Environment.GetEnvironmentVariable("CASCADED_CSHARP_SB_SELFTEST") ?? string.Empty;
            bool badBufferColor = mode == "bad-buffer-color";
            bool unboundField = mode == "unbound-field";
            bool badBufferName = mode == "bad-buffer-name";
            if (mode.Length != 0 && !(badBufferColor || unboundField || badBufferName))
            {
                throw new InvalidOperationException($"结构化缓冲探针：未知的自检模式 {mode}");
            }

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

            LastStructuredBufferSummary =
                $"mode={(mode.Length == 0 ? "normal" : mode)} " +
                $"passCount={StructuredBufferGraph.GetPassCount()} submitStatus={submitStatus} " +
                $"unboundField={unboundField} badBufferName={badBufferName} submitError={submitError}";
        }

        private static void AddStructuredBufferPass(IScriptRenderTarget target)
        {
            CheckStatus(StructuredBufferGraph.AddTrianglePass(target,
                CSharpStructuredBufferProbe.ShaderPath, CSharpStructuredBufferProbe.VertexBufferName,
                CSharpStructuredBufferProbe.VertexBufferName, CSharpStructuredBufferProbe.VertexCount,
                CSharpStructuredBufferProbe.ClearRed, CSharpStructuredBufferProbe.ClearGreen,
                CSharpStructuredBufferProbe.ClearBlue, CSharpStructuredBufferProbe.ClearAlpha),
                StructuredBufferGraph, "AddTrianglePass");
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
        }

        private static void AddTrianglePass(IScriptRenderGraph graph, IScriptRenderTarget target)
        {
            CheckStatus(graph.AddTrianglePass(target, CSharpTriangleProbe.ShaderPath,
                CSharpTriangleProbe.BufferName, CSharpTriangleProbe.BufferName,
                CSharpTriangleProbe.VertexCount, CSharpTriangleProbe.ClearRed,
                CSharpTriangleProbe.ClearGreen, CSharpTriangleProbe.ClearBlue,
                CSharpTriangleProbe.ClearAlpha), graph, "AddTrianglePass");
        }

        private static void CheckStatus(int status, IScriptRenderGraph graph, string step)
        {
            if (status == 0)
            {
                return;
            }
            throw new InvalidOperationException($"渲染探针：{step} 被拒绝（{status}）：{graph.GetLastError()}");
        }

        private static void CheckStatus(int status, IScriptRenderBackend backend, string step)
        {
            if (status == 0)
            {
                return;
            }
            throw new InvalidOperationException($"渲染探针：{step} 被拒绝（{status}）：{backend.GetLastError()}");
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
