# csharp-render-shader-constants 设计

## Context

已归档的 `csharp-render-backend-triangle` 建立了三个窄接口（`IScriptRenderTarget` 3 方法、`IScriptRenderGraph` 6 方法、`IScriptRenderBackend` 6 方法，合计审计表 10 接口 75 方法）与 `int32_t` 状态 + `GetLastError()` 错误通道。本变更在同一适配面上加第一条真实着色器参数通道：常量缓冲参数结构。

已核实的现状事实（本设计依据）：

- `Test/GPUBackendTester/Main.cpp` 的 `TestTriangleWithConstantColor` 用 `CRenderBackend::CreateShaderStruct(CANAME("ConstantColor"))` 创建参数结构，`CreateShaderStruct(CANAME("ColorSubStruct"))` ×3 后 `SetStruct(CANAME("colorStruct"), sub, i)` 挂到数组槽，`SetValue(CANAME("color"), glm::vec3)` 写字段，`RenderPass::SetParam(CANAME("constantColorBlock"), pConstantColor)` 绑定；顶点流只有单个 `POSITION` `float3`（步长 12），清除色 `(0, 0, 1, 1)`。
- `CAResources/Shaders/Test/TestTriangleWithConstantColor.slang`：`ConstantColor { ColorSubStruct colorStruct[3] }`、`ColorSubStruct { float3 color }`，顶点色 = `constantColorBlock.colorStruct[SV_VertexID].color`。
- 元数据通道：`Interface/ShaderCompiler/header/Compiler.h` 的 `ShaderStructData`（`m_TypeName`、`m_StructUniforms.m_Elements` 字段表含 `m_Name/m_ElementMemorySize/m_ElementCount/m_Stride`、`m_SubStructReferences` 含 `m_Name/m_StructTypeName/m_ElementCount`）；D3D12 与 Vulkan 的 `ShaderLibrary` 各有按类型名与按着色器路径（根结构）的两张表。
- 危险事实（读码可证）：`D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.cpp` 构造函数体内第一处实际使用（第二条可执行语句 `m_UniformElementOffsetInStagingBuffer.resize(p_StructData->...)`）就解引用 `p_StructData`，而 `RenderBackend_D3D12::CreateShaderStruct` 对未知类型传空指针继续构造；该崩溃路径仅在 D3D12 侧成立（`VulkanShaderStruct::Init` 对空指针有检查）。`SetValueInternal`/`SetStructInternal` 对未知字段静默忽略、对越界下标只 `CA_ASSERT`。因此一切校验必须在适配层进入后端之前完成。
- 查询陷阱：Vulkan `ShaderLibrary::GetShaderStruct`/`GetShaderRootStruct` 未命中先执行 `CA_LOG_ERR_BREAK`（宏体 `__debugbreak`，Release 不消除），不能用于「未命中返回空」的校验查询；两张结构表（`m_ShaderStructs`/`m_ShaderRootStructs`）在两个后端的 `ShaderLibrary` 里都是 public 段，D3D12 侧 `RenderBackend_D3D12.cpp` 已有直读 `m_ShaderStructs` 的先例。

## Goals / Non-Goals

**Goals:**

- C# 能创建按类型名校验的参数结构、按字段名写 `float3`、按字段名 + 数组下标挂子结构，并把结构绑定到既有 pass 的参数块名；全部无效输入在进入具体后端前被拒绝且点名参数。
- 新增单 `POSITION` 顶点流布局，使 C# 能构建常量色着色器要求的输入。
- `TestCSharpConstantColor` 在 D3D12 与 Vulkan 上以原生 `Readback` 通过精确像素验收，并有能失败的负向模式。
- 手写 ABI 追加第四个渲染接口编号（6），版本 v7；生成接口表版本保持 2。

**Non-Goals:**

- 不支持 `float`/`int`/矩阵/数组字段、纹理、采样器、缓冲引用类参数（后续批次）；不暴露 `ShaderStruct` 的版本/脏标记机制，提交后修改参数结构不保证生效，也不作为验收路径。
- 不新增着色器资源，不改两个后端的执行语义，不做计算参数、通用多 pass 依赖、托管回读。
- 不修改生成器形状规则（`CodeGenMain.cpp` 不动）；参数结构对象只用已归档的句柄与目标解析形状。

## Decisions

### 一、窄接口形状：新接口 `IScriptShaderStruct` + 两个既有接口追加方法

