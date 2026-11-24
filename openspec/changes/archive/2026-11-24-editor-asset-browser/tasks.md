# Tasks: editor-asset-browser

> 验收口径：每条给「命令 / 产物 + 期望读数」，期望值写死、不从被测物推导；红态读数存 `evidence/`。
> 构建唯一路径 `python build.py --config Debug`。编辑器任务 MUST 含截图视觉验收（无截图无条件不通过）。
> 故事映射：US1 = 协议与枚举（§1/§2），US2 = 底部通栏独立 dock + 原生壳三分区（§3），US3 = 验收与回归（§4/§5）。

## 1. 协议增量（US1）

- [x] 1.1 改 `Scripting/Managed/Editor/PROTOCOL.md`（唯一事实源，先改它）：加 `list-assets` 命令（非对象作用域，result value.assets）、`assets-changed` 事件、`AssetEntry` 类型（nodeId/path/kind/sizeBytes/modifiedUtc/registered/children，v1 children 恒空、nodeId=相对路径）、错误码 `E_ASSET_ROOT`（资产根不可用）；协议版本保持 1；既有五条命令/六条错误码形状语义不动。**同步 MODIFIED `editor-window-panel` 协议要求**（命令五→六、事件二→三、错误码六→七，见 specs/editor-window-panel/spec.md）。验收：文档含 list-assets/assets-changed/AssetEntry/E_ASSET_ROOT 逐字定义（grep）；版本仍 1。
- [x] 1.2 镜像 `EditorWeb/src/protocol.ts`：AssetEntry 接口（七字段）、CommandName 加 'list-assets'、ResultOk.value 加 assets、**ErrorCode 加 'E_ASSET_ROOT'（六→七）**、EventMessage 加 'assets-changed' 且 **objectId 改可选 + fields 联合加 { added: number; removed: number }**。验收：`npm run build`（EditorWeb）过；**可执行镜像核对**：grep protocol.ts 含 E_ASSET_ROOT/assets-changed/added/removed，AssetEntry 七字段名与 PROTOCOL.md 逐字一致（脚本/grep 对照，非仅「构建过」）。
- [x] 1.3 镜像 `EditorProtocol.cs`：s_knownCommands 加 list-assets；**错误码常量加 E_ASSET_ROOT（六→七）**；BuildAssetsResult / BuildAssetsChangedEvent（fields added/removed，无 objectId）；形状校验认 list-assets（无 objectId 合法）。验收：构建过；grep EditorProtocol.cs 含 E_ASSET_ROOT 常量；未知命令仍 E_BAD_MESSAGE（既有用例不红）；list-assets 无 objectId 不被拒；三处错误码集一致（PROTOCOL/TS/C# 各七，grep 对照）。

## 2. 服务枚举与显示提供者（US1）

- [x] 2.1 `Host.cs` 门面绑定助手（参照 EnsureRenderProbeBackend 按需模式）：门控开时绑门面表 13，暴露 IResourceRegistry 表面给 EditorService；门控关不绑零行为；**自检 `CASCADED_EDITOR_SELFTEST=no-facade` 时跳过绑定**（参照 no-runtime 先例，供 4.1 构造门面未绑红态）。验收：构建过；门控关时既有用件读数零差异；no-facade 时 list-assets 回 E_ASSET_ROOT。
- [x] 2.2 新增 `Scripting/Managed/Editor/AssetDisplay.cs`：IDisplayProvider（资产中心 Describe→条目树）+ 按种类注册表 + DefaultFileDisplayProvider（一文件一条目 children 空）；种类映射 .shLib/.tex/.mesh/.res，未知落 file。验收：构建过；**默认提供者行为经 4.1 的 C++ 断言核**（项目无托管单测工程，不造不存在的单元测试入口）：一文件一条目、nodeId=相对路径、children 空；IResource.h/门面头 grep 无改动。
- [x] 2.3 `EditorService.cs` list-assets 处理（主线程 ExecuteCommand 内）：GetResourceRootPath → Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories) → FileInfo 大小/时间 + 扩展名种类 + HasResource registered → 默认提供者成条目 → BuildAssetsResult。**每个门面调用后 NoteNativeCall**（GetResourceRootPath/每次 HasResource，计入 s_cmdCalls 供线程归因）。**处理内捕获 IO 异常（DirectoryNotFoundException/UnauthorizedAccessException/IOException，含 FileInfo）+ IResourceRegistryException（门面已绑底层抛 1014）+ ArgumentException（根空串/非法字符）转 E_ASSET_ROOT**；根空串先判空、目录不存在先 Directory.Exists 判；门面表面空（no-facade）→ E_ASSET_ROOT；目录存在无文件 → ok 空数组。不许任何异常穿出到 Host.Tick 外层 catch 致客户端静默挂起。验收：构建过；2.4/4.1 断言过；线程边界运行期归因（4.1 s_cmdCalls 增量）。
- [x] 2.4 assets-changed 事件：每次枚举与上次快照比对，差异广播 assets-changed。验收：4.1 刷新新增后事件断言过（客户端收到重拉）。

