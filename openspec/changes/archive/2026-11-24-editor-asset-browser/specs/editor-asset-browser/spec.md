## ADDED Requirements

### Requirement: 资产浏览器必须是底部通栏独立 dock 浏览资源根下的 ② 阶段文件

编辑器 MUST 有**底部通栏独立资产浏览器 dock**（与右侧对象检查器分离：独立 WebView2 控制器 + 独立页面；场景视口左上、对象检查器右上、资产浏览器底部通栏，三分区铺满客户区，仍单个原生壳窗口），列出资源根（门面 `GetResourceRootPath`）下的编辑器资产文件，每行 MUST 含相对路径 / 种类（扩展名映射）/ 大小（字节）/ 修改时间 / 是否已注册（门面 `HasResource`）。MUST 有刷新动作重新枚举。dock 高 MUST 可配（`CASCADED_EDITOR_DOCK_HEIGHT`，默认值与范围登记 README）。目录存在但无文件 = 合法空态（ok=true 空数组）；门面失败 / 根非法 / 枚举 IO 错误 MUST 显式 E_ASSET_ROOT，MUST NOT 用 ok 空数组冒充成功。本批 MUST 只读，MUST NOT 提供编辑/删除/重命名/导入。

#### Scenario: 列表与真实文件逐条一致

- **WHEN** 资源根预置已知文件（含 .shLib 与测试资产，**已注册与未注册各至少一个**），客户端发 list-assets
- **THEN** 返回条目 MUST 与真实文件逐条一致（path/nodeId/sizeBytes/kind/modifiedUtc/registered/children 七项）；nodeId MUST = path = 相对资源根路径；children MUST 空；modifiedUtc MUST ISO 8601 UTC；sizeBytes MUST 等于 FileInfo 实测字节；registered MUST 等于 HasResource 读数且**真值与假值都出现**（恒假实现不得过）

#### Scenario: 刷新可见新增

- **WHEN** 首次 list-assets 后向资源根新增一个文件，再发 list-assets
- **THEN** 第二次结果 MUST 含新增文件条目；条目数 MUST +1

#### Scenario: 空根显示空态

- **WHEN** 资源根存在但无文件，发 list-assets
- **THEN** 返回 MUST 为空数组（ok=true）；前端 MUST 显示空态文案，MUST NOT 报错

### Requirement: 协议加法必须以 PROTOCOL.md 为唯一事实源且三处镜像一致

新增 MUST 仅为加法：命令 `list-assets`（非对象作用域）、事件 `assets-changed`（无 objectId，`fields: { added: number; removed: number }`）、类型 `AssetEntry`（nodeId/path/kind/sizeBytes/modifiedUtc/registered/children）、错误码 `E_ASSET_ROOT`。MUST 先改 `Scripting/Managed/Editor/PROTOCOL.md`，再镜像 `EditorWeb/src/protocol.ts` 与 `EditorProtocol.cs`，字段名/错误码逐字一致。协议版本 MUST 保持 1。`AssetEntry.nodeId` MUST 稳定（v1 = 相对路径），children MUST 为 v2 投影预留（v1 恒空）。既有五条命令/六条错误码的形状语义 MUST NOT 改动；命令集扩为六条、事件集扩为三条、错误码扩为七条——此即 MODIFIED `editor-window-panel` 协议要求（原为封闭枚举）。

#### Scenario: 三处镜像逐字一致

- **WHEN** 核对 PROTOCOL.md / protocol.ts / EditorProtocol.cs 的 AssetEntry 字段与 list-assets/assets-changed 定义
- **THEN** 字段名与错误码 MUST 逐字一致；协议版本 MUST 均为 1；既有五条命令形状不变；命令集 MUST 为六条、错误码 MUST 为七条（含 E_ASSET_ROOT）

#### Scenario: 未知命令仍被拒

- **WHEN** 客户端发一个不在命令集内的命令
- **THEN** MUST 返回 E_BAD_MESSAGE；list-assets MUST 被认识（非对象作用域，无 objectId 也合法）

### Requirement: 显示提供者必须资产中心且按种类注册，核心资源接口不动

MUST 有 `IDisplayProvider`（C# 编辑器侧）+ 按种类注册表 + 默认提供者（一文件一条目，children 空）。接口输入 MUST 是资产标识（资产中心），输出 MUST 是显示条目树——v1 退化文件=资产 1:1，为 v2 程序集展开/衍生物归组留口。显示逻辑 MUST NOT 加到 `IResource` 核心反向接口上。种类映射 MUST 覆盖已知扩展名（.shLib/.tex/.mesh/.res），未知扩展名 MUST 落默认种类而非报错。

#### Scenario: 默认提供者一文件一条目

- **WHEN** 经默认提供者描述一个文件资产
- **THEN** MUST 产出恰好一个条目，children MUST 为空，nodeId MUST 等于相对路径

#### Scenario: 核心接口零改动

