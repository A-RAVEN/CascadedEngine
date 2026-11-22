// editor-window-panel 任务 1.7：scene view（D11 修订版）—— 目标窗口左侧视口区域 + 同尺寸离屏捕获双目标。
//
// 绘制内容与既有三角形探针一致（CSharpTriangleProbe）：三个顶点 × 24 字节（位置偏移 0、颜色偏移 12）、
// 三个顶点全红、着色器 Shaders/Test/TestSimpleTriangle、清除色 (0,0.5,0,1)。区别只有两点：
//   ① 窗口 pass 的视口/剪裁矩形是**左侧视口区域**（糖层 additive 重载），不是整张窗口；
//   ② 每帧复用同一张图重复提交（糖层 SubmitGraph 的"已提交则跳过落图"扩展），与 TestSimpleTriangle
//      每帧 ExecuteGraph 同一张图的原生语义一致。
//
// 读数（供 C++ 用例的像素断言）：图对象地址 `graphSelf`、离屏捕获纹理地址与 self。像素由 C++ 侧
// 经 `ctx.pGPUBackend->Readback` 读 —— C# 绑定面没有 Readback（1.7 修订记录第 4 条）。
using System;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Editor
{
    internal sealed class EditorSceneView : IDisposable
    {
        // 与 CSharpTriangleProbe 相同的夹具值（三顶点红色三角形）。
        private const uint VertexCount = 3;
        private const uint VertexStride = 24;
        private const uint PositionOffset = 0;
        private const uint ColorOffset = 12;
        private const string VertexBufferName = "EditorSceneVertices";
        private const string ShaderPath = "Shaders/Test/TestSimpleTriangle";
        private const float ClearRed = 0.0f;
        private const float ClearGreen = 0.5f;
        private const float ClearBlue = 0.0f;
        private const float ClearAlpha = 1.0f;

        private readonly ScriptRenderBackend m_Backend;
        private readonly IWindow m_Window;
        private ScriptRenderTarget m_WindowTarget;
        private ScriptRenderTarget m_CaptureTarget;
        private ScriptRenderGraph m_Graph;
        private int m_ViewportWidth;
        private int m_ViewportHeight;
        private bool m_Disposed;

        /// <summary>已提交的帧数（读数 <c>exec=</c>；每帧一次 SubmitGraph + WaitIdle）。</summary>
        internal int ExecuteFrames;

        internal string LastError = string.Empty;

        internal EditorSceneView(ScriptRenderBackend backend, IWindow window)
        {
            m_Backend = backend;
            m_Window = window;
        }

        internal int ViewportWidth => m_ViewportWidth;

        internal int ViewportHeight => m_ViewportHeight;

        internal ulong GraphSelf
        {
            get
            {
                if (m_Graph == null)
                {
                    return 0UL;
                }
                return (ulong)(long)m_Graph.NativeSelf;
            }
        }

        internal ulong CaptureSelf => m_CaptureTarget == null ? 0UL : m_CaptureTarget.CaptureTextureSelf;

        // 读数用（排查窗口白屏）：窗口渲染目标尺寸 / pass 数 / 呈现项数 —— 核对 Present 目标是不是
        // 编辑器那个窗口的后台缓冲（与 C++ 侧 WGC 抓的 HWND 对照）。
        internal int WindowTargetWidth => m_WindowTarget == null ? 0 : (int)m_WindowTarget.GetWidth();

        internal int WindowTargetHeight => m_WindowTarget == null ? 0 : (int)m_WindowTarget.GetHeight();

        internal uint PassCount => m_Graph == null ? 0u : m_Graph.GetPassCount();

        internal int PresentCount => m_Graph == null ? 0 : m_Graph.PresentCount;

        internal bool Ready => m_Graph != null && m_CaptureTarget != null && m_WindowTarget != null;

        /// <summary>建图（首次或改尺寸后重建）。失败返回 false 并把原因写进 LastError。</summary>
        internal bool Build(int viewportWidth, int viewportHeight)
        {
            DisposeResources();
            if (viewportWidth <= 0 || viewportHeight <= 0)
            {
                LastError = "视口尺寸非法：" + viewportWidth + "x" + viewportHeight;
                return false;
            }
            m_ViewportWidth = viewportWidth;
            m_ViewportHeight = viewportHeight;
            try
            {
                m_WindowTarget = m_Backend.GetWindowTarget(m_Window);
                if (m_WindowTarget == null)
                {
                    LastError = "GetWindowTarget 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                m_CaptureTarget = m_Backend.CreateCaptureTarget((uint)viewportWidth, (uint)viewportHeight);
                if (m_CaptureTarget == null)
                {
                    LastError = "CreateCaptureTarget 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                m_Graph = m_Backend.CreateGraph();
                if (m_Graph == null)
                {
                    LastError = "CreateGraph 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                // 顶点字节：三个顶点的位置与颜色（每个 6 个 float），中心红。
                float[] values =
                {
                    -0.25f, -0.25f, 0.0f, 1.0f, 0.0f, 0.0f,
                     0.25f, -0.25f, 0.0f, 1.0f, 0.0f, 0.0f,
                     0.00f,  0.50f, 0.0f, 1.0f, 0.0f, 0.0f,
                };
                byte[] vertexBytes = new byte[values.Length * sizeof(float)];
                Buffer.BlockCopy(values, 0, vertexBytes, 0, vertexBytes.Length);
                var descriptor = new GPUBufferDescriptor { count = VertexCount, stride = VertexStride };
                if (!Check(m_Graph.AddBuffer(VertexBufferName, descriptor, vertexBytes), "AddBuffer"))
                {
                    return false;
                }
                if (!Check(m_Graph.SetPositionColorStream(VertexBufferName, VertexStride, PositionOffset,
                        ColorOffset), "SetPositionColorStream"))
                {
                    return false;
                }
                // 窗口 pass：批级与绘制级都写左侧视口区域（不是整张窗口）。
                if (!Check(m_Graph.AddTrianglePass(m_WindowTarget, ShaderPath, VertexBufferName,
                        VertexBufferName, VertexCount, ClearRed, ClearGreen, ClearBlue, ClearAlpha,
                        0, 0, viewportWidth, viewportHeight), "AddTrianglePass(窗口)"))
                {
                    return false;
                }
                // 离屏捕获 pass：同逻辑绘制，整张捕获目标。
                if (!Check(m_Graph.AddTrianglePass(m_CaptureTarget, ShaderPath, VertexBufferName,
                        VertexBufferName, VertexCount, ClearRed, ClearGreen, ClearBlue, ClearAlpha),
                        "AddTrianglePass(捕获)"))
                {
                    return false;
                }
                if (!Check(m_Graph.Present(m_WindowTarget), "Present"))
                {
                    return false;
                }
                LastError = string.Empty;
                Host.LogFromEditor("[编辑器] scene view 建图：窗口目标 " + m_WindowTarget.GetWidth() + "x" +
                                   m_WindowTarget.GetHeight() + "、视口 " + viewportWidth + "x" + viewportHeight +
                                   "、pass=" + m_Graph.GetPassCount() + "、present=" + m_Graph.PresentCount +
                                   "（每帧复用同一张图提交）");
                return true;
            }
            catch (Exception ex)
            {
                LastError = "建图异常：" + ex.GetType().Name + ":" + ex.Message;
                return false;
            }
        }

        /// <summary>改尺寸后按新视口区域重建捕获目标与图；尺寸没变则空转。</summary>
        internal bool Resize(int viewportWidth, int viewportHeight)
        {
            if (viewportWidth == m_ViewportWidth && viewportHeight == m_ViewportHeight && Ready)
            {
                return true;
            }
            return Build(viewportWidth, viewportHeight);
        }

        /// <summary>每帧提交一次并等待完成（同步语义与既有三角形探针一致，D11）。</summary>
        internal bool RenderFrame()
        {
            if (!Ready)
            {
                LastError = "scene view 未就绪";
                return false;
            }
            int status = m_Backend.SubmitGraph(m_Graph);
            if (status != 0)
            {
                LastError = "SubmitGraph 被拒绝：" + m_Backend.GetLastError();
                return false;
            }
            m_Backend.WaitIdle();
            ++ExecuteFrames;
            return true;
        }

        public void Dispose()
        {
            if (m_Disposed)
            {
                return;
            }
            m_Disposed = true;
            DisposeResources();
        }

        private void DisposeResources()
        {
            // 先释放图（它持有两个目标的附件引用），再释放目标（目标的 Dispose 释放窗口/纹理句柄）。
            if (m_Graph != null)
            {
                m_Graph.Dispose();
                m_Graph = null;
            }
            if (m_CaptureTarget != null)
            {
                m_CaptureTarget.Dispose();
                m_CaptureTarget = null;
            }
            if (m_WindowTarget != null)
            {
                m_WindowTarget.Dispose();
                m_WindowTarget = null;
            }
        }

        private bool Check(int status, string stage)
        {
            if (status == 0)
            {
                return true;
            }
            LastError = stage + " 失败：" + m_Graph?.GetLastError();
            return false;
        }
    }
}
