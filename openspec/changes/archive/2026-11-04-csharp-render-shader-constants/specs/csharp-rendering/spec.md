# csharp-rendering 增量规范

## ADDED Requirements

### Requirement: 着色器参数绑定必须按编译元数据在进入后端前校验

系统 SHALL 提供脚本参数结构接口：按类型名创建、按字段名写 12 字节 `float3`、按字段名与数组下标挂子结构，并可把参数结构按参数块名绑定到图中既有 pass。全部校验 MUST 在适配层依据着色器编译元数据（结构表字段名、元素字节数与类型名、子结构数组的类型名与元素数、着色器根结构的参数块）在调用当场完成。拒绝契约分两类：`CreateShaderStruct` 对未知类型 MUST 返回空句柄并由 `GetLastError()` 点名 `structTypeName`；`SetFloat3`、`SetStruct`、`SetPassParam`、`SetPositionStream` 与 `SubmitGraph` 对无效输入 MUST 返回非零 `int32_t` 状态并由 `GetLastError()` 点名参数。未知字段、非 `float3` 类型或数组字段、子结构类型不符、下标越界、未知参数块名、块类型不符、pass 下标越界、空参数、未登记的着色器路径与缺失必需参数块的提交 MUST NOT 进入具体后端执行。未知类型 MUST NOT 到达 `CRenderBackend::CreateShaderStruct`（读码可证：D3D12 侧构造路径对空元数据解引用）。写入与绑定方法沿用 `int32_t` 状态与成功清错的既有错误通道。

原生 `CRenderBackend` SHALL 追加按类型名与按着色器路径查询结构元数据的纯虚方法，由 D3D12、Vulkan 与原生测试假后端同步实现；两后端实现 MUST 直读 `ShaderLibrary` 公开结构表，未命中 MUST 返回空指针且 MUST NOT 触发断点打断类日志（Vulkan 现有 getter 的未命中路径含 `CA_LOG_ERR_BREAK`，不得复用）；该查询面 MUST NOT 跨脚本边界暴露。

#### Scenario: 无效参数结构输入不得进入后端

- **WHEN** C# 或原生测试用未知类型名创建、写未知字段、12 字节但非 `float3` 类型的字段或数组字段、挂类型不符或下标越界的子结构、绑定未知块名、类型不符的块、越界 pass 下标或未登记的着色器路径
- **THEN** 未知类型项 MUST 返回空句柄且 `GetLastError()` 点名 `structTypeName`，其余各项 MUST 返回非零状态并点名对应参数（类型不符时 MUST 同时点名期望与实际类型名），后端创建与执行调用次数 MUST 为 0，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 参数结构句柄与生命周期沿用既有目标解析

- **WHEN** C# 释放参数结构表面对象后再次调用其方法，或把该句柄交给类型不符的接口
- **THEN** 调用 MUST 在进入原生实现前失败且实现调用次数为 0；已提交图 MUST 通过原生持有继续保活其绑定的参数结构直到 `WaitIdle` 完成

### Requirement: 常量色三角形必须在两个后端通过真实像素验收

系统 SHALL 提供 `TestCSharpConstantColor`：C# 用单 `POSITION` 顶点流、现有着色器 `Shaders/Test/TestTriangleWithConstantColor`、`ConstantColor` 与 3 个 `ColorSubStruct` 构建并提交图，窗口与离屏目标各一个同逻辑 pass 并各自绑定 `constantColorBlock`。正常模式的背景像素 MUST 精确为 `(0, 0, 255, 255)`、中心像素 MUST 精确为 `(255, 0, 0, 255)`，判据 MUST 来自原生 `Readback` 读取的同一次提交的单层 RGBA8 离屏目标，并核对窗口描述快照、真实呈现清单、`WaitIdle` 计数与待完成集合清理。

Debug 负向 SHALL 至少包含：`bad-struct-colors`（三个数组槽颜色改为红/绿/蓝，中心 MUST 不再是纯红，以 `E_CONSTCOLOR_PIXEL` 判红）与 `skip-param`（完全不调用参数绑定，`SubmitGraph` MUST 在调用后端执行前按该 pass 着色器根结构检查必需参数块绑定并返回非零、点名 `constantColorBlock`，原生以 `E_CONSTCOLOR_PARAM` 判红，MUST NOT 进入 `ExecuteGraph` 或对该模式的像素内容编造期望）。对账脚本 MUST 非零识别缺失的正常证据或错误接受负向结果。

#### Scenario: 两个后端都绘制出参数驱动的像素

- **WHEN** 分别以 D3D12 和 Vulkan 运行正常的 `TestCSharpConstantColor`
- **THEN** 两个后端的离屏目标 MUST 与窗口描述快照同尺寸并为 RGBA8，背景与中心 MUST 精确等于 `(0, 0, 255, 255)` 与 `(255, 0, 0, 255)`，两个 pass 的参数表 MUST 各含 1 条以 `constantColorBlock` 为键的真实绑定，报告 MUST 为 `pass`

#### Scenario: 常量缓冲数据必须可证地参与输出

