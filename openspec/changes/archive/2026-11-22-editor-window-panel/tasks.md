# Tasks: editor-window-panel

> 分工：构建（python build.py --config Debug）、CoreTests、GPUBackendTester（含交互/可见窗口运行）、屏幕捕获实测由主循环代跑；执行者做代码/测试/文档编辑与只读检查，需要读数时请求主循环。红绿纪律同前批：缺陷态读数先于修复存档；构造不出的如实登记。

## 0. 开工清点与探针

- [x] 0.1 六项清点（命令+输出存档 evidence/0.1-清点.txt）：① GetNativeWindowHandle 在 Windows 返回 **HWND\*（指针，须再解引用取 HWND 值——Window_Impl.cpp 返回 &m_Win32Window，已实测）**；② UpdateSystem/glfwPollEvents 消息泵事实（读实现）；③ nuget Microsoft.Web.WebView2 最新稳定版号 + nuget.org 可达性（dotnet/nuget 查询，记录实际版本）；④ node 可用性（node --version）；⑤ tester 的 Winsock 链接（ws2_32，读 CMakeLists/build.ninja）；⑥ 糖层三角形路径 API 面（读 NativeRenderSugar.cs 与 UserScript.OnRenderProbe 相关段，列出服务要调的糖方法；核 AddTrianglePass 视口重载扩展点——批级+绘制级矩形同设的既有注释）。验收：六项输出齐备；版本/链接事实与 design 假设不符即修订 design 并记录。
- [x] 0.2 WebView2 贴附探针（主循环构建+运行）：最小验证——建一个原生窗口、CoreWebView2Environment.CreateAsync + CreateCoreWebView2ControllerAsync(父hwnd) 以子 HWND 贴引擎主线程、导航 data URL、读数行报告成功/失败与模式。验收：evidence 记录实测结果，**据此决定 D2 走主线程还是专用 UI 线程回退**；失败则回退路径落地后再继续 1.6。不预设结论。
- [x] 0.3 codegen 快照（主循环，首次构建前）：out/build/x64-debug/codegen/ 全部 *.codegen.h → %TEMP%（保留相对结构），记录份数。验收：份数与源一致；4.2 以此为锚。

## 1. 服务与渲染（C#）