## 3. 底部通栏独立 dock 与原生壳三分区（US2）

- [x] 3.1 资产浏览器独立页面（底部 dock，纯 TS）：新增 `EditorWeb/assets.html` + `EditorWeb/src/assets.ts`（复用 protocol.ts），含标题 + 刷新按钮 #refresh-assets + 表格 #asset-table（五列路径/种类/大小/修改时间/已注册，固定列宽+断词）+ 空态 #asset-empty + 错误条 #asset-error；assets.ts 连接后拉 list-assets、渲染 AssetEntry 行、assets-changed 重拉、空态/错误显示。**从 `index.html`/`main.ts` 移出资产区**（右面板只留对象检查器，删 #asset-section 及相关 main.ts 逻辑）；vite 多页面入口配 assets.html。验收：npm run build 过（产出 index.html + assets.html）；grep index.html 无 asset-section、assets.html 含 asset-table/refresh-assets；4.2 截图核对底部 dock 渲染；既有窗口面板功能不红。
- [x] 3.2 原生壳三分区 + 第二 WebView2 控制器（D9）：① `EditorWindow.cs` ApplyLayout（:240）重算三分区（视口左上宽=客户区宽−面板宽、高=客户区高−dock高；检查器右上 Bounds=(ViewportWidth,0,PanelWidth,ClientHeight−DockHeight)；dock 底部通栏 Bounds=(0,ClientHeight−DockHeight,ClientWidth,DockHeight)）；**m_Controller.Bounds 两处赋值（:202 首次创建 / :261 ApplyLayout）都改新高度**。② dock 高可配 `CASCADED_EDITOR_DOCK_HEIGHT`，**新增常量 EditorService.DefaultDockHeight=240**（照 DefaultPanelWidth=320 先例 :61；配置项默认值 §9④，**CSharpHost/README 配置表登记默认 240 + 范围 1..客户区高−1**）；上界校验与越界禁用（Fail 不 clamp）照面板宽先例 **EditorWindow.cs:248-251**。③ `EditorService.cs` 创建第二 WebView2 控制器加载 assets.html（静态根同 editor-web）；**初始建图 s_scene.Build(:391-392) 改 Build(ViewportWidth, ClientHeight−DockHeight)**；SyncLayout（:584）同步两控制器 Bounds + 视口 + s_scene.Resize(ViewportWidth, ClientHeight−DockHeight)；**读数行 panel/viewport 高度（:1598-1601）改取 ClientHeight−DockHeight + 加 dock 矩形字段 + dockController/nav 就绪字段**。④ **第二控制器失败/关闭处置**：独立 Error/导航检查通道（PumpWindow :454-465 扩查 dock 控制器错误/导航失败→Disable）；EditorWindow.Close（:266-274）处置两个控制器；**红态开关 CASCADED_EDITOR_SELFTEST=no-dock-controller**（运行时正常、第二控制器创建失败→明确报错禁用；no-runtime 于 NewWindow 前 return 不覆盖此分支）。⑤ **assets.html 缺失明确错误页**：ServeStatic（:1264-1300）对 assets.html 缺失返错误页+写日志（非 404 纯文本）；dock 导航判据把 4xx 当失败（EditorWindow.cs:292 现 IsSuccess||HttpStatusCode>=100）。⑥ **set-size 高度判据**：EditorService.cs:720 `Height<=0` 改 `Height<=DockHeight`→E_INVALID_ARG；PROTOCOL.md set-size 定义同步「高>dock高」。⑦ **交互资源根覆盖开关 CASCADED_EDITOR_ASSET_ROOT**（Main.cpp 交互路径 :9812/9886 读此变量，默认仓库根/CAAssets，README 登记；供 4.2 空态指向临时空目录，不动用户文件）。⑧ **抓窗自检钩子 CASCADED_EDITOR_SELFTEST=capture-dock=<png路径>**：dock 控制器 CoreWebView2.CapturePreviewAsync 落盘 PNG（供 4.2 非前置抓窗）。验收：构建过；读数行含 panel/viewport/dock 三矩形且铺满客户区、视口高=ClientHeight−240；GetWindowCount 增量仍 1；CASCADED_EDITOR_DOCK_HEIGHT=0/超客户区高→明确报错禁用（红态 evidence/3.2-dock越界.txt）；no-dock-controller→明确禁用（红态 evidence/3.2-no-dock.txt）；assets.html 单独缺失→错误页非 404（红态 evidence/3.2-assets缺失.txt）；set-size height=240→E_INVALID_ARG（红态）；README 含 CASCADED_EDITOR_DOCK_HEIGHT 默认 240 + CASCADED_EDITOR_ASSET_ROOT（grep）。
- [x] 3.3 既有 `TestEditorWindowPanel` 布局断言随三分区更新（§7：溯源用户 2026-11-24 底部通栏 dock 裁定、不放松、改前改后原文见修订记录 19）：① 启动 ParseRect（Main.cpp:4797）加解析 dock 键；启动 fill 断言（:4802-4803）`viewportRect.h != clientHeight || panelRect.h != clientHeight` 改 `!= clientHeight − 240`，并加 dock 矩形铺满断言（dock.h==240、dock.w==clientWidth、dock.y==clientHeight−240）。② 改尺寸断言（:4977）`viewportRect.h != 640` 改 `!= 640 − 240`（SetWindowSize(1000,640) 后视口高 400）。③ set-size 后 capture 断言（:5186）`"580x560"` 改新视口尺寸（视口宽×(视口高−240)，期望写死实测核对）。④ 就绪判据（:6478 及 TestEditorAssetBrowser 就绪）`controller=1/nav=1` 加 dock 控制器就绪字段。⑤ **像素中心精确匹配 (255,0,0,255) 对高度不敏感（按 capture 尺寸取中心），故 MUST 保留①②③的视口高显式断言**（不只靠像素）。验收：两后端 TestEditorWindowPanel 过；像素判据未放松（仍精确匹配）+ 视口高显式断言存在；改前/改后断言原文见修订记录 19（逐字）。

