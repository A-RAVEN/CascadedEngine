# csharp-rendering 增量规范（csharp-render-structured-buffer）

## MODIFIED Requirements

### Requirement: 适配器必须在进入后端前拒绝无效图与无效参数
缓冲大小不一致、名称重复、属性越过步长、未知流或缓冲、顶点数为零或超过缓冲元素数、非后台缓冲呈现、空目标和空图提交 MUST 返回非零状态，且 MUST NOT 进入具体后端执行。可失败的建图与提交方法 SHALL 返回 `int32_t` 状态，0 表示成功；对应对象的 `GetLastError()` MUST 返回包含参数名的错误文本，成功调用 MUST 清空旧错误，原生异常 MUST NOT 穿过边界。

提交时的参数完整性检查 SHALL 覆盖结构化缓冲引用：被绑定到任何 pass 的参数结构中，凡元数据缓冲表列出的字段，MUST 已被 `SetBuffer` 写入，且写入引用的缓冲名 MUST 存在于**被提交的那张图**的缓冲登记表；字段未写、缓冲名未在本图登记、或同一结构改绑到不含该缓冲的另一张图提交时，`SubmitGraph` MUST 在调用后端执行前返回非零并点名对应字段或缓冲名。本批缓冲字段只支持顶层：`SetStruct` MUST 拒绝任何元数据缓冲表非空的子结构类型并点名字段，防止含缓冲字段的子结构绕过顶层检查。

四个脚本接口（`IScriptRenderTarget`、`IScriptRenderGraph`、`IScriptRenderBackend`、`IScriptShaderStruct`）的每个纯虚方法 MUST 在当前唯一可绑定面审计表中有且只有一行，重复行和表内汇总数字不符 MUST 使审计失败。生成物 MUST 通过真实接口无桩和公开表面判据。

#### Scenario: 无效布局或绘制数不得进入实现
- **WHEN** 位置或颜色属性的 `offset + 12` 超过步长，或绘制数超过对应缓冲描述的元素数
- **THEN** 适配器 MUST 在调用 `RenderPass` 或后端执行前返回非零状态，`GetLastError()` MUST 点名参数，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 缓冲引用不完整或换图错绑不得进入执行
- **WHEN** 参数结构含元数据声明的缓冲字段但从未 `SetBuffer`，或 `SetBuffer` 引用了未在本图 `AddBuffer` 登记的缓冲名，或把为 A 图绑定的结构提交给不含该缓冲的 B 图
- **THEN** `SubmitGraph` MUST 返回非零并点名缺失的字段名或缓冲名，后端执行调用次数 MUST 为 0，MUST NOT 让两后端把未初始化的缓冲句柄带进执行路径

#### Scenario: 未审计或重复审计的方法不得悄悄生成
- **WHEN** 给任一渲染适配接口新增纯虚方法但未更新唯一审计表，或把同一方法登记两次，或表内汇总数字与实际不符
- **THEN** `check_bindable_audit.py` 与构建 MUST 非零退出并点名接口、方法或不符汇总；补齐审计后生成物 MUST 仍满足真实接口无桩和两侧方法对齐

### Requirement: 着色器参数绑定必须按编译元数据在进入后端前校验
系统 SHALL 提供脚本参数结构接口：按类型名创建、按字段名写 12 字节 `float3`、按字段名与数组下标挂子结构、按字段名绑定图内结构化缓冲（`SetBuffer(field, bufferName)`），并可把参数结构按参数块名绑定到图中既有 pass。全部校验 MUST 在适配层依据着色器编译元数据（结构表字段名、元素字节数与类型名、子结构数组的类型名与元素数、缓冲表条目的名字/读写类型/元素数、着色器根结构的参数块）在调用当场完成；缓冲名的存在性按"先绑后建"的原生时序在提交时解析（见上一条需求的提交时检查）。拒绝契约分两类：`CreateShaderStruct` 对未知类型 MUST 返回空句柄并由 `GetLastError()` 点名 `structTypeName`；`SetFloat3`、`SetStruct`、`SetBuffer`、`SetPassParam`、`SetPositionStream` 与 `SubmitGraph` 对无效输入 MUST 返回非零 `int32_t` 状态并由 `GetLastError()` 点名参数。未知字段、非 `float3` 类型或数组字段、子结构类型不符、下标越界、缓冲表外的字段名、访问类型非只读（`m_RWType != eReadOnly`）的缓冲字段、元素数不等于 1 的缓冲字段（数组形态，留后续批）、未知参数块名、块类型不符、pass 下标越界、空参数、未登记的着色器路径与缺失必需参数块的提交 MUST NOT 进入具体后端执行。同一字段再次 `SetBuffer` MUST 成功并以最后一次绑定为准（缓冲绑定是状态写入，不同于流名查重），提交解析 MUST 采用最后登记的缓冲名。未知类型 MUST NOT 到达 `CRenderBackend::CreateShaderStruct`（读码可证：D3D12 侧构造路径对空元数据解引用）。`SetBuffer` MUST NOT 把无效输入转交原生 `ShaderStruct::SetBuffer`——两后端实现对未知字段名静默跳过、对越界下标走 `CA_ASSERT` 断言，二者都 MUST 在适配层被点名拒绝替代。写入与绑定方法沿用 `int32_t` 状态与成功清错的既有错误通道。

原生 `CRenderBackend` SHALL 追加按类型名与按着色器路径查询结构元数据的纯虚方法，由 D3D12、Vulkan 与原生测试假后端同步实现；两后端实现 MUST 直读 `ShaderLibrary` 公开结构表，未命中 MUST 返回空指针且 MUST NOT 触发断点打断类日志（Vulkan 现有 getter 的未命中路径含 `CA_LOG_ERR_BREAK`，不得复用）；该查询面 MUST NOT 跨脚本边界暴露。