- [x] 1.1 csproj 加 PackageReference Microsoft.Web.WebView2（0.1③ 实测版本）；build.py 显式拷 WebView2Loader.dll（nuget 包 runtimes/win-x64/native，0.1 核实实际路径）到 bin。验收：构建通过且 bin 下 WebView2Loader.dll 存在（Test-Path）；restore 失败时错误明确（不静默）。
- [x] 1.2 服务骨架：CASCADED_EDITOR=1 门控；HttpListener 监听线程（静态文件 + /ws 升级 + 资源缺失错误页）；JSON 解析校验入 ConcurrentQueue；Host.Tick drain 挂点；**EditorService.Stop() 主动停服务入口（测试跑后清理调，隔离后续用例，M11）**；Host.Shutdown 停监听/关 WS/join/处置 WebView2 先于 hostfxr_close；读数行 `[编辑器 读数]`。验收可失败（M2 具体化）：默认关零行为——不设环境变量跑 tester，① 日志（test_output/*.log+stderr）grep「[编辑器]」0 命中 ② TcpClient 连 localhost:18390 失败（无监听）③ 17 用例全 pass（改前若泄漏行为即红）；开启时 welcome 握手可达（3.1 覆盖）。
- [x] 1.3 对象表与失效（B3 轮询机制）：单调编号注册目标窗口；**每帧 WindowShouldClose 轮询**发现关闭 ⇒ 移出 + 广播 object-invalidated（CloseWindow 不派发回调、C# 租约无查询入口，实测核实，不用 close 回调/weak_ptr）；未知/失效编号命令零原生调用（原生调用计数器读数，**正控：真实命令使计数递增证明接上，M7**）。红绿：失效后发命令——源码侧变异（去掉轮询失效）会触碰引擎或崩溃（红），实现后 E_OBJECT_INVALIDATED 且计数增量 0（绿）（3.2 ③④断言）。
- [x] 1.4 协议 v1：hello/welcome（protocolVersion=1，不符 E_PROTOCOL_VERSION 关连接）、command/result/event 形状、六错误码、E_BAD_MESSAGE 不入队；PROTOCOL.md 全文（D4）；C# 记录与 TS 类型镜像。验收：PROTOCOL.md 存在且与两侧类型逐字段对拍（grep/走读记录）；版本拒绝与形状错由 3.1/3.2 断言。
- [x] 1.5 命令：get-object / list-objects / set-title / set-size（**宽 > 面板宽、高 >0 校验，否则 E_INVALID_ARG——杜绝视口宽≤0，B2**）+ 单层 undo 槽（记旧值、重放、清槽、E_UNDO_EMPTY）。红绿：非法尺寸（去掉校验的源码变异）会调到引擎（红），实现后 E_INVALID_ARG 零原生调用（绿）。
- [x] 1.6 单窗口分栏壳 + WebView2（0.2 实测 main-thread 成立，回退分支保留防御，D2）：NewWindow 建一个窗口（登记对象 1）、**GetNativeWindowHandle 返回 HWND\* 再解引用**取父 HWND 贴子控制器、面板条矩形（**宽默认 320，CASCADED_EDITOR_PANEL_WIDTH 可配范围 1..客户区宽−1，README 配置表登记，B2**）、导航 http://localhost:<port>/（HTTP 错误页也算导航成功，HttpStatusCode≥100 区分网络失败）、尺寸变化 MoveWindow 子 HWND + Bounds + 视口区域同步、**WindowShouldClose 轮询发现关闭**失效对象+处置控制器、运行时缺失/创建失败明确禁用（**CASCADED_EDITOR_SELFTEST=no-runtime 构造开关供红态，M14**）。验收：读数行含控制器创建+导航完成+模式+面板矩形/视口区域；GetWindowCount 增量 1；C++ SetWindowSize → Bounds 与视口区域读数同步（3.1 ⑥）；no-runtime 开关触发明确禁用不崩、错误码与端口冲突可区分（evidence 登记）。
- [x] 1.7 Scene view 渲染（D11）：糖层 AddTrianglePass 加 additive 可选视口重载（批级+绘制级两级矩形同设，既有整目标默认不变）；糖层 SubmitGraph 的 Materialize「已提交」守卫改「已提交则跳过落图、仍执行并校验」（additive，既有 17 用例只提交一次行为不变）；后端表面绑定检查（缺失明确禁用编辑器）；建图（72 字节顶点/中心红/TestSimpleTriangle/清除色 (0,0.5,0,1)/左侧视口区域+同区域离屏捕获双目标/Present），每帧复用同一张图重复 ExecuteGraph；Tick 内每帧 SubmitGraph+WaitIdle；set-size 成功或窗口回调后按新视口区域重建捕获目标与图；读数行暴露 ExecuteGraph 计数/graphSelf/捕获纹理 self 与句柄/捕获尺寸/视口与面板矩形/nativeCalls（**像素不在托管读——C# 绑定面无读回入口，C++ 侧按 graphSelf 解析捕获纹理 Readback 中心像素打印 [编辑器 像素]，沿用现有探针模式**）。红绿：渲染证明——读数 ExecuteGraph>0 且 C++ Readback 捕获中心 (255,0,0,255)（3.2 ⑨）；改尺寸后捕获尺寸=新视口区域且中心仍红（⑪）；后端缺失禁用路径 evidence 登记；**默认视口正控（M6）：新增断言"无参 AddTrianglePass 批级/绘制级视口矩形==整目标尺寸"（现有 17 用例不读 m_ViewPort/m_Sissor，须独立判据）+ 既有 17 用例糖层默认不变（4.1）**；读数暴露帧耗时（每帧 WaitIdle 已知成本，不声称性能够用，M12）。**窗口上屏调试已结论（apply 期）：M6 视口正控证明编辑器图正确（窗口 pass 画窗口附件、视口对）；屏幕捕获全白含原生 C++ 正控 ⇒ GameViewer 虚拟显示环境阻断硬件 flip 上屏（非代码缺陷）。展示证明改 backbuffer 读回（1.8-1.10 + 3.2⑩，环境无关）。**

## 1b. 后端 backbuffer 读回（展示证明环境无关化，D12 最终方向）

- [x] 1.8 Vulkan Readback 支持 Backbuffer：读当前 swapchain image（呈现前、WaitIdle 后），布局转 TRANSFER_SRC_OPTIMAL 拷贝到 staging 再转回呈现布局；**先查 surface supportedUsageFlags 含 TRANSFER_SRC，不含明确拒绝返回 null token**；复用现有 staging/token/GetImageByteSize 机制。红绿：渲染三角形到窗口→Readback 窗口 ImageHandle 中心红（绿）；构造不支持 TRANSFER_SRC 的表面/或 post-present 读→明确拒绝（红态，源码侧变异或能力检查断言）。evidence 登记。evidence/1.8-Vulkan-backbuffer读回.txt
- [x] 1.9 D3D12 Readback 支持 Backbuffer：读当前 swapchain buffer，COPY_SOURCE 状态转换拷贝到 READBACK heap 后转回 PRESENT；复用现有 fence/token 机制。红绿：同 1.8（中心红 + 拒绝分支）。evidence 登记。
- [x] 1.10 resource-readback 规范 MODIFIED 同步（归档时由归档子 agent 落主规范）：受控例外（呈现前读+surface 能力检查），原 post-present 禁令与 External/格式/subresource 拒绝集保留。验收：增量 specs/resource-readback/spec.md 存在且 §7 三条齐备（已落，修订记录 30）。

## 1c. 视觉验收发现的缺陷修复（2026-11-22 用户明令截图视觉验收后抓出）

- [x] 1.11 修面板静态资源根路径 bug：`ServeStatic` 用 `Path.Combine(AppContext.BaseDirectory, "editor-web")`，但 hostfxr 宿主加载下 `AppContext.BaseDirectory` 为**空字符串**（实测错误日志 root 打印为相对路径 "editor-web"），相对 CWD 解析不到前端 ⇒ 面板永远 503 错误页，前端从未真正加载。改：静态根用程序集所在目录（`Path.GetDirectoryName(typeof(EditorService).Assembly.Location)`，hostfxr 下 Assembly.Location 应为 bin/Scripting.Managed.dll）解析 editor-web，BaseDirectory 非空时优先；**日志打印解析后的绝对根路径**供核。红绿：改前面板 503（红，editor-live2.txt 已存档）；改后请求 / 返回 200 且 body 含前端 index.html 内容（绿，主循环 HTTP GET 实测）。
- [x] 1.12 截图视觉验收（AGENTS §2 用户明令，编辑器类任务缺此无条件不通过）：主循环交互模式真实打开编辑器（走 C# 宿主的测试 + CASCADED_EDITOR=1 + --show-window-seconds，从正确目录启动），把编辑器窗口置前（Win32 SetForegroundWindow），desktop-screenshot 截图 + read_image 核对画面：① 右侧 WebView2 面板加载真前端（连接状态/标题/宽高/撤销/事件日志控件可见，非 503 错误页）② 左侧 scene view 渲染三角形（绿底红三角）③ 面板改标题/尺寸经协议往返生效。截图与核对结论存 evidence/视觉验收.txt。验收：截图可见真面板 + scene view，缺则不通过。

## 2. 前端与构建

- [x] 2.1 EditorWeb/ 项目：package.json（devDeps typescript+vite，无运行时框架）、**package-lock.json（npm install 生成并入库，npm ci 必需，B4）**、tsconfig、vite.config.ts、index.html、src/main.ts（面板：连接状态/标题编辑/宽高编辑/撤销/事件日志/失效横幅）、src/protocol.ts（镜像 PROTOCOL.md）。验收：npm run build 产出 dist（主循环或执行者跑，node 可用时）；**package-lock.json 存在且入库**；面板控件与协议命令一一对应（走读记录）；失效态禁用输入。
- [x] 2.2 build.py 前端步：检测到 node ⇒ npm ci + npm run build（EditorWeb）⇒ 拷 dist 到 bin/editor-web/；缺 node ⇒ 明确警告跳过（构建不失败）；EditorWeb/.gitignore（dist/node_modules）。验收：有 node 时 bin/editor-web/index.html 存在；无 node 时警告原文存档 + 服务返回资源缺失错误页（1.2 判据）。

## 3. 测试（tester 第 18 用例）

- [x] 3.1 最小 WebSocket 客户端（Winsock 手写 RFC6455 客户端握手+掩码文本帧，`#pragma comment(lib,"ws2_32.lib")` 补链）+ TestEditorWindowPanel 骨架（测试自设 CASCADED_EDITOR=1 + 专用端口，**跑后调 EditorService.Stop() 清理，M11**）+ 正向：①set-title 唯一串 C++ 读回逐字相同 ②set-size（宽>面板宽）C++ 读回相等 ⑥单窗口壳读数+GetWindowCount 增量 1+Bounds/视口区域同步 ⑦撤销恢复旧标题+再撤销 E_UNDO_EMPTY ⑧hello 版本 999 → E_PROTOCOL_VERSION。**注册在 TestCSharpWindowCallbacks 之前（M10，该用例后托管关闭）**，--list 18 行。验收：主循环两后端跑该用例全断言过，日志存档。
- [x] 3.2 负向与像素：③未知编号 E_UNKNOWN_OBJECT 零原生调用（**正控：先真实 set-title 证明计数器递增，M7**）④外部 CloseWindow → **下一帧轮询** object-invalidated + 旧编号 E_OBJECT_INVALIDATED 不崩 ⑤线程边界（**先确认监听线程已收帧入队 cmdRequest 递增，M3**，再 drain 前标题不变、drain 后变）+ E_BAD_MESSAGE/E_INVALID_ARG（宽≤面板宽）零原生调用；⑨渲染证明（读数 ExecuteGraph>0 + 捕获目标句柄，C++ Readback 捕获中心 **(255,0,0,255) 精确**，打印 [编辑器 像素]）⑩展示证明（**D12 最终方向：读回窗口真实 backbuffer**——GameViewer 虚拟显示阻断屏幕捕获，WGC/BitBlt/DXGI 实测全白含原生正控；backbuffer 读回环境无关、headless 可跑。`GetWindowHandle(目标窗)→ImageHandle bb→Readback(bb,bytes) 登记→pumpFrames(2)→token.Wait()→按格式色序读视口中心 (255,0,0,255) 精确**；surface 不支持 TRANSFER_SRC 明确拒绝打印原因不静默通过，读回失败/不红判 E_EDITOR_DISPLAY_PIXEL 红；WGC/BitBlt/DXGI 降可选诊断注明虚拟显示限制；原生 C++ 正控同改 backbuffer 读回应红）⑪改尺寸后捕获尺寸=**新视口区域（宽−面板宽，高）**且中心仍精确红（C2）。docs/testing.md 同步 18 用例。验收：两后端全断言过（⑩按环境），红态读数（⑤源码变异网络线程直调引擎可构造）按可构造性存档。

## 4. 全量回归与基线

- [x] 4.1 全量回归：构建绿；CoreTests 全绿（含默认视口正控新断言）；两后端 18/18（编辑器用例可见窗口运行）；**默认关编辑器时现有 17 用例与改前一致（M5 具体化）：比对字段=name/status/error（排除每次都变的 duration_ms），基线=改前跑一次的 result.json 存档 evidence；17 用例 status 全 pass 且无编辑器日志/端口（1.2 判据）**。验收：改前基线 + 改后 result.json 与日志存档 evidence，逐用例 name/status 对拍。
- [x] 4.2 双基线：check_binding_baseline.py（归档基线路径）0/0/0；fc /b 对 0.3 快照逐份全同（零 C++ 接口改动）。验收：两输出存档；任一差异即红。

## 5. 文档

- [x] 5.1 CSharpHost/README 编辑器节（门控/**端口·数据目录·面板宽三项配置表，各列默认值与可配范围（端口 18390/1-65535、数据目录 %TEMP%\cascaded-editor-webview2、面板宽 320/1..客户区宽−1，B2）**/线程模型/WebView2 依赖与缺失行为/self-test 开关/scene view 与两层像素证据（WGC 主通道）；"不建线程"描述更新为"脚本路径不建线程；编辑器门控开启时一个监听线程"）；EditorWeb/README（开发构建、node 要求、package-lock 入库）；PROTOCOL.md 定稿（结果定向/事件广播、set-size 宽>面板宽）；docs/testing.md 18 用例与最小集合。验收：配置表三项含默认值与可配范围；README 示例命令可复制运行（照抄 evidence 读数）。

