# Proposal: editor-window-panel（网页编辑器首切片：单窗口分栏——左 scene view 视口 + 右 WebView2 属性面板）

## Why

阶段 4 绑定面已齐（8 接口 + 糖层，两后端 17/17），阶段 6 网页编辑器是 ROADMAP 下一个产品方向。用户 2026-11-22 裁定首切片路线（TODO/005 §4.6 已合回）：**自有窗口 + WebView2**（原话："可以不用浏览器吗，用自己的壳或者webview"→ 路线①；"其它按照你推荐的来吧，出一个change"）。首切片按 TODO/005 §5 只做"一个面板 + 一个登记窗口"，验证整条栈（网页 → C# 编辑器服务 → 生成绑定 → C++ 窗口），逼出后续一切编辑器功能复用的底座：服务骨架、线程边界、对象编号与失效、命令协议。

调查已核实（2026-11-22）：生成的 IWindow C# 表面含面板所需全部读写（GetWindowName/SetWindowName 字符串编组、尺寸/位置元组等），零 C++ 接口改动；10 类窗口回调已绑定（close/size/pos/focus 可直接用）；窗口枚举面未暴露（只有 GetWindowCount），故服务自己 NewWindow 登记；宿主只加载一个程序集；csharp-host 主规范无禁线程条款；本机 WebView2 运行时已装（157.0.4341.60，注册表实测）；全仓无 package.json（前端空地）；IMGUIContext 唯一消费方是 tester（不碰）。

## What Changes

- **C# 编辑器服务骨架**（`Scripting/Managed/Editor/` 新命名空间，同 Scripting.Managed 程序集）：环境变量 `CASCADED_EDITOR=1` 门控（默认关=零行为）；一个后台监听线程跑 HttpListener（静态文件 + WebSocket 升级），只收包/解析/校验/入队；引擎调用全在主线程 `Host.Tick` drain 命令队列执行；`Host.Shutdown` 停监听、断客户端、join 线程，先于 hostfxr_close。
- **对象编号与失效**：服务 NewWindow 建目标窗口并登记为对象 1（单调递增编号）；close 回调/租约失效 ⇒ 编号失效 + 广播 object-invalidated；失效编号的命令返回明确错误且零原生调用。面板是同一窗口内的 WebView2 子 HWND，不是第二个原生窗口（见单窗口分栏壳条）。
- **Scene view（用户 2026-11-22 追加："至少加一个sceneview窗口吧，里面画一个渲染后端画的三角形"；"你起码得证明有展示渲染后端输出内容的能力"）**：目标窗口本身就是 scene view——服务经现成 C# 糖层（ScriptRenderBackend，TestCSharpTriangle 已验证路径）每帧向其左侧视口区域渲染一个三角形（视口区域 + 同区域尺寸离屏捕获双目标），改尺寸时按新视口区域重建捕获目标与图。**两层像素证据**：① 渲染证明——托管读数行给出捕获目标句柄与执行计数，C++ 侧 Readback 离屏捕获中心像素为三角形红色（确定性、headless 可跑；C# 绑定面无读回入口，像素读取在 C++ 侧，沿用现有探针模式）；② **展示证明——读回窗口真实 swapchain backbuffer 视口中心像素**（2026-11-22 最终方向：本机 GameViewer 虚拟显示适配器阻断屏幕捕获硬件 flip 内容，WGC/BitBlt/DXGI 实测全白含原生 C++ 正控；backbuffer 读回证明 Present 把后端输出渲染进窗口交换链表面，环境无关、headless 可跑、比屏幕捕获更确定）。走 resource-readback **受控例外**（本批 MODIFIED 该规范）：复用现有 Readback(ImageHandle) 登记"下一帧 Present 前捕获"+token，呈现前读、Vulkan 先查 surface supportedUsageFlags 含 TRANSFER_SRC（不含明确拒绝）。屏幕捕获降可选诊断（虚拟显示环境抓不到，注明）。渲染后端不可用 ⇒ 编辑器明确报错禁用（仿探针绑定失败风格）。
- **命令协议 v1（手写）**：hello/welcome 握手带 protocolVersion；命令 get-object / list-objects / set-title / set-size / undo；结果带 requestId 与 ok/value/error；事件 object-changed / object-invalidated 广播给所有已连接客户端。错误码 E_PROTOCOL_VERSION / E_UNKNOWN_OBJECT / E_OBJECT_INVALIDATED / E_INVALID_ARG / E_UNDO_EMPTY / E_BAD_MESSAGE。`Scripting/Managed/Editor/PROTOCOL.md` 为两侧类型唯一事实源，C# 记录与 TS 类型镜像。
- **命令级撤销**：每对象单层 undo 槽（set-title/set-size 执行时记旧值），undo 走同一执行路径重放；空槽 E_UNDO_EMPTY；失效对象 E_OBJECT_INVALIDATED。
- **单窗口分栏壳 + WebView2（2026-11-22 修订：用户要 ImGui 式同窗体验）**：服务 NewWindow 只建一个原生窗口（即 scene view 目标），WebView2 以**子 HWND** 嵌入右侧面板条（Microsoft.Web.WebView2 NuGet，本仓库第一个 NuGet 包，用户已认；面板宽度可配），加载 http://localhost:<port>/；原生渲染占左侧视口区域（DrawCallBatch.SetViewPort/SetScissor 已支持，糖层 AddTrianglePass 加 additive 可选视口重载）；尺寸回调同步子 HWND 布局 + Bounds + 视口区域；关窗失效对象并处置控制器。**UI 线程未决须实测**：控制器要求消息泵线程，引擎主线程经 glfwPollEvents 泵消息——0.2 探针实测决定贴主线程还是回退专用 UI 线程（引擎调用仍走队列）。复杂视口合并（多视口/相机交互）仍后续批。
- **前端**（新顶层目录 `EditorWeb/`）：纯 TS 无框架 + Vite（node 仅开发期）；面板 UI（连接状态、标题/宽高编辑+应用、撤销按钮、事件日志、失效横幅）；build.py 增可选 node 构建步（缺 node 明确警告，运行期资源缺失时服务返回明确错误页，不静默）。
- **本地服务**：HttpListener 监听 http://localhost:<port>/（默认 18390，`CASCADED_EDITOR_PORT` 可配，README 配置表登记）；静态文件从 bin/editor-web/ 托管；WebSocket 端点 /ws；JSON 用 System.Text.Json（BCL）。
- **测试**：tester 新增 `TestEditorWindowPanel`（第 18 个用例）——进程内最小 WebSocket 客户端（Winsock 手写 RFC6455 客户端握手+掩码文本帧，零新依赖）后台线程发命令，C++ 侧读回核对；覆盖 TODO/005 §5 五条验收 + 撤销 + 协议版本拒绝 + 单窗口壳读数（增量 1 + 分栏布局）+ **scene view 两层像素证据（离屏捕获中心红 + 窗口 backbuffer 读回视口中心红，均精确 (255,0,0,255)）与改尺寸后仍红**；原生 C++ 呈现正控同走 backbuffer 读回。
- **文档**：CSharpHost/README 编辑器节与环境变量表、EditorWeb/README、PROTOCOL.md、docs/testing.md（18 用例）。

