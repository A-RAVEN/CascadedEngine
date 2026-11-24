# Design: editor-asset-browser

## Context

资产层地基刚落地（`csharp-resource-asset-foundation`，2026-11-23 归档）：宿主层门面 `cs_script::IResourceRegistry`（编号 13，九方法，含 `GetResourceRootPath` / `HasResource`）、C# 贴图/网格资产、自读加载。资源根 CAAssets 现有内容实测：`VulkanShaderLibrary.shLib`（145710 B）、`D3D12ShaderLibrary.shLib`（293737 B）、`CSharpProbe/*.res` 探针残留。贴图/网格尚未进真实根（探针写临时目录）——浏览器先走内容偏少，但链路真。

编辑器现状（`editor-window-panel` 归档）：单窗口分栏壳（左原生 scene view + 右 WebView2 面板），HttpListener/WebSocket 服务（监听线程只做 I/O + 形状校验，引擎调用全在主线程 Host.Tick），协议 v1 以 `Scripting/Managed/Editor/PROTOCOL.md` 为唯一事实源（`protocol.ts` + `EditorProtocol.cs` 镜像），前端纯 TS（index.html DOM + main.ts，无框架）。命令：get-object / list-objects / set-title / set-size / undo；事件：object-changed / object-invalidated。

### 资产模型（2026-11-23 探索结论，约束本批与后续批）

1. **三阶段**：① 源文件（DCC/源码）→ ② 编辑器资产（导入/编译产物，引擎可用）→ ③ 运行时资产（按平台 cook）。**编辑器浏览 ②**；③ 是 ②+平台设置的纯函数，永不手改（阶段 7）。
2. **② 共享真相（Unreal 式）**：② 是项目拥有的共享产物，改源的人导入、其余同步，**拒绝 Unity 式每机全量重导入**。前提：导入确定性 + 内容哈希增量（身份重设计批）。
3. **代码型 vs 数据型**：代码资产（shader/C#）= 源→程序集（shLib/dll），浏览源、存储程序集；数据资产（贴图/网格）= 源→处理载荷。Slang module 与 C# assembly 同构。
4. **资产 = 导入单元（身份根），物理文件是 manifestations**：映射 N↔M 双向（代码 N 源→1 程序集；模型 1 源→N 衍生物 LOD/proxy/impostor/BLAS）。浏览器主节点是资产，不是裸文件。
5. **显示提供者投影**：每类型经 `IDisplayProvider` 决定显示形态（程序集展开成源树 / 衍生物归组 / 贴图缩略图）。存储布局（容器 vs 多文件+清单）与显示解耦。**溯源元数据是分组 UX 的承重墙**（不止增量导入）。
6. **USD 不进**：平台级依赖 + 第二套渲染架构，对当前规模太重；将来顶多当 ① 交换格式，组合思想供场景层借鉴。
7. **浏览器生长路径**：v1 文件列表 + 默认提供者 → v2 shLib 程序集展开（slang 反射 + 源映射）→ v3 dll 提供者 + 缩略图。溯源/身份链是 v2 前提。

## Goals / Non-Goals

**Goals:**

- 编辑器出现**底部通栏独立资产浏览器 dock**（场景视口左上、对象检查器右上、资产浏览器底部通栏，三分区铺满客户区），列出资源根下 ② 文件（路径/种类/大小/修改时间/已注册），刷新可见，空根有空态。资产浏览器是独立面板单元（独立 WebView2 控制器 + 独立页面），与右侧对象检查器分离（2026-11-24 用户裁定取惯例布局，为将来多窗自由 dock 铺垫）。
- 协议加法（list-assets / assets-changed / AssetEntry）以 PROTOCOL.md 为先，三处镜像逐字一致。
- 显示提供者抽象落地（资产中心接口 + 按种类注册 + 默认 1:1），核心 IResource 不动。
- 线程边界守住（枚举在主线程）；失败可见（门面未绑/根非法显式报错，不静默）。
- 截图视觉验收 + C++ 读回核对列表正确性；既有 22 用例与绑定产物零差异。

**Non-Goals:**

- 程序集展开 / 衍生物归组 / 缩略图 / 导入按钮 / 溯源身份 / 源浏览 / 运行时 / USD / 资产编辑（见 proposal）。