## 修订记录

（执行期间对 proposal/design/specs/tasks 的任何修订，逐条记在此处：改前原文 / 改后原文 / 依据 / 谁裁定。）

### 2026-11-22 propose 阶段范围追加（用户裁定，工件首版即含）

1. **Scene view 追加**：用户原话"至少加一个sceneview窗口吧，里面画一个渲染后端画的三角形"——目标窗口即 scene view，糖层每帧渲染三角形（窗口+离屏双目标），改尺寸重建捕获与图（D11）。
2. **展示证明追加**：用户原话"你起码得证明有展示渲染后端输出内容的能力"——两层像素证据：离屏捕获中心红（渲染证明）+ OS 级屏幕捕获视口区域中心红（展示证明，D12）。**否决 backbuffer 读回方案**：resource-readback 主规范逐字禁止回读已呈现 swapchain backbuffer（post-present 不可读/帧漂移、eTransferSrc usage 非平台保证），本批不碰该规范、零 C++ 后端改动；回退预案（带能力检查的 backbuffer 读回 + 规范 MODIFIED）另批须用户裁定。
3. **单窗口分栏修订**：用户原话"你的意思是你现在没法把编辑器的其它内容和sceneview画进一个窗口咯？类似imgui能做到的那样"——原设计"目标窗+壳窗两窗口分窗"改为**单窗口分栏**：一个原生窗口，WebView2 子 HWND 嵌右侧面板条，原生渲染占左侧视口区域（SetViewPort/SetScissor 已支持，糖层 additive 重载）；窗口增量 2→1；屏幕捕获改抓视口子矩形（窗口中心可能落在面板下）；关窗单路径（失效对象+处置控制器）。复杂视口合并（多视口/相机交互）仍后续批。依据：D2/D11/D12 修订，接口支持 2026-11-22 核实（GPUGraph.h DrawCallBatch.SetViewPort/SetScissor）。