- **WHEN** 在 Debug 下用 `bad-struct-colors` 把三个数组槽分别设为红、绿、蓝并运行两个后端
- **THEN** 中心像素 MUST 不再是纯红且测试 MUST 以 `E_CONSTCOLOR_PIXEL` 失败；不得由 C++ 用硬编码颜色替代 C# 写入的参数值

#### Scenario: 省略参数绑定必须在提交前被拒绝

- **WHEN** 在 Debug 下用 `skip-param` 不调用任何参数绑定并调用 `SubmitGraph`
- **THEN** `SubmitGraph` MUST 在调用后端执行前返回非零并点名 `constantColorBlock`，后端执行调用次数 MUST 为 0，原生测试以 `E_CONSTCOLOR_PARAM` 判红；MUST NOT 以未定义的像素内容充当判据

## MODIFIED Requirements

### Requirement: 脚本窄渲染适配面必须形成真实建图与提交

系统 SHALL 提供脚本专用的窗口目标、离屏目标和图对象接口。C# 调用方 MUST 能提供顶点字节、缓冲描述、顶点流布局、着色器路径、渲染目标、顶点数和清除色，并由原生适配层用这些输入构造现有 `GPUGraph`。原生提交 MUST 在引擎内部创建调度器，公开表面 MUST NOT 暴露 `TaskScheduler*`、`CRenderBackend::Release`、具体建图类、原生资源指针或链式建图返回。

顶点流布局 SHALL 支持两种：`POSITION` 与 `COLOR` 两个 `float3` 属性（`SetPositionColorStream`），或仅 `POSITION` 一个 `float3` 属性（`SetPositionStream`）；任一属性 `offset + 12` 越过步长即拒绝；两种流登记共用图内流名表，重复登记同名流 MUST 返回非零并由 `GetLastError()` 点名 `streamName`（现状 `SetPositionColorStream` 静默覆盖的行为一并加严）。`AddTrianglePass` SHALL 使用调用方给定的目标、着色器、流、缓冲、顶点数和清除色生成一个真实渲染 pass；同一逻辑绘制可以为原生像素验证重复到离屏目标，但该验证副本 MUST NOT 被解释为通用多 pass、计算或依赖图能力。

#### Scenario: C# 输入决定原生图内容

- **WHEN** C# 添加缓冲、设置顶点流（两属性或单 `POSITION`）、添加三角形 pass、指定呈现目标并提交
- **THEN** 原生 `GPUGraph` MUST 包含对应的缓冲描述、上传字节、与调用方设置逐项一致的属性集合（两属性布局恰含 `POSITION` 与 `COLOR` 两条；单 `POSITION` 布局恰含一条 `POSITION`）、着色器路径、顶点数和清除色，且提交使用引擎内部新建的调度器

#### Scenario: 修改脚本顶点数据必须改变真实结果

- **WHEN** 在保持其他输入不变的情况下，仅改变 C# 顶点缓冲区中的一个颜色值
- **THEN** 离屏目标的对应像素 MUST 随之改变；不得由 C++ 用硬编码三角形替代脚本输入

#### Scenario: 验证镜像不扩大为通用图能力

- **WHEN** 为像素验证把同一逻辑绘制重复到离屏目标
- **THEN** 两个目标 MUST 使用相同的着色器、顶点流、缓冲、顶点数和清除色；适配面 MUST NOT 因此接受 pass 依赖、计算调度或通用附件组合

### Requirement: 适配器必须在进入后端前拒绝无效图与无效参数

缓冲大小不一致、名称重复、属性越过步长、未知流或缓冲、顶点数为零或超过缓冲元素数、非后台缓冲呈现、空目标和空图提交 MUST 返回非零状态，且 MUST NOT 进入具体后端执行。可失败的建图与提交方法 SHALL 返回 `int32_t` 状态，0 表示成功；对应对象的 `GetLastError()` MUST 返回包含参数名的错误文本，成功调用 MUST 清空旧错误，原生异常 MUST NOT 穿过边界。

四个脚本接口（`IScriptRenderTarget`、`IScriptRenderGraph`、`IScriptRenderBackend`、`IScriptShaderStruct`）的每个纯虚方法 MUST 在当前唯一可绑定面审计表中有且只有一行，重复行和表内汇总数字不符 MUST 使审计失败。生成物 MUST 通过真实接口无桩和公开表面判据。

#### Scenario: 无效布局或绘制数不得进入实现

- **WHEN** 位置或颜色属性的 `offset + 12` 超过步长，或绘制数超过对应缓冲描述的元素数
- **THEN** 适配器 MUST 在调用 `RenderPass` 或后端执行前返回非零状态，`GetLastError()` MUST 点名参数，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 未审计或重复审计的方法不得悄悄生成

- **WHEN** 给任一渲染适配接口新增纯虚方法但未更新唯一审计表，或把同一方法登记两次，或表内汇总数字与实际不符
- **THEN** `check_bindable_audit.py` 与构建 MUST 非零退出并点名接口、方法或不符汇总；补齐审计后生成物 MUST 仍满足真实接口无桩和两侧方法对齐
