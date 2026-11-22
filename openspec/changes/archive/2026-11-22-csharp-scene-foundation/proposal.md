# Proposal: csharp-scene-foundation（阶段 5 首切片：C# 场景模型 + 序列化 + 脚本生命周期 + 场景→图）

## Why

产品目标是「初步可用的游戏引擎 + 编辑器」。阶段 4 已让 C# 能建资源/设参数/建图提交（8 接口 + 糖层全量直绑），阶段 6 编辑器首切片 `editor-window-panel` 已落地（单窗口分栏 + 本机服务 + 对象编号 + 协议）。但**现在 C# 侧没有任何场景/实体/组件数据模型**（`Scripting/` 下 grep `Scene|Entity|Component|GameObject` 仅命中编辑器的 `EditorSceneView` 渲染视图，数据模型 0 行），脚本只有一个一次性静态入口 `UserScript.OnEngineReady`（`csharp-script` 主规范钉死「整个进程只调一次」），`Host.Tick` 拿到的 `deltaSeconds` 只打日志、没传给任何脚本。

后果：编辑器现在只能改窗口标题/尺寸，**没有可编辑的游戏内容**；C# 脚本无法「操作场景对象」，场景无法「保存和加载」——这正是路线图阶段 5 的阶段结果（「C# 脚本能够操作场景对象，场景能够保存和加载，资源与脚本生命周期能够形成一致闭环」），也是阶段 6 场景编辑切片的前置（路线图阶段 6 段末：「场景编辑依赖阶段 5 的 C# 场景模型与序列化」）。

路线图 2026-11-11 已澄清：阶段 5 这些模块**全是 C# 框架层代码**，坐在阶段 4 已齐的直绑面上，**不依赖任何新 C++ 能力**。本切片据此只做 C# 层，不动 C++ 接口与绑定面。

## What Changes

- **C# 场景数据模型（新建，`Scripting/Managed/Scene/`）**：`Scene { Name, Entities }`；`Entity { Id（持久 uuid 字符串）, Name, Transform, MeshRenderer? }`；`Transform { Position, Rotation, Scale }`（float3）；`MeshRenderer { ShaderPath, Color, 内联三角形顶点 }`。首切片**不做父子层级、不做完整 ECS、不做任意网格/相机/材质**（路线图「首版不要求完整 ECS」）。持久 id 用 uuid 字符串，**不复用编辑器运行期整数编号**（那个服务重启就变，见 `EditorService.s_nextObjectId`）。
- **场景序列化（新建）**：带 `formatVersion` 的 JSON 文档，`System.Text.Json` 手写编解码（`Utf8JsonWriter`/`JsonDocument`，与编辑器协议同技术栈、零 NuGet），`System.IO` 读写文件。**不选 CodeGen 的 C# codec**：调查实测它只服务跨 ABI 的 C++ 记录（122 份 .cs 里仅 20 份有 codec）、`internal` 可见、块头是类型/布局哈希而非文件版本、反序列化对 map/接口引用/定长数组/值类/联合体直接抛（单向），不能用于 C# 自有场景类型；走 CodeGen 等于把场景类型做成 C++ 进绑定面，违背「阶段 5 全 C# 层」。**加载失败显式报错不静默**：版本不符/必填字段缺失/JSON 截断/资源名解析不到都抛带名错误，不返回半个场景、不静默补默认值（沿用 io-error-channel/FileLoader 的教训）。
- **资源引用按名持久化**：场景文件只存着色器路径/资源**名字**，不存句柄——句柄是 `(代际<<48)|序号` 进程内有效、不可持久化（`ImageHandle/BufferHandle` 的 Kind/Handle/Name 还是 internal）；加载期按名解析（`ImageHandle.FromName`/`BufferHandle.FromName` 现成通道）。
- **脚本生命周期（MODIFIED `csharp-script`）**：保留 `OnEngineReady` 一次性入口与全部既有判据不变，**新增** `UserScript.OnStart(Scene)` / `OnUpdate(Scene, double dt)` 两个钩子（默认空实现，既有脚本不受影响）；`Host.Tick` 把 `deltaSeconds` 传给场景运行时（目前被丢弃）。场景运行时无活动场景时零行为（默认关零行为，既有 18 用例不受影响）。
- **场景→图最小翻译（新建）**：场景运行时每帧把活动场景的 `MeshRenderer` 实体按 `Transform` 翻译成三角形 pass（复用糖层 `ScriptRenderBackend`/`ScriptRenderGraph`，`EditorSceneView` 是每帧翻译+Submit+WaitIdle 的现成先例），`SubmitGraph`+`WaitIdle`。首切片只做 Transform + 内联三角形；任意网格/相机/材质/纹理导入留后续切片。
- **测试**：C# 断言全部经 tester `TestCSharpScene` 执行（GPUBackendTester 经 hostfxr 加载 C#，序列化往返/错误路径/生命周期钩子被调 + dt 传递 + 场景渲染像素证据 + 保存/加载往返）；**CoreTests 是纯 C++ 工程不涉 C# 断言**；默认关零行为回归。
- **文档**：CSharpHost/README 场景节、场景文件格式说明、docs/testing.md 同步。

