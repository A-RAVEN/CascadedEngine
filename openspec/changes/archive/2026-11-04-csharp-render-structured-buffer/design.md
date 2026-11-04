# csharp-render-structured-buffer 设计

## Context

已归档三批（绑定基础、三角形、着色器常量参数）把 C# 到 `GPUGraph` 的窄适配面立起来了：四个 `IScriptRender*` 接口（编号 3/4/5/6）、适配层元数据校验（`FindShaderStructData`/`FindShaderRootStructData` 直读两后端公开表）、提交前必需参数块检查、双后端像素验收与报告对账。本批在 `IScriptShaderStruct` 上开第三个写入口 `SetBuffer`。

读码事实（逐字可 grep）：

- 原生目标链：`Test/GPUBackendTester/Main.cpp` 的 `TestTriangleWithStructuredBufferColor` —— 逐字 `BufferHandle structuredColorBuffer(CANAME("StructuredColorBuffer"));` + `pStructuredColor->SetBuffer(CANAME("colorStructuredBuffer"), structuredColorBuffer);`；图侧逐字 `.AllocBuffer(structuredColorBuffer, GPUBufferDescriptor::Create(1, sizeof(glm::vec3)))` + `.ScheduleData(structuredColorBuffer, &testColor, sizeof(testColor))`（**两参数版 `ScheduleData` 无重载**，照抄三参数模板会编译失败），颜色 `glm::vec3 testColor = glm::vec3(1.0f, 0.0f, 1.0f);`；pass 侧逐字 `AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 1, 0, 1))`；`SetParam(CANAME("structuredColorBlock"), pStructuredColor)` + 着色器 `Shaders/Test/TestTriangleWithStructuredBufferColor`。**先绑参数、后建缓冲**——绑定按名，解析发生在图执行期。
- 着色器：`CAResources/Shaders/Test/TestTriangleWithStructuredBufferColor.slang` —— `StructuredColor { StructuredBuffer<float3> colorStructuredBuffer }`，`ParameterBlock<StructuredColor> structuredColorBlock`，片元返回 `float4(structuredColorBlock.colorStructuredBuffer[0], 1.0f)`。顶点输入单 `POSITION` `float3` —— 既有 `SetPositionStream` 面直接够用。
- 原生写入口语义（两后端）：构造期逐字 `m_NameToBufferHandles[buffer.m_Name].resize(buffer.m_ElementCount);`（`D3D12ShaderStruct.cpp` 与 `VulkanShaderStruct.cpp` 的构造函数内，不在 `SetBufferInternal` 里）；`SetBufferInternal` 对未知字段名**静默跳过**（不写入、不报错，仍 `UpdateVersion()`），对越界下标走 `CA_ASSERT(bufferList.size() > elementIndex, "")` —— `CA_ASSERT` 本身只打日志（`CACore/header/DebugUtils.h` 逐字区分：带 `__debugbreak()` 的是 `CA_ASSERT_BREAK`/`CA_LOG_ERR_BREAK`），但紧随其后的 `bufferList[elementIndex] = bufferHandle;` 在 Debug（`/MDd`、`_ITERATOR_DEBUG_LEVEL != 0`）被 `std::vector::operator[]` 的 `_STL_VERIFY` 中止进程，在 RelWithDebInfo 则是**未检查的越界写**。两者都不可作为脚本面：拒绝必须点名、越界必须不可达。
- 元数据：`Interface/ShaderCompiler/header/Compiler.h` 逐字 `struct BufferData { cacore::NameHash m_Name; EShaderResourceAccess m_RWType; uint32_t m_ElementCount; }`；`ShaderStructData` 里逐字 `castl::vector<BufferData> m_Buffers;`。**两个枚举不能混**：`m_RWType` 的类型 `EShaderResourceAccess` 只有 `eUnknown`/`eReadOnly`/`eWriteOnly`/`eReadWrite`；`eStructuredBuffer`/`eRWStructuredBuffer` 属另一个枚举 `EShaderResourceType`，不是 `m_RWType` 的取值。`m_ElementCount` 是**描述符数组长度**（非数组字段为 1，`StructuredBuffer<T> field[N]` 为 N）——`TestTriangleWithStructuredBufferColor.slang` 的 `colorStructuredBuffer` 非数组，归档读数 `cbuffer info` 表同源打印为 1。
- 窄面现状：`IScriptRenderGraph::AddBuffer(name, descriptor, data)` 已存在（顶点缓冲在用），图内缓冲表由图持有强引用；`IScriptShaderStruct` 现为 `SetFloat3`/`SetStruct`/`GetLastError` 三方法。

## Goals / Non-Goals

**Goals:**
- C# 能把图内已登记的结构化缓冲按名绑进参数结构字段，驱动真实像素（两后端）。
- 全部无效组合（未知字段、RW 类型、未登记缓冲名、绑了又换图、字段未写就提交）在适配层得到点名拒绝，且不触达 `CA_ASSERT`/静默跳过路径。
- 拒绝契约与错误命名沿用已归档批次：可失败方法返回 `int32_t`（0=成功），`GetLastError()` 点名；成功清空旧错误。