```cpp
CA_BIND()
class IScriptShaderStruct
{
public:
    // 字段必须是该类型元数据里的 12 字节 float3 单元素字段；否则拒绝并点名 field。
    virtual int32_t SetFloat3(cacore::NameHash const& field, float x, float y, float z) = 0;
    // field 必须是元数据里的子结构数组，sub 的类型名必须等于该条目的 m_StructTypeName，
    // elementIndex 必须小于 m_ElementCount；违反逐项拒绝并点名参数。
    virtual int32_t SetStruct(cacore::NameHash const& field,
        castl::shared_ptr<IScriptShaderStruct> const& sub, uint32_t elementIndex) = 0;
    virtual castl::string GetLastError() const = 0;
    virtual ~IScriptShaderStruct() = default;
};
```

`IScriptRenderBackend` 追加（6 → 7）：

```cpp
virtual castl::shared_ptr<IScriptShaderStruct> CreateShaderStruct(cacore::NameHash const& structTypeName) = 0;
```

`IScriptRenderGraph` 追加（6 → 8）：

```cpp
// 单 POSITION float3 流：positionOffset + 12 越过 stride 即拒绝；流名与现有流共用唯一性规则。
virtual int32_t SetPositionStream(cacore::NameHash const& streamName, uint32_t stride,
    uint32_t positionOffset) = 0;
// 把参数结构绑定到第 passIndex 个 pass 的参数块名；passIndex 越界、parameters 为空、
// blockName 不在该 pass 着色器根结构、块类型与 parameters 类型名不符都拒绝并点名参数。
// 形参名定为 parameters：绑定发射器把 C++ 形参名原样搬进托管表面，params 是 C# 关键字
// （apply 2026-11-04 实测：IScriptRenderGraph.Bindings.cs 报 error CS1041），不许改回。
virtual int32_t SetPassParam(uint32_t passIndex, cacore::NameHash const& blockName,
    castl::shared_ptr<IScriptShaderStruct> const& parameters) = 0;
```

新方法合计 3 + 1 + 2 = 6（脚本窄接口面）；审计面另有 `CRenderBackend` 两个查询各登记 1 行（不可绑 + 理由），合计新增 8 行：审计表 10 接口 75 方法 → 11 接口 83 方法，`CRenderBackend` 行数 10 → 12。`CreateShaderStruct` 返回空表示拒绝（错误文本在 backend 的 `GetLastError()`），其余可失败方法返回 `int32_t`，成功清空旧错误——沿用既有错误通道，不新增形状。

理由：`ShaderStruct` 本体是模板 + protected 虚函数，不能直接绑定；窄包装把「类型名字符串 + 反射元数据」的写入面收敛成两个可校验方法。参数结构由 backend 发行句柄（backend 持有真实 `CRenderBackend`），`SetPassParam` 的 `shared_ptr<IScriptShaderStruct>` 入参走已归档的跨接口目标解析。

### 二、校验全部发生在适配层，依据是编译元数据；为此给 `CRenderBackend` 加两个原生查询

`CRenderBackend` 追加纯虚：

```cpp
virtual ShaderCompilerSlang::ShaderStructData const* FindShaderStructData(cacore::NameHash const& structType) = 0;
virtual ShaderCompilerSlang::ShaderStructData const* FindShaderRootStructData(cacore::PathHash const& shaderPath) = 0;
```

D3D12 与 Vulkan 都直接读各自 `ShaderLibrary` 公开段的 `m_ShaderStructs`/`m_ShaderRootStructs`（与 `RenderBackend_D3D12.cpp` 既有直读一致，不新增 getter 包装）；MUST NOT 复用未命中会执行 `CA_LOG_ERR_BREAK` 断点打断的 Vulkan getter，`Find*` 未命中一律返回空指针且不打断。适配层规则：