#### Scenario: 无效参数结构输入不得进入后端
- **WHEN** C# 或原生测试用未知类型名创建、写未知字段、12 字节但非 `float3` 类型的字段或数组字段、挂类型不符或下标越界的子结构、给缓冲表外的字段名、访问类型非只读或元素数不等于 1 的缓冲字段执行 `SetBuffer`、绑定未知块名、类型不符的块、越界 pass 下标或未登记的着色器路径
- **THEN** 未知类型项 MUST 返回空句柄且 `GetLastError()` 点名 `structTypeName`，其余各项 MUST 返回非零状态并点名对应参数（类型不符时 MUST 同时点名期望与实际类型名），后端创建与执行调用次数 MUST 为 0，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 重复绑定同一缓冲字段以最后一次为准
- **WHEN** 对同一字段连续执行两次 `SetBuffer`（分别指向图中两个已登记的缓冲名）后绑定并提交
- **THEN** 两次调用 MUST 都返回 0，`GetSetBufferCountForTest` 增量 MUST 为 2，提交解析与执行 MUST 采用最后一次登记的缓冲名

#### Scenario: 参数结构句柄与生命周期沿用既有目标解析
- **WHEN** C# 释放参数结构表面对象后再次调用其方法，或把该句柄交给类型不符的接口
- **THEN** 调用 MUST 在进入原生实现前失败且实现调用次数为 0；已提交图 MUST 通过原生持有继续保活其绑定的参数结构直到 `WaitIdle` 完成

## ADDED Requirements

### Requirement: 结构化缓冲参数必须在两个后端通过真实像素验收
系统 SHALL 提供 `TestCSharpStructuredBuffer`：C# 用单 `POSITION` 顶点流、现有着色器 `Shaders/Test/TestTriangleWithStructuredBufferColor`、`AddBuffer` 登记的 1 元素 `float3` 结构化缓冲（颜色 `(1, 0, 1)`）与 `StructuredColor` 参数结构构建并提交图，窗口与离屏目标各一个同逻辑 pass、各自绑定 `structuredColorBlock`，清除色 `(0, 1, 0, 1)`。正常模式的背景像素 MUST 精确为 `(0, 255, 0, 255)`、中心像素 MUST 精确为 `(255, 0, 255, 255)`，判据 MUST 来自原生 `Readback` 读取的同一次提交的单层 RGBA8 离屏目标，并核对窗口描述快照、真实呈现清单、`WaitIdle` 计数与待完成集合清理；MUST NOT 用日志声明、进程未崩溃或非零字节代替像素判据。

Debug 负向 SHALL 至少包含：`bad-buffer-color`（缓冲数据改写为 `(0, 0, 1)`——既非清除色也非 magenta，中心 MUST **精确等于 `(0, 0, 255, 255)`**，以 `E_SB_PIXEL` 判红——证明缓冲数据真实进入输出；判据不得写成"中心非 magenta"或"中心不等于背景"，空帧与黑帧都能通过这种弱判据）、`unbound-field`（创建结构后不执行任何 `SetBuffer` 即绑定并提交，`SubmitGraph` MUST 在调用后端执行前返回非零、点名未写字段，以 `E_SB_BIND` 判红）与 `bad-buffer-name`（`SetBuffer` 引用未在本图登记的缓冲名，提交 MUST 非零、点名缓冲名，后端执行次数 MUST 为 0，以 `E_SB_BIND` 判红）。`GetSetBufferCountForTest` 在正常探针 MUST 读得增量 1、在 `unbound-field` MUST 读得增量 0（计数为进程级累计，一律相对 Tick 前基线）。对账脚本 MUST 非零识别缺失的正常证据或错误接受负向结果。

#### Scenario: 两个后端都绘制出缓冲驱动的像素
- **WHEN** 分别以 D3D12 和 Vulkan 运行正常的 `TestCSharpStructuredBuffer`
- **THEN** 两个后端的离屏目标 MUST 与窗口描述快照同尺寸并为 RGBA8，背景与中心 MUST 精确等于 `(0, 255, 0, 255)` 与 `(255, 0, 255, 255)`，两个 pass 的参数表 MUST 各含 1 条以 `structuredColorBlock` 为键的真实绑定且其缓冲字段解析到本图登记的缓冲，报告 MUST 为 `pass`

#### Scenario: 缓冲内容必须可证地参与输出
- **WHEN** 在 Debug 下用 `bad-buffer-color` 把缓冲元素改写为 `(0, 0, 1)` 并运行两个后端
- **THEN** 中心像素 MUST 精确等于 `(0, 0, 255, 255)` 且测试 MUST 以 `E_SB_PIXEL` 失败；不得由 C++ 用硬编码颜色替代 C# 写入的缓冲数据

#### Scenario: 未绑定字段与错名缓冲必须在提交前被拒绝
- **WHEN** 在 Debug 下用 `unbound-field` 跳过 `SetBuffer` 直接绑定并提交，或用 `bad-buffer-name` 绑定拼错的缓冲名后提交
- **THEN** `SubmitGraph` MUST 在调用 `ExecuteGraph` 之前返回非零并分别点名未写字段名或缓冲名，后端执行调用次数 MUST 为 0，测试 MUST 以 `E_SB_BIND` 失败，`Tools/check_csharp_structured_buffer.py` MUST 把两种形态各自对账为对应错误码