### 2026-11-22 apply 阶段工件修订（主循环裁定，执行者上报冲突）

4. **像素读取侧修订（设计矛盾自纠，不劳用户拍板）**：改前——D11/spec/tasks 1.7 写"托管读数行暴露捕获中心像素 RGBA"、spec 场景"读取托管读数行的捕获中心像素"；改后——读数行暴露 ExecuteGraph 计数/graphSelf/捕获纹理 self 与句柄/捕获尺寸/视口与面板矩形/nativeCalls，**像素由 C++ 侧按 graphSelf 解析捕获纹理 Readback 读取并打印 [编辑器 像素]**（D11/D9⑨/spec 要求与场景/tasks 1.7/3.2⑨ 同步）。依据：执行者实测生成的 C# 绑定面无任何读回入口（CRenderBackend 绑定方法无 Readback，GPUTexture/GPUBuffer 只有描述符与名称；主循环 grep 生成产物复核属实），托管侧读不出像素；现有四个 C# 渲染探针的像素判据本就全在 C++ 侧（graphSelf→解析附件→Readback），沿用该成熟模式。§7 三条：① 溯源=绑定面实测+现有探针模式；② 不放松=两层像素证据本体与容差不变，只换读取侧；③ 本条即对照。否决方案 (b)（Readback 进 C# 绑定面）：与"零 C++ 接口改动"+4.2 双基线判据冲突。
5. **糖层守卫 additive 扩展（1.7 范围内技术工作）**：改前——糖层 SubmitGraph 的 Materialize「已提交」守卫拒绝第二次提交；改后——「已提交则跳过落图、仍执行并校验」，支持编辑器每帧复用同一张图重复 ExecuteGraph（原生 GPUGraph 可重复执行，TestSimpleTriangle 即此模式）。依据：D11 每帧提交语义的实现必需；既有 17 用例都只提交一次，行为不变（4.1 回归判据钉死）；属 tasks 1.7 点名的糖层扩展范围，additive 不改既有默认。