## Capabilities

### New Capabilities

- `editor-window-panel`：编辑器服务线程边界、对象编号与失效、命令协议 v1、单窗口分栏壳、命令级撤销、scene view 渲染与展示证明、首切片往返验收（新增规范见 `specs/editor-window-panel/spec.md`）。

### Modified Capabilities

- `resource-readback`：MODIFIED「回读源」要求——新增窗口 backbuffer **呈现前受控读回**例外（复用 Readback(ImageHandle) 登记式捕获、surface 能力检查、色序按格式），原 post-present 禁令与 External/格式/subresource 拒绝集保留（增量见 `specs/resource-readback/spec.md`）。

### 核对后不动

- `csharp-host`：主规范无禁线程条款（2026-11-22 grep 核实），监听线程边界由新能力规范约束；宿主加载器零改动（同程序集）。README"不建线程"现状描述随批更新（文档任务，非规范改动）。
- `csharp-bindings` / `csharp-bindable-surface`：零 C++ 接口改动，绑定面/ABI 不动。
- ImGui 相关：不碰（TestIMGUI 渲染回归保留）。

## Impact

- 产品：`Scripting/Managed/Scripting.Managed.csproj`（首个 PackageReference：Microsoft.Web.WebView2，版本执行时取 nuget 稳定版实测锁定）、`Scripting/Managed/Host.cs`（Tick drain / Shutdown 停服务 / scene view 每帧渲染挂点）、新增 `Scripting/Managed/Editor/`（服务、协议、对象表、scene view 渲染、PROTOCOL.md）、新增 `EditorWeb/`（TS 前端）、`build.py`（NuGet 原生 WebView2Loader.dll 拷贝 + 可选 node 构建步）、`Test/GPUBackendTester/Main.cpp`（新用例 + 最小 WS 客户端 + backbuffer 读回展示证明 + 可选屏幕捕获诊断）。**C++ 后端行为扩展（2026-11-22 展示证明改 backbuffer 读回）**：`VulkanRenderBackendNew/private/{RenderBackend_Vulkan.cpp, VulkanObjects/VulkanWindowHandle.h/.cpp, GPUGraph/VulkanGraphExecutor.cpp}` 与 `D3D12RenderBackend/private/{RenderBackend_D3D12.cpp, WindowContext.h/.cpp, GPUGraph/GPUGraphExecutor.cpp}` 的 Readback 支持 Backbuffer（呈现前登记式捕获）。**C++ 接口签名不变**（复用 Readback(ImageHandle)，CRenderBackend 纯虚集合不动 ⇒ 绑定面/审计/ABI/双基线零影响）。
- 依赖：首个 NuGet 包（首次 restore 需 nuget.org 可达，缓存后离线可编）；系统 WebView2 运行时（本机已装；缺失时编辑器明确报错禁用，不崩）；node（仅开发期构建前端，可选）。
- 行为变化（故意的）：CASCADED_EDITOR=1 时引擎进程多一个监听线程、一个原生窗口（scene view + 面板分栏）、一个本机端口；默认不设时零行为（现有 17 用例不受影响，回归判据钉死；糖层重载 additive 不改既有默认）。
- 回归口径：构建绿；CoreTests 全绿（含默认视口正控）；两后端 18/18（含新用例，**headless 全断言含展示证明**——backbuffer 读回不依赖屏幕）；bindings 基线 0/0/0 + codegen 产物逐字节不变（C++ 接口签名不变）；默认关编辑器时现有 17 用例 name/status 与改前基线一致（排除 duration_ms）；scene view 两层像素证据（离屏捕获中心红 + 窗口 backbuffer 读回视口中心红，均精确 (255,0,0,255)）；surface 不支持 TRANSFER_SRC 明确拒绝（自检开关红态）。
- 明确不做（Non-Goals，见 design）：视口合并（焦点/组合/缩放）、多目标编辑 UI、完整撤销栈、场景模型、协议同源生成、跨平台、ImGui 改动、热重载、web 内容像素级验证（子 HWND 不在 Vulkan 交换链，诚实登记）。