- `CreateShaderStruct`：`FindShaderStructData` 为空 → 返回空句柄并在后端 `GetLastError()` 点名 `structTypeName`，MUST NOT 调用 `CRenderBackend::CreateShaderStruct`（未知类型在 D3D12 侧会解引用空元数据）。非空 → 创建真实 `ShaderStruct` 并包进窄对象，窄对象记住类型名与其元数据指针。
- `SetFloat3`：字段不在 `m_StructUniforms.m_Elements` → 拒绝点名 `field`；`m_ElementMemorySize != 12` → 拒绝并说明期望 12 字节与实际值；字段的类型名不等于实现期实测的 Slang `float3` 完整类型名、或 `m_ElementCount != 1` → 拒绝并点名 `field`（12 字节的 `int3`、`float3[2]` 数组元素同样被拒）；通过后调 `SetValue(name, float3 值)`。
- `SetStruct`：`sub` 为空 → 拒绝点名 `sub`；字段不在 `m_SubStructReferences` → 拒绝点名 `field`；`sub` 类型名 ≠ 条目 `m_StructTypeName` → 拒绝并点名两者；`elementIndex >= m_ElementCount` → 拒绝点名 `elementIndex`。
- `SetPassParam`：`parameters` 为空 → 拒绝；`passIndex >= GetPassCount()` → 拒绝点名 `passIndex`；按该 pass 的 `GetPipelineStates().m_ShaderInfo.path` 查根结构，路径未登记 → 拒绝点名着色器路径；`blockName` 不在其 `m_SubStructReferences` → 拒绝点名 `blockName`；块类型名 ≠ `parameters` 类型名 → 拒绝并点名两者。通过后经 `GPUGraph` 新增的受控可变 pass 访问器（`RenderPass& GetRenderPassForWrite(uint32_t index)`）对第 `passIndex` 个真实 pass 调用 `RenderPass::SetParam` 生效；`AddTrianglePass` 仍当场 `AddPass`，不延迟建图，既有 `GetPassCount()`/`GetRenderPasses()` 语义与 CoreTests 提交前断言不变。
- `SetPositionStream`：`stride == 0` 或 `positionOffset + 12 > stride` → 拒绝点名参数；流名与 `AddBuffer` 缓冲名分属两张表。流名查重：现状 `SetPositionColorStream` 对同名流直接 `m_Streams[streamName] = record` 静默覆盖，本变更把两种流登记都改为重复即返回非零并点名 `streamName`（加严，不放松）。
- `SubmitGraph` 扩展：调用 `ExecuteGraph` 之前，按每个 pass 的着色器根结构检查其必需参数块是否都已绑定（根结构 `m_SubStructReferences` 列出的块必须有对应 `SetParam` 记录）；缺失即返回非零并在 `GetLastError()` 点名块名，不调用后端——两后端在空参数表下执行会触发 `CA_ASSERT_BREAK` 断点，不得依赖执行期读默认值。无参数块的既有着色器（如 `TestSimpleTriangle`）必需集合为空，提交行为不变。

校验时机都在调用当场（提交前），不依赖执行期断言。

### 三、宿主手写 ABI v7：只追加编号 6

`EngineApi` 末尾追加 `script_shader_struct_interface_id`，值 6（3/4/5 已被后端/图/目标占用），版本 6 → 7；`sizeof(EngineApi)` 由两侧 `sizeof` 实测——现有字段 68 字节 + 尾部对齐填充 4 = 72，追加一个 `uint32_t` 后正文恰 72、仍为 72，判据写 72 不写 80。`IScriptShaderStruct` 与图、目标同属目标类型表：`OnGetInterfaceApi` 对编号 6 允许空实例返回函数表，对象经 `ResolveTargetHandle` 解析。后端表仍要求真实适配器实例。启动布局清单、`EnsureBound` 与 `Shutdown` 覆盖第四个渲染接口；关闭顺序：参数结构表面对象 → 图 → 目标 → 后端表面（与 `UserScript.ReleaseRenderSurfaces` 既有的图先、目标后一致）。生成接口表版本保持 2。既有宿主失败检查的 ABI 期望文本随 v7 同步：`Tools/check_csharp_host_failures.py` 的「托管侧期望 6」改 7，`docs/testing.md` 与 `CSharpHost/README.md` 的对应读数同步。

### 四、C# 探针与负向模式：独立入口，不消耗既有渲染探针

`UserScript` 新增 `OnConstantColorProbe(IWindowSystem, IScriptRenderBackend)`，独立开关 `CASCADED_CSHARP_CONSTCOLOR_PROBE=1` 与独立调用标记；`TestCSharpConstantColor` 在自己的 Tick 前设置、之后清理。正常流程：

1. `NewWindow(320, 240)` → `GetWindowTarget` + `CreateCaptureTarget(320, 240)` + `CreateGraph`。
2. 三个顶点仅位置 `(-0.25,-0.25,0.25) (0.25,-0.25,0.25) (0,0.5,0.5)`，步长 12 → `AddBuffer`（36 字节）→ `SetPositionStream`。
3. `CreateShaderStruct("ConstantColor")` + 3 × `CreateShaderStruct("ColorSubStruct")`，各 `SetFloat3("color", 1, 0, 0)`，`SetStruct("colorStruct", sub, i)`（i=0..2）。
4. 窗口与离屏各一次 `AddTrianglePass`（着色器路径 `Shaders/Test/TestTriangleWithConstantColor`，清除色 `(0, 0, 1, 1)`），对两个 pass 各调 `SetPassParam(passIndex, "constantColorBlock", constantColor)`，`Present(窗口目标)`、`SubmitGraph`、`WaitIdle`；图、目标与参数结构保留到原生核对完成。

Debug 负向（`CASCADED_CSHARP_CONSTCOLOR_SELFTEST`）：

