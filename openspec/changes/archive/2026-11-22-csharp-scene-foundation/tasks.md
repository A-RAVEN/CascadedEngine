# Tasks: csharp-scene-foundation

> 分工：构建（python build.py --config Debug）、CoreTests、GPUBackendTester（含交互/可见窗口）、截图视觉验收由主循环代跑；执行者做代码/测试/文档编辑与只读检查，需要读数时请求主循环。红绿纪律：缺陷态读数先于修复存档；构造不出的如实登记。场景框架全 C# 层，零 C++ 引擎/接口改动（tester Main.cpp 加 TestCSharpScene 是 C++ 改动，如期）。**C# 断言一律经 tester TestCSharpScene 执行（hostfxr 加载 C#）；CoreTests 是纯 C++ 工程，不涉 C# 断言。**

## 0. 开工清点

- [x] 0.1 五项清点（命令+输出存档 evidence/0.1-清点.txt）：① float3 镜像形态（读 out/build/x64-debug/codegen/bindings/graphics_backend__float3.Bindings.cs，确认 x/y/z 字段）；② ImageHandle.FromName/BufferHandle.FromName 签名（读生成产物）；③ 糖层 SubmitGraph 重复提交能力（读 NativeRenderSugar.cs 相关段 + EditorSceneView 每帧翻译先例）；④ Host.Tick 的 deltaSeconds 现状（读 Host.cs，确认目前仅日志）；⑤ UserScript.OnEngineReady 与 csharp-script 主规范一次性判据（读 UserScript.cs + spec）。验收：五项输出齐备；与 design 假设不符即修订 design 并记录。
- [x] 0.2 codegen 快照（主循环）：采用 editor 批首次构建前快照 %TEMP%\ewp-codegen-before（77 份 *.codegen.h，保留相对结构）作跨批基线锚（editor 批已验 codegen 不变、scene 批零接口改动，有效 pre-scene 基线）。fc /b 对拍总=77 差异=0。evidence/0.2-4.1-4.2-快照回归基线.txt。

## 1. 场景模型与序列化（C#，断言经 tester TestCSharpScene 红绿）

- [x] 1.1 数据模型：新增 Scripting/Managed/Scene/Scene.cs（Scene/Entity/Transform/MeshRenderer，命名空间 Scripting.Managed.Scene）；Entity.Id 用 Guid.NewGuid().ToString("D") 持久 uuid；Transform 三分量 float3；MeshRenderer 含 ShaderPath/Color/内联三角形顶点；无父子层级。验收：TestCSharpScene 断言（C# 经 hostfxr）创建实体 id 为非空 uuid、Transform/MeshRenderer 字段齐全；新文件经 SDK 通配编入（不改 csproj）。
- [x] 1.2 序列化编解码：新增 Scene/SceneCodec.cs，手写 Utf8JsonWriter（Save）/JsonDocument（Load），带 formatVersion=1；逐字段显式读写。验收：TestCSharpScene 往返断言——建两实体场景 Save→Load 逐字段相等（id/名称/Transform 三分量/MeshRenderer 各字段）；改一个 Position 分量再 Save 两文件字节必不同（防空对象）。红绿：若误用反射默认序列化生成镜像（public 字段漏掉）往返必红。
- [x] 1.3 失败通道：SceneCodec.Load 遇 JSON 解析失败/截断、formatVersion 缺失或不支持、必填字段缺失、字段类型错抛 SceneFormatException（新增 Scene/SceneFormatException.cs）点名问题、不返回半场景；Save IO 失败抛带路径名错误。**Load 只验格式/结构，不查资源存在性**（无后端入口；坏资源名在注册/渲染期报错，见 3.2）。验收：TestCSharpScene 三条红态——截断文件/版本999/缺id 各抛 SceneFormatException 且不返回场景；改前若静默补默认值即红。

## 2. 脚本生命周期（MODIFIED csharp-script）

