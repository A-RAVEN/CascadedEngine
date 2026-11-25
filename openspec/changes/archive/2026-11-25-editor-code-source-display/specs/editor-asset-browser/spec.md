## MODIFIED Requirements

### Requirement: 资产浏览器必须是底部通栏独立 dock 浏览资源根下的 ② 阶段文件

编辑器 MUST 有**底部通栏独立资产浏览器 dock**（与右侧对象检查器分离：独立 WebView2 控制器 + 独立页面；场景视口左上、对象检查器右上、资产浏览器底部通栏，三分区铺满客户区，仍单个原生壳窗口），列出资源根（门面 `GetResourceRootPath`）下的**数据型编辑器资产文件**（.tex/.mesh/.res/未注册源显示时的 .shLib 等）+ **代码型源条目**（kind=shader-source/csharp-source，由显示提供者产出，见 editor-code-source-display）。每行 MUST 含路径 / 种类（**两列，极简**——用户裁定条目只有资源路径+资源类型，提供者全权浏览器忠实渲染，删大小/修改时间/已注册等强加固定字段，见 editor-code-source-display D5）。**数据型条目** path MUST = 相对资源根路径、与真实文件逐条一致；**源条目** path MUST = 相对工程根路径（源文件在资源根外）。MUST 有刷新动作重新枚举。dock 高 MUST 可配（`CASCADED_EDITOR_DOCK_HEIGHT`，默认值与范围登记 README）。目录存在但无文件 = 合法空态（ok=true 空数组）；门面失败 / 根非法 / 枚举 IO 错误 MUST 显式 E_ASSET_ROOT，MUST NOT 用 ok 空数组冒充成功。本批 MUST 只读，MUST NOT 提供编辑/删除/重命名/导入。

#### Scenario: 数据型列表与真实文件逐条一致

- **WHEN** 资源根预置已知**数据型**文件（.tex/.mesh/.res；源显示 off 或用探针 shLib 落默认），客户端发 list-assets
- **THEN** 数据型条目 MUST 与真实文件逐条一致（**两项：path/kind，极简**）；path MUST = 相对资源根路径；条目 MUST 仅 path+kind 两字段（无 sizeBytes/modifiedUtc/registered/children/nodeId）

#### Scenario: 刷新可见新增

- **WHEN** 首次 list-assets 后向资源根新增一个**数据型**文件，再发 list-assets
- **THEN** 第二次结果 MUST 含新增文件条目；数据型条目数 MUST +1

#### Scenario: 空根显示空态

- **WHEN** 资源根存在但无文件，发 list-assets
- **THEN** 返回 MUST 为空数组（ok=true）；前端 MUST 显示空态文案，MUST NOT 报错

### Requirement: 协议加法必须以 PROTOCOL.md 为唯一事实源且三处镜像一致

新增 MUST 仅为加法：命令 `list-assets`（非对象作用域）、事件 `assets-changed`（无 objectId，`fields: { added: number; removed: number }`）、类型 `AssetEntry`（**path/kind 两字段，极简**——用户裁定条目只有资源路径+资源类型，删 sizeBytes/modifiedUtc/registered/children/nodeId 强加字段）、错误码 `E_ASSET_ROOT`。MUST 先改 `Scripting/Managed/Editor/PROTOCOL.md`，再镜像 `EditorWeb/src/protocol.ts` 与 `EditorProtocol.cs`，字段名/错误码逐字一致。协议版本 MUST 保持 1。**AssetEntry 路径基准 MUST 按种类区分**：数据型种类 path=相对资源根；源种类（shader-source/csharp-source）path=相对工程根（源文件在资源根外，editor-code-source-display 引入）。既有五条命令/六条错误码的形状语义 MUST NOT 改动；命令集六条、事件集三条、错误码七条。

#### Scenario: 三处镜像逐字一致

- **WHEN** 核对 PROTOCOL.md / protocol.ts / EditorProtocol.cs 的 AssetEntry 字段与 list-assets/assets-changed 定义
- **THEN** 字段名与错误码 MUST 逐字一致；协议版本 MUST 均为 1；既有五条命令形状不变；命令集 MUST 为六条、错误码 MUST 为七条（含 E_ASSET_ROOT）

#### Scenario: 源条目路径基准入协议

- **WHEN** 核对 PROTOCOL.md 2.6 对源种类（shader-source/csharp-source）路径基准的定义
- **THEN** MUST 写明源种类 path=相对工程根（区别于数据型相对资源根）；三处镜像 MUST 一致；前端 MUST 按 kind 区分渲染不混基准

### Requirement: 显示提供者必须资产中心且按种类注册，核心资源接口不动