### 2026-11-22 对抗验证裁决处置（裁决-全量.json：4 blocking / 14 misleading / 4 矛盾对 / 8 伪证；主循环逐条修订，§7 三条齐备）

**Blocking（4，全改）：**
6. **B1 捕获坐标基准**：改前 D12/tasks 3.2⑩「GetWindowRect+面板宽算视口」；改后「GetClientRect+ClientToScreen 取客户区原点+DPI 换算」（GetWindowRect 含标题栏/边框会偏移）。依据：复核 verified+实现已用 GetClientRect。spec/D12/tasks 同步。
7. **B2 面板宽范围与最小窗宽**：改前——面板宽 320 无可配范围、set-size 仅宽>0、宽≤面板宽时视口≤0 未定义、配置表无面板宽；改后——面板宽 CASCADED_EDITOR_PANEL_WIDTH 范围 1..客户区宽−1、set-size 宽>面板宽（否则 E_INVALID_ARG）、D10/5.1 配置表含面板宽默认+范围。依据：复核 verified；杜绝未定义态，加严不放松。D2/D4/D10/spec/tasks 1.5/1.6/5.1 同步。
8. **B3 失效机制改轮询**：改前 D5/spec/tasks 1.3「close 回调/weak_ptr 租约失效」；改后「每帧 WindowShouldClose 轮询」。依据：复核实测 CloseWindow 只设 shouldClose 不派发回调、C# 租约裸指针无查询入口，两条旧路不可用；实现已用轮询。失效判据本体不变（外部关窗→失效事件+E_OBJECT_INVALIDATED）。D1/D5/spec/tasks 同步。
9. **B4 package-lock 入库**：改前 D7/tasks 2.1 无 lock、2.2 npm ci 全新克隆必失败；改后 2.1 交付物含 package-lock.json（npm install 生成并入库）、.gitignore 排除 dist/node_modules 但 lock 入库。依据：复核 verified，npm ci 必需 lock。D7/tasks 2.1/2.2/5.1 同步。

