# csharp-render-structured-buffer 提案

## Why

着色器参数批次（已归档的 `csharp-render-shader-constants`）让 C# 能按编译元数据写 `float3` 与嵌套子结构，但着色器还有一类常用资源——结构化缓冲（`StructuredBuffer`）——只能由 C++ 测试使用：原生 `TestTriangleWithStructuredBufferColor` 已验证"图内缓冲经 `ShaderStruct::SetBuffer` 绑进参数块驱动像素输出"这条链，而 C# 侧的 `AddBuffer` 只能建缓冲、参数结构却引用不到它。`docs/ROADMAP.md` 阶段 4 只列了已归档的三个 change 与后续方向（纹理与采样等，均尚未立 change），**批次顺序是本提案自定的**：结构化缓冲先行，理由是 `SetBuffer` 只扩一个方法、复用全部既有链，而纹理采样要新增 `SetImage`/`SetSampler` 与纹理对象面，扩张更大。这也衔接用户已钉的排期方向（合并 C++ 端渲染接口，ROADMAP 阶段 4 末尾）之前的窄接口批次。

## What Changes

- `IScriptShaderStruct` 追加一个方法 `SetBuffer(NameHash const& field, NameHash const& bufferName)`：把**同一张图**里经 `AddBuffer` 登记的缓冲（按名）绑定为该参数结构的结构化缓冲字段。两个 `NameHash` 都可生成绑定，无需新句柄类型。
- 适配层新增校验（现状两后端 `SetBufferInternal` 对未知字段**静默跳过**、对越界下标写入未检查/中止——都不能暴露给脚本）：字段名必须在 `ShaderStructData::m_Buffers` 元数据表里；访问条目必须只读（`m_RWType == EShaderResourceAccess::eReadOnly`，`eWriteOnly`/`eReadWrite`/`eUnknown` 本批拒绝并点名——读写访问涉及屏障语义，留给计算批）；`bufferName` 必须在**被提交的那张图**已登记（参数结构只记住缓冲名，`SubmitGraph` 提交时按本图缓冲登记表解析并核对，不提前在 `SetPassParam` 拒——原生时序就是先绑后建）；数组字段（元数据 `m_ElementCount != 1`）拒绝。
- 缓冲引用保活与解析时机定死：参数结构只记住 `bufferName`，`SubmitGraph` 提交时按**本图**缓冲登记表解析并核对（与既有必需参数块检查同一预执行段）；缓冲的真实 `GPUBuffer` 由图持有，提交后到 `WaitIdle` 前不得失效。
- 验收探针新增第 15 项 `TestCSharpStructuredBuffer`：C# 提交一个用结构化缓冲驱动颜色的三角形（复用单 `POSITION` 流与既有三角形链），D3D12 与 Vulkan 双后端真实像素判据（缓冲颜色值进像素、改缓冲数据为中心精确变为 `(0,0,255,255)`），负向 `CASCADED_CSHARP_SB_SELFTEST` 三形态：`bad-buffer-color`（缓冲数据改写为中心精确等于 `(0,0,255,255)`）、`unbound-field`（字段未绑缓冲提交）与 `bad-buffer-name`（绑不存在的缓冲名）——后两者各自在适配层点名拒绝、后端执行次数 0。
- 审计表新增 1 行（`IScriptShaderStruct::SetBuffer`），合计 **11 接口 84 方法**；`CSharpHost/README.md` 的探针开关表、`docs/testing.md`（15 项清单）同步；`check_csharp_host_readme.py` 与 `check_bindable_audit.py`/`check_bindings.py` 判据跟着扩。
- 不做：`SetImage`/`SetSampler`（纹理采样批）、`eRWStructuredBuffer` 与计算（计算批）、带索引绘制（几何形态批）、双 pass blit（多阶段批）。

## Capabilities

### New Capabilities

（无——本批全部落在既有能力目录内。）

### Modified Capabilities

- `csharp-rendering`: 参数结构能力面追加"结构化缓冲字段绑定"需求（校验、拒绝语义、保活与解析时机、双后端像素验收）；既有"拒绝必须发生在进入后端之前"与"参数校验与常量色像素验收"两条需求范围随方法面扩宽。
- `csharp-host`: 探针开关清单追加 `CASCADED_CSHARP_SB_SELFTEST`；第 15 项测试进入宿主侧测试清单对拍。

## Impact

- 接口与实现：`Interface/RenderInterface/header/ScriptRenderBridge.h`（`IScriptShaderStruct` +1 方法）、`Interface/RenderInterface/private/ScriptRenderBridge.*`（适配实现与校验）。
- 测试：`Test/CoreTests/ScriptRenderBridgeTest.cpp`（拒绝与正例断言）、`Test/GPUBackendTester/Main.cpp`（第 15 项注册与像素判据）、`Scripting/Managed/UserScript.cs`（探针流程）与 `Scripting/Managed/Host.cs`（开关读取、独立调用标记、Tick 派发与释放登记——缺它探针永远不会被调用）、`Scripting/Managed/SurfaceSignatureCheck.cs`（签名）。
- 工具与文档：`Tools/check_csharp_host_readme.py`、`Tools/CodeGen/check_bindable_audit.py` 与审计表、`Tools/CodeGen/check_bindings.py`、`docs/testing.md`、`CSharpHost/README.md`、新增 `Tools/check_csharp_structured_buffer.py`（报告对账）。
- 生成物：`IScriptShaderStruct` 的绑定产物方法数变化，两侧同构建重生成（`build.py` 的既有对账接住）；**ABI 保持 v7、`EngineApi` 字段不变**——本批没加新接口编号，只在既有接口内加方法，两侧必须一起重生成由构建期一致性判据保证，不是宿主 ABI 面变化。
- 元数据事实源：`Interface/ShaderCompiler/header/Compiler.h` 的 `BufferData`（`m_Name`/`m_RWType`/`m_ElementCount`）与 `ShaderStructData::m_Buffers`；着色器事实源 `CAResources/Shaders/Test/TestTriangleWithStructuredBufferColor.slang`（`StructuredColor { StructuredBuffer<float3> colorStructuredBuffer }`，片元读 `[0]`）。
