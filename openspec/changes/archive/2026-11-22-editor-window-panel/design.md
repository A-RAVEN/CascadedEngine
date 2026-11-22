# Design: editor-window-panel

## Context

首切片验证整条栈：网页（纯 TS）→ 本机 HttpListener/WebSocket → C# 编辑器服务（同 Scripting.Managed 程序集）→ 生成绑定 IWindow/IWindowSystem → C++ GLFW 窗口。调查事实（2026-11-22）：IWindow C# 表面含全部所需读写；10 类回调已绑定；窗口枚举未暴露；宿主单程序集加载（hostfxr + runtimeconfig，EnableDynamicLoading）；csharp-host 主规范无禁线程条款；本机 WebView2 运行时 157.0.4341.60 已装； tester 现有 17 用例、C# 测试惯例是环境变量门控探针 + 托管读数行 + C++ 侧解析断言。

## Goals / Non-Goals

**Goals:**
- 编辑器服务骨架：监听线程/主线程边界、对象编号与失效、协议 v1、命令级撤销、壳窗口 + WebView2。
- 端到端可失败验收：网页改标题/尺寸 → C++ 读回逐字相同；失效/未知编号明确报错零原生调用；网络线程不提前动引擎。
- 默认关零行为；现有 17 用例与生成物不受影响。

**Non-Goals:**
- 复杂视口合并（多视口、相机交互、动态布局、复杂焦点路由后续批；本批单窗口简单分栏已含，见 D2）。
- 多目标编辑 UI（协议带对象编号、事件广播，扩展不返工；首切片只登记一个目标窗口）。
- 完整撤销栈/跨对象事务（单层 undo 槽）。
- 场景/实体/组件模型（阶段 5，纯 C# 后续批）。
- 协议同源生成（CodeGen 无 TS 发射器；手写 v1 + PROTOCOL.md 为准）。
- 跨平台（引擎与 WebView2 均 Windows-only；服务层将来可换宿主适配器）。
- ImGui 任何改动（TestIMGUI 渲染回归保留）。
- web 内容像素级自动验证（WebView2 子 HWND 不在 Vulkan 交换链，读不回；UI 正确性靠协议往返 + 控制器读数 + 人工目检，诚实登记）。
- 热重载、多脚本程序集。

## Decisions

**D1 线程模型与生命周期。** 一个后台监听线程：HttpListener 接收循环（静态文件请求 + WebSocket 升级），WS 连接上收文本帧 → System.Text.Json 解析 → 校验形状 → 入并发队列（ConcurrentQueue）；**该线程 MUST NOT 调用任何引擎绑定 API**。主线程 `Host.Tick` 每帧 drain 队列：执行命令（经生成绑定）、把结果与事件写回各客户端的发送队列（发送也在监听线程做，只发不碰引擎）。**结果按 requestId 回发起客户端；事件广播给所有已连接客户端**（C4 修订）。事件来源：命令执行结果 + **每帧 WindowShouldClose 轮询**发现的对象失效（D5；CloseWindow 不派发回调，见 D5）。`Host.Shutdown`：停 HttpListener、关 WS、join 监听线程、处置 WebView2，全部先于 hostfxr_close（CSharpHost 析构顺序既有）。**测试跑后清理调 `EditorService.Stop()` 主动停监听/关窗/处置控制器**（不依赖进程结束；清环境变量后 Tick 早返回但监听线程仍在，须显式 Stop 隔离后续用例——M11 修订）。门控 `CASCADED_EDITOR=1`：不设时服务不启动、不建线程、不建窗口、不占端口（回归判据钉死）。