**Misleading（14，全改或登记）：**
10. **M1 HWND\***：D2/0.1① 「返回 HWND」→「返回 HWND\* 须再解引用」（Window_Impl.cpp 返回 &m_Win32Window，实测）。
11. **M2 默认关判据具体化**：1.2/D9/spec 场景补「日志 grep [编辑器] 0 命中 + TcpClient 连 18390 失败 + 17 用例 pass」（GetWindowCount 看不到子 HWND，另靠日志/端口判泄漏）。
12. **M3 线程边界到达同步**：3.2⑤/D9⑤/spec 场景补「先确认监听线程已收帧入队（cmdRequest 递增）再断言 drain 前不变」，排除"命令未到达"假阴。
13. **M4 会话判定**：D12/3.2⑩/spec 补「OpenInputDesktop 判 interactive 会话；无会话跳过打印原因，捕获失败判 E_EDITOR_SCREEN_PIXEL 红，不冒充跳过」。
14. **M5 回归比对字段**：4.1 「17 用例读数与改前一致」→「比对 name/status/error（排除 duration_ms），基线=改前 result.json 存档」。
15. **M6 默认视口正控**：1.7/D11 补「新增断言无参 AddTrianglePass 视口矩形==整目标尺寸」（现有 17 用例不读 m_ViewPort/m_Sissor，默认改小仍全绿，须独立判据）。
16. **M7 计数器正控**：D5/3.2③/spec 补「先真实命令证明计数器递增，再断言失效/未知增量 0」（防计数器恒 0 恒绿）。
17. **M8 像素容差收紧**：改前「R>200,G<60,B<60 容差」（无四类来源）；改后「(255,0,0,255) 精确匹配」（现有探针精确惯例，DXGI/WGC 无损）。D9⑨⑩/D12/spec/tasks 3.2 同步。**加严不放松**；执行者代码容差须同步改精确。
18. **M9 重试/等待常量**：D12/3.2⑩ 「DWM 经验值」→「命名常量 k_DxgiAcquireAttempts/k_DxgiAcquireTimeoutMs + 注释取值依据（测试轮询参数，非产品上下限）」。
19. **M10 用例顺序**：3.1/D9/spec 补「TestEditorWindowPanel 注册在 TestCSharpWindowCallbacks 之前」（该用例后托管关闭）。
20. **M11 停服务入口**：D1/1.2/3.1/spec 补「EditorService.Stop() 测试跑后主动停服务，隔离后续用例」（清环境变量后 Tick 早返回但监听仍在）。
21. **M12 删性能断言**：D11/风险条 「性能够用」→「每帧 WaitIdle 已知成本，读数暴露帧耗时，不声称性能够用（无依据）」；1.7 读数加帧耗时。
22. **M13 捕获隔离**：D12/3.2⑩ 补「SetWindowPos TOPMOST 置前+校验前景句柄+隐藏自进程其它可见顶层窗（跑完还原）」（实现已做，工件追上）。
23. **M14 运行时缺失构造开关**：D2/1.6/spec 场景补「CASCADED_EDITOR_SELFTEST=no-runtime 强制缺失路径供红态；错误码与端口冲突/数据目录失败可区分」。

