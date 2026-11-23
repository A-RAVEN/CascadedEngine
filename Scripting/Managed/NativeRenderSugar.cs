// 脚本渲染的 C# 糖层（csharp-native-direct-binding 任务 4.1 / design D2、D6、D12、D13）。
//
// 位置：桥退役后脚本侧只剩**原生接口直绑表面**（生成的 `CRenderBackend` / `GPUGraph` /
// `ShaderStruct` / `GPUTexture` / `WindowHandle` / `ImageHandle` / `BufferHandle`）。本文件在这个
// 生成表面上补三件事，全部留在托管侧，**不改任何原生签名**：
//   ① 目标对象：原生没有"渲染目标"这个概念，窗口目标 = GetWindowHandle + GetSizeSafe +
//      ImageHandle 按 WindowHandle 构造 + isBackbuffer=true；捕获目标 = CreateGPUTexture +
//      ImageHandle 按 GPUTexture 构造 + 托管记尺寸（design D12）。
//   ② 提交前置校验（design D6）：缓冲描述/字节数/重名、顶点流越界与重名、绘制顶点数、
//      空图、呈现目标类型、必需参数块、结构化缓冲/纹理/采样器是否齐全，全部在进入函数表调用
//      **之前**按引擎编译元数据（`FindShaderStructData` 两段式只读副本）拒绝并点名；被拒时
//      原生执行入口的调用计数增量为 0。
//   ③ 便利形状：链式自返回、`using`/`Dispose`、字节上传糖 `ScheduleData(handle, byte[])`、
//      `PassCount` 读 `GetPassIndices().Length`（design D13）。
//
// 错误通道与退役前的适配层同义：返回 `int`（0 = 成功、非 0 = 拒绝），拒绝原因由 `GetLastError()`
// 点名参数 / 字段 / 缓冲名 / 纹理名；成功调用先清旧错误。异常只做兜底转换（生成表面抛出的
// `ObjectDisposedException` / `<接口>Exception` 一律转成错误返回，不让托管异常穿过 ABI 边界）。
using System;
using System.Collections.Generic;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.Cawindow;
using Scripting.Generated.Bindings.GraphicsBackend;
using Scripting.Generated.Bindings.CaBindFixture;
using Scripting.Generated.Bindings.Shadercompilerslang;

namespace Scripting.Managed
{
    /// <summary>
    /// 渲染目标（托管糖层对象，design D12）：窗口后台缓冲或后端创建的离屏纹理。
    /// 原生侧没有对应对象，尺寸与 isBackbuffer 由这里记；`Image` 是要过界的资源引用。
    /// </summary>
    internal sealed class ScriptRenderTarget : IDisposable
    {
        internal readonly ImageHandle Image;
        internal readonly uint Width;
        internal readonly uint Height;
        internal readonly bool Backbuffer;
        // 捕获目标持有离屏纹理的句柄；窗口目标持有窗口句柄的句柄。两者都要活到提交之后。
        private readonly GPUTexture m_CaptureTexture;
        private readonly WindowHandle m_WindowHandle;

        internal ScriptRenderTarget(ImageHandle image, uint width, uint height, bool backbuffer,
            GPUTexture captureTexture, WindowHandle windowHandle)
        {
            Image = image;
            Width = width;
            Height = height;
            Backbuffer = backbuffer;
            m_CaptureTexture = captureTexture;
            m_WindowHandle = windowHandle;
        }

        public uint GetWidth()
        {
            return Width;
        }

        public uint GetHeight()
        {
            return Height;
        }

        public bool IsBackbuffer()
        {
            return Backbuffer;
        }

        /// <summary>离屏纹理由后端发行：它的对象句柄（0 = 窗口目标）。测试侧按这个句柄解析原生对象。</summary>
        internal ulong CaptureTextureToken =>
            m_CaptureTexture == null ? 0UL : (ulong)(long)m_CaptureTexture.Token;

        /// <summary>窗口句柄的对象句柄（0 = 离屏目标）。</summary>
        internal ulong WindowHandleToken =>
            m_WindowHandle == null ? 0UL : (ulong)(long)m_WindowHandle.Token;

        /// <summary>原生离屏纹理对象地址（**只给测试读数**：离屏目标不释放，地址在测试期间稳定）。</summary>
        internal ulong CaptureTextureSelf =>
            m_CaptureTexture == null ? 0UL : (ulong)(long)m_CaptureTexture.NativeSelf;

        public void Dispose()
        {
            if (m_CaptureTexture != null)
            {
                m_CaptureTexture.Dispose();
            }
            if (m_WindowHandle != null)
            {
                m_WindowHandle.Dispose();
            }
        }
    }

    /// <summary>只读纹理（托管糖层对象）：后端纹理对象 + 创建时记下的尺寸。</summary>
    internal sealed class ScriptTexture : IDisposable
    {
        internal readonly GPUTexture Native;
        internal readonly uint Width;
        internal readonly uint Height;
        internal bool Disposed;

        internal ScriptTexture(GPUTexture native, uint width, uint height)
        {
            Native = native;
            Width = width;
            Height = height;
        }

        public uint GetWidth()
        {
            return Width;
        }

        public uint GetHeight()
        {
            return Height;
        }

        /// <summary>纹理对象句柄（测试侧按它解析原生 `GPUTexture`）。</summary>
        internal ulong Token => (ulong)(long)Native.Token;

        /// <summary>原生纹理对象地址（**只给测试读数**：纹理对象由绑定对象表与图内引用保活）。</summary>
        internal ulong NativeSelf => (ulong)(long)Native.NativeSelf;

        public void Dispose()
        {
            if (Disposed)
            {
                return;
            }
            // 本批不提供纹理的显式销毁（释放与飞行中提交的语义留给后续批，与退役前适配器同口径：
            // 适配器把纹理留在自己的登记表里直到进程结束）。`Dispose` 只把**托管表面**标成失效，
            // 原生纹理对象继续保活 —— 图里的参数结构还持有它的引用，测试也要按句柄解析到同一对象。
            Disposed = true;
        }

        /// <summary>进程关闭时真正释放原生纹理句柄（在 `ReleaseTextureSurfaces` 里调一次）。</summary>
        internal void ReleaseNative()
        {
            Native.Dispose();
        }
    }

    /// <summary>
    /// 后端直建缓冲（7.1，用户 2026-11-10 裁定）：生命周期跟随后端对象（`shared_ptr`），不跟图走。
    /// `Handle` 交给参数结构后，图以"外部缓冲"引用它：跨两张图复用同一对象、不需要 `AllocBuffer`，
    /// 图销毁也不释放它。
    /// </summary>
    internal sealed class ScriptBuffer : IDisposable
    {
        internal readonly GPUBuffer Native;
        internal readonly GPUBufferDescriptor Descriptor;
        internal bool Disposed;

        internal ScriptBuffer(GPUBuffer native, GPUBufferDescriptor descriptor)
        {
            Native = native;
            Descriptor = descriptor;
        }

        /// <summary>参数结构用的外部缓冲句柄（`BufferHandle` 的 External 分支）。</summary>
        internal BufferHandle Handle
        {
            get
            {
                if (Disposed)
                {
                    throw new ObjectDisposedException("ScriptBuffer");
                }
                return BufferHandle.FromGPUBuffer(Native);
            }
        }

        public GPUBufferDescriptor GetDescriptor()
        {
            return Descriptor;
        }

        public void Dispose()
        {
            if (Disposed)
            {
                return;
            }
            // 只放**托管引用**：仍被图/参数结构持有的原生对象由 `shared_ptr` 保活，
            // 所以 Dispose 之后已经提交/已绑定的图仍能跑完（MUST NOT 提前释放）。
            Disposed = true;
            Native.Dispose();
        }

        /// <summary>进程关闭时真正释放原生缓冲句柄。</summary>
        internal void ReleaseNative()
        {
            Native.Dispose();
        }
    }

    /// <summary>
    /// 着色器参数结构（托管糖层对象，design D6/D9/D11）：生成的 `ShaderStruct` 表面 + 引擎编译
    /// 元数据的只读副本。字段级校验全部在这里做，原生 `Set*` 的未知字段静默跳过 / 越界
    /// `CA_ASSERT` 路径不会被脚本输入触达。
    /// </summary>
    internal sealed class ScriptShaderStruct : IDisposable
    {
        private readonly ShaderStruct m_Native;
        private readonly ShaderStructData m_Data;
        private readonly ScriptRenderBackend m_Backend;
        internal readonly string TypeName;
        private string m_LastError = string.Empty;