## Capabilities

### New Capabilities

- `csharp-scene`: C# 场景数据模型（持久身份 + Transform/MeshRenderer 最小成分）、带版本 JSON 序列化保存/加载与显式失败通道、资源按名持久化与加载期解析、每帧场景→图翻译与提交。

### Modified Capabilities

- `csharp-script`: 「脚本入口必须有固定签名，且只在一个时机被调用」要求扩为「入口 + 生命周期钩子」——保留 `OnEngineReady` 一次性与全部既有场景判据，新增 `OnStart(Scene)`/`OnUpdate(Scene, dt)` 钩子签名与调用时机、`Host.Tick` 向下传 `deltaSeconds`。

## Impact

- 产品（**场景框架全 C# 层，零 C++ 引擎/后端接口改动**）：新增 `Scripting/Managed/Scene/`（场景模型 + 序列化 + 场景运行时 + 场景→图翻译）；改 `Scripting/Managed/Host.cs`（Tick 传 dt + 调场景运行时）、`Scripting/Managed/UserScript.cs`（加 OnStart/OnUpdate 默认空钩子）。新文件走 SDK 默认通配编入同一程序集，**不改 csproj、不加 NuGet**。**tester `Test/GPUBackendTester/Main.cpp` 加 TestCSharpScene 用例（--list/run-all/用例函数）是 C++ 改动，如期列入**——「零 C++」仅指引擎/后端接口与绑定面，不含 tester。**另含一处必然的回归修复**：`Scripting/Managed/Editor/EditorService.cs` 一行（「面板静态根：」读数改走 LogServiceMarker 双通道，可见窗口模式也读得到），配合 Main.cpp 静态页判据期望值取自服务日志根——修复 editor 批 1.11 遗留的 hasIndex CWD 相对 bug，只加读数通道、不改编辑器服务行为/对象表。
- 规范：新增 `openspec/specs/csharp-scene/spec.md`；MODIFIED `openspec/specs/csharp-script/spec.md`（入口要求扩生命周期）。**不动** `editor-window-panel`（场景对象进编辑器另立 change）、不动绑定/审计/编号锁。
- 依赖：零新增（System.Text.Json/System.IO 均 BCL，编辑器已有先例）。
- 行为变化（故意的）：注册活动场景后每帧多一次场景→图翻译提交 + OnUpdate 调用；无活动场景时零行为（既有 18 用例与默认关读数不变，回归判据钉死）。
- 回归口径：构建绿；CoreTests 全绿（既有 C++ 断言）；两后端 18+1=19 用例（新增 TestCSharpScene）；绑定基线 0/0/0 + codegen 逐字节不变（证引擎/后端接口零改动）；默认关现有 18 用例 name/status 与改前基线（18 条）一致。
- 明确不做（Non-Goals，见 design）：父子层级/完整 ECS、任意网格/相机/材质/灯光、纹理资源导入、脚本组件注册表/热重载/多程序集、时间接口（dt 来自 Tick）、场景对象进编辑器、协议同源生成。