- `bad-struct-colors`：三槽颜色改为红/绿/蓝 → 中心像素不再是纯红，原生以 `E_CONSTCOLOR_PIXEL` 判红。
- `skip-param`：完全不调 `SetPassParam` → `SubmitGraph` 按第二节扩展在调用后端执行前拒绝并点名 `constantColorBlock`（返回非零、`E_CONSTCOLOR_PARAM`），MUST NOT 进入 `ExecuteGraph`、不读像素、不对未定义内容编造期望；C# 侧把该拒绝状态记入摘要，原生测试核对提交返回值与真实 pass 参数表条目数（正常为每 pass 1 条且键为 `constantColorBlock`）。
- 调用层拒绝（未知类型、错字段、非 `float3` 类型、越界下标、错块名、类型不符、passIndex 越界、重复流名、未登记路径）由 CoreTests 覆盖，不占 GPU 负向名额。

### 五、像素判据与测试登记

背景清除色 `(0, 0, 1, 1)` → 角点像素精确 `(0, 0, 255, 255)`；三槽同红 → 中心精确 `(255, 0, 0, 255)`（三顶点同色，无插值歧义，判据不依赖尺寸）。同上一变更模式：窗口描述快照核对尺寸/格式，`Readback` 读单层 RGBA8 离屏目标，核对呈现清单、`WaitIdle` 计数与待完成集合清理，保存日志与 PNG；C++ 与 C# 各写独立夹具值。`GPUBackendTester` 注册 `TestCSharpConstantColor`（第 14 项），`Tools/check_csharp_constant_color.py` 对账 2 后端 × (normal pass + 2 负向 fail + 错误码)。

### 六、审计、生成物与文档

窄接口 6 个新方法加 `CRenderBackend` 两个查询共 8 行逐行进唯一审计表并同步汇总（11 接口 83 方法、`CRenderBackend` 12 行）；`check_bindable_audit.py` 用头文件实算核对；`check_bindings.py` 增加新接口与两个接口新方法集合、公开签名与真实实现调用判据；`build.py` 期望产物前缀加 `IScriptShaderStruct`。`CSharpHost/README.md` 更新 ABI v7、编号表（6 = 参数结构）与两个探针开关，并新增 `Tools/check_csharp_host_readme.py` 把 README 的 ABI 版本、编号表与开关清单对拍 `Host.cs`/`EngineApi.cs`/`CSharpHost.cpp` 的真实值（`verify_readme.py` 只覆盖 `Tools/CodeGen/README.md`，不能代劳）。`docs/testing.md` 以实测更新清单与读数。CoreTests 需要真实可写的 `ShaderStruct` 桩：现状 `FakeRenderBackend::CreateShaderStruct` 固定返回空指针，本批为它写测试用子类（实现 5 个 protected 纯虚与 `GetStructTypeName`，记录写入的字段与值），正例断言「参数表 1 条且条目与夹具实例是同一对象」，不拿空指针充当绑定。

## Risks / Trade-offs

- **元数据在探针时点必须已就绪**：`ShaderLibrary` 由 tester 启动时 `ScanSourceDirectory` 生成；若查询为空，全部拒绝路径会把合法类型也挡下——CoreTests 用假后端注入元数据表验证逻辑，端到端以 `TestCSharpConstantColor` 正常通过证明真实表可用。
- **`CRenderBackend` 加纯虚影响所有实现者**：仓内实现只有 D3D12、Vulkan 与 CoreTests 假后端；纯虚强制三处同步实现，漏一处编译失败——这是期望行为，不做带默认实现的软扩展。
- **参数脏版本**：提交后 `SetFloat3` 再改值，两后端行为依赖既有版本机制，本变更不承诺也不测试；文档明写「提交后修改不作为验收路径」。
- **`skip-param` 改为提交前拒绝**：两后端在空参数表下执行会触发 `CA_ASSERT_BREAK` 断点，判据不依赖执行期；`SubmitGraph` 的必需块检查对无参数块着色器（既有三角形、窗口路径）必需集合为空、行为不变——回归以既有用例全绿证明。
- **窄接口继续膨胀**：本批只加 6 方法；纹理/采样器/其余标量类型明确留给后续批次，不借机开放通用 `SetValue` 字节口。

## Migration Plan

1. 先加 `CRenderBackend` 两个查询与假后端实现、窄接口声明、审计行与 CoreTests 拒绝路径断言，留红态。
2. 实现适配层校验与生成物，过 `check_bindings.py` / `check_bindable_audit.py`。
3. 宿主 v7 接线与托管绑定、关闭顺序。
4. C# 探针与 `TestCSharpConstantColor`，先负向红再双后端绿。
5. 文档与全量回归（含既有窗口、回调、三角形全套）。

回退需同时撤回接口、ABI v7 字段、生成物与测试，不得混用 v6/v7 宿主表。

## Open Questions

无未决范围决策。其余参数类型（float/int/纹理/采样器/缓冲引用）、提交后参数更新与通用多 pass 均留给后续批次。