        // 资源绑定的托管侧登记：提交前置校验要逐字段核对"写过没有 / 缓冲在不在本图"。
        private readonly Dictionary<string, string> m_BoundBuffers = new Dictionary<string, string>();
        // 7.1：以 `ScriptBuffer` 绑定的外部缓冲 —— 不在图内分配表里，提交校验跳过 `HasBuffer`。
        private readonly HashSet<string> m_ExternalBufferFields = new HashSet<string>();
        private readonly Dictionary<string, ScriptTexture> m_BoundImages =
            new Dictionary<string, ScriptTexture>();
        private readonly Dictionary<string, TextureSamplerDescriptor> m_BoundSamplers =
            new Dictionary<string, TextureSamplerDescriptor>();

        internal ScriptShaderStruct(ShaderStruct native, ShaderStructData data, string typeName,
            ScriptRenderBackend backend)
        {
            m_Native = native;
            m_Data = data;
            TypeName = typeName;
            m_Backend = backend;
        }

        internal ShaderStruct Native => m_Native;

        internal ShaderStructData Data => m_Data;

        public string GetLastError()
        {
            return m_LastError;
        }

        /// <summary>按字段名写 `float3`（模板 `SetValue<float3>` 的显式实例化形态）。</summary>
        public int SetFloat3(string field, float x, float y, float z)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            UniformElement? element = null;
            if (m_Data.m_StructUniforms != null && m_Data.m_StructUniforms.m_Elements != null)
            {
                foreach (UniformElement candidate in m_Data.m_StructUniforms.m_Elements)
                {
                    if (candidate.m_Name == field)
                    {
                        element = candidate;
                        break;
                    }
                }
            }
            if (element == null)
            {
                return Fail("field", $"字段不在类型 {TypeName} 的元数据里：{field}");
            }
            // 三条都要核：字节数、类型名、元素数。12 字节的 `int3` 与 `float3[2]` 的元素也过不了。
            if (element.Value.m_ElementMemorySize != 12u)
            {
                return Fail("field", $"字段 {field} 必须是 12 字节的类型，实际 {element.Value.m_ElementMemorySize} 字节");
            }
            if (element.Value.m_TypeName != "vector<float,3>")
            {
                return Fail("field", $"字段 {field} 类型不符：期望 vector<float,3>，实际 {element.Value.m_TypeName}");
            }
            if (element.Value.m_ElementCount != 1u)
            {
                return Fail("field", $"字段 {field} 必须是单元素，实际元素数 {element.Value.m_ElementCount}");
            }
            try
            {
                m_Native.SetValue(field, new float3 { x = x, y = y, z = z }, 0u);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("field", ex.Message);
            }
            ++m_Backend.SetFloat3Count;
            return ClearError();
        }