## 4. 用例与视觉验收（US3）

- [x] 4.1 tester 第 23 用例 `TestEditorAssetBrowser`（Main.cpp + --list 登记）：预置已知文件到临时资源根（**含已注册与未注册各至少一个**，使 registered 有 true 有 false）并设到 ResourceManagingSystem → 起编辑器服务 → WebSocket 发 list-assets 断言条目与真实文件逐条一致（**path/nodeId/sizeBytes/kind/modifiedUtc/registered/children 七项全断言**，期望写死；nodeId=path=相对路径、children 空、modifiedUtc ISO8601、registered 等于 HasResource 且真值假值都出现）→ 新增文件第二次枚举断言 +1 + **assets-changed 事件 fields added=1/removed=0**（事件绑定第二次枚举，非首次）→ 空根 ok 空数组 → 根非法 E_ASSET_ROOT（SetResourceRootPath 设非法路径）→ 门面未绑 E_ASSET_ROOT（CASCADED_EDITOR_SELFTEST=no-facade）→ IO 异常 E_ASSET_ROOT 不挂起（根指向不存在目录，断言该 requestId 有 error result）→ 根空串 E_ASSET_ROOT（ArgumentException 捕获）→ **线程边界运行期归因**：list-assets 命令期间 s_cmdCalls 增量 = 1（GetResourceRootPath）+ N（HasResource 次数），证明门面调用都在主线程命令上下文（每个门面调用已 NoteNativeCall）；监听线程路径无门面调用。**本用例独立进程跑**（编辑器服务一次性，D8：`--test TestEditorAssetBrowser`，不进全量同进程顺序跑）。验收：--list 23 行含 TestEditorAssetBrowser；两后端独立进程 --headless 该用例过；失败面各自能红（evidence/4.1-*.txt）。
- [x] 4.2 截图视觉验收（主循环，编辑器硬要求）：交互模式真实打开编辑器（门控 + 正确 CWD，真实根 CAAssets），**底部通栏资产浏览器 dock** 渲染**预期行写死（路径含相对资源根全前缀）**：VulkanShaderLibrary.shLib 145710B / D3D12ShaderLibrary.shLib 293737B / CSharpProbe/Contrast.res 4B / CSharpProbe/Imported_A.res 2B / CSharpProbe/Imported_B.res 2B，五列路径/种类/大小/修改时间/已注册可见。**抓窗 MUST 先实测正控验证能抓到 dock 的 WebView2 内容**：首选 3.2⑧ 的 capture-dock 钩子（CoreWebView2.CapturePreviewAsync 落盘，不依赖前景），PrintWindow 不作假设（对 WebView2 合成未验证）；CapturePreview 不可用回退 capture.ps1 -Mode full（仅编辑器恰可见时）；**MUST NOT 置前/最小化/移动用户窗口（C:\Users\76956\.dsh\AGENTS.md §2 2026-11-24 用户明令）**；read_image 核对底部 dock 列出预期行。**空根空态**：CASCADED_EDITOR_ASSET_ROOT 指向临时空目录（3.2⑦，不动用户 CAAssets）截图核对空态文案。**刷新观测**：向真实根新增一个文件后点 #refresh-assets，截图 MUST 比刷新前多一行（对照 4.1 +1）。验收：截图存 evidence/4.2-截图.png（非空 + 空态 + 刷新前后）+ read_image 核对记录 + 抓窗正控记录；抓不到内容/缺截图/不符判不通过。