## Decisions

### D1 文件中心 v1，接口资产中心

v1 枚举资源根文件（`Directory.EnumerateFiles`），一文件一条目（1:1 默认提供者）。但 `IDisplayProvider` 接口形状按**资产中心**定：输入资产标识（v1 = 相对路径），输出显示条目树（含 children）。v2 溯源落地后，资产=导入单元、文件=manifestations，接口不用推翻。**不新增原生枚举接口**——文件中心走 C# IO，门面只供根路径与已注册标志。

### D2 协议加法，仍 v1，PROTOCOL.md 为先

新增（加法，不改既有命令/事件形状）：
- 命令 `list-assets`（非对象作用域，无 objectId）→ result `value.assets: AssetEntry[]`。
- 事件 `assets-changed`（广播，**无 objectId**，`fields: { added: number; removed: number }`＝本次枚举相对上次快照的增删条目数；客户端收到重拉 list-assets）。**EventMessage 形状须扩**：objectId 改可选（assets-changed 不带），fields 联合类型加 `{ added: number; removed: number }`（既有 object-changed 的 title/width/height 不变）。
- 类型 `AssetEntry { nodeId: string; path: string; kind: string; sizeBytes: number; modifiedUtc: string; registered: boolean; children: AssetEntry[] }`。v1 children 恒空、nodeId = path = **相对资源根的路径（正斜杠）**；modifiedUtc = **ISO 8601 UTC 字符串**（FileInfo.LastWriteTimeUtc.ToString("o")）；sizeBytes = FileInfo.Length。
- 错误码 `E_ASSET_ROOT` 须进 protocol.ts 的 ErrorCode 联合（六→七）与 EditorProtocol.cs 错误码常量（六→七），三处同步（BuildError 收 string 不会拦漂移，靠镜像核对判据）。
- 错误码：新增 **`E_ASSET_ROOT`**（资产根不可用：门面未绑/根非法/目录不存在/枚举 IO 错误），不用 E_INVALID_ARG 兜底（其 PROTOCOL.md 触发定义是 set-size 参数，语义不合）。**这修改了 editor-window-panel 的封闭错误码枚举**（六→七）——故本批 MODIFIED 该能力协议要求（命令五→六、事件二→三、错误码六→七），不是「纯加法不改既有要求」。既有命令/错误码形状语义不变。

先改 PROTOCOL.md，再 protocol.ts，再 EditorProtocol.cs；形状校验认 list-assets（s_knownCommands 加行，非对象作用域无 objectId 合法）。协议版本保持 1（加法扩展）。

### D3 门面绑定与枚举（主线程）

EditorService 经 Host 助手绑定门面表 13（参照探针 `EnsureRenderProbeBackend` 的按需绑定模式，门控关不绑）。命令执行（主线程）内：`GetResourceRootPath()` 取根 → `Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)` → 每文件 `FileInfo`（大小/修改时间）+ 扩展名映射种类 + `HasResource(相对路径)` 定 registered → 经默认提供者成条目。**每个门面调用（GetResourceRootPath / 每次 HasResource）后 NoteNativeCall**（计入命令计数 s_cmdCalls，供线程边界运行期归因）。监听线程绝不碰这些。**捕获面**：处理内捕获 IO 异常（DirectoryNotFoundException/UnauthorizedAccessException/IOException，含 FileInfo 读取）**+ IResourceRegistryException（门面已绑但底层抛 1014，如 ResourceManagingSystem 缺失）+ ArgumentException（根空串/非法字符）**，一律转 E_ASSET_ROOT；根空串先判空、目录不存在先 Directory.Exists 判（避免 ArgumentException 逃逸）。ExecuteCommand 现只捕 ObjectDisposedException/IWindowException，这些异常会穿到 Host.Tick 外层 catch 吞成 s_lastError、该 requestId 无 result 客户端静默挂起。门面表面为空（no-facade 自检跳过绑定）→ 显式 E_ASSET_ROOT；目录存在但无文件 = 合法空态 ok 空数组。**门面未绑红态用自检 `CASCADED_EDITOR_SELFTEST=no-facade`**（参照 no-runtime 先例 EditorWindow.cs:67）；门面已绑但抛 1014 的捕获为防御纵深（其机制由 TestCSharpAssets 的 1014 用例佐证）。

