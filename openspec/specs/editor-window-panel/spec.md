# editor-window-panel

## Purpose

网页编辑器首切片：单窗口分栏壳、本机服务线程边界、对象编号与失效、协议 v1、命令级撤销、scene view 渲染与 backbuffer 展示证明、C++ 读回往返验收。

## Requirements

### Requirement: 编辑器服务必须守住监听线程与主线程的边界
编辑器服务（`Scripting/Managed/Editor/`，同 Scripting.Managed 程序集，`CASCADED_EDITOR=1` 门控）MUST 只有一个后台监听线程负责 HttpListener 接收、WebSocket 收帧、JSON 解析校验与入队；该线程 MUST NOT 调用任何引擎绑定 API。引擎调用 MUST 全部在主线程 `Host.Tick` drain 命令队列时执行；**结果 MUST 按 requestId 回发起客户端，事件 MUST 广播给所有已连接客户端**。`Host.Shutdown` MUST 停监听、关连接、join 线程、处置 WebView2，全部先于 hostfxr_close；**测试跑后清理 MUST 经 EditorService.Stop() 主动停服务（不依赖进程结束），隔离后续用例**。门控未设时 MUST 零行为：不建线程、不建窗口、不占端口、不打编辑器日志。

#### Scenario: 网络到达不提前改变引擎状态
- **WHEN** 客户端发送 set-title 命令、**确认监听线程已收帧入队（cmdRequest 计数递增）后**、主线程下一次 Tick drain 之前读取 C++ 侧目标窗口标题
- **THEN** 标题 MUST 仍是旧值；drain 之后 MUST 变为新值——MUST NOT 出现网络线程直接调用引擎导致的提前变更（先确认到达，排除"命令未到达"假阴）