## 5. 回归与文档（US3）

- [x] 5.1 全量回归（主循环跑）：**既有 22 用例同进程顺序跑**（含 TestEditorWindowPanel）name+status 与上批基线 report（归档 `2026-11-23-csharp-resource-asset-foundation/evidence/5.1-全量-vulkan.json` / `-d3d12.json`）逐条零差异（不许拿本轮过滤结果自比）；**TestEditorAssetBrowser 独立进程跑**两后端过（编辑器服务一次性，D8，不与窗口面板用例同进程）；CoreTests 全绿（exit 0 + PASSED 26 行，evidence/5.1-CoreTests.txt）；绑定基线：封存上批新锚 221（归档 `.../evidence/新锚.txt`）为本批旧锚，本批不碰生成器/原生接口，对账 缺失0/不一致0/多余0（evidence/5.1-基线.txt）。验收：零差异 + 新用例独立过 + 基线 0/0/0。
- [x] 5.2 对外文档：`docs/testing.md` 清单加第 23 用例 + 计数 22→23（各处）+ 注明 TestEditorAssetBrowser 独立进程跑；`CSharpHost/README.md` 环境变量表补 `CASCADED_EDITOR_SELFTEST=no-facade`（check_csharp_host_readme 只比变量名、不会拦漏值，须人工补）；`PROTOCOL.md` §5 面板控件映射补资产区→list-assets/刷新→重拉。验收：testing.md 清单与 --list 逐条一致、计数 23（grep）；README 含 no-facade（grep）；PROTOCOL §5 含资产区映射。
- [x] 5.3 验收自查（主循环可自勾）：spec 场景 ↔ 实测读数对照（5 要求），红态证据齐全，openspec validate editor-asset-browser valid。验收：evidence/5.3-场景对照.md 无「声称过但无读数」行。
- [x] 5.4 编译验证与修复（最后一项）：python build.py --config Debug 失败则修到成功；成功后复跑 5.1 确认读数不变。验收：BUILD SUCCESSFUL + 复跑一致（evidence/5.4-构建.txt）。
- [x] 5.5 对外文档 dock 增量（接管 5.2 的 PROTOCOL §5 段落，5.2 该段验收由本条作废）：① `PROTOCOL.md` §5（现 :173「资产区…资产区标题 + 表格 → list-assets」）**改写**为底部通栏资产浏览器 dock→list-assets/刷新→重拉（与右面板对象检查器分离）；红绿 grep：§5 段落「资产区」旧词 0 处（红态防喂绿）、「底部」/「dock」present（绿）。② `CSharpHost/README.md` 旧布局事实改三分区（**:123 右面板资产区 / :125 单窗口分栏 / :389 面板矩形到客户区高** 三处点名）+ **两张配置表（:113「三项」/ :361）加 CASCADED_EDITOR_DOCK_HEIGHT 默认 240 范围 1..客户区高−1 + CASCADED_EDITOR_ASSET_ROOT** + no-dock-controller/capture-dock 自检值。③ `EditorWeb/README.md`（:35 / :41-48）补 assets.html 独立页面 + 底部 dock。④ `docs/testing.md` 注明 TestEditorWindowPanel 布局断言随三分区更新（3.3）。验收：PROTOCOL §5 红绿 grep 过；CSharpHost/README 三处旧事实已改 + 两配置表含 dock 高（grep 默认 240）；EditorWeb/README 含 assets.html；testing.md 含三分区说明。