**矛盾对（4，全统一）：**
24. **C1 两窗口残留**：design Non-Goals/D5/风险条/spec 要求 2/TODO/005 §9 的"分窗/壳窗口登记/两路关闭"残留 → 统一单窗口分栏（面板是同窗子 HWND、单关闭路径、复杂合并延后）。proposal 标题同步。
25. **C2 捕获尺寸口径**：spec 场景/D9⑪ 「捕获尺寸=set-size 请求新宽高」→「=新视口区域（宽−面板宽，高）」（与 D11/tasks 一致）。
26. **C3 展示采样点**：D9⑩ 「客户区中心」→「视口子矩形中心」（与 D12/spec/tasks 一致，窗口中心可能落面板下）。
27. **C4 结果路由**：spec 要求 1 「结果与事件广播所有客户端」→「结果按 requestId 回发起客户端、事件广播」（与 D4/proposal 一致）。

**伪证（8，不改，登记理由）：**
28. ws2_32 链接（0.1 已记 #pragma comment(lib) 补链）；1.3/1.5/3.2⑤ 红态不可自然出现（红绿纪律允许源码侧变异构造）；0.2 无成功判据（已 [x] 有 state=ok 读数）；单窗口"用户裁定"措辞（原话表达同窗诉求+"其它按推荐"概括授权，修订记录逐字引原话）；Boot 启动门控时机（工件未写 Boot，D9 明确 Tick 前设环境变量）；无会话恒绿（实现 OpenInputDesktop 判定，非红即 fail）。以上复核已证伪或已有实测，不改。

