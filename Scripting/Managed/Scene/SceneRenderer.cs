// 场景 → 渲染图的最小翻译（csharp-scene-foundation 任务 3.1，design D6）。
//
// 每帧做什么：
//   ① 把活动场景里**带 MeshRenderer 的**实体逐个翻译成三角形顶点字节：
//      内联三角形局部坐标先乘 Transform.Scale、再加 Transform.Position（位移 + 缩放），
//      顶点色取该实体的 MeshRenderer.Color（design D6「Color 作顶点色」）；
//   ② 按 ShaderPath 分组：同一份着色器的实体合并进**同一个 pass**（一张顶点缓冲里串起多个三角形）。
//      图里每个分组两个同逻辑 pass（窗口目标 + 同尺寸离屏捕获目标），最后 Present 窗口目标；
//      翻译结果（着色器路径 + 顶点字节）与上一帧相同就复用同一张图重复提交
//      （与 EditorSceneView 每帧 SubmitGraph 同一语义，糖层 SubmitGraph 支持「已落盘则跳过 Materialize」）；
//   ③ SubmitGraph + WaitIdle（同步语义与既有三角形探针一致）。
//
// 为什么按着色器分组、而不是一实体一个 pass：糖层的 `AddTrianglePass` 每个 pass 的附件都是
// `eAttachmentLoadOp.eClear`（自带的清屏色），同一目标上第二个 pass 会把前一个 pass 的画面清掉。
// 一个着色器一个 pass 时，多实体场景里所有三角形在同一 pass 里绘制，画面是对的；
// **已知限制**（首切片，诚实登记）：一个场景里出现**两种以上 ShaderPath** 时，后一个分组的 pass
// 会清掉前一个分组的绘制（要正确合成需要在糖层新增「不清屏」的 additive 重载，留后续切片）。
//
// 其它首切片限制（design D6 诚实登记，MUST NOT 被当成完整实现）：
//   · **Rotation 序列化保留，但渲染暂不施加** —— 只施加 Position / Scale（下面刻意只用这两个）；
//   · 一实体一三角形，没有任意网格 / 相机 / 材质 / 纹理；
//   · 视口固定为整张窗口目标，没有相机矩阵。
//
// 资源存在性（design D4）：着色器路径在**建图前**用后端元数据查询 `FindRootStructData` 校验；
// 解析不到就点名该路径报错，**不静默跳过该实体**。不在保存 / 加载期校验 —— Load 是纯文件操作，
// 没有后端入口（见 SceneCodec 的注释）。
using System;
using System.Collections.Generic;
using System.Text;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Scene
{
    /// <summary>
    /// 一个活动场景的渲染器：持有窗口目标、同尺寸离屏捕获目标与一张图，按内容变化重建、每帧提交。
    /// 只在有活动场景时被创建（`SceneRuntime`）；无活动场景时这个对象根本不存在。
    /// </summary>
    internal sealed class SceneRenderer : IDisposable
    {
        // 顶点布局与既有三角形探针一致：位置 float3（偏移 0）+ 颜色 float3（偏移 12），步长 24。
        private const uint VertexStride = 24;
        private const uint PositionOffset = 0;
        private const uint ColorOffset = 12;

        // 清除色 (0,0,1,1) 蓝色：与既有四个渲染探针的绿色背景明确区分 ——
        // 场景用例的像素读数能证明「红三角来自场景这张图」，不是别的探针残留。
        private const float ClearRed = 0.0f;
        private const float ClearGreen = 0.0f;
        private const float ClearBlue = 1.0f;
        private const float ClearAlpha = 1.0f;

        private readonly ScriptRenderBackend m_Backend;
        private readonly IWindow m_Window;
        private ScriptRenderTarget m_WindowTarget;
        private ScriptRenderTarget m_CaptureTarget;
        private ScriptRenderGraph m_Graph;
        private string m_Signature = string.Empty;
        private int m_RenderedEntities;
        private int m_RenderedVertices;
        private int m_ShaderGroups;
        private bool m_Disposed;

        internal SceneRenderer(ScriptRenderBackend backend, IWindow window)
        {
            m_Backend = backend;
            m_Window = window;
        }

        /// <summary>图对象地址（读数用；图不存在时 0）。</summary>
        internal ulong GraphSelf => m_Graph == null ? 0UL : (ulong)(long)m_Graph.NativeSelf;

        /// <summary>图的对象句柄（读数用；图不存在时 0）。</summary>
        internal ulong GraphToken => m_Graph == null ? 0UL : m_Graph.Token;

        /// <summary>离屏捕获纹理地址（读数用；未建图时 0）。</summary>
        internal ulong CaptureSelf => m_CaptureTarget == null ? 0UL : m_CaptureTarget.CaptureTextureSelf;

        /// <summary>离屏捕获纹理的对象句柄（读数用；未建图时 0）。</summary>
        internal ulong CaptureToken => m_CaptureTarget == null ? 0UL : m_CaptureTarget.CaptureTextureToken;

        internal uint PassCount => m_Graph == null ? 0u : m_Graph.GetPassCount();

        internal int PresentCount => m_Graph == null ? 0 : m_Graph.PresentCount;

        internal uint CaptureWidth => m_CaptureTarget == null ? 0u : m_CaptureTarget.GetWidth();

        internal uint CaptureHeight => m_CaptureTarget == null ? 0u : m_CaptureTarget.GetHeight();

        /// <summary>最近一次翻译进图里的实体数（带 MeshRenderer 的那些）。</summary>
        internal int RenderedEntities => m_RenderedEntities;

        /// <summary>最近一次翻译进图里的三角形顶点总数（每实体 3 个）。</summary>
        internal int RenderedVertices => m_RenderedVertices;

        /// <summary>最近一次翻译出来的着色器分组数（= 图里 pass 数的一半）。</summary>
        internal int ShaderGroups => m_ShaderGroups;

        /// <summary>第一个被翻译顶点的位置分量（读数用：证明 Position / Scale 真施加到了顶点上）。</summary>
        internal float FirstVertexX;
        internal float FirstVertexY;
        internal float FirstVertexZ;

        /// <summary>
        /// 渲染一帧。返回 false 表示失败，<paramref name="error"/> 点名原因（调用方转成异常）。
        /// <paramref name="submitted"/> 为 false 表示这一帧没有可画的实体（不建图、不提交，不算错误）。
        /// </summary>
        internal bool Render(Scene scene, out string error, out bool submitted)
        {
            error = string.Empty;
            submitted = false;

            Translation translation = Translate(scene, out error);
            if (translation == null)
            {
                return false;
            }
            m_RenderedEntities = translation.Entities;
            m_RenderedVertices = 0;
            m_ShaderGroups = translation.Groups.Count;
            foreach (ShaderGroup group in translation.Groups)
            {
                m_RenderedVertices += group.VertexCount;
            }
            if (translation.Groups.Count == 0)
            {
                // 场景里没有渲染成分：不建图、不提交（没有可画的东西不是错误）。
                DisposeSurfaces();
                m_Signature = string.Empty;
                FirstVertexX = 0.0f;
                FirstVertexY = 0.0f;
                FirstVertexZ = 0.0f;
                return true;
            }
            ShaderGroup first = translation.Groups[0];
            FirstVertexX = first.Vertices[0];
            FirstVertexY = first.Vertices[1];
            FirstVertexZ = first.Vertices[2];

            string signature = BuildSignature(translation);
            if (m_Graph == null || !string.Equals(signature, m_Signature, StringComparison.Ordinal))
            {
                if (!Build(translation, out error))
                {
                    return false;
                }
                m_Signature = signature;
            }

            int status = m_Backend.SubmitGraph(m_Graph);
            if (status != 0)
            {
                error = "场景渲染：SubmitGraph 被拒绝（" + status + "）：" + m_Backend.GetLastError();
                return false;
            }
            m_Backend.WaitIdle();
            submitted = true;
            return true;
        }

        public void Dispose()
        {
            if (m_Disposed)
            {
                return;
            }
            m_Disposed = true;
            DisposeSurfaces();
        }

        // ---- 翻译 ----

        /// <summary>实体 → 按着色器分组的顶点字节。任何一步不合法就返回 null 并点名（不静默跳过实体）。</summary>
        private Translation Translate(Scene scene, out string error)
        {
            error = string.Empty;
            Translation translation = new Translation();
            if (scene == null || scene.Entities == null)
            {
                return translation;
            }
            Dictionary<string, ShaderGroup> byShader =
                new Dictionary<string, ShaderGroup>(StringComparer.Ordinal);
            for (int index = 0; index < scene.Entities.Count; ++index)
            {
                Entity entity = scene.Entities[index];
                if (entity == null)
                {
                    error = "场景渲染：实体列表里有空项（null），不知道要画什么";
                    return null;
                }
                MeshRenderer renderer = entity.MeshRenderer;
                if (renderer == null)
                {
                    // 没有渲染成分的实体不产生 pass（这是数据形状，不是「资源被跳过」）。
                    continue;
                }
                if (string.IsNullOrEmpty(renderer.ShaderPath))
                {
                    error = $"场景渲染：实体 {Describe(entity)} 的 MeshRenderer 缺少 ShaderPath";
                    return null;
                }
                // design D4：资源存在性在这里（渲染期、后端可用）校验，不在保存 / 加载期。
                if (m_Backend.FindRootStructData(renderer.ShaderPath) == null)
                {
                    error = $"场景渲染：实体 {Describe(entity)} 引用的着色器路径没有登记：{renderer.ShaderPath}";
                    return null;
                }
                if (renderer.Vertices == null || renderer.Vertices.Length != MeshRenderer.VertexFloatCount)
                {
                    error = $"场景渲染：实体 {Describe(entity)} 的内联顶点个数是 " +
                            $"{(renderer.Vertices == null ? 0 : renderer.Vertices.Length)}，" +
                            $"期望 {MeshRenderer.VertexFloatCount}（3 顶点 × 3 分量）";
                    return null;
                }
                ShaderGroup group;
                if (!byShader.TryGetValue(renderer.ShaderPath, out group))
                {
                    group = new ShaderGroup { ShaderPath = renderer.ShaderPath };
                    byShader[renderer.ShaderPath] = group;
                    translation.Groups.Add(group);
                }
                AppendTriangle(group, renderer, entity.Transform ?? Transform.Identity());
                ++translation.Entities;
            }
            return translation;
        }

        /// <summary>
        /// 追加一个三角形（3 个顶点 × 6 个 float：位置 xyz + 颜色 rgb）。
        /// **只施加 Position / Scale**：Rotation 序列化保留但渲染暂不施加（design D6，见文件头）。
        /// </summary>
        private static void AppendTriangle(ShaderGroup group, MeshRenderer renderer, Transform transform)
        {
            // Debug 自检 `bad-color`：颜色换成蓝色 → 中心像素不再是纯红（用例必须以 E_SCENE_PIXEL 判红）。
            bool badColor = SceneRuntime.SelfTestMode == "bad-color";
            float red = badColor ? 0.0f : renderer.Color.x;
            float green = badColor ? 0.0f : renderer.Color.y;
            float blue = badColor ? 1.0f : renderer.Color.z;
            for (int vertex = 0; vertex < MeshRenderer.VertexCount; ++vertex)
            {
                int source = vertex * MeshRenderer.ComponentsPerVertex;
                group.Vertices.Add(renderer.Vertices[source + 0] * transform.Scale.x + transform.Position.x);
                group.Vertices.Add(renderer.Vertices[source + 1] * transform.Scale.y + transform.Position.y);
                group.Vertices.Add(renderer.Vertices[source + 2] * transform.Scale.z + transform.Position.z);
                group.Vertices.Add(red);
                group.Vertices.Add(green);
                group.Vertices.Add(blue);
            }
            group.VertexCount += MeshRenderer.VertexCount;
        }

        private static string Describe(Entity entity)
        {
            string name = string.IsNullOrEmpty(entity.Name) ? "(无名)" : entity.Name;
            return name + "（id=" + entity.Id + "）";
        }

        /// <summary>翻译结果签名：着色器路径 + 顶点字节，用来判断「这一帧要不要重建图」。</summary>
        private static string BuildSignature(Translation translation)
        {
            StringBuilder builder = new StringBuilder();
            foreach (ShaderGroup group in translation.Groups)
            {
                builder.Append(group.ShaderPath).Append('|');
                foreach (float value in group.Vertices)
                {
                    builder.Append(BitConverter.SingleToInt32Bits(value).ToString("x8"));
                }
                builder.Append(';');
            }
            return builder.ToString();
        }

        // ---- 建图 ----

        private bool Build(Translation translation, out string error)
        {
            error = string.Empty;
            DisposeSurfaces();
            try
            {
                m_WindowTarget = m_Backend.GetWindowTarget(m_Window);
                if (m_WindowTarget == null)
                {
                    error = "场景渲染：GetWindowTarget 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                uint width = m_WindowTarget.GetWidth();
                uint height = m_WindowTarget.GetHeight();
                if (width == 0u || height == 0u)
                {
                    error = "场景渲染：窗口目标尺寸为零（" + width + "x" + height + "），无法建同尺寸离屏捕获目标";
                    return false;
                }
                m_CaptureTarget = m_Backend.CreateCaptureTarget(width, height);
                if (m_CaptureTarget == null)
                {
                    error = "场景渲染：CreateCaptureTarget 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                m_Graph = m_Backend.CreateGraph();
                if (m_Graph == null)
                {
                    error = "场景渲染：CreateGraph 返回空：" + m_Backend.GetLastError();
                    return false;
                }
                for (int groupIndex = 0; groupIndex < translation.Groups.Count; ++groupIndex)
                {
                    ShaderGroup group = translation.Groups[groupIndex];
                    string bufferName = "SceneShader" + groupIndex + "Vertices";
                    float[] values = group.Vertices.ToArray();
                    byte[] vertexBytes = new byte[values.Length * sizeof(float)];
                    Buffer.BlockCopy(values, 0, vertexBytes, 0, vertexBytes.Length);
                    var descriptor = new GPUBufferDescriptor
                    {
                        count = (uint)group.VertexCount,
                        stride = VertexStride,
                    };
                    if (!Check(m_Graph.AddBuffer(bufferName, descriptor, vertexBytes), "AddBuffer", out error))
                    {
                        return false;
                    }
                    if (!Check(m_Graph.SetPositionColorStream(bufferName, VertexStride, PositionOffset, ColorOffset),
                            "SetPositionColorStream", out error))
                    {
                        return false;
                    }
                    // 窗口与离屏各一个同逻辑 pass（像素验证镜像，与既有探针同形）。
                    if (!Check(m_Graph.AddTrianglePass(m_WindowTarget, group.ShaderPath, bufferName, bufferName,
                            (uint)group.VertexCount, ClearRed, ClearGreen, ClearBlue, ClearAlpha),
                            "AddTrianglePass(窗口)", out error))
                    {
                        return false;
                    }
                    if (!Check(m_Graph.AddTrianglePass(m_CaptureTarget, group.ShaderPath, bufferName, bufferName,
                            (uint)group.VertexCount, ClearRed, ClearGreen, ClearBlue, ClearAlpha),
                            "AddTrianglePass(离屏)", out error))
                    {
                        return false;
                    }
                }
                if (!Check(m_Graph.Present(m_WindowTarget), "Present", out error))
                {
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                error = "场景渲染：建图抛异常 " + ex.GetType().Name + "：" + ex.Message;
                return false;
            }
        }

        private bool Check(int status, string stage, out string error)
        {
            if (status == 0)
            {
                error = string.Empty;
                return true;
            }
            error = "场景渲染：" + stage + " 被拒绝：" + (m_Graph == null ? string.Empty : m_Graph.GetLastError());
            return false;
        }

        private void DisposeSurfaces()
        {
            // 先释放图（它持有两个目标的附件引用），再释放目标（目标的 Dispose 释放窗口 / 纹理句柄）。
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

        /// <summary>一次翻译的结果：按着色器分组 + 带渲染成分的实体数。</summary>
        private sealed class Translation
        {
            internal List<ShaderGroup> Groups = new List<ShaderGroup>();
            internal int Entities;
        }

        /// <summary>同一份着色器的所有三角形：一张顶点缓冲（每顶点 6 个 float）+ 顶点数。</summary>
        private sealed class ShaderGroup
        {
            internal string ShaderPath;
            internal List<float> Vertices = new List<float>();
            internal int VertexCount;
        }
    }
}