**D2 单窗口分栏壳：scene view + 面板同窗（2026-11-22 修订，用户要 ImGui 式同窗体验）。** NewWindow 只建**一个**原生窗口（登记为对象 1，即 scene view 目标）。WebView2 以**子 HWND** 嵌进该窗口右侧面板条：`GetNativeWindowHandle()` 返回 **HWND\*（指针，须再解引用一次取 HWND 值——Window_Impl.cpp 返回 &m_Win32Window，0.1 实测核实）** → `CoreWebView2Environment.CreateAsync`（用户数据目录默认 `%TEMP%\cascaded-editor-webview2`，`CASCADED_EDITOR_WEBVIEW2_DATA` 可配）→ `CreateCoreWebView2ControllerAsync(父hwnd)` → controller.Bounds = 右侧面板矩形 → `Navigate(http://localhost:<port>/)`。**面板宽度默认 320px，`CASCADED_EDITOR_PANEL_WIDTH` 可配，范围 1..(客户区宽−1)，README 配置表登记默认与范围（B2 修订）**。**set-size 的宽必须 > 面板宽（否则 E_INVALID_ARG），保证视口宽 ≥1；高 >0**——杜绝"宽≤面板宽 ⇒ 视口≤0"的未定义态（B2）。原生渲染占**左侧视口区域**（客户区宽 − 面板宽），经 `DrawCallBatch.SetViewPort/SetScissor`（GPUGraph.h 已支持，2026-11-22 核实）；糖层 `AddTrianglePass` 加**可选视口重载**（批级+绘制级两级矩形同设——糖层既有注释警告零矩形会画成 0×0；既有调用保持整目标默认，17 用例不受影响）。**UI 线程已实测成立 main-thread**（0.2 探针两后端 state=ok mode=main-thread controller=1 nav=1，evidence/0.2）：引擎主线程 glfwPollEvents 线程级消息泵足以承载 WebView2 控制器；专用 STA UI 线程回退分支保留为防御代码（实测未触发）。尺寸：窗口尺寸变化（set-size 命令或 GLFW 回调）→ 主线程 MoveWindow 子 HWND + controller.Bounds + 按新视口区域重建捕获与图（D11）。关闭：**每帧 WindowShouldClose 轮询发现窗口关闭**（CloseWindow 只设 shouldClose 不派发回调，D5）→ 失效对象 + 处置控制器 + 编辑器会话结束（单窗口单关闭路径）。WebView2 运行时缺失：CreateAsync 失败 ⇒ 明确错误日志 + 编辑器禁用（服务线程也停），进程不崩；**构造开关 `CASCADED_EDITOR_SELFTEST=no-runtime` 强制走缺失路径供红态实测（M14）**。输入：首切片三角形静态、无场景交互，GLFW 父窗口输入与 WebView2 子窗口焦点互不冲突；相机交互/复杂焦点路由后续批。**"复杂视口合并延后"指多视口/相机交互/动态布局；本简单分栏在首切片内。**

**D3 本地服务与端口。** HttpListener 前缀 `http://localhost:<port>/`，默认 **18390**（配置项：`CASCADED_EDITOR_PORT` 可改，CSharpHost/README 配置表登记默认与范围 1-65535——合法数字来源④配置项）。静态文件根 `bin/editor-web/`（build.py 拷入）；`/ws` 升级 WebSocket（System.Net.WebSockets，BCL）。资源缺失（缺 node 未构建）：静态请求返回明确错误页（文本说明"编辑器界面资源未构建：缺 node 或构建步被跳过"），不静默 404 了事。仅绑 localhost，不对外网。

**D4 协议 v1（手写，PROTOCOL.md 唯一事实源）。** 消息均 JSON 文本帧：
- C→S `hello {type:"hello", protocolVersion:1}`；S→C `welcome {type:"welcome", protocolVersion:1, objects:[{id, title, width, height}]}`。版本不符 ⇒ S 发 `result` 错误 E_PROTOCOL_VERSION 并关连接。
- C→S `command {type:"command", requestId:<int>, command:"get-object"|"list-objects"|"set-title"|"set-size"|"undo", objectId:<int>, args:{...}}`（set-title args:{title:string}；set-size args:{width:int,height:int}，**宽 > 面板宽、高 >0**，否则 E_INVALID_ARG，B2）。
- S→C `result {type:"result", requestId, ok:true, value:{...}}` 或 `{ok:false, error:{code, message}}`，**按 requestId 回发起客户端**（C4）。
- S→C `event {type:"event", event:"object-changed"|"object-invalidated", objectId, fields:{...}}`，**广播给所有已连接客户端**（C4：结果定向、事件广播）。
- 错误码：E_PROTOCOL_VERSION / E_UNKNOWN_OBJECT（编号从未存在）/ E_OBJECT_INVALIDATED（曾存在已失效）/ E_INVALID_ARG / E_UNDO_EMPTY / E_BAD_MESSAGE（形状错）。
- 形状错/未知命令 ⇒ E_BAD_MESSAGE，不入引擎队列。C# 记录类型与 TS 类型逐字段镜像 PROTOCOL.md；改协议先改文档再改两侧（任务判据 grep 对拍）。