- **WHEN** 核对本批改动面
- **THEN** `IResource.h` / `IResourceFactory.h` / 门面头 MUST 无改动；显示提供者 MUST 只在编辑器侧（Scripting/Managed/Editor/）

### Requirement: 资产枚举必须守线程边界且失败可见

文件枚举与门面调用 MUST 在引擎主线程的命令执行内；监听线程 MUST 只做网络 I/O 与形状校验，MUST NOT 调引擎绑定 API。门面未绑定 / 根路径非法 / 根目录不存在 MUST 显式报错（错误码 + 文本点名原因），MUST NOT 静默返回空列表冒充成功。

#### Scenario: 门面未绑显式报错

- **WHEN** 自检开关 `CASCADED_EDITOR_SELFTEST=no-facade` 跳过门面绑定后发 list-assets（运行期 CSharpHost 无条件登记门面，无开关此分支不可构造）
- **THEN** MUST 返回 E_ASSET_ROOT（文本点名门面未绑/根不可用），MUST NOT 返回 ok=true 的空列表

#### Scenario: 枚举异常显式报错不挂起

- **WHEN** 资源根指向不存在目录 / 无权限目录 / 空串 / 非法字符，或门面已绑但底层抛 1014（IResourceRegistryException），发 list-assets
- **THEN** MUST 在处理内捕获 IO 异常 / IResourceRegistryException / ArgumentException 转 E_ASSET_ROOT 回该 requestId，MUST NOT 穿出到 Host.Tick 外层 catch（那会使客户端无 result 静默挂起）

#### Scenario: 门面调用主线程归因（运行期）

- **WHEN** 执行 list-assets 命令（预置 N 个文件）
- **THEN** 命令计数 s_cmdCalls 增量 MUST = 1（GetResourceRootPath）+ N（HasResource），证明门面调用都在主线程命令上下文（每个门面调用 MUST NoteNativeCall）；监听线程路径 MUST 无门面调用（运行期读数归因，非仅代码核对）

### Requirement: 资产浏览器必须有视觉与读回验收且回归零差异

MUST 新增 tester 第 23 用例 `TestEditorAssetBrowser`（--list 22→23）：C++ 侧 WebSocket 发 list-assets 断言条目与真实文件一致 + 刷新可见新增 + 空根空态。MUST 有真实打开界面的截图 + 主循环 read_image 核对底部通栏资产浏览器 dock 渲染（编辑器任务硬要求，无截图无条件不通过）。既有 22 用例 + CoreTests MUST 全绿；绑定产物 MUST 逐字节不变（本批不碰生成器/原生接口）。

#### Scenario: 用例清单与读回

- **WHEN** 跑 `GPUBackendTester.exe --list` 与 TestEditorAssetBrowser
- **THEN** MUST 列 23 个用例含 TestEditorAssetBrowser；用例 MUST 断言 list-assets 条目与预置文件逐条一致、刷新新增可见、空根空态

#### Scenario: 截图视觉验收

- **WHEN** 交互模式真实打开编辑器界面（门控 + 真实根 CAAssets），底部通栏资产浏览器 dock 渲染预置资产
- **THEN** 截图 MUST 经 read_image 核对底部 dock 可见且列出预置资产行（五列：路径/种类/大小/修改时间/已注册；路径 MUST 含相对资源根全前缀：VulkanShaderLibrary.shLib / D3D12ShaderLibrary.shLib / CSharpProbe/Contrast.res / CSharpProbe/Imported_A.res / CSharpProbe/Imported_B.res）；抓窗方法 MUST 先实测正控验证能抓到 dock 的 WebView2 内容（首选 CoreWebView2.CapturePreviewAsync 经自检钩子落盘，不依赖前景；PrintWindow 不作假设），MUST NOT 置前/最小化/移动用户窗口（C:\Users\76956\.dsh\AGENTS.md §2）；抓不到内容或缺截图或画面不符 MUST 判不通过

#### Scenario: 空根空态与刷新可见（视觉）

- **WHEN** 交互资源根覆盖开关 `CASCADED_EDITOR_ASSET_ROOT` 指向临时空目录打开编辑器；及向真实根新增一个文件后点 #refresh-assets
- **THEN** 空根截图 MUST 显示空态文案（非错误）；刷新后截图 MUST 比刷新前多一行（新增文件可见，对照 4.1 的 +1 断言）；空根构造 MUST NOT 移动/删除用户 CAAssets 文件

#### Scenario: 回归零差异

- **WHEN** 本批完成后既有 22 用例同进程顺序跑、TestEditorAssetBrowser 独立进程跑（编辑器服务一次性，两个编辑器用例不同进程）、CoreTests、绑定基线比对
- **THEN** 既有 22 用例 name+status MUST 与上批基线 report 零差异、新用例独立进程两后端 MUST 过、CoreTests MUST exit 0 且 PASSED 26 行；既有绑定产物 MUST 逐字节不变（基线 0/0/0）