**Non-Goals:**
- `SetImage`/`SetSampler`（纹理采样批）、`eRWStructuredBuffer` 写访问与屏障语义（计算批）、带索引绘制与多元素缓冲数组绑定（几何/后续批）、双 pass 渲染目标采样（多阶段批）、C++ 端接口合并排期方向（ROADMAP 已记，另行立项）。

## Decisions

### 一、`SetBuffer` 签名收两个名字，不收句柄、不收下标

```cpp
// IScriptShaderStruct 追加（声明在 SetStruct 之后、GetLastError 之前）：
// 把**将绑定的那张图**里经 AddBuffer 登记的结构化缓冲按名绑到本结构的缓冲字段。
// 缓冲名此刻允许尚未登记（原生链就是先绑后建），存在性在 SubmitGraph 解析。
virtual int32_t SetBuffer(cacore::NameHash const& field, cacore::NameHash const& bufferName) = 0;
```

- 为什么按名不按句柄：原生 `SetBuffer` 收 `BufferHandle`，其本体就是名字值（逐字 `BufferHandle structuredColorBuffer(CANAME("StructuredColorBuffer"));`）；窄面已有 `AddBuffer` 的登记名表，句柄化要新建目标角色接口而对象本体仍在图里——名是唯一既有事实源。备选（返回 `IScriptBuffer` 窄对象）否决：图与结构两侧都得认识它，面扩一倍而无新校验力。
- 为什么不带 `elementIndex`：本批只支持缓冲字段单绑定（`m_ElementCount == 1` 的描述符，即非数组字段）。接口只增不改是已钉形态——后续批想加数组化绑定就再加一个带下标的重载方法，不改本方法。
- 图侧要有提交检查的数据通道（对抗验证第 3 条裁决）：`SetPassParam` 现状只把原生 `ShaderStruct` 写进 pass，图不保留窄对象与映射——`SubmitGraph` 的缓冲检查需要新登记面。决定：`CScriptRenderGraph::SetPassParam` 成功后把 `(passIndex, blockName) → shared_ptr<CScriptShaderStruct>` 记进**图侧登记表**（强引用，与参数保活同孔）；`CScriptShaderStruct` 提供只读 `HasBoundBuffer(field)`/`GetBoundBufferName(field)`；`CScriptRenderGraph` 提供只读 `HasBuffer(name)`（查自己的 `m_Buffers`）与 `GetBoundBufferNameForTest(passIndex, blockName, field)` 供测试核对。不依赖后端 `GetBufferHandles`（只在两个具体后端类上、基类没有，测试不能含后端私有头）。
- 绑定面：`NameHash` 与 `int32_t`/`GetLastError` 都是已验证可生成形状，无新类型、无 ABI 字段变化。

### 二、校验分两级时机：字段级当场、图级提交时

- `SetBuffer` 当场校验（读窄对象记住的 `ShaderStructData*`，与上批 `SetFloat3` 同通道）：字段在 `m_Buffers` 表外 → 非零点名 `field`；条目 `m_RWType != EShaderResourceAccess::eReadOnly`（即 `eWriteOnly`/`eReadWrite`/`eUnknown`）→ 非零点名字段并附文本「读写缓冲属计算批」（`eStructuredBuffer` 不是该枚举的合法取值，禁止写成对它比较）；条目 `m_ElementCount != 1` → 非零点名（`m_ElementCount` 是描述符数组长度：非数组恒为 1、`field[N]` 为 N；本批只支持非数组单绑定，数组字段留后续批）。**同一字段重复 `SetBuffer` 成功并以最后一次为准**（缓冲绑定是状态写入，与流名查重的登记语义不同；有效性由提交检查兜底）。写入成功后窄对象记录「字段 → 缓冲名」。
- `SubmitGraph` 提交时校验（接在既有"必需参数块检查"同一预执行段，数据通道见决策一的图侧登记表）：对每个 pass 绑定的参数结构，其 `m_Buffers` 表的每个条目都必须已被 `SetBuffer` 写过（未写 → 非零点名字段，防原生把未初始化默认 `BufferHandle` 带进执行）；被引用的缓冲名必须存在于**本图**缓冲表（不存在 → 非零点名 `bufferName`；绑 A 图缓冲改绑 B 图提交即走这条）。子结构递归防线（对抗验证第 48 条裁决，取"拒绝"方案）：`SetStruct` MUST 拒绝任何 `m_Buffers` 非空的子结构类型并点名字段——本批只支持顶层缓冲字段，含缓冲字段的子结构一旦放行即可绕过顶层检查（读码证实这类类型存在：夹具形态 `TestDataStruct2` 含 `StructuredBuffer`/`RWStructuredBuffer` 字段）。全部通过才调 `ExecuteGraph`——两后端的未检查写/中止路径从脚本面不可达。
- 为什么不"当场查图"：原生链就是先 `SetBuffer` 后 `AllocBuffer`（逐字 `pStructuredColor->SetBuffer(CANAME("colorStructuredBuffer"), structuredColorBuffer);` 早于 `.AllocBuffer(structuredColorBuffer, GPUBufferDescriptor::Create(1, sizeof(glm::vec3)))`），当场强求存在会把合法的先绑后建也拒掉。