**D5 对象编号与失效（B3 修订：轮询机制，非 close 回调）。** 单调递增 int（从 1）；注册表 id →（IWindow 租约, kind="window"）。**失效检测靠每帧 WindowShouldClose 轮询**——实测核实 `CloseWindow()` 只设 shouldClose 标志、**不派发 close 回调**，且 C# 租约是裸指针无存活查询入口，故"close 回调/weak_ptr 过期"两条路都不可用；主线程每帧对登记对象调 WindowShouldClose，为真即失效。失效 ⇒ 移出注册表 + 广播 object-invalidated；此后该编号命令 ⇒ E_OBJECT_INVALIDATED，**零原生调用**（托管侧原生调用计数器读数，C++ 测试解析断言增量 0）。未知编号 ⇒ E_UNKNOWN_OBJECT 同样零原生调用。**计数器正控（M7）：测试必须先发一条真实命令证明计数器递增（接上了），再断言失效/未知编号增量 0——否则计数器恒 0 时判据恒绿。** 面板是同一窗口的子 HWND，无第二个登记对象（C1）。编号不复用。

**D6 撤销。** 每对象一个 undo 槽：set-title/set-size 成功执行时记（命令类型, 旧值）；undo 命令取槽重放（走同一执行路径，产生 object-changed 事件），清槽；空槽 ⇒ E_UNDO_EMPTY；对象失效 ⇒ E_OBJECT_INVALIDATED。单层，文档写明；完整栈后续批。

**D7 前端与构建（B4 修订：package-lock 入库）。** 新顶层目录 `EditorWeb/`：package.json（devDeps: typescript + vite，无运行时框架）、**package-lock.json（`npm install` 生成并入库——`npm ci` 必须有 lock，全新克隆才可复现构建；2.1 交付物含 lock）**、src/（main.ts 面板逻辑 + protocol.ts 镜像类型）、vite.config.ts、index.html。面板：连接状态、对象标题/宽高输入+应用按钮、撤销按钮、事件日志、失效横幅。build.py 托管段后增前端步：检测到 node ⇒ `npm ci` + `npm run build`（EditorWeb 内）⇒ 拷 dist 到 `out/build/<preset>/bin/editor-web/`；缺 node ⇒ 打印明确警告"编辑器界面资源未构建（缺 node）"并跳过（构建不失败——node 是可选开发依赖，与 --without-managed 先例同风格；运行期 D3 错误页兜底）。dist 与 node_modules 不入库（EditorWeb/.gitignore）；**package-lock.json 入库**。

**D8 NuGet 与原生 loader。** csproj 加 PackageReference Microsoft.Web.WebView2（版本：执行时查 nuget 稳定版实测锁定，记录实际版本号；不预设）。`dotnet build` 自动 restore（首次需 nuget.org 可达；缓存后离线可编——风险条登记）。EnableDynamicLoading 形态下原生资产可能不自动拷贝：build.py 显式从 nuget 包 `runtimes/win-x64/native/WebView2Loader.dll` 拷到 bin（0.1 清点实际路径）。运行时缺 WebView2 Runtime ⇒ D2 明确禁用路径。

