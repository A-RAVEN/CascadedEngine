# csharp-render-shader-constants

## Why

已归档的 `csharp-render-backend-triangle` 只支持顶点属性着色（POSITION+COLOR），C# 无法给着色器传参数。路线图登记的「其它 GPU 后端接口的 C# 绑定」首批方向就是着色器参数：现有后端测试 `TestTriangleWithConstantColor` 用 `ShaderStruct` 常量缓冲按 `SV_VertexID` 取色，是验证参数通道的最小真实场景，且其元数据校验通道（`ShaderStructData` 字段表、`SubStructReference.m_StructTypeName`、按着色器路径索引的根结构）已经存在。

## What Changes

- 新增脚本窄接口 `IScriptShaderStruct`（3 个方法）：按字段名写 `float3`、按字段名 + 数组下标挂子结构、读取错误文本；对象以后端为发行者的目标类型句柄跨边界。
- `IScriptRenderBackend` 追加 `CreateShaderStruct(structTypeName)`：未知类型在适配层依据着色器编译元数据拒绝（返回空句柄 + `GetLastError()` 点名），MUST NOT 到达 `CRenderBackend::CreateShaderStruct`。读码可证：`D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.cpp` 构造函数体内第一处实际使用即解引用 `p_StructData`，该崩溃路径仅在 D3D12 侧成立（`VulkanShaderStruct` 对空指针有检查）。
- `IScriptRenderGraph` 追加 `SetPositionStream(streamName, stride, positionOffset)`（单 `POSITION` `float3` 布局，匹配常量色着色器）与 `SetPassParam(passIndex, blockName, parameters)`（把参数结构绑定到既有 pass 的参数块名；形参名用 parameters，因 params 是 C# 关键字，见 design）。
- 全部参数校验在进入具体后端前完成：类型名存在于结构表、字段存在且为 12 字节 `float3` 单元素字段（类型名与元素数都要核对，12 字节的 `int3` 或 `float3[2]` 数组同样拒绝）、子结构字段存在、类型匹配 `m_StructTypeName`、`elementIndex` 小于 `m_ElementCount`、块名存在于该 pass 着色器根结构且类型一致。写入、绑定与建图类方法用 `int32_t` 状态 + `GetLastError()` 点名参数（沿用既有错误通道）；`CreateShaderStruct` 的拒绝契约是空句柄 + 错误文本。
- `CRenderBackend` 追加两个原生查询虚方法（按类型名查 `ShaderStructData`、按着色器路径查根结构），D3D12 与 Vulkan 直接读各自 `ShaderLibrary` 公开段的两张结构表（D3D12 已有直读先例；Vulkan 不得复用未命中会执行 `CA_LOG_ERR_BREAK` 断点打断的现有 getter），未命中返回空指针且不打断；CoreTests 的假后端同步实现。这是原生接口扩展，不跨脚本边界。
- `CSharpHost` 手写 ABI 追加 `script_shader_struct_interface_id`（编号 6），版本 v7、`sizeof(EngineApi)` 实测仍为 72 字节（新增 `uint32_t` 落在既有尾部填充内，两侧 `sizeof` 判据不变）；生成接口表版本不变。既有宿主失败检查的 ABI 期望文本随 v7 同步（`Tools/check_csharp_host_failures.py`、`docs/testing.md`、`CSharpHost/README.md`）。
- 新增 `TestCSharpConstantColor`：C# 建单 `POSITION` 流三角形、创建 `ConstantColor` 与 3 个 `ColorSubStruct`、逐槽设同一红色、绑定 `constantColorBlock`、窗口 + 离屏双 pass、呈现、提交、等待；原生读取离屏目标，背景精确 `(0, 0, 255, 255)`、中心精确 `(255, 0, 0, 255)`。Debug 负向 `bad-struct-colors`（三槽红/绿/蓝 → 中心非纯红，以 `E_CONSTCOLOR_PIXEL` 判红）与 `skip-param`（不绑定参数 → `SubmitGraph` 在调用后端执行前按该 pass 着色器根结构检查必需参数块绑定，缺失即返回非零并点名 `constantColorBlock`，原生以 `E_CONSTCOLOR_PARAM` 判红；两后端在空参数表下执行会触发 `CA_ASSERT_BREAK` 断点，不得依赖执行期读默认值）。
- 审计表从 10 接口 75 方法扩到 11 接口 83 方法：脚本窄接口新增 6 行（`IScriptShaderStruct` 3 + 图 2 + 后端 1），`CRenderBackend` 两个查询也各登记 1 行（不可绑 + 理由），`CRenderBackend` 行数 10 → 12；生成物、公开表面签名、托管编译与文档同步。

## Capabilities

### New Capabilities

（无——本变更全部落在既有能力内。）

### Modified Capabilities

- `csharp-rendering`：新增「着色器参数绑定必须按编译元数据在进入后端前校验」与「常量色三角形必须在两个后端通过真实像素验收」两条需求；「脚本窄渲染适配面」的顶点布局要求扩展为支持单 `POSITION` 流；「适配器必须在进入后端前拒绝无效图与无效参数」的审计覆盖从三个新接口改为四个脚本接口。
- `csharp-host`：手写 ABI 追加第四个渲染接口编号并升版本 v7；渲染表面启动、重绑与关闭清理覆盖参数结构对象；既有宿主失败检查的 ABI 期望版本随 v7 同步。

## Impact

- 适配接口与实现：`Interface/RenderInterface/header/ScriptRenderBridge.h`、`Interface/RenderInterface/private/ScriptRenderBridge.*`。
- 原生接口扩展：`Interface/RenderInterface/header/CRenderBackend.h`、`Interface/RenderInterface/header/GPUGraph.h`（新增受控可变 pass 访问器，供 `SetPassParam` 写入已构建的真实 pass）、`D3D12RenderBackend/private/RenderBackend_D3D12.*`、`VulkanRenderBackendNew/private/RenderBackend_Vulkan.*`（两个元数据查询实现）。
- 宿主与托管：`CSharpHost/private/CSharpHost.cpp`、`CSharpHost/CMakeLists.txt`、`CSharpHost/README.md`、`Scripting/Managed/EngineApi.cs`、`Host.cs`、`UserScript.cs`、`SurfaceSignatureCheck.cs`。
- 审计与生成判据：`Tools/CodeGen/check_bindable_audit.py`、`Tools/CodeGen/check_bindings.py`、`build.py` 绑定清单、唯一审计表 `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`。
- 测试与文档：`Test/CoreTests/ScriptRenderBridgeTest.cpp`、`Test/GPUBackendTester/Main.cpp`、新增 `Tools/check_csharp_constant_color.py`、`Tools/check_csharp_host_failures.py`（ABI 期望 6 → 7）、`docs/testing.md`、本变更 `evidence/`。
- 明确依赖：已归档的 `csharp-render-binding-foundations` 与 `csharp-render-backend-triangle`；复用现有着色器 `CAResources/Shaders/Test/TestTriangleWithConstantColor.slang`，不新增着色器资源，不改两个后端的执行语义。