### 三、像素判据与探针

- 新测试第 15 项 `TestCSharpStructuredBuffer`，独立入口 `OnStructuredBufferProbe`、独立开关 `CASCADED_CSHARP_SB_PROBE=1`、自测 `CASCADED_CSHARP_SB_SELFTEST` ∈ {`bad-buffer-color`, `unbound-field`, `bad-buffer-name`}；与 `OnRenderProbe`/`OnConstantColorProbe` 互不消耗（沿用既有隔离模式）。注册位置：`--list` 追加在 `TestCSharpConstantColor` 之后；run-all 的调用必须插在 `TestCSharpWindowCallbacks` **之前**（该用例关托管运行时，之后 C# 探针不可用）。
- 正常流程（对照原生同形）：`AddBuffer("sbColors", 元素 12 字节 × 1, magenta 字节)` → 顶点缓冲 → `SetPositionStream` → `AddTrianglePass` 窗口+离屏（清除色 `(0,1,0,1)`）→ `CreateShaderStruct("StructuredColor")` → `SetBuffer("colorStructuredBuffer", "sbColors")` → 两次 `SetPassParam("structuredColorBlock")` → `Present` → `SubmitGraph` → `WaitIdle`。
- 像素判据：背景 `(0,255,0,255)`、中心 `(255,0,255,255)`；负向 `bad-buffer-color`（缓冲数据写 `(0,0,1)`——既非清除色也非 magenta）中心 MUST **精确等于 `(0,0,255,255)`** → `E_SB_PIXEL`（判据不能写成"中心非 magenta"或"中心≠背景"：空帧/黑帧也能过）；`unbound-field` → 提交前非零 + `E_SB_BIND`；`bad-buffer-name` → 提交前非零 + `E_SB_BIND` 点名缓冲名。三者判据均为"后端执行计数 0"（提交前拒绝）或像素逐点，互不混用。
- 测试钩子追加 `GetSetBufferCountForTest`（正常探针**相对基线增量** +1；`unbound-field` 形态增量 0 且提交被拒——计数是后端实例上的进程级累计，全量跑到第 15 项时绝对读数含前批探针的贡献，一律以 Tick 前基线做增量断言）。
- 报告对账：新增 `Tools/check_csharp_structured_buffer.py`，2 后端 × normal/bad-buffer-color/unbound-field/bad-buffer-name 八份报告，模式与 `check_csharp_constant_color.py` 同构（后者是 2×3 六份，本批多一种负向）。

### 四、生成面与登记

- 审计表 +1 行：`IScriptShaderStruct::SetBuffer`，合计 **11 接口 84 方法**；`check_bindable_audit.py` 期望随改。
- `check_bindings.py` 适配面签名清单 + `SetBuffer` 的 C ABI 形状（两参数名 + 返回 `int32_t`）；托管侧 `SurfaceSignatureCheck.cs` 同步。
- `EngineApi` 不变（无新接口编号），ABI 保持 v7、`sizeof(EngineApi)=72`；`check_csharp_host_readme.py` 对拍逻辑不动，探针开关表 +`CASCADED_CSHARP_SB_PROBE`/`CASCADED_CSHARP_SB_SELFTEST` 两行。
- 数字 83 的**全部出现处**同步为 84（审计脚本读不到散文）：`bindable-audit.md` 表头与引言的"合计 **83** 条"、`Tools/CodeGen/README.md` 示例输出、`Interface/RenderInterface/private/ScriptRenderBridge.h` 注释里的"83 方法口径"——逐处改并加 grep 零命中判据。
- `docs/testing.md`：15 项清单、红态引用路径；`CSharpHost/README.md` 同步。

## Risks / Trade-offs

- [`m_ElementCount` 语义误读为"缓冲内元素数"] → 已按裁决更正：它是描述符数组长度，非数组字段恒为 1（本批字段的既有读数逐字 `... colorStructuredBuffer buffer space: 0 binding: 0 arrayLength: 1` 同源）；2.1 规则 `!= 1` 拒绝，4.1 只复核读数并记录，不用实测临时定规则。
- [字段未写时原生带默认 `BufferHandle` 进执行的行为未读全] → 不依赖、不实测其崩溃面：适配层提交前必拒（决策二），CoreTests 断言"未写就提交 → 拒绝且执行计数 0"即封顶。
- [先绑后建意味着 `SetBuffer` 当场无法拒绝拼错的缓冲名，报错点推迟到提交] → 提交时点名 `bufferName` 且零执行；托管示例与证据把这条时序写明，文档如实呈现"当场校验字段、提交时校验缓冲"。
- [同一结构复用到第二张图的错绑风险] → 提交按"本图缓冲表"解析（决策二），非全局表；CoreTests 双图用例锁死。
- [新增方法改变 `IScriptShaderStruct` 函数表布局] → 两侧同一构建重生成（`check_bindings.py` 逐行对拍兜底），不存在旧表新表共存窗口。

## Open Questions

（无——数值与规则均有出处或列为实测项，实测前不预设判据。）