**D9 测试挂点（TestEditorWindowPanel，第 18 用例）。** 沿用 C# 测试惯例：测试在自己的 Tick 前设 `CASCADED_EDITOR=1`（+ 测试专用端口避免冲突）、跑后清理。C++ 侧进程内最小 WebSocket 客户端（Winsock 手写 RFC6455：客户端握手 Sec-WebSocket-Key + 掩码文本帧，零新依赖）在后台线程连接/发命令/收结果事件；主线程跑帧让 Tick drain。断言（C++ 侧对目标窗口直接读回 + 解析托管读数行 `[编辑器 读数]`）：
**用例注册在 TestCSharpWindowCallbacks 之前**（M10：该用例后托管关闭，编辑器用例须在其前跑；Main.cpp 注册顺序判据）。
① set-title 唯一串 → 结果 ok 后 C++ GetWindowName 逐字相同；
② set-size 合法宽高（宽>面板宽）→ C++ GetWindowSize 相等；
③ 未知编号 → E_UNKNOWN_OBJECT，目标状态不变，原生调用计数增量 0；**正控：先发一条真实 set-title 证明计数器递增（M7），再断言增量 0**；
④ C++ 侧 CloseWindow 关目标 → **下一帧轮询**收到 object-invalidated；再发命令 → E_OBJECT_INVALIDATED，进程不崩，计数增量 0；
⑤ 线程边界：发 set-title 后**先确认监听线程已收帧入队（cmdRequest 计数递增，M3——否则"不变"可能只是命令未到达）**，再进入不 Tick 窗口断言 C++ 标题不变，drain 后变（抓网络线程直调引擎）；
⑥ 单窗口壳：读数行含控制器创建+导航完成+UI 线程模式（main/fallback）+面板矩形与视口区域；GetWindowCount 增量 1；C++ SetWindowSize 改窗口尺寸 → 读数行 Bounds 与视口区域同步；
⑦ 撤销：set-title A→B，undo → 标题 A；再 undo → E_UNDO_EMPTY；
⑧ hello protocolVersion=999 → E_PROTOCOL_VERSION；
⑨ 渲染证明：托管读数行 ExecuteGraph 计数 >0 且给出捕获目标句柄，C++ 侧 Readback 该捕获目标中心像素 = (255,0,0,255) 精确红（确定性，沿用现有探针精确匹配惯例，M8）；
⑩ 展示证明：屏幕捕获 scene view 窗口**视口区域**中心像素 = (255,0,0,255) 精确红（D12 双通道；无 desktop 会话经 OpenInputDesktop 判定则跳过并打印原因，捕获失败判 E_EDITOR_SCREEN_PIXEL 红，不静默通过，M4）；
⑪ 改尺寸后：set-size 新宽高 → **捕获尺寸读数 = 新视口区域（宽−面板宽，高）**、中心仍红（C2：捕获随视口区域非整窗）。
默认关回归（M2 具体化）：不设 CASCADED_EDITOR 跑现有 17 用例——① 日志文件（test_output/*.log 与 stderr）grep「[编辑器]」0 命中；② 端口探测：TcpClient 连 localhost:18390 必须失败（无监听）；③ 17 用例 status 全 pass（GetWindowCount 只数原生窗口、看不到子 HWND，故另靠①②判泄漏）。

**D10 文档（B2：配置表含面板宽）。** CSharpHost/README 增编辑器节（门控、**端口/数据目录/面板宽三项配置表，各列默认值与可配范围**：端口 18390 范围 1-65535、数据目录 %TEMP%\cascaded-editor-webview2、面板宽 320 范围 1..客户区宽−1；线程模型、WebView2 依赖与缺失行为、self-test 开关、"不建线程"描述更新为"脚本路径不建线程；编辑器门控开启时一个监听线程"）；EditorWeb/README（开发构建步骤、node 要求、package-lock 入库说明）；PROTOCOL.md（D4 全文）；docs/testing.md 18 用例清单与最小集合。

**D11 Scene view 渲染（用户 2026-11-22 追加；单窗口分栏见 D2）。** 目标窗口即 scene view：服务经现成糖层（ScriptRenderBackend / ScriptRenderGraph / GetWindowTarget / CreateCaptureTarget，TestCSharpTriangle 已验证路径）建图——72 字节顶点（3 顶点 × 24 步长，位置偏移 0、颜色偏移 12，中心红）、`Shaders/Test/TestSimpleTriangle`、清除色 `(0,0.5,0,1)`、**窗口左侧视口区域 + 同区域尺寸离屏捕获**双目标（视口经 AddTrianglePass 可选视口重载设批级+绘制级矩形）、Present、SubmitGraph + WaitIdle。**每帧提交**（编辑器开启期间，主线程 Tick 内；SubmitGraph+WaitIdle 同步语义与探针一致，**每帧 WaitIdle 串行化 CPU/GPU 是已知成本，读数暴露帧耗时供观察；不声称"性能够用"（无实测依据，M12），优化后续批**）。改尺寸（set-size 执行成功或窗口回调）⇒ 按新视口区域尺寸重建捕获目标与图（窗口后台缓冲随 GLFW 窗口尺寸由引擎处理；三角形在 NDC，无需顶点改动）。后端表面（编号 3）未绑定/不可用 ⇒ 编辑器明确报错禁用（仿 BindRenderInterfaces 失败风格），不静默降级。**像素读取在 C++ 侧（2026-11-22 修订，执行者实测绑定面后裁定）**：生成的 C# 表面没有任何读回入口（CRenderBackend 绑定方法只有 ExecuteGraph/WaitIdle/Create*/Get* 等，无 Readback；GPUTexture/GPUBuffer 只有描述符与名称），托管侧读不出像素。沿用现有四个 C# 渲染探针的成熟模式：托管读数行 `[编辑器 读数]` 暴露 **ExecuteGraph 计数 + graphSelf + 捕获纹理 self/句柄 + 捕获尺寸 + 视口/面板矩形 + nativeCalls**；C++ 用例按 graphSelf 解析出捕获纹理、调 `ctx.pGPUBackend->Readback` 读中心像素并打印 `[编辑器 像素]`。两层证据本体不变（离屏红 + 屏幕红），只是"谁读像素"从托管改为 C++，判据不放松。**每帧复用同一张图**（原生 GPUGraph 可重复 ExecuteGraph，TestSimpleTriangle 即此模式）；糖层 SubmitGraph 的 Materialize「已提交」守卫改为「已提交则跳过落图、仍执行并校验」——additive 糖层改动（既有 17 用例都只提交一次，行为不变），属 1.7 糖层扩展范围。糖层视口重载亦 additive（既有整目标默认不变）。**默认视口正控（M6）：现有 17 用例不读 m_ViewPort/m_Sissor，默认矩形改小仍全绿——须新增一条断言"无参 AddTrianglePass 的批级/绘制级视口矩形 == 整目标尺寸"（CoreTests 或 tester 读数），默认不变才有可失败判据，不只靠 17 用例。**