- [x] 2.1 UserScript 钩子：UserScript.cs 加 OnStart(Scene)/OnUpdate(Scene,double dt) 默认空实现；OnEngineReady 与既有逻辑不动。验收：TestCSharpScene/走读确认默认空钩子不抛；TestScriptHost/TestCSharpWindow 既有判据不变（4.1 回归）。
- [x] 2.2 场景运行时：新增 Scene/SceneRuntime.cs（静态类，持可选活动 Scene；Register 调一次 OnStart；Tick(dt) 有活动场景调 OnUpdate(scene,dt)+场景→图翻译，无则立即返回零行为）。验收：TestCSharpScene 断言 Register 调 OnStart 一次、重复注册不重复调；无注册 Tick 零调用。
- [x] 2.3 Host.Tick 传 dt：Host.cs 在 EditorService.Tick 之后调 SceneRuntime.Tick(deltaSeconds)（把目前仅日志的 dt 下传）。验收：tester 读数行 OnUpdate 收到的 dt 等于该帧 deltaSeconds（>0）；无活动场景时 Host.Tick 行为与改前一致（默认关零行为，4.1）。红绿：改前 dt 被丢弃，OnUpdate 收不到 dt 即红。

## 3. 场景→图翻译与测试（tester 第 19 用例）

- [x] 3.1 场景→图翻译：SceneRuntime.Tick 每帧把 MeshRenderer 实体按 Transform.Position/Scale 施加到内联三角形顶点、Color 作顶点色、ShaderPath 建三角形 pass，窗口+同尺寸离屏捕获双目标，SubmitGraph+WaitIdle（复用糖层与 EditorSceneView 路径）。Rotation 序列化保留但渲染暂不施加（文档诚实登记）。验收：tester 场景渲染离屏捕获中心精确红（Readback，按格式色序）。
- [x] 3.2 tester TestCSharpScene：注册场景 ⇒ 读数行 OnStart 计数=1、OnUpdate 每帧计数递增且 dt>0；场景渲染离屏捕获中心精确红；保存→清空内存→Load→注册→渲染仍红；**坏资源名红态：注册/渲染引用不存在着色器路径的场景 ⇒ 明确报错点名该资源（后端 FindRootStructData 解析失败），不静默跳过/假成功**；序列化往返/截断/版本999/缺id 断言（1.1-1.3 的 C# 验收在此执行）。注册进 tester 清单（--list 19 行，排在 TestCSharpWindowCallbacks 之前）。验收：主循环两后端跑该用例全断言过，日志存档。
- [x] 3.3 截图视觉验收（AGENTS §2，主循环实测）：交互模式（--test TestCSharpScene --headless 10 --show-window-seconds 30）「C# 场景窗口」置前 + desktop-screenshot + read_image 核对：蓝底+中央大红三角+右上小绿三角，与离屏捕获一致。evidence/视觉验收.txt + 视觉验收-场景窗口.png。交互模式 Vulkan 可见（虚拟显示只阻 headless）。

## 4. 全量回归与基线

- [x] 4.1 全量回归（主循环实测）：构建绿；CoreTests 24 PASSED exit 0；两后端 19/19；新 19 去 TestCSharpScene=18 与改前基线 18 逐条 name+status 差异 0；零场景日志 grep 命中 0。evidence/4.1-全量-*.json + 4.1-改前基线-*.json + 0.2-4.1-4.2-快照回归基线.txt。含既有 TestEditorWindowPanel 判据修复（editor 批 1.11 遗留 hasIndex bug，见 4.1-既有用例判据修复.txt）。
- [x] 4.2 双基线（主循环实测）：check_binding_baseline.py 缺失 0/不一致 0/多余 0；codegen fc /b 对 0.2 快照总=77 差异=0（证引擎/后端接口零改动）。tester Main.cpp 加 TestCSharpScene 由 4.1 的 19/19 覆盖。evidence/0.2-4.1-4.2-快照回归基线.txt。

## 5. 文档

- [x] 5.1 CSharpHost/README 场景节（数据模型/文件格式 formatVersion+JSON 形状/失败码/生命周期钩子 OnStart/OnUpdate/激活方式 SceneRuntime.Register/默认零行为/Rotation 暂不渲染等限制诚实登记）；场景文件格式说明；docs/testing.md 19 用例与最小集合。验收：README 示例命令可复制运行（照抄 evidence 读数）；check_csharp_host_readme.py 问题 0 处。

