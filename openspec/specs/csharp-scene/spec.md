# csharp-scene Specification

## Purpose
C# 场景数据模型（持久 uuid 身份 + Transform/MeshRenderer 最小成分）、带版本 JSON 序列化保存加载与显式失败通道、资源按名持久化注册渲染期校验、脚本生命周期钩子、每帧场景→图翻译与像素证据。
## Requirements
### Requirement: 场景数据模型必须有持久身份与最小成分
C# 场景框架（`Scripting/Managed/Scene/`，命名空间 `Scripting.Managed.Scene`）MUST 提供 `Scene`（名称 + 实体列表）、`Entity`（持久 id + 名称 + Transform + 可选 MeshRenderer）、`Transform`（Position/Rotation/Scale 三个 float3）、`MeshRenderer`（着色器路径 + 颜色 + 内联三角形顶点）。实体 id MUST 是创建时生成的持久 uuid 字符串，序列化往返保持不变；MUST NOT 复用编辑器运行期整数编号（`EditorService.s_nextObjectId`，服务重启即变）。首切片 MUST NOT 含父子层级、通用组件容器、任意网格/相机/材质。

#### Scenario: 实体带持久 uuid 身份
- **WHEN** 创建一个实体
- **THEN** 其 id MUST 是非空 uuid 字符串；保存场景再加载后同一实体的 id MUST 逐字相同

#### Scenario: 最小成分齐全
- **WHEN** 创建一个带 Transform 与 MeshRenderer 的实体
- **THEN** Transform MUST 含 Position/Rotation/Scale 三个 float3，MeshRenderer MUST 含着色器路径/颜色/内联三角形顶点

### Requirement: 场景必须能保存为带版本 JSON 并加载还原
场景 MUST 能经 `SceneCodec.Save(Scene, path)` 保存为带 `formatVersion`（当前 1）的 JSON 文件、经 `SceneCodec.Load(path)` 加载还原。编解码 MUST 用 System.Text.Json 手写（Utf8JsonWriter/JsonDocument），MUST NOT 用 CodeGen 的 C# codec（其只覆盖跨 ABI C++ 记录、反序列化单向），MUST NOT 用反射默认序列化生成镜像（public 字段会被 STJ 默认漏掉）。保存→加载往返 MUST 逐字段相等（实体 id/名称/Transform 各分量/MeshRenderer 各字段）。

#### Scenario: 保存加载往返逐字段相等
- **WHEN** 建一个含两实体（各带 Transform 与 MeshRenderer）的场景，Save 到文件，再 Load 回来
- **THEN** 加载场景的名称、实体数、每个实体的 id/名称/Transform 三分量/MeshRenderer 各字段 MUST 与保存前逐字段相等

#### Scenario: 改字段再保存文件必不同
- **WHEN** 保存场景后修改某实体 Transform.Position 的一个分量再保存到另一文件
- **THEN** 两份文件字节 MUST 不同（证明字段真写进了文件，非空对象）

### Requirement: 场景加载失败必须显式报错不静默
`SceneCodec.Load` 遇 JSON 解析失败/文件截断、`formatVersion` 缺失或不支持、必填字段缺失、字段类型错，MUST 抛带具体问题名的 `SceneFormatException` 且 MUST NOT 返回部分场景；`Save` 遇 IO 失败 MUST 抛带路径名的错误。MUST NOT 静默补默认值、MUST NOT 静默返回半个场景。**Load 只做格式/结构校验，不查资源存在性**（Load 无后端入口；资源存在性在注册/渲染期校验，见下条）。

#### Scenario: 截断文件报错不返回半场景
- **WHEN** 把一个合法场景文件截断成不完整 JSON 后 Load
- **THEN** MUST 抛 SceneFormatException（消息点名解析失败），MUST NOT 返回任何场景对象

#### Scenario: 未知格式版本报错
- **WHEN** Load 一个 formatVersion=999 的文件
- **THEN** MUST 抛 SceneFormatException 点名版本不支持，MUST NOT 当成当前版本解析

#### Scenario: 必填字段缺失报错
- **WHEN** Load 一个实体缺 id 或缺 transform 的文件
- **THEN** MUST 抛 SceneFormatException 点名缺失字段，MUST NOT 静默补默认值

### Requirement: 场景资源引用必须按名持久化、注册渲染期解析校验
场景文件 MUST 只存着色器路径/资源名字字符串，MUST NOT 存原生句柄数值（句柄是进程内代际值、不可持久化）。**资源存在性 MUST 在注册/渲染期（后端可用时）校验，不在 Load 期**：`ImageHandle.FromName`/`BufferHandle.FromName` 只构造值对象（Kind=0/Handle=0，不查询不抛），着色器路径存在性由糖层建图时后端解析（FindRootStructData）校验；引用不存在的资源 MUST 在注册/渲染期明确报错点名该资源，MUST NOT 静默跳过该实体。首切片 MeshRenderer 只含 ShaderPath/Color/内联顶点，无纹理/缓冲名字段（纹理/缓冲引用留后续切片）。

#### Scenario: 文件不含句柄数值
- **WHEN** 保存一个引用资源的场景并检查文件内容
- **THEN** 文件 MUST 只含资源名字/路径字符串，MUST NOT 含句柄 token 数值

#### Scenario: 坏资源名注册渲染报错
- **WHEN** 注册并渲染一个 MeshRenderer 引用不存在着色器路径的场景
- **THEN** MUST 在注册/渲染期明确报错点名该资源名（后端解析失败），MUST NOT 静默跳过该实体或假成功

### Requirement: 场景必须每帧翻译为渲染图并提交且有像素证据
注册活动场景后，场景运行时 MUST 每帧把 MeshRenderer 实体按 Transform（首切片 Position/Scale）翻译成三角形 pass（复用糖层 ScriptRenderBackend/ScriptRenderGraph），窗口目标 + 同尺寸离屏捕获双目标，SubmitGraph + WaitIdle。无活动场景时 MUST 零行为（不建图、不渲染、不打场景日志）。验收 MUST 含离屏捕获中心像素精确证据（Readback），MUST NOT 以「没崩」代替。首切片 Rotation 序列化保留但渲染暂不施加，MUST 在文档诚实登记。

#### Scenario: 场景渲染中心像素红
- **WHEN** 注册一个含红色三角形 MeshRenderer 的场景并跑过至少一帧，C++ 侧 Readback 离屏捕获中心像素
- **THEN** 中心像素 MUST 为精确红色（按格式色序），证明场景真驱动了渲染

#### Scenario: 无活动场景零行为
- **WHEN** 不注册任何场景跑现有全部 tester 用例
- **THEN** MUST NOT 出现场景渲染、场景日志或额外图提交，现有用例 name/status MUST 与改前基线一致

#### Scenario: 加载后的场景仍能渲染
- **WHEN** 保存场景→清空内存→Load 回来→注册并跑帧
- **THEN** 离屏捕获中心像素 MUST 仍为精确红色（加载还原的场景驱动渲染与保存前一致）