**D12 展示证明：读回窗口真实 backbuffer（2026-11-22 apply 期最终方向，环境实测驱动）。** 用户要求"证明有展示渲染后端输出内容的能力"。

**屏幕捕获路线的实测结论（已排除）**：先后试 BitBlt 屏幕 DC、DXGI Desktop Duplication（光标/GDI 刺激递帧）、Windows.Graphics.Capture（按窗口/按显示器）——视口中心**全白**，且**原生 C++ 呈现正控（TestSimpleTriangle 成熟路径）也白**，headless 与交互可见窗口皆然。环境根因（主循环实测）：本机有 **GameViewer Virtual Display Adapter**（虚拟显示适配器）+ NVIDIA RTX 4070，显示输出经虚拟适配器，Vulkan 硬件 flip 内容进不了虚拟显示合成输出（WebView2 软件合成能显示）。**屏幕捕获在此环境被虚拟显示阻断，非代码缺陷**（编辑器视口正控 M6 已过、离屏渲染红）。

**最终方向：读回窗口 swapchain backbuffer（环境无关、GPU 级展示证明）**。证明 Present 把后端输出渲染进窗口交换链表面——比屏幕捕获更确定（无 DWM/遮挡/虚拟显示/会话问题），headless 可跑。走 resource-readback **受控例外**（本批 MODIFIED 该规范）：
- **呈现前读**：ExecuteGraph 渲染窗口后 WaitIdle、下次 Present 前读窗口 ImageHandle（Backbuffer）；不读 post-present 回收帧。
- **surface 能力检查**：Vulkan 先查 supportedUsageFlags 含 TRANSFER_SRC，不含明确拒绝返回 null token；D3D12 swapchain buffer COPY_SOURCE 转换拷贝后转回 PRESENT。
- 视口中心像素 **(255,0,0,255) 精确匹配**（M8）；读回失败/不红判 E_EDITOR_DISPLAY_PIXEL 红；surface 不支持明确拒绝打印原因（不静默通过）。
- 屏幕捕获（WGC/BitBlt）降为**可选诊断**（虚拟显示环境抓不到，evidence 注明限制）；坐标换算（GetClientRect+ClientToScreen+DPI，B1）保留供诊断通道用。