#### Scenario: 默认关闭零行为
- **WHEN** 不设 CASCADED_EDITOR 跑现有全部 tester 用例
- **THEN** 17 用例 status MUST 全 pass；日志（test_output/*.log 与 stderr）grep「[编辑器]」MUST 0 命中；TcpClient 连 localhost:18390 MUST 失败（无监听）——MUST NOT 出现编辑器窗口、监听端口或编辑器日志行

#### Scenario: 关闭顺序先于运行时关闭
- **WHEN** 引擎关闭触发 Host.Shutdown
- **THEN** 监听线程 MUST 已 join、WebSocket MUST 已关、WebView2 MUST 已处置，之后才 hostfxr_close；MUST NOT 出现运行时关闭后监听线程仍访问托管对象

### Requirement: 编辑器对象编号必须可失效且失效后零原生调用
服务 MUST 以单调递增整数编号登记其创建的目标窗口（首切片一个，编号从 1），编号不复用；面板是同一窗口的子 HWND，MUST NOT 有第二个登记对象。**失效检测 MUST 靠每帧 WindowShouldClose 轮询**（CloseWindow 只设 shouldClose 标志、不派发 close 回调，C# 租约无存活查询入口——实测核实）：轮询为真 MUST 使编号失效、移出注册表并广播 object-invalidated 事件。对未知编号的命令 MUST 返回 E_UNKNOWN_OBJECT，对已失效编号的命令 MUST 返回 E_OBJECT_INVALIDATED，两者 MUST 零原生引擎调用（以托管原生调用计数器读数为判据；**计数器 MUST 有正控：真实命令使计数递增证明计数器接上，增量 0 断言才有意义**）。

#### Scenario: 外部关闭目标后旧编号报失效
- **WHEN** C++ 侧在编辑器服务之外 CloseWindow 关闭目标窗口，下一帧轮询后客户端再用旧编号发命令
- **THEN** MUST 先收到 object-invalidated 事件，命令结果 MUST 为 E_OBJECT_INVALIDATED，进程 MUST NOT 崩溃，原生调用计数增量 MUST 为 0（此前正控已证明计数器递增）

#### Scenario: 未知编号不触碰引擎
- **WHEN** 客户端用从未存在的编号发 set-title
- **THEN** 结果 MUST 为 E_UNKNOWN_OBJECT，目标窗口状态 MUST 不变，原生调用计数增量 MUST 为 0

### Requirement: 命令协议 v1 必须以 PROTOCOL.md 为唯一事实源
协议 MUST 为 JSON 文本帧：hello/welcome 握手携带 protocolVersion（当前 1），版本不符 MUST 回 E_PROTOCOL_VERSION 并关连接；命令（get-object / list-objects / set-title / set-size / undo）携带 requestId 与 objectId；结果携带 requestId 与 ok/value 或 error{code,message}，**按 requestId 回发起客户端**；事件（object-changed / object-invalidated）**广播给所有已连接客户端**。set-size 的宽 MUST > 面板宽、高 MUST >0，否则 E_INVALID_ARG。错误码 MUST 限于 E_PROTOCOL_VERSION / E_UNKNOWN_OBJECT / E_OBJECT_INVALIDATED / E_INVALID_ARG / E_UNDO_EMPTY / E_BAD_MESSAGE。形状错或未知命令 MUST 回 E_BAD_MESSAGE 且不入引擎队列。`Scripting/Managed/Editor/PROTOCOL.md` MUST 是 C# 与 TS 两侧类型的唯一事实源，两侧逐字段镜像。

#### Scenario: 协议版本不符被拒绝
- **WHEN** 客户端 hello 携带 protocolVersion=999
- **THEN** MUST 收到 E_PROTOCOL_VERSION 错误且连接被关闭；MUST NOT 进入命令处理

#### Scenario: 形状错不入引擎队列
- **WHEN** 客户端发送缺 requestId 或未知 command 名的消息
- **THEN** MUST 回 E_BAD_MESSAGE，原生调用计数增量 MUST 为 0

#### Scenario: 改尺寸参数非法被拒
- **WHEN** set-size 携带 width≤面板宽（含 0/负数，视口宽将≤0）或 height≤0
- **THEN** MUST 回 E_INVALID_ARG，目标窗口尺寸 MUST 不变，零原生调用

### Requirement: 编辑器必须是引擎创建的单窗口分栏壳
服务 MUST 用 NewWindow 创建**一个**原生窗口（登记为对象 1，即 scene view），WebView2 MUST 以子 HWND 嵌入该窗口右侧面板条（**面板宽默认 320，CASCADED_EDITOR_PANEL_WIDTH 可配，范围 1..客户区宽−1，README 配置表登记**；GetNativeWindowHandle 返回 HWND*，须再解引用取 HWND 值），原生渲染 MUST 占左侧视口区域（客户区宽−面板宽，经 SetViewPort/SetScissor，糖层 additive 重载、既有整目标默认不变且有正控判据）。窗口尺寸变化 MUST 同步子 HWND 布局、控制器 Bounds 与视口区域，并按新视口区域重建捕获目标与图；**窗口关闭经每帧 WindowShouldClose 轮询发现** MUST 失效对象并处置控制器。WebView2 UI 线程路径 MUST 由实测探针决定并在读数行标明模式（0.2 实测 main-thread 成立，专用 UI 线程回退保留为防御）。WebView2 运行时缺失或控制器创建失败 MUST 明确报错并禁用编辑器（停服务线程），进程 MUST NOT 崩溃；**MUST 提供构造开关（CASCADED_EDITOR_SELFTEST=no-runtime）强制走缺失路径供红态实测**。面板静态资源缺失 MUST 返回明确错误页说明原因，MUST NOT 静默。

#### Scenario: 面板与视口同窗分栏
- **WHEN** 编辑器开启后读取窗口计数与托管读数行
- **THEN** GetWindowCount 增量 MUST 为 1，读数行 MUST 含面板矩形与视口区域且两者分栏铺满客户区——MUST NOT 出现第二个壳窗口

#### Scenario: 单窗口尺寸同步
- **WHEN** C++ 侧 SetWindowSize 修改该窗口尺寸
- **THEN** 托管读数行 MUST 显示控制器 Bounds 与视口区域同步为新布局

#### Scenario: 运行时缺失明确禁用
- **WHEN** 设 CASCADED_EDITOR_SELFTEST=no-runtime 强制走运行时缺失路径（或真实运行时不可用）
- **THEN** MUST 有明确错误日志且编辑器禁用，进程 MUST NOT 崩溃；错误码 MUST 与端口冲突/数据目录失败可区分

#### Scenario: 资源缺失明确错误页
- **WHEN** bin/editor-web/ 无面板资源时请求面板页
- **THEN** MUST 返回说明"资源未构建"的错误页，MUST NOT 静默 404 或空白

### Requirement: 撤销必须覆盖改标题与改尺寸且失效对象报错
每对象 MUST 有单层 undo 槽：set-title/set-size 成功执行时记录旧值；undo MUST 经同一执行路径重放旧值并产生 object-changed 事件，随后清槽。空槽 undo MUST 回 E_UNDO_EMPTY；失效对象 undo MUST 回 E_OBJECT_INVALIDATED。

#### Scenario: 撤销恢复旧标题
- **WHEN** set-title 把标题从 A 改为 B 后发送 undo
- **THEN** C++ 侧读回标题 MUST 为 A，且收到 object-changed 事件；再 undo MUST 回 E_UNDO_EMPTY

#### Scenario: 失效对象撤销报错
- **WHEN** 目标窗口已失效后发送 undo
- **THEN** MUST 回 E_OBJECT_INVALIDATED，零原生调用

### Requirement: Scene view 必须由渲染后端真实渲染并证明展示
目标窗口 MUST 是 scene view：编辑器开启期间，服务 MUST 经生成绑定的渲染后端表面（C# 糖层）每帧向其**左侧视口区域**渲染三角形（窗口视口区域 + 同区域尺寸离屏捕获双目标），改尺寸成功后 MUST 按新视口区域重建捕获目标与图。渲染后端不可用 MUST 明确报错并禁用编辑器，MUST NOT 静默降级。验收 MUST 含两层像素证据，**像素判据均为 (255,0,0,255) 精确匹配**（沿用现有探针精确匹配惯例，不用凭空容差）：① 渲染证明——托管读数行给出捕获目标句柄与执行计数，C++ 侧经 Readback 读离屏捕获中心像素（确定性；C# 绑定面无读回入口，像素读取在 C++ 侧，沿用现有 C# 渲染探针的像素断言模式）；② **展示证明——读回窗口真实 swapchain backbuffer 视口区域中心像素**（2026-11-22 改方向：本机 GameViewer 虚拟显示适配器阻断屏幕捕获硬件 flip 内容，WGC/BitBlt 实测全白含原生正控；backbuffer 读回证明 Present 把后端输出渲染进窗口交换链表面，环境无关、headless 可跑）。展示证明 MUST 走 resource-readback 受控例外（**呈现前读：ExecuteGraph 渲染窗口后 WaitIdle、下次 Present 前；Vulkan 先查 surface supportedUsageFlags 含 TRANSFER_SRC，不含明确拒绝**），读窗口 ImageHandle（Backbuffer）视口中心。屏幕捕获（WGC/BitBlt）降为可选诊断（虚拟显示环境抓不到，注明限制）。backbuffer 读回失败/像素不红 MUST 判红（E_EDITOR_DISPLAY_PIXEL）；surface 不支持 TRANSFER_SRC 时 MUST 明确拒绝并打印原因（不静默通过）；渲染证明仍 MUST 执行。

#### Scenario: 渲染证明中心像素红
- **WHEN** 编辑器开启并跑过至少一帧，托管读数行给出捕获目标句柄与执行计数，C++ 侧经 Readback 读该捕获目标中心像素
- **THEN** ExecuteGraph 计数 MUST >0 且中心像素 MUST 为 (255,0,0,255)

#### Scenario: 展示证明 backbuffer 读回红
- **WHEN** 编辑器渲染三角形到窗口、WaitIdle 后下次 Present 前，C++ 侧 Readback 窗口 ImageHandle（Backbuffer，surface 支持 TRANSFER_SRC）读视口区域中心像素
- **THEN** 中心像素 MUST 为 (255,0,0,255)——证明 Present 把后端输出渲染进窗口交换链表面（GPU 级展示能力，环境无关）；surface 不支持时 MUST 明确拒绝打印原因

#### Scenario: 改尺寸后渲染延续
- **WHEN** set-size 改为新合法宽高（宽>面板宽）并收到 ok 结果
- **THEN** 捕获尺寸读数 MUST 等于**新视口区域（宽−面板宽，高）**且中心像素 MUST 仍为 (255,0,0,255)（捕获目标与图已按视口区域重建）

#### Scenario: 后端不可用明确禁用
- **WHEN** 渲染后端表面未绑定或不可用时开启编辑器
- **THEN** MUST 有明确错误日志且编辑器禁用，进程 MUST NOT 崩溃，MUST NOT 出现空壳 scene view 假成功

### Requirement: 首切片往返必须以 C++ 读回为验收
set-title/set-size 命令成功结果返回后，C++ 侧对同一目标窗口 GetWindowName/GetWindowSize 的读回 MUST 与请求值逐字/逐值相同；结果 MUST 来自真实执行而非本地乐观显示。tester MUST 新增 TestEditorWindowPanel 用例以进程内 WebSocket 客户端覆盖上述往返与全部负向场景，**注册在 TestCSharpWindowCallbacks 之前**（该用例后托管关闭，编辑器用例须在其前跑），docs/testing.md 清单 MUST 同步。

#### Scenario: 改标题 C++ 读回逐字相同
- **WHEN** 客户端 set-title 为本次测试生成的唯一字符串并收到 ok 结果
- **THEN** C++ 侧 GetWindowName 读回 MUST 与该字符串逐字相同

#### Scenario: 改尺寸 C++ 读回相等
- **WHEN** 客户端 set-size 为与当前不同的合法宽高并收到 ok 结果
- **THEN** C++ 侧 GetWindowSize 读回的宽和高 MUST 等于请求值