## 修订记录（对抗验证首轮，溯源面 4 条，主循环复核后修订）

1. **主张-溯源-001（blocking，采纳）**：「v1 纯加法、editor-window-panel 要求不变」不成立——生效规范封闭枚举五命令/六错误码，加 list-assets/E_ASSET_ROOT 即修改该要求；design 允许新增 E_ASSET_ROOT 与 proposal「Modified 无」/tasks「沿用 E_*」三处互斥。
   - 改前：proposal Modified「无」；design D2「E_INVALID_ARG 或新增 E_ASSET_ROOT」；新 spec「既有命令集不变」。
   - 改后：新增 specs/editor-window-panel/spec.md MODIFIED 协议要求（命令五→六/事件二→三/错误码六→七，含既有场景 + 非对象作用域场景）；proposal Modified 列 editor-window-panel；design D2 定死 E_ASSET_ROOT（不用 E_INVALID_ARG 兜底）；新 spec 要求 2 改「扩为六命令/七错误码」；tasks 1.1 加 E_ASSET_ROOT + MODIFIED 同步。依据：live spec L36 封闭枚举（主循环 read 核实）。
2. **主张-溯源-002（blocking，采纳）**：「门面未绑」运行期不可构造（CSharpHost Init 无条件登记，无开关），4.1 失败面落不了地。
   - 改前：tasks 4.1「门面未绑显式错误」无构造路径。
   - 改后：加自检开关 `CASCADED_EDITOR_SELFTEST=no-facade`（参照既有 no-runtime 先例 EditorWindow.cs:67）跳过门面绑定；tasks 2.1/4.1、design D3、spec 要求 4 场景同步。依据：CSharpHost 无条件登记 + no-runtime 先例（主循环 grep 核实）。
3. **主张-溯源-003（misleading，采纳）**：ExecuteCommand 只捕 ObjectDisposed/IWindow，枚举 IO 异常穿到 Host.Tick 外层 catch 吞成 s_lastError、客户端无 result 静默挂起；spec 空目录语义两处互斥。
   - 改前：tasks 2.3 无 IO 捕获；spec 要求 1「MUST NOT 把空目录冒充正常列表」与空根 ok 空数组场景互斥。
   - 改后：tasks 2.3/design D3 处理内捕获 IO 异常转 E_ASSET_ROOT；spec 要求 1 改「空目录=合法空态 ok 空数组；门面失败/根非法/IO 错误显式 E_ASSET_ROOT 不冒充」；spec 要求 4 加 IO 异常场景。依据：EditorService.cs ExecuteCommand catch 范围 + DirectoryNotFoundException 实测（攻击方）。
4. **主张-溯源-004（misleading，采纳）**：tasks 5.1「旧锚 221」与归档命名相反（归档旧锚封存件=218、新锚=221）；CoreTests 判据缺基数。
   - 改前：「绑定基线旧锚 221」「CoreTests PASSED 行数与基线一致」。
   - 改后：「封存上批新锚 221 为本批旧锚，对账 0/0/0」「CoreTests exit 0 + PASSED 26 行」；spec 要求 5 同步 26。依据：归档 evidence 命名 + CoreTests 26 实测。
