# Design: csharp-scene-foundation

## Context

阶段 4 全量直绑已齐（8 接口 + 糖层 `ScriptRenderBackend`/`ScriptRenderGraph`/`ScriptShaderStruct`），阶段 6 编辑器首切片已落地。调查事实（2026-11-22，只读子 agent 带路径引文核实）：

- C# 框架是单程序集「宿主 Host.cs + 糖层 NativeRenderSugar.cs + 一次性脚本 UserScript.cs + 编辑器 Editor/」；**场景数据模型 0 行**。新 .cs 走 SDK 默认通配编入，不改 csproj、不加 NuGet（本机宿主只加载一个程序集）。
- `Host.Tick(double deltaSeconds)` 是唯一每帧挂点；`dt` 目前只打日志（`LogLine($"Tick #{s_tickCount}：dt = {deltaSeconds:F3} 秒")`）没下传。`EditorService.Tick` 是每帧挂点先例。
- 脚本唯一入口 `UserScript.OnEngineReady(IWindowSystem)`，`csharp-script` 主规范钉死「整个进程只调一次」；无 OnStart/OnUpdate（grep 0 命中）。
- CodeGen 给 C# 发射的 codec 只服务跨 ABI 的 C++ 序列化块记录（122 份 .cs 仅 20 份有 `Serialize`），`internal` 可见、块头是 TypeNameHash/LayoutHash/FormatVersion、反序列化对 map/接口引用/定长数组/值类/联合体抛（单向）；**C# 自有类型不会被生成**。生成镜像全是 public 字段，System.Text.Json 默认不序列化字段（直接序列化得空对象）。
- 句柄不可持久化：`ImageHandle/BufferHandle` 是 `(代际<<48)|序号` 进程内值类，Kind/Handle/Name 均 internal；`FromName(string)` 是加载期按名重建通道。C++ 统一句柄表保证跨语言同一性。
- C# 直接用 System.IO/System.Text.Json 有先例（EditorService 静态资源/读数落盘、EditorProtocol 协议）；无 FileLoader/IOManager 的 C# 投影，C++ 那两套错误语义不约束 C#。
- 糖层建图只有三角形 pass + 三种顶点流（POSITION+COLOR / POSITION / POSITION+TEXCOORD）+ 固定 RGBA8 纹理；`SubmitGraph` 支持重复提交（已落盘跳过 Materialize 仍校验再执行）；`EditorSceneView` 是每帧翻译+Submit+WaitIdle 先例。

## Goals / Non-Goals

**Goals:**
- C# 场景数据模型（持久身份 + 最小成分）+ 带版本 JSON 保存/加载闭环 + 显式失败通道。
- 脚本生命周期钩子（OnStart/OnUpdate + dt 下传），C# 脚本能操作场景对象。
- 每帧场景→图最小翻译（Transform + 三角形）并真实渲染（像素证据）。
- 全 C# 层、零 C++ 接口/绑定改动；默认无活动场景零行为。

**Non-Goals:**
- 父子层级 / 完整 ECS / 通用组件_bag_（首切片 Transform + 可选 MeshRenderer 两个具体成分）。
- 任意网格 / 相机 / 材质 / 灯光 / 纹理资源导入（留后续切片；首切片内联三角形 + 着色器路径）。
- 脚本组件注册表 / 热重载 / 多脚本程序集（README 已列已知限制）。
- 时间接口（dt 来自 Host.Tick 的 deltaSeconds，不新增 C++ 时钟）。
- 场景对象进编辑器（编辑器对象编号现在只映射 IWindow，扩结构另立 change；本批不动 editor-window-panel 规范）。
- 协议同源生成 / 把 C++ 接口直接序列化给网页（TODO/005 排除项）。

## Decisions

**D1 数据模型形状（最小、持久身份）。** 命名空间 `Scripting.Managed.Scene`，新目录 `Scripting/Managed/Scene/`。
- `Scene { string Name; List<Entity> Entities; }`。
- `Entity { string Id; string Name; Transform Transform; MeshRenderer? MeshRenderer; }`——`Id` 是持久 **uuid 字符串**（`Guid.NewGuid().ToString("D")`），创建时生成、序列化往返不变；**不复用编辑器运行期整数编号**（`EditorService.s_nextObjectId` 服务重启就变，不能当持久 id）。首切片成分用具体可选字段（Transform 必有、MeshRenderer 可选），不做通用组件容器。
- `Transform { float3 Position; float3 Rotation; float3 Scale; }`（float3 用生成的 `Scripting.Generated.Bindings.GraphicsBackend.float3` 镜像，或自定义三 float；Rotation 首切片欧拉角，翻译时仅用 Position/Scale 平移缩放三角形，Rotation 序列化保留但渲染暂不施加——诚实登记，见 D6）。
- `MeshRenderer { string ShaderPath; float3 Color; 内联三角形顶点 }`——首切片顶点是写死的三角形局部坐标（与 TestSimpleTriangle 同形），ShaderPath 存字符串（如 `Shaders/Test/TestSimpleTriangle`）。
- 不做父子层级（Entity 无 parent/children）。