        /// <summary>把子结构挂到该子结构数组字段的第 elementIndex 个元素。</summary>
        public int SetStruct(string field, ScriptShaderStruct sub, uint elementIndex)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            if (sub == null)
            {
                return Fail("sub", "子结构不能为空");
            }
            if (sub.Disposed)
            {
                throw new ObjectDisposedException("ScriptShaderStruct");
            }
            SubStructReference? reference = null;
            foreach (SubStructReference candidate in m_Data.m_SubStructReferences)
            {
                if (candidate.m_Name == field)
                {
                    reference = candidate;
                    break;
                }
            }
            if (reference == null)
            {
                return Fail("field", $"子结构字段不在类型 {TypeName} 的元数据里：{field}");
            }
            if (sub.TypeName != reference.Value.m_StructTypeName)
            {
                return Fail("sub", $"子结构类型不符：期望 {reference.Value.m_StructTypeName}，实际 {sub.TypeName}");
            }
            // 下标必须小于该子结构数组声明的元素数（越界在 D3D12 侧只 CA_ASSERT，数据会被悄悄写坏）。
            if (elementIndex >= reference.Value.m_ElementCount)
            {
                return Fail("elementIndex",
                    $"下标越界：元素数 {reference.Value.m_ElementCount}，实际下标 {elementIndex}");
            }
            // 含资源字段的子结构会绕过顶层提交检查（提交检查只走顶层类型自己的三张表），
            // 本批只支持顶层资源字段，必须拒绝。
            if (sub.HasResourceFields(out string resourceKind))
            {
                return Fail("field", $"子结构类型 {sub.TypeName} 含{resourceKind}字段，本批只支持顶层{resourceKind}字段：{field}");
            }
            try
            {
                m_Native.SetStruct(field, sub.m_Native, elementIndex);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("sub", ex.Message);
            }
            ++m_Backend.SetStructCount;
            return ClearError();
        }

        /// <summary>把"将绑定的那张图"里登记的缓冲按名绑到本结构的缓冲字段（先绑后建，提交时解析）。</summary>
        public int SetBuffer(string field, string bufferName)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            if (string.IsNullOrEmpty(bufferName))
            {
                return Fail("bufferName", "缓冲名不能为空");
            }
            BufferData? bufferData = null;
            if (m_Data.m_Buffers != null)
            {
                foreach (BufferData candidate in m_Data.m_Buffers)
                {
                    if (candidate.m_Name == field)
                    {
                        bufferData = candidate;
                        break;
                    }
                }
            }
            if (bufferData == null)
            {
                return Fail("field", $"字段不在类型 {TypeName} 的缓冲元数据里：{field}");
            }
            if (bufferData.Value.m_RWType != EShaderResourceAccess.eReadOnly)
            {
                return Fail("field", $"字段 {field} 的访问类型不是只读；读写缓冲属计算批，本批不支持");
            }
            if (bufferData.Value.m_ElementCount != 1u)
            {
                return Fail("field",
                    $"字段 {field} 必须是单元素（描述符数组长度为 1），实际 {bufferData.Value.m_ElementCount}");
            }
            try
            {
                m_Native.SetBuffer(field, BufferHandle.FromName(bufferName), 0u);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("field", ex.Message);
            }
            m_BoundBuffers[field] = bufferName;
            ++m_Backend.SetBufferCount;
            return ClearError();
        }

        /// <summary>
        /// 7.1：把后端直建的 `ScriptBuffer` 按字段名绑到本结构的缓冲字段。它走 `BufferHandle` 的
        /// External 分支（不占图内分配表），所以同一对象可以跨多张图复用，图销毁不释放它。
        /// 字段名 / 只读 / 单元素的校验与按名版本一致。
        /// </summary>
        public int SetBuffer(string field, ScriptBuffer buffer)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            if (buffer == null || buffer.Disposed)
            {
                return Fail("buffer", $"字段 {field} 的缓冲为空或已释放");
            }
            BufferData? bufferData = null;
            if (m_Data.m_Buffers != null)
            {
                foreach (BufferData candidate in m_Data.m_Buffers)
                {
                    if (candidate.m_Name == field)
                    {
                        bufferData = candidate;
                        break;
                    }
                }
            }
            if (bufferData == null)
            {
                return Fail("field", $"字段不在类型 {TypeName} 的缓冲元数据里：{field}");
            }
            if (bufferData.Value.m_RWType != EShaderResourceAccess.eReadOnly)
            {
                return Fail("field", $"字段 {field} 的访问类型不是只读；读写缓冲属计算批，本批不支持");
            }
            if (bufferData.Value.m_ElementCount != 1u)
            {
                return Fail("field",
                    $"字段 {field} 必须是单元素（描述符数组长度为 1），实际 {bufferData.Value.m_ElementCount}");
            }
            try
            {
                m_Native.SetBuffer(field, buffer.Handle, 0u);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("field", ex.Message);
            }
            m_BoundBuffers[field] = $"external:{field}";
            m_ExternalBufferFields.Add(field);
            ++m_Backend.SetBufferCount;
            return ClearError();
        }

        /// <summary>该字段是否以外部 `ScriptBuffer` 绑定（提交校验据此跳过图内分配表）。</summary>
        internal bool IsExternalBuffer(string field) => m_ExternalBufferFields.Contains(field);

        /// <summary>把后端发行的纹理按字段名绑到本结构的纹理字段。</summary>
        public int SetImage(string field, ScriptTexture texture)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            if (texture != null && texture.Disposed)
            {
                // 释放后使用：防线在托管表面 —— 必须在进入原生实现前抛 ObjectDisposedException
                // （原生 SetImage 调用计数保持 0）。
                throw new ObjectDisposedException("ScriptTexture");
            }
            if (texture == null)
            {
                return Fail("field", $"字段 {field} 的纹理是空句柄；纹理必须先经 CreateTexture 创建");
            }
            ShaderTextureData? textureData = null;
            if (m_Data.m_Textures != null)
            {
                foreach (ShaderTextureData candidate in m_Data.m_Textures)
                {
                    if (candidate.m_Name == field)
                    {
                        textureData = candidate;
                        break;
                    }
                }
            }
            if (textureData == null)
            {
                return Fail("field", $"字段不在类型 {TypeName} 的纹理元数据里：{field}");
            }
            if (textureData.Value.m_RWType != EShaderResourceAccess.eReadOnly)
            {
                return Fail("field", $"字段 {field} 的访问类型不是只读；读写纹理（RWTexture）属计算批，本批不支持");
            }
            if (textureData.Value.m_ElementCount != 1u)
            {
                return Fail("field",
                    $"字段 {field} 必须是单元素（描述符数组长度为 1），实际 {textureData.Value.m_ElementCount}");
            }
            try
            {
                m_Native.SetImage(field, texture.Native, 0u);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("field", ex.Message);
            }
            m_BoundImages[field] = texture;
            ++m_Backend.SetImageCount;
            return ClearError();
        }

        /// <summary>采样器描述按字段名绑到采样器表内的字段。</summary>
        public int SetSampler(string field, TextureSamplerDescriptor sampler)
        {
            if (string.IsNullOrEmpty(field))
            {
                return Fail("field", "字段名不能为空");
            }
            bool found = false;
            if (m_Data.m_TextureSamplers != null)
            {
                foreach (string candidate in m_Data.m_TextureSamplers)
                {
                    if (candidate == field)
                    {
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
            {
                return Fail("field", $"字段不在类型 {TypeName} 的采样器元数据里：{field}");
            }
            try
            {
                m_Native.SetSampler(field, sampler, 0u);
            }
            catch (ObjectDisposedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                return Fail("field", ex.Message);
            }
            m_BoundSamplers[field] = sampler;
            ++m_Backend.SetSamplerCount;
            return ClearError();
        }

        internal bool HasBoundBuffer(string field) => m_BoundBuffers.ContainsKey(field);

        internal string GetBoundBufferName(string field) =>
            m_BoundBuffers.TryGetValue(field, out string name) ? name : string.Empty;

        internal bool HasBoundImage(string field) => m_BoundImages.ContainsKey(field);

        internal ScriptTexture GetBoundImage(string field) =>
            m_BoundImages.TryGetValue(field, out ScriptTexture texture) ? texture : null;

        internal bool HasBoundSampler(string field) => m_BoundSamplers.ContainsKey(field);

        private bool HasResourceFields(out string kind)
        {
            if (m_Data.m_Buffers != null && m_Data.m_Buffers.Length != 0)
            {
                kind = "缓冲";
                return true;
            }
            if (m_Data.m_Textures != null && m_Data.m_Textures.Length != 0)
            {
                kind = "纹理";
                return true;
            }
            if (m_Data.m_TextureSamplers != null && m_Data.m_TextureSamplers.Length != 0)
            {
                kind = "采样器";
                return true;
            }
            kind = string.Empty;
            return false;
        }

        public void Dispose()
        {
            if (Disposed)
            {
                return;
            }
            Disposed = true;
            m_Native.Dispose();
        }

        /// <summary>表面对象是否已释放（释放后使用由这里挡在原生实现之前）。</summary>
        internal bool Disposed;

        private int Fail(string parameter, string reason)
        {
            m_LastError = $"ScriptShaderStruct.{parameter}：{reason}";
            return 1;
        }

        private int ClearError()
        {
            m_LastError = string.Empty;
            return 0;
        }
    }

    /// <summary>
    /// 图（托管糖层对象，design D6/D11/D13）：在生成的 `GPUGraph` 表面之上维护"提交前计划"——
    /// 缓冲登记表、顶点流、每个 pass 的记录与参数结构、呈现清单。提交时把计划落成原生 pass
    /// （`AddPass(RenderPass)` 的序列化块），再做完整性校验，最后才调原生执行入口。
    ///
    /// 为什么要先计划后落图：原生 `GPUGraph` 没有"改既有 pass 参数"的可绑通道（`GetRenderPassForWrite`
    /// 按出参方向排除），而脚本 API 的时序是"先 AddTrianglePass、后 SetPassParam"。把参数记在
    /// 托管计划里、提交时一起序列化，语义与退役前适配层"事后补写既有 pass"逐项等价。
    /// </summary>
    internal sealed class ScriptRenderGraph : IDisposable
    {
        private sealed class PassPlan
        {
            public RenderPass Record;
            public Dictionary<string, ScriptShaderStruct> Params =
                new Dictionary<string, ScriptShaderStruct>();
        }

        private readonly GPUGraph m_Graph;
        private readonly ScriptRenderBackend m_Backend;
        private readonly Dictionary<string, GPUBufferDescriptor> m_Buffers =
            new Dictionary<string, GPUBufferDescriptor>();
        private readonly Dictionary<string, VertexInputsDescriptor> m_Streams =
            new Dictionary<string, VertexInputsDescriptor>();
        private readonly List<PassPlan> m_Passes = new List<PassPlan>();
        private readonly List<ImageHandle> m_Presents = new List<ImageHandle>();
        private int m_PresentCount;
        private bool m_Materialized;
        private string m_LastError = string.Empty;

        internal ScriptRenderGraph(GPUGraph graph, ScriptRenderBackend backend)
        {
            m_Graph = graph;
            m_Backend = backend;
        }

        internal GPUGraph Native => m_Graph;

        /// <summary>图对象句柄（测试侧按它解析原生 `GPUGraph`）。</summary>
        internal ulong Token => (ulong)(long)m_Graph.Token;

        /// <summary>原生图对象的地址（**只给测试读数**：对象由租约/target 句柄强持有，测试期间稳定）。</summary>
        internal ulong NativeSelf => (ulong)(long)m_Graph.NativeSelf;

        internal bool Materialized => m_Materialized;

        public string GetLastError()
        {
            return m_LastError;
        }

        /// <summary>
        /// 登记并上传一个图内缓冲（字节数组 → `AllocBuffer` + 上传糖 `ScheduleData(handle, byte[])`）。
        /// 校验：名称非空/唯一、描述非零且不溢出、字节数与 `count * stride` 一致。
        /// </summary>
        public int AddBuffer(string name, GPUBufferDescriptor descriptor, byte[] data)
        {
            if (string.IsNullOrEmpty(name))
            {
                return Fail("name", "缓冲名称不能为空");
            }
            if (descriptor.count == 0UL || descriptor.stride == 0UL)
            {
                return Fail("descriptor", "缓冲描述 count / stride 必须非零");
            }
            if (descriptor.count > ulong.MaxValue / descriptor.stride)
            {
                return Fail("descriptor", "缓冲描述的 count * stride 溢出");
            }
            if (m_Buffers.ContainsKey(name))
            {
                return Fail("name", "同一张图里缓冲名称必须唯一");
            }
            if (data == null)
            {
                return Fail("data", "字节数组不能为空");
            }
            ulong expectedBytes = descriptor.count * descriptor.stride;
            if (expectedBytes != (ulong)data.Length)
            {
                return Fail("data", $"字节数组长度与描述里的 SizeInByte 不一致：期望 {expectedBytes} 字节，实际 {data.Length} 字节");
            }
            try
            {
                BufferHandle handle = BufferHandle.FromName(name);
                m_Graph.AllocBuffer(handle, descriptor);
                ScriptUpload.ScheduleData(m_Graph, handle, data);
            }
            catch (Exception ex)
            {
                return Fail("data", ex.Message);
            }
            m_Buffers[name] = descriptor;
            ++m_Backend.AddBufferCount;
            return ClearError();
        }

        /// <summary>`POSITION` + `COLOR` 两个 `float3` 属性；任一 `offset + 12` 越过步长即拒绝。</summary>
        public int SetPositionColorStream(string streamName, uint stride, uint positionOffset, uint colorOffset)
        {
            if (string.IsNullOrEmpty(streamName))
            {
                return Fail("streamName", "顶点流名称不能为空");
            }
            if (stride == 0u)
            {
                return Fail("stride", "步长必须非零");
            }
            if (positionOffset > stride || stride - positionOffset < 12u)
            {
                return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
            }
            if (colorOffset > stride || stride - colorOffset < 12u)
            {
                return Fail("colorOffset", "color 属性越过步长（offset + 12 > stride）");
            }
            if (m_Streams.ContainsKey(streamName))
            {
                return Fail("streamName", "同一张图里流名必须唯一（这次是重复登记）");
            }
            m_Streams[streamName] = new VertexInputsDescriptor
            {
                stride = stride,
                perInstance = false,
                attributes = new VertexAttribute[]
                {
                    new VertexAttribute
                    {
                        offset = positionOffset, format = VertexInputFormat.eR32G32B32_SFloat,
                        semanticName = "POSITION", sematicIndex = 0u,
                    },
                    new VertexAttribute
                    {
                        offset = colorOffset, format = VertexInputFormat.eR32G32B32_SFloat,
                        semanticName = "COLOR", sematicIndex = 0u,
                    },
                },
            };
            return ClearError();
        }

        /// <summary>单 `POSITION` `float3` 流（常量色着色器只要位置）。</summary>
        public int SetPositionStream(string streamName, uint stride, uint positionOffset)
        {
            if (string.IsNullOrEmpty(streamName))
            {
                return Fail("streamName", "顶点流名称不能为空");
            }
            if (stride == 0u)
            {
                return Fail("stride", "步长必须非零");
            }
            if (positionOffset > stride || stride - positionOffset < 12u)
            {
                return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
            }
            if (m_Streams.ContainsKey(streamName))
            {
                return Fail("streamName", "同一张图里流名必须唯一（这次是重复登记）");
            }
            m_Streams[streamName] = new VertexInputsDescriptor
            {
                stride = stride,
                perInstance = false,
                attributes = new VertexAttribute[]
                {
                    new VertexAttribute
                    {
                        offset = positionOffset, format = VertexInputFormat.eR32G32B32_SFloat,
                        semanticName = "POSITION", sematicIndex = 0u,
                    },
                },
            };
            return ClearError();
        }

        /// <summary>
        /// 单 `POSITION` `float3` + 单 `TEXCOORD` `float2` 流：`TEXCOORD` 的顶点格式是
        /// `eR32G32_SFloat`（8 字节），不是 12 字节的三分量格式。
        /// </summary>
        public int SetPositionTextureStream(string streamName, uint stride, uint positionOffset,
            uint texcoordOffset)
        {
            if (string.IsNullOrEmpty(streamName))
            {
                return Fail("streamName", "顶点流名称不能为空");
            }
            if (stride == 0u)
            {
                return Fail("stride", "步长必须非零");
            }
            if (positionOffset > stride || stride - positionOffset < 12u)
            {
                return Fail("positionOffset", "position 属性越过步长（offset + 12 > stride）");
            }
            if (texcoordOffset > stride || stride - texcoordOffset < 8u)
            {
                return Fail("texcoordOffset", "texcoord 属性越过步长（offset + 8 > stride）");
            }
            if (m_Streams.ContainsKey(streamName))
            {
                return Fail("streamName", "同一张图里流名必须唯一（这次是重复登记）");
            }
            m_Streams[streamName] = new VertexInputsDescriptor
            {
                stride = stride,
                perInstance = false,
                attributes = new VertexAttribute[]
                {
                    new VertexAttribute
                    {
                        offset = positionOffset, format = VertexInputFormat.eR32G32B32_SFloat,
                        semanticName = "POSITION", sematicIndex = 0u,
                    },
                    new VertexAttribute
                    {
                        offset = texcoordOffset, format = VertexInputFormat.eR32G32_SFloat,
                        semanticName = "TEXCOORD", sematicIndex = 0u,
                    },
                },
            };
            return ClearError();
        }

        /// <summary>
        /// 向图里追加一个三角 pass（窗口与离屏各一份同逻辑绘制的"像素验证镜像"，不是通用多 pass）。
        /// 校验：目标非空、着色器路径非空、顶点流/缓冲登记过、顶点数非零且不超过缓冲元素数。
        /// 记录里同时带清除色、着色器路径、顶点流描述与绘制调用。
        ///
        /// 这是 additive 默认重载（editor-window-panel 1.7）：既有调用保持"整张目标"的视口/剪裁矩形，
        /// 17 个既有用例的绘制路径不变；编辑器 scene view 用下面的带矩形重载把绘制限制在左侧视口区域。
        /// </summary>
        public int AddTrianglePass(ScriptRenderTarget target, string shaderPath, string streamName,
            string bufferName, uint vertexCount, float red, float green, float blue, float alpha)
        {
            if (target == null)
            {
                return Fail("target", "渲染目标不能为空");
            }
            return AddTrianglePass(target, shaderPath, streamName, bufferName, vertexCount, red, green, blue, alpha,
                0, 0, (int)target.Width, (int)target.Height);
        }

        /// <summary>
        /// 带视口矩形的三角 pass（editor-window-panel 1.7）：批级与绘制级的视口/剪裁矩形同设为
        /// `(x, y, width, height)`。零矩形会把执行器默认的整目标视口覆盖成 0×0（见下面的既有注释），
        /// 所以原点必须非负、宽高必须 &gt; 0；上界不在这里判 —— 窗口改尺寸的瞬间后台缓冲尺寸可能还没更新，
        /// 让执行器按它自己的裁剪语义处理。
        /// </summary>
        public int AddTrianglePass(ScriptRenderTarget target, string shaderPath, string streamName,
            string bufferName, uint vertexCount, float red, float green, float blue, float alpha,
            int viewportX, int viewportY, int viewportWidth, int viewportHeight)
        {
            if (target == null)
            {
                return Fail("target", "渲染目标不能为空");
            }
            if (viewportX < 0 || viewportY < 0 || viewportWidth <= 0 || viewportHeight <= 0)
            {
                return Fail("viewport", "视口矩形必须原点非负、宽高 > 0");
            }
            if (string.IsNullOrEmpty(shaderPath))
            {
                return Fail("shaderPath", "着色器路径不能为空");
            }
            if (streamName == null || !m_Streams.TryGetValue(streamName, out VertexInputsDescriptor vertices))
            {
                return Fail("streamName", "顶点流没有登记过");
            }
            if (bufferName == null || !m_Buffers.TryGetValue(bufferName, out GPUBufferDescriptor buffer))
            {
                return Fail("bufferName", "缓冲没有登记过");
            }
            if (vertexCount == 0u)
            {
                return Fail("vertexCount", "绘制顶点数必须非零");
            }
            if ((ulong)vertexCount > buffer.count)
            {
                return Fail("vertexCount", "绘制顶点数超过缓冲描述的元素数");
            }

            RenderPass record = new RenderPass
            {
                m_PipelineStates = ScriptPipelineDefaults.CreatePipelineDesc(shaderPath),
                shaderStructs = new Dictionary<string, ShaderStruct>(),
                m_AttachmentConfigs = new[]
                {
                    new AttachmentConfig
                    {
                        loadOp = EAttachmentLoadOp.eClear,
                        storeOp = EAttachmentStoreOp.eStore,
                        clearValue = ScriptPipelineDefaults.ClearColorBytes(red, green, blue, alpha),
                    },
                },
                m_DrawCallBatches = new[]
                {
                    new DrawCallBatch
                    {
                        // 批级与绘制级都要给**同一个**视口/剪裁矩形（默认重载传整张目标，编辑器传左侧
                        // 视口区域）：序列化块反序列化出来的 `HashObj<ViewRectData>` 恒为"有效"，写零矩形
                        // 会把执行器默认的整目标视口覆盖成 0×0（一个像素都画不出来）。两后端对这个矩形
                        // 的原点约定一致（左上）。
                        pipelineStateDesc = ScriptPipelineDefaults.CreatePipelineDesc(shaderPath),
                        m_DrawCalls = new[]
                        {
                            new DrawCall
                            {
                                m_BoundVertexBuffers = new Dictionary<string, BufferHandle>
                                {
                                    [bufferName] = BufferHandle.FromName(bufferName),
                                },
                                // 原生 `DrawCall::New()` 的索引缓冲是默认构造的 Invalid 句柄：
                                // 非索引绘制不会读它，但记录字段必须能表达"未设置"。
                                m_IndexBufferData = new IndexBufferData
                                {
                                    indexBufferHandle = BufferHandle.Invalid,
                                    indexBufferType = EIndexBufferType.e16,
                                    indexBufferOffset = 0u,
                                },
                                m_DrawInfo = new DrawInfo
                                {
                                    drawIndexed = false,
                                    indexOffset = 0u,
                                    indexCount = 0u,
                                    vertexOffset = 0u,
                                    vertexCount = vertexCount,
                                    firstInstanceID = 0u,
                                    instanceCount = 1u,
                                },
                                m_ViewPort = new ViewRectData
                                {
                                    x = viewportX, y = viewportY, width = viewportWidth, height = viewportHeight,
                                },
                                m_Sissor = new ViewRectData
                                {
                                    x = viewportX, y = viewportY, width = viewportWidth, height = viewportHeight,
                                },
                            },
                        },
                        shaderStructs = new Dictionary<string, ShaderStruct>(),
                        m_VertexInputDescs = new Dictionary<string, VertexInputsDescriptor>
                        {
                            [streamName] = vertices,
                        },
                        m_ViewPort = new ViewRectData
                        {
                            x = viewportX, y = viewportY, width = viewportWidth, height = viewportHeight,
                        },
                        m_Sissor = new ViewRectData
                        {
                            x = viewportX, y = viewportY, width = viewportWidth, height = viewportHeight,
                        },
                    },
                },
                m_Attachments = new[] { target.Image },
                m_HasDepthAttachment = false,
                m_Name = string.Empty,
            };
            m_Passes.Add(new PassPlan { Record = record });
            return ClearError();
        }

        /// <summary>
        /// 索引绘制 pass（csharp-resource-asset-foundation 4.3）：与 `AddTrianglePass` 同一份管线 /
        /// 附件 / 参数结构，区别只有两处 —— `DrawInfo.drawIndexed = true` 与
        /// `DrawCall.m_IndexBufferData` 指向本图里的索引缓冲（`e32`，偏移 0）。
        /// 索引缓冲必须已经用 `AddBuffer` 登记（count × stride，stride 必须是 4，索引是 uint32）。
        /// </summary>
        public int AddIndexedTrianglePass(ScriptRenderTarget target, string shaderPath, string streamName,
            string vertexBufferName, string indexBufferName, uint indexCount, float red, float green,
            float blue, float alpha)
        {
            if (target == null)
            {
                return Fail("target", "渲染目标不能为空");
            }
            if (string.IsNullOrEmpty(shaderPath))
            {
                return Fail("shaderPath", "着色器路径不能为空");
            }
            if (streamName == null || !m_Streams.TryGetValue(streamName, out VertexInputsDescriptor vertices))
            {
                return Fail("streamName", "顶点流没有登记过");
            }
            if (vertexBufferName == null || !m_Buffers.ContainsKey(vertexBufferName))
            {
                return Fail("vertexBufferName", "顶点缓冲没有登记过");
            }
            if (indexBufferName == null || !m_Buffers.TryGetValue(indexBufferName, out GPUBufferDescriptor indexBuffer))
            {
                return Fail("indexBufferName", "索引缓冲没有登记过");
            }
            if (indexBuffer.stride != 4UL)
            {
                return Fail("indexBufferName",
                    $"索引缓冲的 stride 是 {indexBuffer.stride}，期望 4（uint32 索引）");
            }
            if (indexCount == 0u)
            {
                return Fail("indexCount", "索引绘制数量必须非零");
            }
            if ((ulong)indexCount > indexBuffer.count)
            {
                return Fail("indexCount", "索引绘制数量超过索引缓冲描述的元素数");
            }
            RenderPass record = new RenderPass
            {
                m_PipelineStates = ScriptPipelineDefaults.CreatePipelineDesc(shaderPath),
                shaderStructs = new Dictionary<string, ShaderStruct>(),
                m_AttachmentConfigs = new[]
                {
                    new AttachmentConfig
                    {
                        loadOp = EAttachmentLoadOp.eClear,
                        storeOp = EAttachmentStoreOp.eStore,
                        clearValue = ScriptPipelineDefaults.ClearColorBytes(red, green, blue, alpha),
                    },
                },
                m_DrawCallBatches = new[]
                {
                    new DrawCallBatch
                    {
                        pipelineStateDesc = ScriptPipelineDefaults.CreatePipelineDesc(shaderPath),
                        m_DrawCalls = new[]
                        {
                            new DrawCall
                            {
                                m_BoundVertexBuffers = new Dictionary<string, BufferHandle>
                                {
                                    [streamName] = BufferHandle.FromName(vertexBufferName),
                                },
                                // 索引绘制必须给有效索引缓冲（e32 = 4 字节 uint32 索引）。
                                m_IndexBufferData = new IndexBufferData
                                {
                                    indexBufferHandle = BufferHandle.FromName(indexBufferName),
                                    indexBufferType = EIndexBufferType.e32,
                                    indexBufferOffset = 0u,
                                },
                                m_DrawInfo = new DrawInfo
                                {
                                    drawIndexed = true,
                                    indexOffset = 0u,
                                    indexCount = indexCount,
                                    // 索引绘制不读 vertexCount（两后端的 DrawIndexed* 都用索引数）。
                                    vertexOffset = 0u,
                                    vertexCount = 0u,
                                    firstInstanceID = 0u,
                                    instanceCount = 1u,
                                },
                                m_ViewPort = new ViewRectData
                                {
                                    x = 0, y = 0, width = (int)target.Width, height = (int)target.Height,
                                },
                                m_Sissor = new ViewRectData
                                {
                                    x = 0, y = 0, width = (int)target.Width, height = (int)target.Height,
                                },
                            },
                        },
                        shaderStructs = new Dictionary<string, ShaderStruct>(),
                        m_VertexInputDescs = new Dictionary<string, VertexInputsDescriptor>
                        {
                            [streamName] = vertices,
                        },
                        m_ViewPort = new ViewRectData
                        {
                            x = 0, y = 0, width = (int)target.Width, height = (int)target.Height,
                        },
                        m_Sissor = new ViewRectData
                        {
                            x = 0, y = 0, width = (int)target.Width, height = (int)target.Height,
                        },
                    },
                },
                m_Attachments = new[] { target.Image },
                m_HasDepthAttachment = false,
                m_Name = string.Empty,
            };
            m_Passes.Add(new PassPlan { Record = record });
            return ClearError();
        }

        /// <summary>
        /// 把参数结构绑定到第 passIndex 个 pass 的参数块名。校验：pass 下标、参数非空、
        /// 着色器根结构里的块名、块类型与参数结构类型名一致。真正写入在提交时随记录一起序列化。
        /// </summary>
        public int SetPassParam(uint passIndex, string blockName, ScriptShaderStruct parameters)
        {
            if (parameters == null)
            {
                return Fail("params", "参数结构不能为空");
            }
            if (passIndex >= (uint)m_Passes.Count)
            {
                return Fail("passIndex",
                    $"pass 下标越界：pass 数 {m_Passes.Count}，实际下标 {passIndex}");
            }
            PassPlan plan = m_Passes[(int)passIndex];
            string shaderPath = plan.Record.m_PipelineStates.m_ShaderInfo.path;
            ShaderStructData rootStruct = m_Backend.FindRootStructData(shaderPath);
            if (rootStruct == null)
            {
                return Fail("shaderPath", $"着色器路径没有登记的根结构：{shaderPath}");
            }
            SubStructReference? reference = null;
            if (rootStruct.m_SubStructReferences != null)
            {
                foreach (SubStructReference candidate in rootStruct.m_SubStructReferences)
                {
                    if (candidate.m_Name == blockName)
                    {
                        reference = candidate;
                        break;
                    }
                }
            }
            if (reference == null)
            {
                return Fail("blockName", $"参数块不在该 pass 着色器的根结构里：{blockName}");
            }
            if (parameters.TypeName != reference.Value.m_StructTypeName)
            {
                return Fail("params",
                    $"参数块类型不符：期望 {reference.Value.m_StructTypeName}，实际 {parameters.TypeName}");
            }
            plan.Params[blockName] = parameters;
            ++m_Backend.SetPassParamCount;
            return ClearError();
        }

        /// <summary>只接受后台缓冲目标（Present 的目标类型校验在托管层，原生 `Present` 对非后台缓冲静默忽略）。</summary>
        public int Present(ScriptRenderTarget target)
        {
            if (target == null)
            {
                return Fail("target", "渲染目标不能为空");
            }
            if (!target.IsBackbuffer())
            {
                return Fail("target", "Present 只接受后台缓冲目标");
            }
            m_Presents.Add(target.Image);
            ++m_PresentCount;
            return ClearError();
        }

        /// <summary>呈现目标个数（读数用；原生 `Present` 对非后台缓冲会静默忽略，这里在托管层先挡住）。</summary>
        internal int PresentCount => m_PresentCount;

        /// <summary>某个 pass 的某个字段最终解析到的缓冲名（提交前置校验的读数口）。</summary>
        internal string GetBoundBufferName(int passIndex, string field)
        {
            if (passIndex < 0 || passIndex >= m_Passes.Count)
            {
                return string.Empty;
            }
            foreach (KeyValuePair<string, ScriptShaderStruct> pair in m_Passes[passIndex].Params)
            {
                if (pair.Value.HasBoundBuffer(field))
                {
                    return pair.Value.GetBoundBufferName(field);
                }
            }
            return string.Empty;
        }

        /// <summary>某个 pass 的某个字段最终解析到的纹理对象句柄（0 = 没绑）。</summary>
        internal ulong GetBoundImageToken(int passIndex, string field)
        {
            if (passIndex < 0 || passIndex >= m_Passes.Count)
            {
                return 0UL;
            }
            foreach (KeyValuePair<string, ScriptShaderStruct> pair in m_Passes[passIndex].Params)
            {
                ScriptTexture texture = pair.Value.GetBoundImage(field);
                if (texture != null)
                {
                    return texture.Token;
                }
            }
            return 0UL;
        }

        /// <summary>
        /// pass 数量（render pass 的计划条数）。提交后与原生 `GetRenderPasses().size()` 一致；
        /// design D13 的容器读取通道（`GetPassIndices`）数的是**图阶段**（含数据上传阶段），
        /// 不是 render pass 数，所以这里不拿它当 pass 计数。
        /// </summary>
        public uint GetPassCount()
        {
            return (uint)m_Passes.Count;
        }

        /// <summary>
        /// design D13 的 `PassCount` 糖：`GetPassIndices().Length`（count + 定长拷贝两段式）。
        /// 注意它数的是**图阶段下标**：`ScheduleData` 的字节上传阶段也会占一个，正常运行时
        /// 比 render pass 数多。
        /// </summary>
        internal uint PassCount => m_Materialized ? (uint)m_Graph.GetPassIndices().Length : 0u;

        /// <summary>
        /// 把计划落成原生图：逐 pass `AddPass(RenderPass)`（参数结构随记录的 `shaderStructs` 一起过界）、
        /// 逐呈现目标 `Present`。只在提交时做一次。
        /// </summary>
        internal int Materialize()
        {
            if (m_Materialized)
            {
                return Fail("graph", "图已经提交过，不能重复提交");
            }
            try
            {
                foreach (PassPlan plan in m_Passes)
                {
                    plan.Record.shaderStructs.Clear();
                    foreach (KeyValuePair<string, ScriptShaderStruct> pair in plan.Params)
                    {
                        plan.Record.shaderStructs[pair.Key] = pair.Value.Native;
                    }
                    m_Graph.AddPass(plan.Record);
                }
                foreach (ImageHandle present in m_Presents)
                {
                    m_Graph.Present(present);
                }
            }
            catch (Exception ex)
            {
                return Fail("graph", ex.Message);
            }
            m_Materialized = true;
            return ClearError();
        }

        /// <summary>提交前置校验的访问口：每个 pass 的着色器路径、参数表、以及计划里的缓冲登记表。</summary>
        internal IReadOnlyList<(string ShaderPath, Dictionary<string, ScriptShaderStruct> Params)> PassPlans()
        {
            var result = new List<(string, Dictionary<string, ScriptShaderStruct>)>(m_Passes.Count);
            foreach (PassPlan plan in m_Passes)
            {
                result.Add((plan.Record.m_PipelineStates.m_ShaderInfo.path, plan.Params));
            }
            return result;
        }

        internal bool HasBuffer(string name) => m_Buffers.ContainsKey(name);

        public void Dispose()
        {
            m_Graph.Dispose();
        }

        private int Fail(string parameter, string reason)
        {
            m_LastError = $"ScriptRenderGraph.{parameter}：{reason}";
            return 1;
        }

        private int ClearError()
        {
            m_LastError = string.Empty;
            return 0;
        }
    }

    /// <summary>
    /// 后端的托管糖层（design D6/D12）：窗口/捕获目标、图、纹理、参数结构的创建入口，以及
    /// **提交前置校验 + 原生执行入口** 的唯一落点。所有计数都是"进入原生实现之前"的托管读数，
    /// 供拒绝用例断言"原生执行计数增量 0"。
    /// </summary>
    internal sealed class ScriptRenderBackend : IDisposable
    {
        private readonly CRenderBackend m_Native;
        private string m_LastError = string.Empty;

        internal ScriptRenderBackend(CRenderBackend native)
        {
            m_Native = native;
        }

        internal CRenderBackend Native => m_Native;

        // ---- 调用计数（与退役前适配器的计数口径一致，供探针读数）----
        internal int CreateShaderStructCount;
        internal int CreateCaptureTargetCount;
        internal int CreateTextureCount;
        /// <summary>7.1：后端直建缓冲的调用次数（与图内 `AddBufferCount` 分开计）。</summary>
        internal int CreateBufferCount;
        internal int TextureUploadExecuteCount;
        internal int ExecuteGraphCount;
        internal int WaitIdleCount;
        internal int AddBufferCount;
        internal int SetPassParamCount;
        internal int SetFloat3Count;
        internal int SetStructCount;
        internal int SetBufferCount;
        internal int SetImageCount;
        internal int SetSamplerCount;

        public string GetLastError()
        {
            return m_LastError;
        }

        /// <summary>窗口目标（design D12）：窗口句柄 + 尺寸 + 按窗口句柄构造的后台缓冲引用。</summary>
        public ScriptRenderTarget GetWindowTarget(IWindow window)
        {
            if (window == null)
            {
                Fail("window", "窗口不能为空");
                return null;
            }
            try
            {
                WindowHandle handle = m_Native.GetWindowHandle(window);
                if (handle == null)
                {
                    Fail("window", "后端没有返回窗口句柄");
                    return null;
                }
                uint2 size = handle.GetSizeSafe();
                if (!handle.IsValid() || size.x == 0u || size.y == 0u)
                {
                    handle.Dispose();
                    Fail("window", "窗口后台缓冲尺寸为零或句柄无效");
                    return null;
                }
                ClearError();
                return new ScriptRenderTarget(ImageHandle.FromWindowHandle(handle), size.x, size.y, true,
                    null, handle);
            }
            catch (Exception ex)
            {
                Fail("window", ex.Message);
                return null;
            }
        }

        /// <summary>离屏捕获目标（design D12）：后端纹理 + 按纹理句柄构造的引用 + 托管记尺寸。</summary>
        public ScriptRenderTarget CreateCaptureTarget(uint width, uint height)
        {
            if (width == 0u || height == 0u)
            {
                Fail("width", "离屏目标尺寸必须非零");
                return null;
            }
            try
            {
                // 固定使用当前原生验证路径的 RGBA8、二维、单层、单 mip、单采样。
                GPUTextureDescriptor descriptor = new GPUTextureDescriptor
                {
                    format = ETextureFormat.E_R8G8B8A8_UNORM,
                    textureType = ETextureType.e2D,
                    width = width,
                    height = height,
                    layers = 1u,
                    mipLevels = 1u,
                    samples = EMultiSampleCount.e1,
                };
                GPUTexture texture = m_Native.CreateGPUTexture(descriptor,
                    AccessFlags(ETextureAccessType.eTransferSrc, ETextureAccessType.eTransferDst,
                        ETextureAccessType.eRT));
                if (texture == null)
                {
                    Fail("width", "后端拒绝了离屏纹理创建");
                    return null;
                }
                ++CreateCaptureTargetCount;
                ClearError();
                return new ScriptRenderTarget(ImageHandle.FromGPUTexture(texture), width, height, false,
                    texture, null);
            }
            catch (Exception ex)
            {
                Fail("width", ex.Message);
                return null;
            }
        }

        /// <summary>新建一张图：原生 `GPUGraph` 的隐式公开默认构造（生成的 `GPUGraph.Create`）。</summary>
        public ScriptRenderGraph CreateGraph()
        {
            try
            {
                ScriptRenderGraph graph = new ScriptRenderGraph(GPUGraph.Create(), this);
                ClearError();
                return graph;
            }
            catch (Exception ex)
            {
                Fail("graph", ex.Message);
                return null;
            }
        }

        /// <summary>
        /// 一次调用完成「创建 RGBA8 纹理 → 图内上传字节 → 转采样态 → 同步等待」。
        /// 字节数只认 `width * height * 4`（64 位算，不用 32 位乘）；零尺寸、字节数不符、重名、
        /// 底层创建失败都返回空并由 `GetLastError()` 点名原因；上传的执行与等待**不**计入
        /// 脚本提交路径的 `ExecuteGraphCount` / `WaitIdleCount`（单列 `TextureUploadExecuteCount`）。
        /// </summary>
        public ScriptTexture CreateTexture(string name, uint width, uint height, byte[] rgba)
        {
            if (string.IsNullOrEmpty(name))
            {
                Fail("name", "纹理名称不能为空");
                return null;
            }
            if (width == 0u || height == 0u)
            {
                Fail("width", "纹理尺寸必须非零");
                return null;
            }
            ulong expectedBytes = (ulong)width * height * 4UL;
            ulong receivedBytes = rgba == null ? 0UL : (ulong)rgba.Length;
            if (expectedBytes != receivedBytes)
            {
                Fail("rgba",
                    $"字节数与 GetImageByteSize 不一致：期望 {expectedBytes} 字节（RGBA8，宽 {width} × 高 {height}），实际收到 {receivedBytes} 字节");
                return null;
            }
            // 纹理重名登记：与退役前适配器的同一张表同义（进程内一张，纹理不随图销毁）。
            if (s_TextureNames.Contains(name))
            {
                Fail("name", $"同名纹理已经创建过：{name}");
                return null;
            }
            try
            {
                GPUTextureDescriptor descriptor = new GPUTextureDescriptor
                {
                    format = ETextureFormat.E_R8G8B8A8_UNORM,
                    textureType = ETextureType.e2D,
                    width = width,
                    height = height,
                    layers = 1u,
                    mipLevels = 1u,
                    samples = EMultiSampleCount.e1,
                };
                GPUTexture texture = m_Native.CreateGPUTexture(descriptor,
                    AccessFlags(ETextureAccessType.eTransferDst, ETextureAccessType.eSampled));
                if (texture == null)
                {
                    Fail("name", $"后端没有创建出纹理：{name}");
                    return null;
                }
                // 内部上传图：不进脚本的图登记表、不含任何渲染 pass。
                using (GPUGraph uploadGraph = GPUGraph.Create())
                {
                    ImageHandle image = ImageHandle.FromGPUTexture(texture);
                    ScriptUpload.ScheduleData(uploadGraph, image, rgba);
                    uploadGraph.Finalize(image, ETextureAccessType.eSampled);
                    ++TextureUploadExecuteCount;
                    m_Native.ExecuteGraph(uploadGraph);
                    m_Native.WaitIdle();
                }
                ++CreateTextureCount;
                s_TextureNames.Add(name);
                ClearError();
                return new ScriptTexture(texture, width, height);
            }
            catch (Exception ex)
            {
                Fail("name", ex.Message);
                return null;
            }
        }

        /// <summary>
        /// 后端直建缓冲（7.1）：返回的 `ScriptBuffer` 持有生成的 `GPUBuffer` 表面，生命周期跟随后端
        /// `shared_ptr`，不跟任何一张图。描述符非零且不溢出；创建失败返回空并由 `GetLastError()` 点名。
        /// </summary>
        public ScriptBuffer CreateBuffer(GPUBufferDescriptor descriptor, EBufferUsage usageFlags)
        {
            if (descriptor.count == 0UL || descriptor.stride == 0UL)
            {
                Fail("descriptor", "缓冲描述 count / stride 必须非零");
                return null;
            }
            if (descriptor.count > ulong.MaxValue / descriptor.stride)
            {
                Fail("descriptor", "缓冲描述的 count * stride 溢出");
                return null;
            }
            try
            {
                GPUBuffer buffer = m_Native.CreateGPUBuffer(descriptor, usageFlags);
                if (buffer == null)
                {
                    Fail("descriptor", "后端拒绝了缓冲创建");
                    return null;
                }
                ++CreateBufferCount;
                ClearError();
                return new ScriptBuffer(buffer, descriptor);
            }
            catch (Exception ex)
            {
                Fail("descriptor", ex.Message);
                return null;
            }
        }

        /// <summary>按类型名创建参数结构：未知类型先在元数据查询处挡下，MUST NOT 进后端创建路径。</summary>
        public ScriptShaderStruct CreateShaderStruct(string structTypeName)
        {
            if (string.IsNullOrEmpty(structTypeName))
            {
                Fail("structTypeName", "参数结构类型名不能为空");
                return null;
            }
            try
            {
                ShaderStructData data = m_Native.FindShaderStructData(structTypeName);
                if (data == null)
                {
                    Fail("structTypeName", $"类型名没有编译元数据（不是任何着色器结构）：{structTypeName}");
                    return null;
                }
                ShaderStruct shaderStruct = m_Native.CreateShaderStruct(structTypeName);
                if (shaderStruct == null)
                {
                    Fail("structTypeName", $"后端没有创建出参数结构：{structTypeName}");
                    return null;
                }
                ++CreateShaderStructCount;
                ClearError();
                return new ScriptShaderStruct(shaderStruct, data, structTypeName, this);
            }
            catch (Exception ex)
            {
                Fail("structTypeName", ex.Message);
                return null;
            }
        }

        /// <summary>
        /// 提交：先落图（AddPass/Present），再按 design D6 做完整性校验（必需参数块 → 结构化缓冲
        /// 引用 → 纹理/采样器绑定），全部通过之后才调用原生执行入口。被拒时 `ExecuteGraphCount`
        /// 不增长。校验数据源是引擎编译元数据的两段式只读副本（`FindShaderStructData` 系）。
        ///
        /// editor-window-panel 1.7（additive）：图**已落过盘**时跳过 `Materialize`、仍然做完整校验
        /// 并再执行一次 —— 原生 `GPUGraph` 允许重复 `ExecuteGraph`（`TestSimpleTriangle` 就是每帧执行
        /// 同一张图），编辑器的 scene view 靠这一点每帧复用同一张图；既有调用只提交一次，行为不变。
        /// </summary>
        public int SubmitGraph(ScriptRenderGraph graph)
        {
            if (graph == null)
            {
                return Fail("graph", "提交的图不能为空");
            }
            if (graph.GetPassCount() == 0u)
            {
                return Fail("graph", "空图不能提交（至少需要一个 render pass）");
            }
            if (!graph.Materialized)
            {
                int materialize = graph.Materialize();
                if (materialize != 0)
                {
                    return Fail("graph", graph.GetLastError());
                }
            }
            // 必需参数块：按每个 pass 的着色器根结构核对，必须在进入执行之前完成
            // （两后端在空参数表下执行会触发 CA_ASSERT_BREAK）。
            IReadOnlyList<(string ShaderPath, Dictionary<string, ScriptShaderStruct> Params)> plans =
                graph.PassPlans();
            for (int passIndex = 0; passIndex < plans.Count; ++passIndex)
            {
                ShaderStructData rootStruct = FindRootStructData(plans[passIndex].ShaderPath);
                if (rootStruct == null || rootStruct.m_SubStructReferences == null)
                {
                    continue;
                }
                foreach (SubStructReference reference in rootStruct.m_SubStructReferences)
                {
                    if (!plans[passIndex].Params.TryGetValue(reference.m_Name, out ScriptShaderStruct bound) ||
                        bound == null)
                    {
                        return Fail("blockName",
                            $"pass {passIndex} 的着色器 {plans[passIndex].ShaderPath} 缺少必需参数块：{reference.m_Name}");
                    }
                }
            }
            // 结构化缓冲引用：字段没写过 / 缓冲名不在**本图**登记表都点名拒绝。
            for (int passIndex = 0; passIndex < plans.Count; ++passIndex)
            {
                foreach (KeyValuePair<string, ScriptShaderStruct> pair in plans[passIndex].Params)
                {
                    ShaderStructData data = pair.Value.Data;
                    if (data == null || data.m_Buffers == null)
                    {
                        continue;
                    }
                    foreach (BufferData buffer in data.m_Buffers)
                    {
                        if (!pair.Value.HasBoundBuffer(buffer.m_Name))
                        {
                            return Fail("field",
                                $"pass {passIndex} 的块 {pair.Key} 没有为缓冲字段 {buffer.m_Name} 调 SetBuffer");
                        }
                        string bufferName = pair.Value.GetBoundBufferName(buffer.m_Name);
                        // 7.1：外部缓冲（`ScriptBuffer`）不在本图分配表里，它的生命周期跟随后端，
                        // 跨图复用正是本入口的目的；只对图内按名分配的缓冲要求 HasBuffer。
                        if (!pair.Value.IsExternalBuffer(buffer.m_Name) && !graph.HasBuffer(bufferName))
                        {
                            return Fail("bufferName",
                                $"pass {passIndex} 的字段 {buffer.m_Name} 引用了本图未登记的缓冲：{(bufferName.Length == 0 ? "(空)" : bufferName)}");
                        }
                    }
                }
            }
            // 纹理与采样器：元数据列出的字段必须都写过。
            for (int passIndex = 0; passIndex < plans.Count; ++passIndex)
            {
                foreach (KeyValuePair<string, ScriptShaderStruct> pair in plans[passIndex].Params)
                {
                    ShaderStructData data = pair.Value.Data;
                    if (data == null)
                    {
                        continue;
                    }
                    if (data.m_Textures != null)
                    {
                        foreach (ShaderTextureData texture in data.m_Textures)
                        {
                            if (!pair.Value.HasBoundImage(texture.m_Name))
                            {
                                return Fail("field",
                                    $"pass {passIndex} 的块 {pair.Key} 没有为纹理字段 {texture.m_Name} 调 SetImage");
                            }
                        }
                    }
                    if (data.m_TextureSamplers != null)
                    {
                        foreach (string sampler in data.m_TextureSamplers)
                        {
                            if (!pair.Value.HasBoundSampler(sampler))
                            {
                                return Fail("field",
                                    $"pass {passIndex} 的块 {pair.Key} 没有为采样器字段 {sampler} 调 SetSampler");
                            }
                        }
                    }
                }
            }
            try
            {
                ++ExecuteGraphCount;
                m_Native.ExecuteGraph(graph.Native);
            }
            catch (Exception ex)
            {
                return Fail("graph", ex.Message);
            }
            ClearError();
            return 0;
        }

        public void WaitIdle()
        {
            try
            {
                m_Native.WaitIdle();
                ++WaitIdleCount;
                ClearError();
            }
            catch (Exception ex)
            {
                Fail("wait", ex.Message);
            }
        }

        /// <summary>参数结构元数据的根结构查询（着色器路径 → 根结构记录，未命中为 null）。</summary>
        internal ShaderStructData FindRootStructData(string shaderPath)
        {
            if (string.IsNullOrEmpty(shaderPath))
            {
                return null;
            }
            try
            {
                return m_Native.FindShaderRootStructData(shaderPath);
            }
            catch (Exception)
            {
                return null;
            }
        }

        public void Dispose()
        {
            m_Native.Dispose();
        }

        private static ETextureAccessType AccessFlags(params ETextureAccessType[] values)
        {
            byte mask = 0;
            foreach (ETextureAccessType value in values)
            {
                mask |= (byte)value;
            }
            return (ETextureAccessType)mask;
        }

        private int Fail(string parameter, string reason)
        {
            m_LastError = $"ScriptRenderBackend.{parameter}：{reason}";
            return 1;
        }

        private int ClearError()
        {
            m_LastError = string.Empty;
            return 0;
        }

        // 纹理重名登记：与退役前适配器的同一张表同义（进程内一张，纹理不随图销毁）。
        private static readonly HashSet<string> s_TextureNames = new HashSet<string>();
    }

    /// <summary>字节上传糖（design D2）：`ScheduleData(handle, byte[])` 便利形态，offset 固定 0、复制固定开。</summary>
    internal static class ScriptUpload
    {
        internal static void ScheduleData(GPUGraph graph, BufferHandle handle, byte[] bytes)
        {
            graph.ScheduleData(handle, bytes, 0UL, true);
        }

        internal static void ScheduleData(GPUGraph graph, ImageHandle handle, byte[] bytes)
        {
            graph.ScheduleData(handle, bytes, 0UL, true);
        }
    }

    /// <summary>
    /// 管线的默认值（design D11 的记录图）：C# 侧没有原生默认构造，这里逐字段复写
    /// `CPipelineStateObject.h` 的**默认成员初始化**——漏掉一项都会变形（实测：`channelMask`
    /// 写成 0 会关掉全部颜色写；`cullMode` 写成 0 会变成 `eBack` 把三角形剔掉）。
    /// </summary>
    internal static class ScriptPipelineDefaults
    {
        internal static PipelineDescData CreatePipelineDesc(string shaderPath)
        {
            return new PipelineDescData
            {
                m_ShaderInfo = new ShaderInfo { path = shaderPath ?? string.Empty },
                m_PipelineStates = new CPipelineStateObject
                {
                    depthStencilStates = new DepthStencilStates
                    {
                        depthTestEnable = false,
                        depthWriteEnable = false,
                        // `DepthStencilStates::depthCompareOp = ECompareOp::eGEqual`
                        depthCompareOp = ECompareOp.eGEqual,
                        stencilTestEnable = false,
                        stencilStateFront = CreateStencilStates(),
                        stencilStateBack = CreateStencilStates(),
                    },
                    rasterizationStates = new RasterizerStates
                    {
                        enableDepthClamp = false,
                        discardRasterization = false,
                        // `RasterizerStates::cullMode = ECullMode::eNone`（0 是 eBack）
                        cullMode = ECullMode.eNone,
                        frontFace = EFrontFace.eCounterClockWise,
                        polygonMode = EPolygonMode.eFill,
                        lineWidth = 1.0f,
                    },
                    colorAttachments = new ColorAttachmentsBlendStates
                    {
                        attachmentBlendStates = CreateBlendStates(8),
                    },
                    msCount = EMultiSampleCount.e1,
                },
                m_InputAssemblyStates = new InputAssemblyStates { topology = ETopology.eTriangleList },
                m_Viewport = new RectSate { x = 0u, y = 0u, width = 0u, height = 0u },
                m_Scissor = new RectSate { x = 0u, y = 0u, width = 0u, height = 0u },
            };
        }

        private static StencilStates CreateStencilStates()
        {
            return new StencilStates
            {
                failOp = EStencilOp.eKeep,
                passOp = EStencilOp.eKeep,
                depthFailOp = EStencilOp.eKeep,
                compareOp = ECompareOp.eAlways,
                compareMask = 0u,
                writeMask = 0u,
                reference = 0u,
            };
        }

        private static SingleColorAttachmentBlendStates[] CreateBlendStates(int count)
        {
            SingleColorAttachmentBlendStates[] states = new SingleColorAttachmentBlendStates[count];
            for (int index = 0; index < count; ++index)
            {
                states[index] = new SingleColorAttachmentBlendStates
                {
                    blendEnable = false,
                    // `SingleColorAttachmentBlendStates::channelMask = EColorChannelMask::eRGBA`
                    channelMask = EColorChannelMask.eRGBA,
                    sourceColorBlendFactor = EBlendFactor.eOne,
                    destColorBlendFactor = EBlendFactor.eZero,
                    sourceAlphaBlendFactor = EBlendFactor.eOne,
                    destAlphaBlendFactor = EBlendFactor.eZero,
                    colorBlendOp = EBlendOp.eAdd,
                    alphaBlendOp = EBlendOp.eAdd,
                };
            }
            return states;
        }

        /// <summary>`GraphicsClearValue` 的 `ClearColorValue{r,g,b,a}` 四 float 原样 16 字节。</summary>
        internal static byte[] ClearColorBytes(float red, float green, float blue, float alpha)
        {
            byte[] bytes = new byte[16];
            Buffer.BlockCopy(new[] { red, green, blue, alpha }, 0, bytes, 0, 16);
            return bytes;
        }
    }
}