5. **主张-一致-001 / 验收-001（blocking，合并采纳）**：E_ASSET_ROOT 进了 design/spec/1.1/MODIFIED，但 1.2/1.3 镜像清单漏 ErrorCode/错误码常量，三处会漂移（PROTOCOL 七 / TS 六 / C# 六）；1.2/1.3 仅「构建过」无执行比对。
   - 改后：1.2 加 ErrorCode E_ASSET_ROOT + 可执行 grep 镜像核对；1.3 加错误码常量 + 三处七码一致 grep；4.1 断言 E_ASSET_ROOT 码。依据：protocol.ts ErrorCode 六成员 / EditorProtocol.cs 六常量实测。
6. **主张-一致-002 / 验收-001（采纳）**：assets-changed 无 fields 定义，EventMessage 仍要求 objectId:number 与「无 objectId」冲突。
   - 改后：design D2 定 fields { added, removed } + EventMessage objectId 改可选 + fields 联合；spec 要求 2 / tasks 1.2/1.3 同步；4.1 断言 added=1/removed=0。依据：protocol.ts EventMessage L72-77 实测。
7. **主张-一致-003 / 验收-001（blocking，合并采纳）**：默认提供者「单测」无托管测试工程（只有 Scripting.Managed.csproj，零测试框架）。
   - 改后：tasks 2.2 验收改「经 4.1 的 C++ 断言核」（不造不存在的确良入口）；4.1 断言 children 空 / nodeId=相对路径。依据：项目测试层只有 CoreTests/tester/Python checks。
8. **主张-一致-004 / 验收-002 / 验收-004（采纳）**：4.1 列表漏 modifiedUtc/nodeId/children；前端空态无验收。
   - 改后：4.1 七项全断言；spec 要求 1 场景同步七项；4.2 加空根空态截图 + 刷新观测。
9. **主张-验收-002（blocking，采纳）**：registered 只预置未注册文件（恒假可过）；assets-changed 未绑定第二次枚举。
   - 改后：4.1 预置已注册+未注册各一（registered 真假都出现）；assets-changed 绑定第二次枚举断言 added/removed；spec 要求 1 场景加「真值假值都出现」。
10. **主张-验收-003（blocking，采纳）**：监听线程零引擎调用仅「代码核对」，无运行期读数。
    - 改后：4.1 加运行期断言（list-assets 引擎调用计入 s_cmdCalls 主线程、监听线程增量 0）；spec 要求 4 场景改运行期。依据：既有 NoteNativeCall/s_cmdCalls 机制。
11. **主张-验收-004（misleading，采纳）**：4.2 截图无预期行/根；5.1 基线 report 未点名（可自比）。
    - 改后：4.2 预期行写死（CAAssets 真实内容）+ 空态 + 刷新；5.1 点名上批 5.1-全量-*.json 基线。另修次要：ROADMAP「协议 v1 加法」→「加法扩展 MODIFIED 封闭枚举」；design D2 定 path=相对正斜杠 / modifiedUtc=ISO8601。
12. **主张-存在-001（misleading，采纳）**：design D3/tasks 2.3 把 C# 枚举写成 `Directory.EnumerateFiles(root, AllDirectories)`（无此两参重载），异常名 DirectoryNotFound/UnauthorizedAccess 非真实类型。
    - 改后：改 `Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)`；异常名 DirectoryNotFoundException/UnauthorizedAccessException/IOException（含 FileInfo 读取）。依据：攻击方反射实测 EnumerateFiles 重载 + 主循环采纳（.NET 8 真实签名）。
13. **主张-影响-001（blocking，采纳）**：EditorService 一次性状态机（s_state==4 终态无复位），TestEditorWindowPanel 收尾停服务，第 23 用例与它同进程不可共存，5.1 全量计划不可执行。
    - 改后：新增 design D8——TestEditorAssetBrowser 独立进程跑（--test），全量顺序跑保持既有 22；tasks 4.1/5.1、spec 要求 5 场景同步「独立进程」。不改服务生命周期（复位超本批）。依据：EditorService.cs:135/289 实测。
14. **主张-影响-002（blocking，采纳）**：门面已绑但注册表缺失抛 1014 IResourceRegistryException，ExecuteCommand 不捕，穿到 Host.Tick 静默挂起；no-facade 只构造未绑、真实 1014 无测试面。
    - 改后：tasks 2.3/design D3 捕获面加 IResourceRegistryException → E_ASSET_ROOT（防御纵深，机制由 TestCSharpAssets 1014 佐证）；spec 要求 4 异常场景扩。依据：EditorService.cs:773/782 catch 范围 + Bindings ThrowIfFailed 实测。