## 修订记录

（执行期间对 proposal/design/specs/tasks 的任何修订，逐条记在此处：改前原文 / 改后原文 / 依据 / 谁裁定。）

### 2026-11-22 对抗验证裁决处置（并行攻击 agent 四面，主循环复核；5 个独立 blocking 全改，无伪证）

1. **C# 断言测试台凭空（工件一致性001 + 无中生有001）**：改前——proposal/design D8/tasks 1.1-1.3/2.1-2.2 把 C# SceneCodec/SceneRuntime 断言交给 CoreTests；改后——C# 断言全部经 tester TestCSharpScene 执行（hostfxr 加载 C#），CoreTests 不涉。依据：实测 Test/CoreTests 是纯 C++ 可执行文件（CMakeLists GLOB *.cpp + 只链 CACore/glm，0 个 .cs，无 hostfxr），仓库无 C# 测试工程，build.py 无 dotnet test。§7：溯源=源码实测；不放松=断言搬到真能跑 C# 的台、判据本体不变；本条即对照。
2. **用例数 17/18/19 自相矛盾（工件一致性002 + 影响面002）**：改前——多处「既有 17 用例」（editor-window-panel 加入前的旧数）与「18+1=19」并存；改后——既有 18、新增 TestCSharpScene 第 19、默认关基线 18 条。依据：Main.cpp --list 实为 18 条（editor 批已加 TestEditorWindowPanel）。§7：溯源=--list 实测；不放松=基线对拍补齐到 18 条不漏；本条即对照。
3. **MODIFIED 改标题无 RENAMED 归档必失败（工件一致性003 + 真实存在性001 + 影响面004）**：改前——csharp-script 增量把需求标题从「脚本入口必须有固定签名，且只在一个时机被调用」改为「脚本入口与生命周期钩子必须有固定签名和调用时机」，只有 ## MODIFIED；改后——加 ## RENAMED Requirements（FROM 旧标题 TO 新标题）+ ## MODIFIED 用新标题。依据：OpenSpec specs-apply.js 按标题逐字匹配 MODIFIED，改名须 RENAMED（openspec instructions specs 明示 FROM:/TO: 格式）；validate 现绿但归档才红。§7：溯源=OpenSpec 机制；不放松=五场景判据逐字保留；本条即对照。
4. **Load 期资源名校验不可实现（工件一致性004 + 真实存在性002 + 影响面001）**：改前——D4「模型预留名字字段」、spec R4「坏资源名 Load 抛 SceneFormatException」、tasks 1.3 坏资源名红态；改后——MeshRenderer 首切片只 ShaderPath/Color/顶点（删「预留名字字段」凭空表述）；Load 只验格式/结构（无后端入口）；资源存在性在注册/渲染期由后端 FindRootStructData 校验，坏资源名注册/渲染期报错（红态移到 3.2）。依据：实测 ImageHandle/BufferHandle.FromName 只 new 值对象（Kind=0/Handle=0 不查询不抛），Load 是纯 C# 文件操作无后端。§7：溯源=源码实测；不放松=坏资源名仍须报错只是移到真能校验的期；本条即对照。
5. **「零 C++ 改动」过宽 + Impact 漏 Main.cpp + 基线测不到 tester（影响面003）**：改前——proposal「零 C++ 改动」、Impact 未列 Main.cpp、tasks 4.2 只 codegen fc/b；改后——措辞改「零 C++ 引擎/后端接口改动」，Impact 明列 tester Main.cpp 加 TestCSharpScene 是 C++ 改动（如期），4.2 注明 codegen fc/b 证引擎/接口零改动、tester 改动由 4.1 的 19/19 全量跑覆盖。依据：TestCSharpScene 必改 Main.cpp（--list/run-all/用例函数），codegen fc/b 对 C++ 改动不可能变红。§7：溯源=测试结构事实；不放松=回归覆盖补齐 tester；本条即对照。

**攻击面确认属实（不报/不改）**：122 份 .cs/20 份 codec、8 接口、路线图 2026-11-11 澄清、FromName 存在、SubmitGraph 重复提交、EditorSceneView 每帧先例、float3 镜像 x/y/z、Handle Kind/Handle/Name internal、CodeGen 反序列化五类抛点、Host.Tick/UserScript.OnEngineReady/EditorService.s_nextObjectId 签名——均核实存在相符。