### D4 显示提供者（C# 编辑器侧，按种类注册）

`Scripting/Managed/Editor/AssetDisplay.cs`：
- `IDisplayProvider`：`IReadOnlyList<AssetEntry> Describe(AssetContext ctx)`（资产中心；ctx 含路径/种类/文件元数据/根）。
- 注册表 `Dictionary<string, IDisplayProvider>`（键=种类），`DefaultFileDisplayProvider` 兜底（一文件一条目，children 空）。
- 种类映射：.shLib→shader-library、.tex→texture、.mesh→mesh、.res→resource、其它→file。
- **不挂到 IResource**（核心序列化反向接口不背编辑器义务）；提供者纯编辑器框架代码。

### D5 刷新与事件

客户端连接后拉 list-assets；刷新按钮重拉。服务端每次枚举与上次快照比对，差异则广播 assets-changed（其它客户端随之重拉）。v1 不做文件监听轮询（无定时器）——刷新由客户端驱动，事件由「枚举发现差异」触发。导入/保存批将来主动推 assets-changed。

### D6 前端资产浏览器（底部通栏独立 dock，纯 TS 独立页面）

资产浏览器是**独立面板单元**：独立页面 `EditorWeb/assets.html` + 独立入口 `src/assets.ts`（复用 `protocol.ts`），由底部 dock 的第二个 WebView2 控制器加载（D9）。页面含标题 + 刷新按钮 `#refresh-assets` + 表格 `#asset-table`（五列：路径/种类/大小/修改时间/已注册，固定列宽 + 断词）+ 空态 `#asset-empty` + 错误条 `#asset-error`。`assets.ts` 连接后拉 list-assets、渲染 AssetEntry 行、assets-changed 重拉、空态/错误显示。**右侧对象检查器页面（index.html/main.ts）不再含资产区**——资产区从右面板移出，独立成底部 dock 页面。底部通栏宽 = 客户区宽，五列横向空间充足（不再受 320px 右面板挤压）。

### D9 原生分栏壳扩为三分区（底部 dock + 第二 WebView2 控制器）