**D2 序列化方案：带版本 JSON + 手写编解码（不选 CodeGen）。** 文档形如 `{ "formatVersion": 1, "scene": { "name": ..., "entities": [ { "id":..., "name":..., "transform": {"position":[x,y,z],"rotation":[...],"scale":[...]}, "meshRenderer": {"shaderPath":...,"color":[...],"vertices":[...]} } ] } }`。
- 用 `System.Text.Json` 的 `Utf8JsonWriter`（写）/ `JsonDocument`（读）**手写** `SceneCodec.Save(Scene, path)` / `SceneCodec.Load(path)`，逐字段显式读写与校验（与 EditorProtocol 同风格，不用反射默认序列化）。**依据**：① BCL 零 NuGet，与编辑器协议同栈；② 人类可读便于逐字段对拍；③ CodeGen C# codec 只覆盖跨 ABI C++ 记录、internal、块头是类型/布局哈希非文件版本、反序列化单向（调查第 2 节），不能写 C# 自有类型；走 CodeGen 等于把场景类型做成 C++ 进绑定面，违背路线图「阶段 5 全 C# 层、不依赖新 C++」。
- `formatVersion` 是显式文件版本常量（当前 1），与 CodeGen 的 FormatVersion 无关。
- **不用 `JsonSerializer.Serialize(生成镜像)`**：生成镜像是 public 字段，STJ 默认不序列化字段会得空对象（调查实测陷阱）；手写编解码绕开此陷阱且校验可控。

**D3 失败通道：显式报错不静默（沿用 io-error-channel 教训）。** `SceneCodec.Load` 遇下列情形 MUST 抛 `SceneFormatException`（自定义异常，消息点名具体问题）且不返回半个场景：① JSON 解析失败/文件截断；② `formatVersion` 缺失或不等于当前支持版本；③ 必填字段缺失（entity 无 id/transform 等）；④ 字段类型错（如 position 不是三元数组）。`Save` 遇 IO 失败（目录不存在/写盘失败）MUST 抛带路径名的错误。**MUST NOT 静默补默认值、MUST NOT 返回部分场景**。**Load 只做格式/结构校验，不查资源存在性**（Load 是纯 C# 文件操作，无后端入口；资源存在性见 D4 注册/渲染期）。红绿：截断文件/未知版本/缺字段各造一条红态（改前若静默吞掉即红）。

**D4 资源引用按名持久化、注册/渲染期解析校验（修订：Load 期验不了资源）。** 场景文件只存 `ShaderPath`/资源**名字**字符串，**不存句柄**（句柄 `(代际<<48)|序号` 进程内有效、不可持久化，Kind/Handle/Name internal）。**资源存在性在注册/渲染期（后端可用时）校验，不在 Load 期**：`ImageHandle.FromName`/`BufferHandle.FromName` 只构造值对象（Kind=0/Handle=0，不查询不抛——实测）；着色器路径存在性由糖层建图时后端解析（`FindRootStructData`→`m_Native.FindShaderRootStructData`）校验，解析不到 MUST 在注册/渲染期明确报错点名该资源、不静默跳过。**首切片 MeshRenderer 只含 ShaderPath/Color/内联顶点，无纹理/缓冲名字段**（纹理/缓冲引用留后续切片，删除早先「模型预留名字字段」的凭空表述）。跨语言同一性由 C++ 统一句柄表反查保证（现成）。

**D5 脚本生命周期（MODIFIED csharp-script，additive 不破坏既有）。**
- 保留 `UserScript.OnEngineReady(IWindowSystem)` 一次性入口与 `csharp-script` 全部既有场景判据（两张表绑好调一次、重绑不调、半绑不调、失败不冒充）**逐字不变**。
- **新增** `UserScript.OnStart(Scene scene)` / `UserScript.OnUpdate(Scene scene, double dt)`，**默认空实现**（既有脚本/测试不受影响）。
- 新增 `SceneRuntime`（静态类，`Scripting/Managed/Scene/`）：持一个可选活动 `Scene`；`Register(Scene)` 注册活动场景并调一次 `OnStart`；`Tick(double dt)` 在有活动场景时调 `OnUpdate(scene, dt)` + 场景→图翻译（D6），无活动场景立即返回（零行为）。
- `Host.Tick` 把 `deltaSeconds` 传给 `SceneRuntime.Tick(dt)`（目前 dt 被丢弃）；挂点在 `EditorService.Tick` 之后。
- 激活方式：脚本/测试经 `SceneRuntime.Register` 注册场景才激活；默认无注册 ⇒ 零行为（既有 18 用例不受影响，回归判据钉死）。不做脚本组件注册表（OnStart/OnUpdate 是全局静态钩子，与 OnEngineReady 同形态）。