### 2026-11-22 4.1 全量回归暴露的既有测试判据修复（editor 批 1.11 遗留，不放松判据）

**现象（改前红态）**：从仓库根跑全量回归，两后端都只有一条红 —— `TestEditorWindowPanel`，错误逐字
`E_EDITOR_HTTP_PAGE: 缺面板资源时没有返回说明原因的错误页：status=200`（单跑同样红，不是顺序问题）；
同一份代码从 `bin/` 目录跑则绿。

**根因**：`Main.cpp` 的静态页判据用 **CWD 相对**探测
`const bool hasIndex = std::filesystem::exists("editor-web/index.html");`，
而服务侧 `EditorService.ResolveEditorWebRoot()` 从 editor 批 1.11 起返回**绝对路径**
（`AppContext.BaseDirectory` / `Assembly.Location` / `ProcessPath` 下的 `editor-web`）。
cwd=仓库根时探测 false（期望 503 错误页）、服务实际从 `bin/editor-web/index.html` 返 200 ⇒ 判据与服务行为脱节。
与本 change 的场景改动无关（场景代码不碰编辑器路径），是 editor 批把静态根改成绝对路径后没跟着改判据。

**改法（改前 / 改后对照）**：

- 改前：`const bool hasIndex = std::filesystem::exists("editor-web/index.html");`
- 改后：
  ```cpp
  std::string staticRoot;
  if (!ReadLoggedValue(readingsPath, "面板静态根：", &staticRoot))
  {
      fail("E_EDITOR_HTTP_PAGE", "读数文件里没有「面板静态根：」行，静态页判据的期望值无法确定（服务没走 ServeStatic？）");
  }
  const bool hasIndex = std::filesystem::exists(std::filesystem::path(staticRoot) / "index.html");
  ```
- 配套（让这条读数在**可见窗口模式**下也读得到 —— 读数文件是唯一两种模式都存在的通道）：
  `EditorService.ServeStatic` 里 `Host.LogFromEditor("[编辑器] 面板静态根：" + root)` → `LogServiceMarker(...)`
  （双通道：宿主日志 + `CASCADED_EDITOR_READINGS_FILE`）。只加一条读数行，不改服务行为、不动对象表。

**§7 三条**：① 溯源 = 服务源码 `ResolveEditorWebRoot()`（返回绝对路径）+ 实跑错误原文；
② 不放松 = 断言本体不变（资源存在必须 200、缺失必须 503 且错误页点名 `editor-web`，两条分支都保留，503 分支仍可由删除/改名 `bin/editor-web` 构造），只是把**期望值的来源**从「测试端按 CWD 另算一遍」换成「服务自己解析出来的那个根」；
③ 对照 = 本节即改前/改后原文。实跑复验见 4.1 的重跑读数（evidence）。

### 2026-11-22 收尾工件补充（主循环，执行者建议，不放松判据）

6. **proposal Impact 补 EditorService.cs**：改前——Impact 只列 Scene/ + Host.cs + UserScript.cs + Main.cpp；改后——补「EditorService.cs 一行（面板静态根读数改走 LogServiceMarker 双通道）配合 Main.cpp 静态页判据修复，editor 批 1.11 遗留 hasIndex bug 的必然回归修复，只加读数通道不改服务行为/对象表」。依据：4.1 修既有判据实际动了该文件，Impact 须如实列。
7. **design D6 补两条实现期发现**：改前——D6 限制只列 Rotation 暂不施加/无相机/任意网格留后续；改后——补 ① 场景窗口自建 NewWindow（宿主 IWindow 表面 Token/Handle=0 不能 GetWindowHandle，自建带真实句柄，同四探针做法）② 按 ShaderPath 分组一着色器一 pass，多着色器时后组 pass 的 eClear 清掉前组绘制（正确合成需 additive 不清屏重载，留后续）。依据：实现期发现，原只写 README/代码注释，补进设计诚实登记。两条均不放松判据。