生效规范「单窗口分栏壳」原为「左原生视口 + 右 WebView2 面板条」两分区。本批 MODIFIED 为三分区（仍**单个原生壳窗口**，GetWindowCount 增量仍 1，MUST NOT 第二壳窗口）：
- **场景视口**（左上）：宽 = 客户区宽 − 面板宽，高 = 客户区高 − dock 高；原生渲染经 SetViewPort/SetScissor 占此区。
- **对象检查器**（右上）：既有 WebView2 控制器，Bounds = (ViewportWidth, 0, PanelWidth, ClientHeight − DockHeight)。
- **资产浏览器 dock**（底部通栏）：**第二个 WebView2 控制器**，Bounds = (0, ClientHeight − DockHeight, ClientWidth, DockHeight)，加载 assets.html。
- **dock 高可配，默认值钉死**：`CASCADED_EDITOR_DOCK_HEIGHT`，**默认 240**（新增常量 `EditorService.DefaultDockHeight = 240`，照 `DefaultPanelWidth = 320` 先例 EditorService.cs:61；240 是配置项默认值，§9④合法来源，登记 CSharpHost/README 配置表，理由：够显资产表头 + 5 行预置资产）。范围 1..客户区高−1，**上界校验与越界禁用（Fail，不 clamp）照面板宽先例 EditorWindow.cs:248-251**（TryReadPanelWidth 只校下界 <1，上界在 ApplyLayout）；运行期固定，无拖拽分隔条（拖拽调高留后续）。**所有期望视口高 = 客户区高 − 240 写死此默认值**（3.3/读数行断言据此，不从被测读数自取）。
- `EditorWindow.ApplyLayout`（EditorWindow.cs:240）重算三分区；**m_Controller.Bounds 两处赋值（EditorWindow.cs:202 首次创建 / :261 ApplyLayout）都改 (ViewportWidth, 0, PanelWidth, ClientHeight − DockHeight)**；`SyncLayout`（EditorService.cs:584）同步两控制器 Bounds + 视口区域 + `s_scene.Resize(ViewportWidth, ClientHeight − DockHeight)`。**初始建图 `s_scene.Build(window.ViewportWidth, window.ClientHeight)`（EditorService.cs:391-392）改 Build(ViewportWidth, ClientHeight − DockHeight)**（启动即三分区，不靠尺寸回调）；**托管读数行 panel/viewport 高度（EditorService.cs:1598-1601）改取 ClientHeight − DockHeight，并加 dock 矩形字段**。
- **第二控制器失败/关闭处置（可执行判据）**：EditorService 创建第二控制器须有独立 Error/导航检查通道（PumpWindow EditorService.cs:454-465 现只看 EditorWindow.Error，须扩查 dock 控制器错误/导航失败 → Disable）；就绪读数行加 dockController/nav 字段；**EditorWindow.Close（EditorWindow.cs:266-274）处置两个控制器**（现只关 m_Controller）。**红态构造开关 `CASCADED_EDITOR_SELFTEST=no-dock-controller`**（运行时正常、第二控制器创建失败 → 明确报错禁用；no-runtime 在 EditorWindow.cs:67-71 于 NewWindow 前 return，只覆盖运行时缺失，**不覆盖第二控制器失败**，故另加此开关）。
- **dock 静态资源缺失明确错误页**：ServeStatic（EditorService.cs:1264-1300）现只拿 index.html 当门、assets.html 缺返 404 纯文本且不写日志；须对 assets.html 缺失返回明确错误页 + 写日志；dock 控制器导航判据须把 4xx 当失败（现 EditorWindow.cs:292 `IsSuccess || HttpStatusCode>=100` 把 4xx 当成功）。红态：bin/editor-web 有 index 无 assets.html → 请求 dock 页得错误页（非 404 纯文本）。
- **set-size 高度判据随三分区改**：现 EditorService.cs:720 `Height <= 0` 与 spec「高 MUST >0」须改 **高 MUST > dock 高**（否则视口高 = 高 − dock 高 ≤ 0，EditorSceneView.cs:85 以视口非法 Disable 而非 E_INVALID_ARG）；高度 1..dock 高 → E_INVALID_ARG。PROTOCOL.md set-size 定义同步。
- **既有 TestEditorWindowPanel 布局断言随三分区更新**（§7：溯源用户布局裁定、不放松、改前改后原文留 tasks 修订记录 19）：启动 fill 断言 `viewportRect.h != clientHeight || panelRect.h != clientHeight`（Main.cpp:4802-4803）改 `!= clientHeight − 240`；改尺寸 `viewportRect.h != 640`（Main.cpp:4977）改 `!= 640 − 240`；capture `"580x560"`（Main.cpp:5186）改新视口尺寸；就绪判据 `controller=1/nav=1`（Main.cpp:6478）加 dock 控制器；启动 ParseRect 加 dock 键。**像素中心精确匹配对高度不敏感（按 capture 尺寸取中心），故 MUST 另加视口高显式断言**（不只靠像素）。
- 将来多窗自由 dock 由专门 dock 框架 change 取代本要求（dock 树驱动布局）；本批独立面板单元（独立控制器+页面）是其铺垫，协议/服务层与布局解耦可复用。

### D7 验收（视觉 + 读回）