**apply 期捕获通道实测发现（工件追上实现，D12 已修订）：**
29. BitBlt 屏幕 DC 抓不到 Vulkan flip-model 上屏内容（实测视口恒白=窗口 GDI 背景，诊断 PNG 证实）；展示证明主通道改 Windows.Graphics.Capture（按 HWND 抓窗口内容，不依赖桌面变化/flip 可见性），DXGI 桌面复制备选（光标刺激+同会话），BitBlt 仅诊断。**WGC 实测视口中心仍白 ⇒ 坐实假设 B：窗口未上屏（非捕获问题），转查 Present/交换链路径（tasks 1.7 加调试项）——此为当前实现待修缺陷，判据不变。**
30. **展示证明最终方向：屏幕捕获 → 窗口 backbuffer 读回（环境实测驱动，主循环裁定）**：改前——D12/spec/tasks ⑩ 用屏幕捕获（WGC/DXGI/BitBlt）抓视口中心，"零 C++ 后端改动、不碰 resource-readback 规范"；改后——**读回窗口真实 swapchain backbuffer 视口中心**（复用 Readback(ImageHandle) 登记"下一帧 Present 前捕获"+token，呈现前读、surface 能力检查、色序按格式），resource-readback 规范 MODIFIED（受控例外，原 post-present 禁令与拒绝集保留），两后端 Readback 扩 Backbuffer（tasks 1.8/1.9/1.10），屏幕捕获降可选诊断。**依据**：① M6 视口正控通过证明编辑器图正确（窗口 pass 画窗口附件、视口对）；② 屏幕捕获全白**含原生 C++ 呈现正控**（TestSimpleTriangle 成熟路径），headless 与交互可见窗口皆然；③ 主循环实测本机有 **GameViewer Virtual Display Adapter**（虚拟显示适配器）+ NVIDIA RTX 4070，显示输出经虚拟适配器，Vulkan 硬件 flip 内容进不了虚拟显示合成输出（WebView2 软件合成能显示）⇒ 屏幕捕获被环境阻断，非代码缺陷。backbuffer 读回证明 Present 渲染进窗口交换链表面（GPU 级展示能力），环境无关、headless 可跑、更确定。**§7 三条**：溯源=用户展示证明需求（"起码得证明有展示渲染后端输出内容的能力"）+ 虚拟显示环境实测；不放松=精确红判据不变、新增 surface 能力检查显式拒绝分支、原拒绝集全保留；本条即对照。**scope 修订**：推翻主循环早先"零 C++ 后端改动"推荐（该推荐非用户约束，属必要技术工作，按 2026-11-22 规则不劳用户拍板）；C++ 接口签名仍不变（绑定/审计/双基线零影响）。执行者评估可行（复用 Readback 不新增接口、PresentWindows 前钩子、自检开关 CASCADED_READBACK_FORCE_NO_BB_TRANSFER 构造拒绝红态）。
31. **视觉验收抓出面板静态根 bug + 新增视觉验收任务（2026-11-22 用户明令「编辑器相关开发任务必须要有截图视觉验收，没有截图和视觉验收无条件视作不通过」，已记 AGENTS §2）**：改前——本 change 以 remaining=0 + 自动化测试全绿报完成，**未做截图视觉验收**；用户要求"编辑器打开给我看"时主循环首次交互打开，抓出真缺陷：`ServeStatic` 用 `AppContext.BaseDirectory`（hostfxr 宿主下为空字符串，错误日志 root 打印为相对 "editor-web"）⇒ 静态根相对 CWD 解析不到前端 ⇒ 面板永远 503 错误页，前端从未加载。自动化测试漏检（只验协议往返，把 503 当导航成功容忍，没验面板内容）。改后——新增 tasks 1.11（修静态根：用程序集所在目录解析 editor-web，日志打印绝对根；红态 503 已存档、绿态 HTTP GET / 返回 200 含前端内容）+ 1.12（截图视觉验收：交互打开+置前+截图+read_image 核对真面板与 scene view，存 evidence）。**依据**：用户明令视觉验收（AGENTS §2）+ 实测错误日志 root="editor-web"（相对路径，证 BaseDirectory 空）。**§7 三条**：溯源=用户明令+实测日志；不放松=新增可失败判据（HTTP 200 含前端内容、截图核对），原协议判据保留；本条即对照。**教训**：测试绿≠用户打开能用；编辑器类 change 即便 remaining=0 测试全绿，缺视觉验收不算完成（AGENTS §2 已固化）。本 change 重新开放至 1.11/1.12 完成。