**D6 场景→图最小翻译（复用糖层，诚实登记限制）。** `SceneRuntime.Tick` 每帧把活动场景的 MeshRenderer 实体翻译成图：取 Transform.Position/Scale 施加到内联三角形局部顶点（平移+缩放），Color 作顶点色，ShaderPath 建三角形 pass，窗口目标 + 同尺寸离屏捕获双目标，`SubmitGraph`+`WaitIdle`（复用 `EditorSceneView` 路径与糖层重复提交能力）。**首切片限制诚实登记**：Rotation 序列化保留但渲染暂不施加（三角形旋转留后续）；无相机（视口固定）；任意网格/材质/纹理留后续。像素证据沿用「离屏捕获 + Readback 精确像素」（不用「没崩」代替）。**两条实现期发现（补登记）**：① 场景窗口是场景运行时按宿主窗口尺寸**自己 NewWindow** 建的——宿主 EnsureBound 拿到的 IWindow 表面 Token/Handle=0，拿去做 GetWindowHandle 会被判句柄失效；自建表面带真实句柄（与四个渲染探针同做法），窗口只建一次不关（弱持有，截图验收留最后一帧）。② **按 ShaderPath 分组、一着色器一 pass**（同着色器实体并进同一 pass）；糖层每 pass 自带 eAttachmentLoadOp.eClear，**两种以上着色器时后组 pass 会清掉前组绘制**——正确合成需 additive 不清屏重载，留后续切片。

**D7 对象身份与编辑器边界。** 场景持久 id（uuid 字符串）与编辑器运行期编号（单调整数）是两套，互不复用。场景对象进编辑器（扩 `EditorObject` 加 kind、网页投影场景树）另立 change，本批**不动** `editor-window-panel` 规范与 `EditorService` 对象表。

**D8 测试与验收（都能失败；修订：C# 断言只能在 tester 跑，CoreTests 是纯 C++ 工程）。**
- **C# 断言全部经 tester `TestCSharpScene` 执行**（GPUBackendTester 经 hostfxr 加载 Scripting.Managed，C# 代码真跑）：序列化往返（建场景→Save→Load→逐字段相等，id 不变）；改一个字段再 Save 文件字节必不同；截断/未知版本/缺字段各抛 SceneFormatException 且不返回半场景；坏资源名注册/渲染期报错。**CoreTests 不涉及**——实测 Test/CoreTests 是纯 C++ 可执行文件（CMakeLists GLOB *.cpp + 只链 CACore/glm，0 个 .cs，无 hostfxr），仓库无 C# 测试工程，C# 断言无处在 CoreTests 执行（删除早先「CoreTests 断言 C#」的凭空表述）。
- tester `TestCSharpScene`（第 19 用例，两后端）：注册场景 ⇒ OnStart 调一次、OnUpdate 每帧调且 dt>0 被收到（读数行计数）；场景渲染离屏捕获中心精确红（Readback）；保存→清空内存→加载→渲染仍红。
- 默认关零行为：不注册场景跑现有 18 用例，name/status 与改前基线（18 条）一致、无场景日志/无额外渲染。
- 双基线：绑定 0/0/0 + codegen 逐字节不变（证**引擎/后端接口**零改动）；tester Main.cpp 加 TestCSharpScene 是 C++ 改动（如期），由 19/19 全量跑覆盖，不在 codegen fc/b 范围。

**D9 文档。** CSharpHost/README 场景节（数据模型/文件格式/生命周期钩子/激活方式/默认零行为）；场景文件格式说明（formatVersion + JSON 形状 + 失败码）；docs/testing.md 19 用例与最小集合。

## Risks / Trade-offs

- [手写 JSON 编解码漏字段] → D8 往返逐字段对拍 + 改字段字节必不同判据钉死；编解码与模型字段一一对应走读。
- [STJ 字段陷阱误用] → D2 明确手写 Utf8JsonWriter/JsonDocument，不用反射默认序列化生成镜像。
- [生命周期改动破坏既有脚本测试] → D5 additive：OnEngineReady 判据逐字保留，OnStart/OnUpdate 默认空，无注册零行为；TestScriptHost/TestCSharpWindow 回归钉死。
- [句柄误持久化] → D4 文件只存名字，加载 FromName 解析；代码审查 + 往返测试确认文件无句柄数值。
- [场景→图与编辑器渲染互相干扰] → 场景运行时与 EditorService 各自独立挂点；默认两者都不激活则零行为；测试分别门控。
- [Rotation 序列化但不渲染被误以为完整] → D6 诚实登记限制，Non-Goals 写明，文档标注。

## Migration Plan

1. 0.x 清点（核 float3 镜像/FromName/SubmitGraph 重复提交/Host.Tick dt 现状）+ codegen 快照。
2. 1.x 场景模型 + 序列化 + 失败通道（C#，CoreTests 红绿）。
3. 2.x 生命周期（UserScript 钩子 + SceneRuntime + Host.Tick 传 dt）+ csharp-script MODIFIED。
4. 3.x 场景→图翻译 + tester TestCSharpScene（两后端像素 + 生命周期读数）。
5. 4.x 全量回归 + 双基线 + 默认关零行为。
6. 5.x 文档。

## Open Questions

- 无阻塞性未决。Rotation 渲染、任意网格、相机、纹理导入、场景进编辑器均已明确列后续切片（Non-Goals），不在本批裁定。