- **C++ 读回**：tester 第 23 用例 `TestEditorAssetBrowser` 起编辑器服务，WebSocket 发 list-assets，断言返回条目与测试预置的真实文件逐条一致（路径/大小/种类/registered），刷新后新增文件可见，空根空态。
- **截图视觉验收**（编辑器硬要求）：真实打开界面（交互模式 + 门控），主循环 read_image 核对**底部通栏资产浏览器 dock** 渲染出预置资产行（五列可见，路径含 CSharpProbe/ 前缀）。**截图不得干扰用户桌面**（C:\Users\76956\.dsh\AGENTS.md §2，2026-11-24 用户明令禁止前置窗口）：不置前/不最小化/不移动用户窗口。**抓窗方法 MUST 先实测验证能抓到 dock 的 WebView2 内容（正控：已知资产行在 PNG 可见）再作判据**——首选 `CoreWebView2.CapturePreviewAsync`（dock 控制器直接渲染内容到图像流，不依赖前景/不被遮挡影响，经自检钩子 `CASCADED_EDITOR_SELFTEST=capture-dock=<png路径>` 触发落盘）；**PrintWindow 不作假设**（官方只文档化 WM_PRINT 路径，PW_RENDERFULLCONTENT 语义未文档化，WebView2 合成不经父窗 WM_PRINT，CSharpHost/README:129 记 WGC/BitBlt 抓不到硬件 flip）；CapturePreview 不可用时回退 `capture.ps1 -Mode full`（仅编辑器恰可见时）。抓不到内容 MUST 判红，不静默通过。无截图无条件不通过。
- **回归**：既有 22 用例 + CoreTests 全绿；绑定产物逐字节不变（本批不碰生成器/原生接口）。

### D8 编辑器服务一次性，新用例独立进程

EditorService 是进程级一次性状态机：`s_state==4`（Shutdown 落 4）直接 return，无复位路径（EditorService.cs:135/289）。既有 TestEditorWindowPanel 收尾 CASCADED_EDITOR_STOP=1 停服务，全量共用同一进程/托管运行时。**两个编辑器用例无法同进程共存**（一个停了服务另一个起不来）。故 `TestEditorAssetBrowser` MUST 在**独立进程**跑（`GPUBackendTester.exe --test TestEditorAssetBrowser`，新鲜服务）；全量顺序跑保持既有 22 用例（含 TestEditorWindowPanel），新用例不进同进程顺序跑。--list 仍列 23（清单完整），run-all 与独立跑分开。5.1 回归口径 accordingly：既有 22 同进程零差异 + 新用例独立进程过。不改服务生命周期（复位涉及窗口/scene 重建与 hostfxr 关闭语义，超本批）。

## Risks / Trade-offs

- [内容少（根里只有 shLib + 探针残留）] → 测试预置已知文件保证列表可核；真实贴图网格待导入器批。明示不阻塞。
- [门面未绑（渲染/脚本关闭）] → 显式错误禁用资产浏览器 dock，不静默空列表（D3）。
- [交互资源根固定 CAAssets，空根空态无构造路径] → 加交互资源根覆盖开关 `CASCADED_EDITOR_ASSET_ROOT`（默认仓库根/CAAssets，Main.cpp 交互路径 :9812/9886 读此变量；登记 README），4.2 空态截图指向临时空目录，不动用户 CAAssets 文件。
- [大目录性能] → v1 全量枚举无分页；性能优化后续批，不预先发明上限数字。
- [协议三处漂移] → PROTOCOL.md 为先 + 一致性判据（既有 editor 批已有协议镜像核对，沿用）。
- [v1 文件中心与 v2 资产中心冲突] → 接口资产中心先定（D1），v1 退化 1:1，不推翻。
- [原生壳改三分区 + 第二 WebView2 控制器，触及既有 TestEditorWindowPanel 布局断言] → MODIFIED editor-window-panel 壳要求 + 按 §7 更新该用例布局断言（不放松，像素判据仍精确匹配）；第二控制器失败与既有控制器同处置（明确报错禁用，no-runtime 自检覆盖）。绑定产物仍逐字节不变（WebView2/布局均 C# 侧，不碰生成器/原生接口）。

## Migration Plan

协议加法 + 服务枚举 + 原生壳三分区（底部 dock + 第二 WebView2 控制器）+ 独立资产浏览器页面 + 用例。回滚 = 撤增量（壳回到两分区）。既有窗口面板的对象检查器/改标题/改尺寸/撤销/scene view 功能保留，布局断言随三分区更新（§7）。绑定产物逐字节不变是硬判据（布局/WebView2 均 C# 侧）。

## Open Questions

（无阻塞项。种类映射表 v1 写死已知扩展名；新种类随资产批扩。资产浏览器布局已定：底部通栏独立 dock（2026-11-24 用户裁定取惯例布局），dock 高可配运行期固定；多窗自由 dock / 拖拽分隔条调高由后续 dock 框架 change 做，本批独立面板单元是其铺垫。）