15. **主张-影响-003（blocking，采纳）**：「监听线程零增量」用全局计数无法归因（无线程 ID、CheckObjectHealth 每帧 +1、门面调用不自动计数）。
    - 改后：tasks 2.3 每个门面调用 NoteNativeCall；4.1/spec 要求 4 改运行期归因——s_cmdCalls 增量 = 1+N（证明主线程命令上下文），非「监听零增量」空断言。依据：NoteNativeCall/s_cmdCalls 机制实测。
16. **主张-影响-004（minor，采纳）**：no-facade 自检值未进 CSharpHost/README 环境变量表、PROTOCOL §5 无资产区映射；check_csharp_host_readme 只比变量名永远绿。
    - 改后：tasks 5.2 补 README no-facade 值 + PROTOCOL §5 资产区→list-assets 映射（人工补，脚本不拦）。另采纳影响面风险注记：根空串/非法字符抛 ArgumentException，tasks 2.3 先判空/Directory.Exists + 捕获 ArgumentException。
17. **用户布局裁定（2026-11-24，原话「资产浏览器一般不是在下侧的吗？而且应该有一个独立的栏」+ 选「底部通栏独立 dock（惯例布局）」）**：资产浏览器从右面板并列区改为底部通栏独立 dock（惯例布局，与右检查器分离）。
     - 改前：design D6「index.html 加资产区…右面板」；spec 要求 1「编辑器右面板 MUST 有资产区」；tasks 3.1「index.html 加资产区…main.ts」；editor-window-panel 壳要求两分区（左视口+右面板）。
     - 改后：design D6 改独立页面 assets.html/assets.ts（底部 dock 第二 WebView2 加载）+ 新增 D9 原生壳三分区（视口左上/检查器右上/dock 底部通栏，第二控制器，dock 高 CASCADED_EDITOR_DOCK_HEIGHT 可配照面板宽先例，运行期固定无拖拽）；spec 要求 1 改「底部通栏独立 dock」；editor-window-panel MODIFIED 壳要求改三分区（仍单壳窗口）+ 场景同步；tasks 3.1 改独立页面 + 新增 3.2 原生壳三分区 + 3.3 TestEditorWindowPanel 布局断言更新（§7 不放松，像素仍精确匹配）+ 5.5 文档 dock 增量。依据：用户当场裁定 + 生效壳要求 editor-window-panel spec L50（主循环 read 核实）+ EditorWindow.cs:240/EditorService.cs:584/1598 布局符号。dock 高可配运行期固定为主循环依面板宽先例的默认选型（用户未选拖拽，留后续）；多窗自由 dock 由后续 dock 框架 change 做，本批独立面板单元是铺垫。
18. **用户截图纪律裁定（2026-11-24，原话「禁止前置窗口，阻碍我工作」）**：视觉验收不得置前/最小化/移动用户窗口。
     - 改前：tasks 4.2「desktop-screenshot 截图」（主循环此前用 SetForegroundWindow/ShowWindow 置前抓窗，干扰用户桌面）。
     - 改后：tasks 4.2/design D7/spec 要求 5 视觉场景改 PrintWindow 按 HWND 抓窗（被挡也能抓，不动前景），MUST NOT 置前/最小化/移动用户窗口；已记 AGENTS §2。依据：用户当场明令。（抓窗方法后经布局轮溯源-003/验收-004 复核：PrintWindow 对 WebView2 合成未验证，改 CapturePreviewAsync，见记录 19。）