MUST 有 `IDisplayProvider`（C# 编辑器侧）+ 注册表 + 默认提供者（一文件一条目）。注册表 MUST 支持**按种类**与**按种类+路径**两种注册（`Register(kind, provider)` 与 `Register(kind, path, provider)`，path 空＝种类默认）；`Resolve(kind, path)` MUST 先精确 (kind,path)、落回种类默认、再落默认提供者。接口输入 MUST 是资产标识（资产中心），输出 MUST 是显示条目（**path+kind 两字段**）。**提供者 MUST 纯投影**：不碰文件系统、不调门面（IO 由主线程命令做完填进 AssetContext，含源文件信息 SourceFiles）。显示逻辑 MUST NOT 加到 `IResource` 核心反向接口上。种类映射 MUST 覆盖已知扩展名（.shLib/.tex/.mesh/.res），未知扩展名 MUST 落默认种类而非报错。`EnumerateAssets` MUST 整合资产根文件枚举条目与注册显示资源条目，按 path 去重、按 path 序数排序；list-assets 两字段形状与 assets-changed 协议 MUST 不变。

#### Scenario: 默认提供者一文件一条目

- **WHEN** 经默认提供者描述一个文件资产
- **THEN** MUST 产出恰好一个条目（path+kind），path MUST 等于相对路径

#### Scenario: 核心接口零改动

- **WHEN** 核对本批改动面
- **THEN** `IResource.h` / `IResourceFactory.h` / 门面头 MUST 无改动；显示提供者 MUST 只在编辑器侧（Scripting/Managed/Editor/）

#### Scenario: 注册键按种类加路径

- **WHEN** 对 (kind, path) 注册提供者后 Resolve(kind, path) 与 Resolve(kind, 其它 path)
- **THEN** 精确 (kind,path) MUST 返回该提供者；其它 path MUST 落回种类默认；仅种类注册（path 空）MUST 仍兼容

#### Scenario: 提供者纯投影守契约

- **WHEN** 核对显示提供者实现
- **THEN** 提供者 MUST NOT 调 Directory/FileInfo/门面（IO 在主线程填 AssetContext.SourceFiles）；grep 提供者文件文件系统/门面调用 MUST 0

### Requirement: 资产浏览器必须有视觉与读回验收且回归零差异

MUST 新增 tester 第 23 用例 `TestEditorAssetBrowser`（--list 22→23）：C++ 侧 WebSocket 发 list-assets 断言条目与真实文件一致 + 刷新可见新增 + 空根空态。MUST 有真实打开界面的截图 + 主循环 read_image 核对底部通栏资产浏览器 dock 渲染（编辑器任务硬要求，无截图无条件不通过）。既有 22 用例 + CoreTests MUST 全绿；绑定产物 MUST 逐字节不变（本批不碰生成器/原生接口）。**代码型源显示的视觉验收在 editor-code-source-display（TestCodeSourceDisplay 钉死临时夹具）**；本要求的视觉夹具用**探针 shLib（ShaderLibraries/Browser.shLib，落默认仍显）+ 数据型探针资产**，不依赖真实 shLib 显隐。

#### Scenario: 用例清单与读回

- **WHEN** 跑 `GPUBackendTester.exe --list` 与 TestEditorAssetBrowser
- **THEN** MUST 列含 TestEditorAssetBrowser；用例 MUST 断言 list-assets 条目与预置文件逐条一致、刷新新增可见、空根空态

#### Scenario: 截图视觉验收

- **WHEN** 交互模式真实打开编辑器界面，底部通栏资产浏览器 dock 渲染**探针夹具**（临时根：ShaderLibraries/Browser.shLib 落默认 + CSharpProbe/*.res 数据型）
- **THEN** 截图 MUST 经 read_image 核对底部 dock 可见且列出预置资产行（**两列：路径/种类，极简，无大小/时间/已注册列**；路径 MUST 含探针文件 ShaderLibraries/Browser.shLib / CSharpProbe/Contrast.res / CSharpProbe/Imported_A.res / CSharpProbe/Imported_B.res）；抓窗 MUST 用 CoreWebView2.CapturePreviewAsync 经自检钩子落盘（不依赖前景），MUST NOT 置前/最小化/移动用户窗口（AGENTS §2）；抓不到内容或缺截图或画面不符 MUST 判不通过

#### Scenario: 空根空态与刷新可见（视觉）

- **WHEN** 交互资源根覆盖开关 `CASCADED_EDITOR_ASSET_ROOT` 指向临时空目录打开编辑器；及向临时根新增一个数据型文件后点 #refresh-assets
- **THEN** 空根截图 MUST 显示空态文案（非错误）；刷新后截图 MUST 比刷新前多一行；空根构造 MUST NOT 移动/删除用户 CAAssets 文件

#### Scenario: 回归零差异

- **WHEN** 本批完成后既有 22 用例同进程顺序跑、TestEditorAssetBrowser 独立进程跑、CoreTests、绑定基线比对
- **THEN** 既有 22 用例 name+status MUST 与上批基线 report 零差异、新用例独立进程两后端 MUST 过、CoreTests MUST exit 0 且 PASSED 26 行；既有绑定产物 MUST 逐字节不变（基线 0/0/0）