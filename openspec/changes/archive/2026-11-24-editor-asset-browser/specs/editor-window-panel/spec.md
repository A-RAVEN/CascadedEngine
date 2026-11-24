## MODIFIED Requirements

### Requirement: 命令协议 v1 必须以 PROTOCOL.md 为唯一事实源
协议 MUST 为 JSON 文本帧：hello/welcome 握手携带 protocolVersion（当前 1），版本不符 MUST 回 E_PROTOCOL_VERSION 并关连接；命令（get-object / list-objects / set-title / set-size / undo / **list-assets**）携带 requestId，**对象作用域命令**（get-object / set-title / set-size / undo）另携带 objectId，**非对象作用域命令**（list-objects / list-assets）无 objectId 也合法；结果携带 requestId 与 ok/value 或 error{code,message}，**按 requestId 回发起客户端**；事件（object-changed / object-invalidated / **assets-changed**）广播给所有已连接客户端（assets-changed 无 objectId）。set-size 的宽 MUST > 面板宽、高 MUST > dock 高（默认 240，否则视口高 = 高 − dock 高 ≤ 0），否则 E_INVALID_ARG。错误码 MUST 限于 E_PROTOCOL_VERSION / E_UNKNOWN_OBJECT / E_OBJECT_INVALIDATED / E_INVALID_ARG / E_UNDO_EMPTY / E_BAD_MESSAGE / **E_ASSET_ROOT**（资产根不可用：门面未绑/根非法/目录不存在/枚举 IO 错误）。形状错或未知命令 MUST 回 E_BAD_MESSAGE 且不入引擎队列。`Scripting/Managed/Editor/PROTOCOL.md` MUST 是 C# 与 TS 两侧类型的唯一事实源，两侧逐字段镜像。协议版本保持 1（命令/事件/错误码加法扩展，既有五条命令与六条错误码的形状与语义不变）。

#### Scenario: 协议版本不符被拒绝
- **WHEN** 客户端 hello 携带 protocolVersion=999
- **THEN** MUST 收到 E_PROTOCOL_VERSION 错误且连接被关闭；MUST NOT 进入命令处理

#### Scenario: 形状错不入引擎队列
- **WHEN** 客户端发送缺 requestId 或未知 command 名的消息
- **THEN** MUST 回 E_BAD_MESSAGE，原生调用计数增量 MUST 为 0

#### Scenario: 改尺寸参数非法被拒
- **WHEN** set-size 携带 width≤面板宽（含 0/负数，视口宽将≤0）或 height≤dock 高（含 0/负数，视口高将≤0）
- **THEN** MUST 回 E_INVALID_ARG，目标窗口尺寸 MUST 不变，零原生调用

#### Scenario: 非对象作用域命令无 objectId 合法
- **WHEN** 客户端发 list-objects 或 list-assets（不带 objectId）
- **THEN** MUST 正常处理（list-assets 回资产条目数组），MUST NOT 因缺 objectId 回 E_BAD_MESSAGE

### Requirement: 编辑器必须是引擎创建的单窗口分栏壳
服务 MUST 用 NewWindow 创建**一个**原生窗口（登记为对象 1，即 scene view），壳内 MUST 为**三分区**铺满客户区（仍单壳窗口，MUST NOT 出现第二个壳窗口）：① **场景视口**（左上）原生渲染占位，宽 = 客户区宽 − 面板宽、高 = 客户区高 − dock 高，经 SetViewPort/SetScissor（糖层 additive 重载、既有整目标默认不变且有正控判据）；② **对象检查器**（右上）WebView2 以子 HWND 嵌入右侧面板条（**面板宽默认 320，CASCADED_EDITOR_PANEL_WIDTH 可配，范围 1..客户区宽−1，README 配置表登记**；GetNativeWindowHandle 返回 HWND*，须再解引用取 HWND 值），控制器 Bounds = (ViewportWidth, 0, PanelWidth, ClientHeight − DockHeight)；③ **资产浏览器 dock**（底部通栏）**第二个 WebView2 控制器**，Bounds = (0, ClientHeight − DockHeight, ClientWidth, DockHeight)，加载独立资产浏览器页面（**dock 高默认 240，CASCADED_EDITOR_DOCK_HEIGHT 可配，范围 1..客户区高−1，上界校验与越界禁用照面板宽先例 EditorWindow.cs:248-251，README 配置表登记**；运行期固定）。窗口尺寸变化 MUST 同步两个子 HWND 布局、两个控制器 Bounds 与视口区域，并按新视口区域（高 = ClientHeight − DockHeight）重建捕获目标与图；**窗口关闭经每帧 WindowShouldClose 轮询发现** MUST 失效对象并处置两个控制器。WebView2 UI 线程路径 MUST 由实测探针决定并在读数行标明模式（0.2 实测 main-thread 成立，专用 UI 线程回退保留为防御），托管读数行 MUST 含面板矩形、视口区域与 dock 矩形。WebView2 运行时缺失或任一控制器创建失败 MUST 明确报错并禁用编辑器（停服务线程），进程 MUST NOT 崩溃；**MUST 提供构造开关 CASCADED_EDITOR_SELFTEST=no-runtime 强制走运行时缺失路径（于 NewWindow 前返回，覆盖运行时缺失）、CASCADED_EDITOR_SELFTEST=no-dock-controller 强制走「运行时正常、第二控制器创建失败」路径（no-runtime 不覆盖此分支）供红态实测**。第二控制器失败/导航失败 MUST 经独立 Error/导航检查通道触发禁用（就绪读数行含 dock 控制器字段）；窗口关闭 MUST 处置两个控制器。面板或 dock 静态资源缺失 MUST 返回明确错误页说明原因并写日志，MUST NOT 静默；**assets.html 单独缺失（index.html 在）MUST 返回错误页而非 404 纯文本，dock 导航判据 MUST 把 4xx 当失败**。

#### Scenario: 三分区同窗铺满客户区
- **WHEN** 编辑器开启后读取窗口计数与托管读数行
- **THEN** GetWindowCount 增量 MUST 为 1，读数行 MUST 含面板矩形、视口区域与 dock 矩形且三者铺满客户区（视口左上 + 检查器右上 + dock 底部通栏）——MUST NOT 出现第二个壳窗口

#### Scenario: 单窗口尺寸同步
- **WHEN** C++ 侧 SetWindowSize 修改该窗口尺寸
- **THEN** 托管读数行 MUST 显示两个控制器 Bounds 与视口区域同步为新三分区布局

#### Scenario: 运行时缺失明确禁用
- **WHEN** 设 CASCADED_EDITOR_SELFTEST=no-runtime 强制走运行时缺失路径（或真实运行时不可用）
- **THEN** MUST 有明确错误日志且编辑器禁用，进程 MUST NOT 崩溃；错误码 MUST 与端口冲突/数据目录失败可区分

#### Scenario: 第二控制器创建失败明确禁用
- **WHEN** 设 CASCADED_EDITOR_SELFTEST=no-dock-controller（运行时正常、第一控制器建成、第二控制器创建失败）
- **THEN** MUST 有明确错误日志且编辑器禁用，进程 MUST NOT 崩溃；就绪读数行 MUST 反映 dock 控制器失败（非仅第一控制器 controller=1/nav=1 即判就绪）

#### Scenario: 资源缺失明确错误页
- **WHEN** bin/editor-web/ 无面板或 dock 资源时请求对应页（含 index.html 在而 assets.html 单独缺失）
- **THEN** MUST 返回说明"资源未构建"的错误页并写日志，MUST NOT 静默 404 纯文本或空白；dock 导航 MUST 把 4xx 当失败