19. **布局轮对抗验证（2026-11-24，五面 22 条主张全采纳，主循环复核修订）**：验收 4 blocking、一致 3 blocking+3 misleading、影响 3 blocking+1 misleading、存在 4 misleading、溯源 1 blocking+2 misleading+1 minor。逐条归宿：
     - **dock 高默认值缺失**（一致-002/存在-002/溯源-002/验收-001/一致-005/影响-002）：钉死 DefaultDockHeight=240（配置项默认值 §9④，照 DefaultPanelWidth=320 先例 EditorService.cs:61），README 配置表登记；所有期望视口高=客户区高−240 写死；先例引用补全上界/越界 EditorWindow.cs:248-251（溯源-002）。
     - **set-size 高度判据互斥**（一致-001/影响-002）：spec/PROTOCOL/EditorService.cs:720 `Height<=0` 改 `Height<=DockHeight`→E_INVALID_ARG；场景「改尺寸参数非法」height≤dock 高。
     - **第二控制器失败/关闭无判据 + no-runtime 假覆盖**（影响-001/溯源-001）：tasks 3.2④ 独立 Error/导航通道 + Close 处置两控制器 + 就绪读数行 dock 字段；新增红态开关 no-dock-controller（no-runtime 于 NewWindow 前 return 不覆盖第二控制器）；spec 加场景。
     - **assets.html 缺失非错误页**（影响-003）：tasks 3.2⑤ ServeStatic 对 assets.html 缺失返错误页+写日志、dock 导航 4xx 当失败；spec 场景含 index 在 assets 单独缺失。
     - **视口高波及面漏点名**（影响-002/存在-001/影响-004）：tasks 3.2③ 初始 Build(:391-392)/读数行高度(:1598-1601)/m_Controller.Bounds 两处(:202/:261)；tasks 3.3 点名 Main.cpp:4802-4803/4977/5186/6478 + ParseRect dock 键 + 视口高显式断言（像素中心对高度不敏感）。
     - **4.2 预期行漏前缀**（验收-003）：Imported_A.res/Imported_B.res 补 CSharpProbe/ 前缀（实测真实相对路径）。
     - **4.2 空根/刷新/抓窗不可判**（验收-004/存在-003/溯源-003）：空根用 CASCADED_EDITOR_ASSET_ROOT 覆盖开关指向临时空目录（不动用户文件）；刷新观测改「新增文件后点刷新→多一行」；抓窗改 CoreWebView2.CapturePreviewAsync（capture-dock 钩子）+ 正控验证，PrintWindow 不作假设（官方只文档化 WM_PRINT、PW_RENDERFULLCONTENT 未文档化、WebView2 合成不经父窗 WM_PRINT、README:129 记 flip 抓不到），回退 -Mode full。
     - **5.2/5.5 冲突重复**（一致-004）：5.5 接管 PROTOCOL §5 段落改写（红绿 grep：§5「资产区」0 处防喂绿、「底部/dock」present），5.2 该段验收作废；README 多处点名（CSharpHost:123/125/389 + 配置表 :113/:361 + EditorWeb/README:35/41-48）（存在-004）。
     - **旧措辞残留**（一致-003）：spec 要求 5 正文「核对资产区渲染」→「底部 dock」、design 风险「禁用资产区」→「禁用资产浏览器 dock」。
     - **AGENTS 路径/用户裁定落盘**（溯源-004，minor）：工件「AGENTS §2」补全路径 C:\Users\76956\.dsh\AGENTS.md §2；用户裁定 17 原话系对话当场裁定（tasks 引文为据），dock 高运行期固定已标主循环自选不冒充用户要求。
     - **断言改前/改后逐字原文**（验收-001/一致-006 要求，§7 对照）：
       - 启动 fill（Main.cpp:4803）：改前 `viewportRect.h != clientHeight || panelRect.h != clientHeight`；改后 `viewportRect.h != clientHeight - 240 || panelRect.h != clientHeight - 240`（另加 dock.h==240 && dock.w==clientWidth && dock.y==clientHeight-240 铺满断言）。
       - 改尺寸（Main.cpp:4977）：改前 `viewportRect.w != 680 || panelRect.x != 680 || panelRect.w != 320 || viewportRect.h != 640`；改后末项 `viewportRect.h != 640 - 240`（视口高 400）。
       - capture（Main.cpp:5186）：改前 `readings.Get("capture") != "580x560"`；改后 `!= "<视口宽>x<视口高-240>"`（期望写死，实测核对）。
       - set-size（EditorService.cs:720）：改前 `command.Width <= 0 || command.Height <= 0`；改后高度分支 `command.Height <= s_window.DockHeight`（E_INVALID_ARG「高度必须大于 dock 高」）。
       - 像素判据 (255,0,0,255) 精确匹配**未放松**（保留），另加视口高显式断言补高度分辨力。