**实现影响（scope 修订）**：本批由"零 C++ 后端改动"扩为**两后端 Readback 支持 Backbuffer 类型**（Vulkan swapchain image 布局追踪+拷贝、D3D12 swapchain buffer 状态转换）+ resource-readback 规范 MODIFIED。这是 fulfill 用户展示证明需求在虚拟显示环境下的必要技术工作（主循环裁定，推翻自己早先"零 C++ 改动"推荐——该推荐非用户约束）。原生 C++ 呈现正控同样改 backbuffer 读回（应红，证引擎呈现到交换链正常）。⑩ 回归 headless 全断言（backbuffer 读回不依赖屏幕），不再需交互模式拆分。

## Risks / Trade-offs

- [WebView2 主线程贴附失败] → D2 已设实测探针 + 专用 UI 线程回退，边界不变；evidence 记实际路径。这是本批最大技术未决，0.2 先验证再铺开。
- [首个 NuGet 包破零包惯例] → 用户已认（路线①代价）；版本实测锁定；restore 首次需网，离线机器风险登记（缓存后可编）。
- [glfwPollEvents 与 WebView2 消息泵交互未知] → 同 D2 探针；失败回退路径已定。
- [监听线程与主线程竞态] → D1 单向队列边界 + 测试⑤专门抓提前执行；发送队列只发不碰引擎。
- [端口冲突] → 测试用专用端口（环境变量）；默认端口冲突时 HttpListener 启动失败 ⇒ 明确错误日志 + 编辑器禁用，不崩。
- [web 内容无法像素验证] → Non-Goals 诚实登记；协议往返 + 控制器读数 + 人工目检覆盖。
- [node 缺失] → D7 明确警告 + D3 错误页，不静默；构建不失败（可选开发依赖）。
- [单窗口生命周期] → 单窗口单关闭路径：WindowShouldClose 轮询发现关闭 ⇒ 失效对象 + 处置控制器 + 会话结束（D5）；测试④覆盖。
- [屏幕捕获受环境影响（遮挡/DWM/无 desktop 会话）+ BitBlt 抓不到 flip 内容] → D12 置前+隐藏自进程它窗+校验前景；主通道 DXGI 桌面复制/Windows.Graphics.Capture（看 flip 内容），BitBlt 仅诊断；OpenInputDesktop 判会话，无会话跳过打印原因、捕获失败判红（不静默通过）；渲染证明独立兜底。
- [每帧 SubmitGraph+WaitIdle 串行化 CPU/GPU] → 首切片同步语义（与探针一致），已知成本，读数暴露帧耗时；不声称性能够用（无依据），优化后续批。
- [scene view 渲染依赖后端表面绑定] → D11 绑定失败明确禁用编辑器，测试⑨覆盖渲染计数。

## Migration Plan

1. 0.1 清点（HWND 类型/消息泵/nuget 版本与可达性/node/Winsock 链接）→ 0.2 WebView2 贴附探针（实测定主线程/回退）。
2. 1.x 服务骨架（csproj+loader、线程/队列/Tick/Shutdown、对象表与失效、协议、命令+撤销、壳+WebView2）。
3. 2.x 前端（EditorWeb 项目+面板、build.py node 步）。
4. 3.x 测试（WS 客户端+正负向用例+注册）。
5. 4.x 全量回归 + 双基线。
6. 5.x 文档。

## Open Questions

- WebView2 贴主线程是否可行——**不是悬而未决的设计选择，是 0.2 实测任务**，两条路径都已定死（D2），实测读数决定走哪条。
- 其余无（路线/框架/服务/程序集/协议/撤销均经用户 2026-11-22 裁定，TODO/005 §4.6）。
