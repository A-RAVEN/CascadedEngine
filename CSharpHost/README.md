# CSharpHost：把托管运行时（C#）接进引擎的宿主模块

**它做什么**：在一个普通引擎模块的初始化阶段，把 .NET 运行时（CoreCLR）拉进本进程、加载 `Scripting.Managed.dll`、拿到托管侧 `Boot` / `Tick` 两个入口，并每帧调一次 `Tick`；同时把一张函数表交给托管侧，让它能回调引擎的日志、`echo`，以及按"接口编号 + 实例名"取引擎接口的函数表。

函数表（`EngineApi`）当前是 **v12**、56 字节（9 字段；桥五类的五个编号字段已随退役删除），字段顺序就是 ABI、只许在末尾追加：

| 字段 | 从哪一版起 | 作用 |
|---|---|---|
| `abi_version` / `size` | v1 | 两侧握手：版本与大小都必须相等 |
| `log` / `echo` | v1 | 引擎回调（日志、echo 自检） |
| `get_interface_api` | v2（v4 加实例名参数） | 按 (接口编号, 实例名) 取该接口的函数表与实例；`size == 0` 是"只问大小"的探针调用 |
| `window_interface_id` / `window_system_interface_id` | v3 | 接口编号由引擎侧单向下发，托管侧不手抄 |
| `get_binding_layout_type` / `get_binding_layout_field` | v5 | 布局清单的引擎侧读数（真实 `sizeof` / `offsetof`）：托管 `Host.Boot` 逐项比对后才允许绑定 |
| `script_render_backend_interface_id` / `script_render_graph_interface_id` / `script_render_target_interface_id` | v6 | 脚本渲染适配面的三个编号（3 / 4 / 5）；后端表有默认实例，图与目标表是目标类型表 |
| `script_shader_struct_interface_id` | v7 | 脚本参数结构表的编号（6）；与图、目标同属目标类型表（没有默认实例） |
| `script_texture_interface_id` | v8 | 脚本只读纹理表的编号（7）；同样属目标类型表（没有默认实例） |

**v12（csharp-bindings-namespace-alignment 7.1）**：补 `GPUBuffer` 直绑（第六原生接口、编号 8），
`CreateGPUBuffer` 解 SKIP；函数表布局不变（仍 9 字段 / 56 字节），版本号 11 → 12。

**v11（csharp-bindings-namespace-alignment 4.1）**：接口身份链改完整限定名（生成的编号常量
`kBindingInterfaceId<线形>` / C# `GraphicsBackend__GPUGraphInterfaceId` 等），函数表布局不变
（仍 9 字段 / 56 字节），版本号 10 → 11；宿主与托管侧都只读生成常量。

**v10（csharp-native-direct-binding 6.2）**：桥五类的五个编号字段随退役删除，布局从 14 字段 / 80 字节重建为 **9 字段 / 56 字节**；
版本升级是因为这张表改由绑定生成器从 `Tools/CodeGen/interface-ids.lock` 取编号后发射 —— C++ 侧的
`struct EngineApi` 与其装配代码在生成的 `BindingRegistry.Bindings.h/.cpp` 里，托管侧的逐字段
镜像与编号常量在生成的 `EngineApi.Bindings.cs` 里；两侧手写镜像已退役。宿主只保留按固定名字
暴露的几项实现（`log` / `echo` / 实例查找 / 错误出口 / 默认实例工厂 / 角色登记）。

`sizeof(EngineApi)` 由**生成的** `BindingRegistry.Bindings.h` 里的
`static_assert(sizeof(EngineApi) == 56u)` 由编译器核对：v10 的正文 = 4 + 4 + 8×5 + 4 + 4 = 56 字节，8 字节对齐没有尾部填充；
旧版 v8 追加一个 `uint32_t` 后正文 76 字节、按 8 字节对齐补到 **80 字节**
（不是 72）；v9 布局不变，v10 因删字段缩到 56 字节（不是 80）。

接口编号表（编号**在 `Tools/CodeGen/interface-ids.lock` 里，构建时核对；发号经 `--update-ids`**，由生成器发射成
`kBindingInterfaceId*` 常量与 `EngineApi.Bindings.cs` 的编号常量；这里只是给它一个可读的名字）：

| 编号 | 接口 | 实例名（本仓库现有的那份） |
|---|---|---|
| 1 | `cawindow::IWindow` | `Window` |
| 2 | `cawindow::IWindowSystem` | `WindowSystem_GLFW` |
| 3 | `graphics_backend::CRenderBackend` | 空（默认实例；宿主在模块 link 阶段把引擎里那**一个**真实后端登记进来，托管侧取的函数表就是它） |
| 4 | `graphics_backend::GPUGraph` | **目标类型表**：没有默认实例；图对象由 `GPUGraph.Create` 产出、按目标句柄解析 |
| 5 | `graphics_backend::ShaderStruct` | **目标类型表**：同上（参数结构，`CreateShaderStruct` 产出） |
| 6 | `graphics_backend::GPUTexture` | **目标类型表**：同上（后端纹理对象，`CreateGPUTexture` 产出） |
| 7 | `graphics_backend::WindowHandle` | **目标类型表**：同上（窗口后台缓冲句柄，`GetWindowHandle` 产出） |
| 8 | `graphics_backend::GPUBuffer` | **目标类型表**：同上（后端直建缓冲，`CreateGPUBuffer` 产出；生命周期跟随后端，跨图复用） |
| 9 | `resource_management::IResourceFactory` | **目标类型表（反向）**：C# 实现 `ResourceFactoryBase`，经 `IResourceFactoryBindings.Create` 拿强句柄交给资源系统的工厂注册表；C++ 原生工厂走同一个注册表，不进这张编号表 |
| 10 | `resource_management::IResource` | **目标类型表（反向）**：C# 实现 `ResourceBase`，由反向工厂的 `Create` 在托管桥里包成原生 `IResourceProxy`；`Deserialize` 本批排除 |
| 11 | `resource_management::ResourceImporter` | **目标类型表（反向）**：C# 实现 `ResourceImporterBase`（`GetTags` / `GetSourceDirectory` / `GetDestDirectory` / `Produce`），经 `ResourceImporterBindings.Create` 拿强句柄，原生侧 `ResolveImpl` 后交给 `ResourceImportingSystem::AddImporter`；C++ 原生导入器（两个着色器导入器）走同一个导入器列表、不进这张编号表 |
| 12 | `resource_management::ResourceImporterProductions` | **目标类型表（反向）**：C# 实现 `ResourceImporterProductionsBase`（`HasNext` / `NextPath` / `NextResource`），由 `Produce` 的托管桥用 `CreateProxy` 包成原生代理；迭代器归资源管线所有（扫描结束即析构） |

同一个接口可以登记多份实现，靠实例名区分；实例名传 `nullptr` = 该接口**默认那一份**（最先登记的）。登记与查找都走 `IScriptHost::SetInterfaceInstance` / `GetInterfaceInstance`，两边必须用同一对 (编号, 实例名)。

**它不做什么**：

- **不生成任何绑定**：绑定产物由 `Tools/CodeGen`（`--emit-binding` / `--emit-registry`）生成；宿主只 `#include` 生成的 `BindingRegistry.Bindings.cpp`，由它在同一个翻译单元里再包含各接口产物。哪个引擎接口能调、用什么形状过界，属于 `openspec/specs/csharp-bindable-surface/spec.md` 的规矩。
- **不做 AOT**：现在是 framework-dependent 的 JIT 形态（`runtimeconfig.json` 指向 `Microsoft.NETCore.App`）。
- **不建线程、不做热重载、不支持多脚本程序集**：`Boot` / `Tick` 都在引擎主线程上被调用。**例外（编辑器门控）**：`CASCADED_EDITOR=1` 时服务额外开**一个监听线程**（HttpListener + WebSocket），网络线程只解析/入队，命令仍在引擎主线程的下一次 `Tick` 执行（见下文编辑器面板服务）。

## 脚本层：`Scripting/Managed/UserScript.cs`

- **写在哪**：`Scripting/Managed/UserScript.cs`（与宿主同程序集，编译期一起编进去）。使用者改这个文件、重新构建即生效，**不必碰 `Host.cs`**。
- **入口签名**：`public static void OnEngineReady(Scripting.Generated.Bindings.Cawindow.IWindowSystem windows)` —— 一个静态类、一个静态方法。
- **什么时候被调**：宿主 `EnsureBound()` 把**编号 1、2 两张函数表都绑定成功之后**调一次；**整个进程生命周期内只调一次**（重绑不会再调，所以脚本不会越建越多窗口）。
- **怎么判"调了几次"**：脚本里 `internal static int CalledCount` 自增，宿主把它打进日志 `[脚本] OnEngineReady 调用 #N`。
- **边界**：传进来的 `windows` 在引擎换默认实例之前一直有效，**不要存进静态字段跨重绑使用**；建出来的窗口是弱持有，不 `CloseWindow()` 就一直活到进程退出（`Dispose` 只释放句柄）。
- **失败**：脚本抛异常由宿主 `Tick` 的 `catch` 转成非零返回码 + `LastError`（带异常类型与消息），不会静默。

## 脚本渲染直绑面 + C# 糖层（csharp-native-direct-binding 4.1）

渲染侧不再有专门的适配桥：`CRenderBackend` / `GPUGraph` / `ShaderStruct` / `GPUTexture` / `WindowHandle`
五类原生接口**直接进绑定面**（编号见上表），生成的函数表由 `CSharpHost` 编进同一个翻译单元。脚本侧的
便利形状全部在托管层 `Scripting/Managed/NativeRenderSugar.cs`：窗口 / 捕获目标对象、图计划与提交前置
校验（D6）、参数结构元数据校验、字节上传糖 `ScheduleData(handle, byte[])`、`PassCount` 读
`GetPassIndices().Length`（D13）。原生接口签名零改动。

- **入口**：`UserScript.OnRenderProbe(IWindowSystem windows, ScriptRenderBackend backend)`，与一次性 `OnEngineReady` **互不消耗**。`Host` 只在 `CASCADED_CSHARP_RENDER_PROBE=1` 且后端 + 五张目标类型表都绑定成功时调用一次；开关由测试在**自己的 Tick 之前**设置、之后清理。
- **脚本做的事**：`NewWindow` 建窗 → `GetWindowTarget`（`GetWindowHandle` + `GetSizeSafe` + `ImageHandle.FromWindowHandle`，`isBackbuffer=true`）+ 同尺寸 `CreateCaptureTarget`（`CreateGPUTexture` + `ImageHandle.FromGPUTexture`，托管记尺寸）→ `CreateGraph` → 写 72 字节顶点（三个顶点 × 24 字节步长，位置偏移 0、颜色偏移 12）→ `AddBuffer`（内部 `AllocBuffer` + 上传糖）/ `SetPositionColorStream` / 两次同逻辑 `AddTrianglePass`（窗口 + 离屏）→ `Present` → `SubmitGraph`（糖层先把计划落成原生 pass，再跑完整性校验，最后 `ExecuteGraph`）→ `WaitIdle`，并把图与两个目标保留到 `Shutdown` 再释放。
- **依赖**：宿主在模块 link 阶段从模块管理器取 `CRenderBackend` 与 `CThreadManager`，把**原生后端**登记为编号 3；缺任何一个时编号 3 **不登记**并留下明确错误，未启用探针的窗口路径保持原行为。`ExecuteGraph` 的调度器仍由引擎注入（`CAInject_CRenderBackend_ExecuteGraph_scheduler`）。
- **Debug 自检开关**（只在 Debug 生效，供 `TestCSharpTriangle` 构造负向证据）：`CASCADED_CSHARP_RENDER_SELFTEST=skip-draw`（在调用层省略窗口与离屏目标的两次 `AddTrianglePass`，图 pass 数为 0；托管前置校验拒绝空图提交，原生测试与正常模式的 2 个 pass 对比后用 `E_TRIANGLE_DRAW` 判红）、`bad-vertex`（一个顶点改蓝：中心不再是纯红）、`skip-present`（省略呈现）、`skip-wait`（省略 `WaitIdle`）。生产脚本不依赖它们。
- **关闭顺序**：托管 `Shutdown` 按 **参数结构 → 纹理 → 图 → 离屏目标 → 后端表面** 逐项释放，每一步都往宿主日志与 stderr 打一行 `[托管侧 关闭顺序] N/4 …`；原生后端由引擎模块生命周期持有，宿主不再 new 适配器。
- **阻塞语义**：渲染探针里的 `WaitIdle` 会在引擎主线程上等到 GPU 空闲；这是本切片的同步提交语义，不是后台异步。

## 常量色（着色器参数）探针（csharp-render-shader-constants）

- **入口**：`UserScript.OnConstantColorProbe(IWindowSystem windows, ScriptRenderBackend backend)`，与 `OnRenderProbe` **互不消耗**：独立开关 `CASCADED_CSHARP_CONSTCOLOR_PROBE=1`、独立调用标记，由 `TestCSharpConstantColor` 在**自己的 Tick 之前**设置、之后清理。
- **脚本做的事**：`NewWindow` → `GetWindowTarget` + 同尺寸 `CreateCaptureTarget` → `CreateGraph` → 36 字节顶点（三个顶点 × 12 字节步长，只有 `POSITION`）→ `AddBuffer` / `SetPositionStream` → `CreateShaderStruct("ConstantColor")` + 3 个 `CreateShaderStruct("ColorSubStruct")`（各 `SetFloat3("color", …)`、`SetStruct("colorStruct", sub, i)`）→ 窗口与离屏各一个 `AddTrianglePass`（`Shaders/Test/TestTriangleWithConstantColor`，清除色 `(0,0,1,1)`）→ 两次 `SetPassParam(passIndex, "constantColorBlock", params)` → `Present` → `SubmitGraph` → `WaitIdle`。
- **参数句柄生命周期**：正常模式在**提交并等待之后**释放参数结构表面对象，旧表面再调用会抛 `ObjectDisposedException`（已提交的图由原生侧继续持有绑定的 `ShaderStruct` 直到等待完成）。
- **Debug 自检开关**（`CASCADED_CSHARP_CONSTCOLOR_SELFTEST`，供 `TestCSharpConstantColor` 构造负向证据）：`bad-struct-colors`（三槽改成红/绿/蓝：中心不再是纯红，以 `E_CONSTCOLOR_PIXEL` 判红）、`skip-param`（完全不调 `SetPassParam`：`SubmitGraph` 在进入后端执行前拒绝并点名 `constantColorBlock`，以 `E_CONSTCOLOR_PARAM` 判红，不读像素）。生产脚本不依赖它们。

## 结构化缓冲探针（csharp-render-structured-buffer）

- **入口**：`UserScript.OnStructuredBufferProbe(IWindowSystem windows, ScriptRenderBackend backend)`，与 `OnRenderProbe` / `OnConstantColorProbe` **互不消耗**：独立开关 `CASCADED_CSHARP_SB_PROBE=1`、独立调用标记，由 `TestCSharpStructuredBuffer` 在**自己的 Tick 之前**设置、之后清理。
- **脚本做的事**：`NewWindow` → `GetWindowTarget` + 同尺寸 `CreateCaptureTarget` → `CreateGraph` → 36 字节顶点（三个顶点 × 12 字节步长，只有 `POSITION`）→ `AddBuffer("sbColors", 1 元素 × 12 字节, magenta)` → `SetPositionStream` → `CreateShaderStruct("StructuredColor")` → `SetBuffer("colorStructuredBuffer", "sbColors")` → 窗口与离屏各一个 `AddTrianglePass`（`Shaders/Test/TestTriangleWithStructuredBufferColor`，清除色 `(0,1,0,1)`）→ 两次 `SetPassParam(passIndex, "structuredColorBlock", params)` → `Present` → `SubmitGraph` → `WaitIdle`。缓冲名按"先绑后建"在 `SubmitGraph` 时按**本图**缓冲登记表解析（`SetBuffer` 当场只校验字段级元数据）。
- **Debug 自检开关**（`CASCADED_CSHARP_SB_SELFTEST`，供 `TestCSharpStructuredBuffer` 构造负向证据）：`bad-buffer-color`（缓冲元素写 `(0,0,1)`：中心精确变成 `(0,0,255,255)`，以 `E_SB_PIXEL` 判红）、`unbound-field`（跳过 `SetBuffer`：`SubmitGraph` 在进入后端执行前拒绝并点名 `colorStructuredBuffer`，以 `E_SB_BIND` 判红，不读像素）、`bad-buffer-name`（绑未登记的 `sbColorsMissing`：同样在提交前拒绝并点名缓冲名，`E_SB_BIND`）。生产脚本不依赖它们。

## 纹理采样探针（csharp-render-texture-sampling）

- **入口**：`UserScript.OnTextureProbe(IWindowSystem windows, ScriptRenderBackend backend)`，与另外三个渲染探针 **互不消耗**：独立开关 `CASCADED_CSHARP_TEX_PROBE=1`、独立调用标记，由 `TestCSharpTextureSampling` 在**自己的 Tick 之前**设置、之后清理。
- **脚本做的事**：`NewWindow` → `GetWindowTarget` + 同尺寸 `CreateCaptureTarget` → `CreateGraph` → `CreateTexture("texYellow2x2", 2, 2, 16 字节)`（**一次调用完成创建 + 上传 + 转采样态 + 同步等待**）→ 60 字节顶点（三个顶点 × 20 字节步长：位置偏移 0、UV 偏移 12）→ `AddBuffer` / `SetPositionTextureStream` → `CreateShaderStruct("TextureSampleBlock")` → `SetImage("testTexture", tex)` + `SetSampler("testSampler", LinearClamp)` → 窗口与离屏各一个 `AddTrianglePass`（`Shaders/Test/TestCSharpTextureSampling`，清除色 `(0,0,1,1)`）→ 两次 `SetPassParam(passIndex, "textureBlock", params)` → `Present` → `SubmitGraph` → `WaitIdle`。
- **释放后使用**：探针末尾先 `tex.Dispose()`，再用同一个表面对象调 `SetImage` —— 糖层在进入原生实现之前抛 `ObjectDisposedException`（原生调用 0），这一行写进摘要 `disposed=ObjectDisposedException 原生 SetImage 调用 0`，`TestCSharpTextureSampling` 从本次运行的日志里读它。
- **Debug 自检开关**（`CASCADED_CSHARP_TEX_SELFTEST`，供 `TestCSharpTextureSampling` 构造负向证据）：`bad-texture-color`（四纹素改青 `(0,255,255,255)`：中心精确变成它，以 `E_TEX_PIXEL` 判红）、`unbound-image`（跳过 `SetImage`：`SubmitGraph` 在进入后端执行前拒绝并点名 `testTexture`，以 `E_TEX_BIND` 判红，不读像素）、`unbound-sampler`（跳过 `SetSampler`：同样点名 `testSampler`，`E_TEX_BIND`）。生产脚本不依赖它们。
- **计数语义**：纹理上传的执行与等待走**后端直连**（`ExecuteGraph` + `WaitIdle`），单列糖层的 `TextureUploadExecuteCount`；`ExecuteGraphCount` / `WaitIdleCount` 只反映脚本提交。一切"本次提交执行增量 0"的判据只看后者（C++ 用例从托管读数行 `[托管 读数]` 里读这些增量）。
- **已知限制**：本批**不提供纹理显式销毁**（`GPUTexture` 只有描述符/名称读取，销毁由引擎资源生命周期决定）；托管 `ScriptTexture.Dispose()` 只把表面标成失效（释放后使用在托管层拒绝），原生纹理对象由绑定对象表与图内参数结构保活到进程末。显式释放的语义（谁最后持引用、飞行中提交）留给后续批。

## 编辑器面板服务（editor-window-panel）

**门控与入口**：`CASCADED_EDITOR=1` 时 `Host.Tick` 每帧调 `Editor.EditorService.Tick`；`Host.Shutdown` 先停服务（`Stop("Host.Shutdown")`：停监听、断客户端、join 监听线程、关 WebView2 控制器与窗口）再关运行时。**脚本路径不建线程；门控开启时额外一个监听线程**（HttpListener 静态文件 + `/ws` 升级），网络线程只做协议解析与入队，命令统一在引擎主线程的下一次 `Tick` 执行，`result` 在命令执行完成之后才发出。

**配置（三项，默认值与可配范围）**：

| 变量 | 作用 | 默认 | 可配范围 |
|---|---|---|---|
| `CASCADED_EDITOR_PORT` | 监听端口（只绑本机 localhost） | `18390` | `1`-`65535`（非法值明确禁用） |
| `CASCADED_EDITOR_WEBVIEW2_DATA` | WebView2 用户数据目录 | `%TEMP%\cascaded-editor-webview2` | 任意可写目录 |
| `CASCADED_EDITOR_PANEL_WIDTH` | 右侧面板宽（像素） | `320` | `1`..`客户区宽-1`（越界明确禁用并点名范围） |

其余开关：`CASCADED_EDITOR_SELFTEST=no-runtime`（**仅 Debug**：模拟 WebView2 运行时缺失 → 明确禁用、不崩）、`CASCADED_EDITOR_STOP=1`（主动停服务，测试收尾用）、`CASCADED_EDITOR_READINGS_FILE`（读数行同时追加到该文件，测试用）、`CASCADED_EDITOR_DISPLAY_PROOF=1`（可选屏幕捕获诊断）、`CASCADED_READBACK_FORCE_NO_BB_TRANSFER`（**仅 Debug**：强制窗口 backbuffer 回读走"能力不支持"拒绝分支，供红态）。

**单窗口分栏**：`NewWindow(1280, 720, "Cascaded 编辑器")` 建**一个**窗口；`GetNativeWindowHandle` 返回 `HWND*`，解引用一次拿到父 HWND 贴 WebView2 子控制器；面板矩形 `(客户区宽-面板宽, 0, 面板宽, 客户区高)`，`controller.Bounds` 与子 HWND 位置随尺寸变化同步；左侧视口由 scene view 渲染（`Shaders/Test/TestSimpleTriangle`，72 字节顶点、绿底红三角），每帧复用同一张图 `SubmitGraph` + `WaitIdle` 并 `Present`；`set-size` 成功或窗口尺寸回调后按新视口重建捕获目标与图。`set-size` 要求宽 **> 面板宽**且高 > 0，否则 `E_INVALID_ARG`（杜绝视口宽 ≤ 0）。

**WebView2 依赖与缺失行为**：NuGet `Microsoft.Web.WebView2`（版本见 `Scripting/Managed/Scripting.Managed.csproj`，`build.py` 从 NuGet 缓存显式拷 `WebView2Loader.dll`）；运行期需要 WebView2 Runtime。运行时缺失/创建失败 → 日志 `[编辑器] 禁用：…运行时缺失…` 并走关闭路径（不静默、不崩）。导航判据：`NavigationCompleted.IsSuccess` 或 `HttpStatusCode >= 100`（HTTP 错误页也算导航成功，只有网络失败才禁用）。**静态资源根**是 `bin/editor-web/`：解析顺序为 `AppContext.BaseDirectory`（非空时）→ 程序集所在目录（hostfxr 下 `Assembly.Location` = `bin/Scripting.Managed.dll`）→ 进程可执行文件目录，结果必须是绝对路径；启动后第一次请求时日志打印 `[编辑器] 面板静态根：<绝对路径>`（同一条也写进 `CASCADED_EDITOR_READINGS_FILE` 指定的读数文件 —— `TestEditorWindowPanel` 的静态页判据按这个**服务实际用的绝对路径**判资源在不在，不再自己按 CWD 另算一遍）；hostfxr 下 `AppContext.BaseDirectory` 实测为空字符串，早期实现因此退化成相对 CWD 的 `editor-web`，导致面板永远 503，已由 tasks 1.11 修复）。

**两层像素证据**：① 离屏捕获目标 Readback 中心精确 `(255,0,0,255)`；② 窗口**真实 swapchain backbuffer 呈现前读回**视口中心精确红：`Readback(窗口 backbuffer)` 登记"下一帧 Present 前捕获" → 跑一帧含 Present 的图 → `token->Wait()`；Vulkan 走 `TRANSFER_SRC` 能力位（不含则明确拒绝返回 null token），D3D12 走 `COPY_SOURCE` 状态转换，色序按格式（Vulkan 常为 BGRA，D3D12 常为 RGBA）。屏幕捕获（WGC/BitBlt/DXGI）在本机虚拟显示适配器环境中抓不到硬件 flip 内容（原生 C++ 正控也白），已降为 `CASCADED_EDITOR_DISPLAY_PROOF=1` 的可选诊断，不参与断言。

**读数行**：`[编辑器 读数] probe=editor tick=… state=… mode=main-thread port=… objects=1 invalidated=… windowSelf=… client=… panel=… viewport=… controller=… nav=… navHttp=… exec=… capture=… graphSelf=… captureSelf=… wtarget=… passes=2 presents=1 nativeCalls=… cmdRequest=… cmdCalls=…`；C++ 用例（`TestEditorWindowPanel`）据此断言分栏、视图、渲染与命令计数。

## 场景框架（csharp-scene-foundation：阶段 5 首切片）

`Scripting/Managed/Scene/` 七个文件，全部是 C# 层：场景数据模型、带版本 JSON 编解码、脚本生命周期钩子、
场景运行时、场景→图翻译，以及用例驱动 `SceneProbe`。**引擎 / 后端接口与绑定面零改动**（codegen 产物逐字节不变）。

### 数据模型（`Scene/Scene.cs`）

| 类型 | 内容 | 约束 |
|---|---|---|
| `Scene` | `Name` + `Entities`（`List<Entity>`） | 首切片无父子层级、无场景设置、无相机 |
| `Entity` | `Id` + `Name` + `Transform`（必有）+ `MeshRenderer`（可选） | `Id` = `Guid.NewGuid().ToString("D")`，**持久 uuid 字符串**；创建时生成、保存再加载逐字不变。**不复用编辑器运行期整数编号**（`EditorService.s_nextObjectId` 服务重启就变） |
| `Transform` | `Position` / `Rotation` / `Scale`，各一个 `float3`（生成的 `GraphicsBackend.float3`，字段 `x/y/z`） | `Rotation` 是欧拉角；**序列化保留、渲染暂不施加**（见下文限制） |
| `MeshRenderer` | `ShaderPath`（字符串）+ `Color`（`float3`）+ `Vertices`（9 个 `float`） | 顶点是**一个三角形**的局部坐标（3 顶点 × 3 分量）；顶点色不逐顶点存，整三角形用 `Color`；资源只按**名字**持久化，模型里没有任何句柄字段 |

无父子层级、无通用组件容器、无任意网格 / 相机 / 材质 / 灯光 / 纹理导入 —— 这些都是后续切片（design 的 Non-Goals）。

### 场景文件格式（`formatVersion = 1`）

`SceneCodec.Save(scene, path)` / `SceneCodec.Load(path)`，UTF-8 JSON、缩进可读：

```json
{
  "formatVersion": 1,
  "scene": {
    "name": "场景甲",
    "entities": [
      {
        "id": "50500213-3831-4c7b-8d39-ee17303fb376",
        "name": "主三角",
        "transform": {
          "position": [0.25, 0, 0],
          "rotation": [0, 0, 1.5707964],
          "scale": [2, 2, 2]
        },
        "meshRenderer": {
          "shaderPath": "Shaders/Test/TestSimpleTriangle",
          "color": [1, 0, 0],
          "vertices": [-0.25, -0.25, 0, 0.25, -0.25, 0, 0, 0.5, 0]
        }
      },
      { "id": "…", "name": "没有渲染成分", "transform": { … }, "meshRenderer": null }
    ]
  }
}
```

- **编解码是手写的**（`Utf8JsonWriter` 写 / `JsonDocument` 读，逐字段显式读写与校验）。**不用** CodeGen 给 C# 发射的 codec（只覆盖跨 ABI 的 C++ 记录、`internal`、块头是类型 / 布局哈希而非文件版本、反序列化单向）；**也不用** `JsonSerializer.Serialize(生成镜像)`（生成镜像是 public **字段**，STJ 默认不序列化字段，会得到空对象）。
- **文件里只有名字 / 字符串，没有句柄数值**：句柄是进程内 `(代际<<48)|序号` 值，不可持久化。实测往返文件里出现的字段名恰好 13 个（`formatVersion` / `scene` / `name` / `entities` / `id` / `transform` / `position` / `rotation` / `scale` / `meshRenderer` / `shaderPath` / `color` / `vertices`），多一个都算红。
- **失败通道（显式报错，不静默）**：

| 情形 | 抛什么 | 消息点名 | 实测原文（节选） |
|---|---|---|---|
| JSON 解析失败 / 文件截断 | `SceneFormatException` | 文件路径 + 「解析失败」 | `场景文件解析失败：test_output/scene_probe_truncated.json（JSON 不完整或格式错：… LineNumber: 55）` |
| `formatVersion` 缺失 | `SceneFormatException` | 缺哪个字段 | `缺少必填字段 顶层.formatVersion：…` |
| `formatVersion` 不支持（如 999） | `SceneFormatException` | 文件里是几、只支持几 | `formatVersion 不支持：文件是 999，本程序只支持 1（…）` |
| 必填字段缺失（id / transform / …） | `SceneFormatException` | `entities[N].字段` | `缺少必填字段 entities[0].id：…` |
| 字段类型错（如 position 不是三元数组） | `SceneFormatException` | 哪个字段、期望什么形状 | `字段 entities[0].transform.position 不是数组：…` |
| 读不到 / 写不进文件 | `IOException` | 完整路径 | `保存场景失败：…\不存在目录\scene_probe_io.json（DirectoryNotFoundException: …）` |

  一律**不返回半个场景、不静默补默认值**。**`Load` 只做格式 / 结构校验，不查资源存在性** —— Load 是纯文件操作，没有后端入口；着色器路径是否登记由**渲染期**校验（见下）。

### 脚本生命周期钩子（MODIFIED `csharp-script`，additive）

| 钩子 | 什么时候被调 | 默认实现 |
|---|---|---|
| `UserScript.OnEngineReady(IWindowSystem)` | 既有入口，两张表都绑好之后**整个进程只调一次**（判据逐字不变） | 建一个保持可见的窗口（既有行为） |
| `UserScript.OnStart(Scene)` | `SceneRuntime.Register(scene)` 时一次；**重复注册同一个场景对象不重复调** | 只累加 `StartCount` 并记下场景名（不做事） |
| `UserScript.OnUpdate(Scene, double dt)` | 有活动场景时**每帧**一次；`dt` 就是引擎这一帧传给 `IScriptHost::Tick` 的 `deltaSeconds`（此前只打日志、被丢弃） | 只累加 `UpdateCount` 并记下 `dt`（不做事） |

- 激活方式只有一条：脚本 / 测试显式 `SceneRuntime.Register(scene)`；想停掉就 `SceneRuntime.Unregister()`。
- **默认无活动场景 = 零行为**：不调任何钩子、不建图、不提交、不打场景日志（既有 18 条 tester 用例的 name / status 与改前基线一致，见 `docs/testing.md` 的最小集合）。
- 绑定挂在 `Host.Tick` 里、`EditorService.Tick` **之后**：`SceneProbe`（仅开关打开时）→ `SceneRuntime.Attach(后端, 窗口系统, 宿主窗口)` → `SceneRuntime.Tick(deltaSeconds)`。`Shutdown` 与窗口重绑都会先释放场景的表面与场景窗口。

### 场景→图翻译（每帧）

- 场景运行时按**宿主绑定窗口的尺寸**自己 `IWindowSystem.NewWindow(width, height, "C# 场景窗口", visible:true, …)` 建场景窗口（表面带真实句柄；宿主手上那个 `IWindow` 只有实例指针、句柄为 0，拿去做 `GetWindowHandle` 会被判失效），再建一张**同尺寸离屏捕获目标**；窗口 + 离屏各一个同逻辑 pass，最后一帧 `SubmitGraph` + `WaitIdle`。
- 顶点：内联顶点先乘 `Transform.Scale`、再加 `Transform.Position`；顶点色取该实体的 `MeshRenderer.Color`。实测第一个顶点 `(-0.25,-0.5,0)` = 局部 `(-0.25,-0.25,0)` × `Scale(2,2,2)` + `Position(0.25,0,0)`（`v0xMicros=-250000 v0yMicros=-500000 v0zMicros=0`）。
- **按 `ShaderPath` 分组**：同一份着色器的所有三角形并进**同一个 pass**（一张顶点缓冲里串多个三角形，顶点色逐顶点给）。原因：糖层的 `AddTrianglePass` 每个 pass 的附件都是 `eAttachmentLoadOp.eClear`，同一目标上第二个 pass 会把前一个 pass 的画清掉。
- **已知限制（醒目，首切片诚实登记）**：
  - **`Rotation` 序列化保留，但渲染暂不施加** —— 只施加 `Position` / `Scale`（用例里主三角的 `Rotation` 是 `(0,0,π/2)`，而顶点读数仍是未旋转值，这条被断言钉住）；
  - 一个场景里出现**两种以上 `ShaderPath`** 时，后一个分组的 pass 会清掉前一个分组的绘制（要正确合成得给糖层加「不清屏」的 additive 重载，留后续切片）；
  - 一实体一三角形；没有相机矩阵（视口固定为整张目标）；任意网格 / 材质 / 纹理导入留后续切片；
  - 场景窗口只建一次、**不关**（与脚本建的窗口同一弱持有约定：不 `CloseWindow()` 就活到进程退出；`Host.Shutdown` / 窗口重绑时才关）。
- **资源存在性在渲染期校验**：建图前用后端元数据查询 `FindRootStructData(shaderPath)`，未命中就抛 `SceneRenderException` 并点名该路径（宿主转成非零返回码 `4`），**不静默跳过该实体、不假成功**。实测：引用 `Shaders/Test/csharp_scene_missing_shader` 的场景 → `Tick` 回值 `4`，错误文本点名该路径；同一份文件**能正常加载**（`Load` 不查资源，`badResLoad=ok`）。

### 读数行与环境变量

```
[托管 读数] probe=scene-idle   tick=… active=0 onStart=0 onUpdate=0 renderFrames=0 …     ← 注册之前的零行为
[托管 读数] probe=scene-format rtChecked=54 rtMismatch=0 file1Bytes=… diffBytes=… reloadDeltaMicros=500000
                               trunc=… version=… missingId=… badType=… missingVersion=… saveIo=IOException
                               badResLoad=ok badResEntities=1 keysTotal=13 keysUnknown=0
[托管 读数] probe=scene-state  tick=… active=… onStart=… onUpdate=… lastDtMicros=… startScene=… renderFrames=…
                               entities=… rendered=… vertices=… shaderGroups=… firstEntityId=…
                               graph=… graphSelf=… capture=… captureSelf=… size=WxH pass=… present=…
                               v0xMicros=… v0yMicros=… v0zMicros=…
```

| 变量 | 作用 | 默认 |
|---|---|---|
| `CASCADED_CSHARP_SCENE_PROBE=1` | 启用场景用例驱动（只有 `TestCSharpScene` 在自己的 Tick 之前设置、之后清理；生产脚本不设，注册场景走 `SceneRuntime.Register`） | 不设 |
| `CASCADED_CSHARP_SCENE_SELFTEST` | **仅 Debug**：`skip-onupdate`（Tick 故意不调 `OnUpdate` → 用例必须 `E_SCENE_DT` 判红）/ `bad-color`（顶点色换蓝 → 中心不再是纯红 → `E_SCENE_PIXEL` 判红）；写错模式名时 `Tick` 明确报错，不静默空转 | 空串 |

## 构建

```powershell
python build.py --config Debug
```

托管侧由 `build.py` 的托管段编译，产物（`Scripting.Managed.dll`、`Scripting.Managed.runtimeconfig.json`、`Scripting.Managed.deps.json`）拷到 `out/build/x64-debug/bin/`，与 `CSharpHost.dll` 同目录 —— `hostfxr` 按路径找程序集，同目录最省事。

- **缺 .NET SDK 时构建失败**（不静默跳过）：报 `托管侧构建失败：缺少 .NET SDK：PATH 里找不到 dotnet 命令…`。
- 要走"明知没有托管侧"的路，加 `--without-managed`：构建成功，并打印 `已跳过托管侧，C# 不可用（--without-managed）：产物目录里不会有 Scripting.Managed.dll`。

`nethost` 由 CMake 在 `%ProgramFiles%\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\*\runtimes\win-x64\native` 里**取最新的一份**（不写死版本）。找不到时引擎照样能编出来，运行期会报错并列出找过的路径。

## 反向接口：C# 实现引擎接口（`ResourceFactoryBase` / `ResourceBase`）

绑定面第一次出现"实现方在托管侧"的接口（csharp-reverse-interface-binding）：`resource_management::IResourceFactory`
与 `resource_management::IResource` 用 `CA_BIND_REVERSE()` 标注，生成器产出**公开抽象基类**
`ResourceFactoryBase` / `ResourceBase`（在 `Scripting.Generated.Bindings.ResourceManagement` 命名空间里，
与接口同镜像命名空间），C# 继承它们写自己的工厂与资源。原生细节（函数指针槽、`GCHandle`、句柄）全部在
`internal` 的 `IResourceFactoryBindings` / `IResourceBindings` 里，公开签名只有托管类型。

**C# 工厂（两种语言对称）**：

```csharp
using Scripting.Generated.Bindings.ResourceManagement;

internal sealed class MyResource : ResourceBase
{
    public override byte[] Serialize() => new byte[] { 0xDE, 0xAD, 0xBE, 0xEF };
}

internal sealed class MyFactory : ResourceFactoryBase
{
    public override string GetResourceType() => "MyResource";
    public override ResourceBase Create() => new MyResource();
}

// 注册（C++ 侧）：句柄 + 借用指针 + 释放回调，三样一起交给资源系统的工厂注册表
//   RegisterFactory("MyResource", handle,
//                   static_cast<IResourceFactory*>(factoryTable->ResolveImpl(handle)),
//                   factoryTable->ReleaseImpl);
ulong handle = IResourceFactoryBindings.Create(new MyFactory());
```

C++ 原生工厂**原生继承** `resource_management::IResourceFactory`（`GetResourceType()` / `Create()`），
走同一张工厂注册表；`Create()` 不收 `ResourceManagingSystem` 参数 —— 工厂只构造对象，注册由系统负责。
两种语言工厂的对照判据写死期望表（类型名 / 字节 / 失败都抛异常），不用"行为一致"这种散文。

**所有权与寿命（单一强所有者，C# 句柄是借用令牌）**：反向工厂用 `IResourceFactoryBindings.Create` 拿到
**强句柄**（kind 1 —— 句柄表持代理的 `shared_ptr`，这就是它唯一的强所有者）；反向资源用
`IResourceBindings.CreateProxy` 拿到**借用句柄**（kind 4 —— 句柄表不持强引用，资源注册表里那份
`shared_ptr` 才是唯一强所有者，代理析构时自己把借用项退役）。两种句柄上的 `Dispose`（内部调
`ReleaseImpl`）都只释放"调用方这一份"：**之后同一句柄报 1001，但注册表仍持有对象**（`GetResource(路径)`
仍是同一个实例、`ReverseKeepAliveCount` 仍是 1）。注册表释放/替换时：反向工厂调 `release(handle)`
（`__ReleaseImpl` → 掉强引用 → 代理析构 → `releaseUser` → 托管桥 `GCHandle.Free`，**唯一一次 Free 在
托管侧**）→ 保活计数归零；反向资源由注册表那份 `shared_ptr` 析构触发同一条链。重复 `Dispose` 幂等返回
1001，不崩。C# 不要自己持有原生指针或试图释放它。

**跨模块注意**：句柄表是生成在头里的 `inline` 函数 + 函数级 `static`，EXE 与 DLL 各一份 —— 句柄只能在
**创建它的模块**（这里是 `CSharpHost.dll`）里解析。C++ 调用方 MUST 用反向函数表的 `ResolveImpl(handle)`
拿借用指针、用 `ReleaseImpl(handle)` 释放，MUST NOT 调自己模块里那份 `<线形>__*` 函数（会查错表）。

**失败契约**：C# 覆写抛的异常在托管桥里被捕获（不穿过 C ABI），长度槽返回 `UINT64_MAX` 哨兵；代理中止
（写路径**不写、不提交**）并把错误落槽，**下一次注册线程调用**抛
`cascaded_reverse::ReverseBindingError`（`Code()` = 1012，消息含反向标识与原始异常文本）。非注册线程调用
被代理拒绝（1005，C# 覆写不会执行）。工厂 `Create` 失败按**异常**：C# 抛异常或返回 `null` 都让 C++
侧的 `CreateResource` 抛 `resource_management::resource_creation_error`，系统不注册空资源
（`HasResource(路径)` 为 false，下一次调用会重新调 `Create`）。

**`Deserialize` 不支持（本批）**：`IResource::Deserialize` 用
`CA_BIND_SKIP(Deserialize, "本批不支持反向读，另立批")` 排除，`ResourceBase` 里**只有 `Serialize` 一个
抽象方法**。代理的 `Deserialize` 存在但抛 `ReverseBindingError(1013)`。读回路径仍走 `TResource`。

### 导入器也是反向接口（拉取式，`resource-importer-refactor`）

`resource_management::ResourceImporter` 与 `resource_management::ResourceImporterProductions` 同样用
`CA_BIND_REVERSE()` 标注，C# 侧继承 `ResourceImporterBase` / `ResourceImporterProductionsBase` 写自己的
目录级导入器。**产出是拉取式的**：原生管线调 `Produce()` 拿到迭代器，再逐条
`HasNext()` → `NextPath()` + `NextResource()`；导入器**只产对象、不注册**，注册 / 替换 / 落盘全由
`ResourceImportingSystem::ScanSourceDirectory` 做（先 `ReleaseResource(路径)` 再 `RegisterResource`，
所以重扫会换掉旧对象）。

```csharp
using Scripting.Generated.Bindings.ResourceManagement;

internal sealed class MyRes : ResourceBase
{
    public override byte[] Serialize() => new byte[] { 0xC5, 0x01 };
}

internal sealed class MyProductions : ResourceImporterProductionsBase
{
    private readonly string[] m_Paths = { "My/A.res", "My/B.res" };
    private int m_Index;
    public override bool HasNext() => m_Index < m_Paths.Length;
    public override string NextPath() => m_Paths[m_Index];
    public override ResourceBase NextResource()
    {
        ResourceBase resource = new MyRes();   // 一条产出换一个对象
        ++m_Index;                             // 取走一条即推进
        return resource;
    }
}

internal sealed class MyImporter : ResourceImporterBase
{
    public override string GetTags() => "MyImporter";
    // C# 反向实现读不到管线传入的扫描根，目录 MUST 自己声明（空 = 跟随，但托管侧看不到那个值）。
    public override string GetSourceDirectory() => "C:/assets/src";
    public override string GetDestDirectory() => string.Empty;
    public override ResourceImporterProductionsBase Produce() => new MyProductions();
}

// 注册（C++ 侧）：强句柄 + ResolveImpl 借用指针 → ResourceImportingSystem::AddImporter
ulong handle = ResourceImporterBindings.Create(new MyImporter());
```

**所有权**：`Produce()` 返回的迭代器由管线持有的 `shared_ptr` 管（C# 侧用 `CreateProxy` 造原生代理），
每次 `NextResource()` 交出的资源同样由管线包 `shared_ptr(ptr, delete)` 后注册 —— 与
`CreateResource` 同构，注册表是唯一强所有者；`ReleaseResource` 之后代理析构 → `releaseUser` →
`GCHandle.Free`，`ReverseKeepAliveCount` 从 1 归 0，旧句柄重复释放报 1001。**MUST NOT 用
`ResolveImpl` 处理新对象**（新对象还没有代理/借用句柄，必须 `CreateProxy`）。

## 环境变量

| 变量 | 作用 | 默认 |
|---|---|---|
| `CASCADED_CSHARP_RUNTIMECONFIG` | 指定托管侧的 `runtimeconfig.json` 路径 | `<bin>\Scripting.Managed.runtimeconfig.json` |
| `CASCADED_CSHARP_SELFTEST=abi-mismatch` | **仅 Debug**：引擎侧故意把 `abi_version` 加一，用来构造"ABI 不符"失败路径 | 不设 |
| `CASCADED_CSHARP_SELFTEST=throw` | **仅 Debug**：托管侧第一次 `Tick` 抛一个异常，用来构造"托管抛异常"失败路径 | 不设 |
| `CASCADED_CSHARP_SELFTEST=layout-mismatch` | **仅 Debug**：引擎侧把布局清单第一条字段的偏移加 8，模拟"引擎与托管清单不同源"；`Host.Boot` 必须拒绝启动并点名字段 | 不设 |
| `CASCADED_CSHARP_SELFTEST=monitor-mismatch` | **仅 Debug**：托管侧打印 `GetMonitor` 读数时把 `m_MonitorRect.width` 加 1，用来证明 `Tools/check_window_system_bindings.py` 会报字段错配 | 不设 |
| `CASCADED_CSHARP_SELFTEST=monitor-skip-invalid` | **仅 Debug**：托管侧跳过两次无效显示器编号调用，用来证明脚本会因"放过无效编号"变红 | 不设 |
| `CASCADED_CSHARP_CALLBACK_PROBE=1` | 启用 10 类窗口回调探针（只有 `TestCSharpWindowCallbacks` 设置） | 不设 |
| `CASCADED_CSHARP_CALLBACK_PHASE` | 回调探针阶段机（`verify-all` / `verify-replace` / `verify-exception` / `verify-thread` / `verify-rebind` / `verify-rebind-done`），由测试在两次 `Tick` 之间设置 | 空串 |
| `CASCADED_CSHARP_RENDER_PROBE=1` | 启用脚本渲染探针；只有 `TestCSharpTriangle` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_RENDER_SELFTEST` | **仅 Debug**：`skip-draw` / `bad-vertex` / `skip-present` / `skip-wait` 四个负向模式；C++ 侧 `TestCSharpTriangle` 读同一份值来核对错误码 | 空串 |
| `CASCADED_CSHARP_CONSTCOLOR_PROBE=1` | 启用常量色（着色器参数）探针；只有 `TestCSharpConstantColor` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_CONSTCOLOR_SELFTEST` | **仅 Debug**：`bad-struct-colors` / `skip-param` 两个负向模式；C++ 侧 `TestCSharpConstantColor` 读同一份值来核对错误码 | 空串 |
| `CASCADED_CSHARP_SB_PROBE=1` | 启用结构化缓冲探针；只有 `TestCSharpStructuredBuffer` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_SB_SELFTEST` | **仅 Debug**：`bad-buffer-color` / `unbound-field` / `bad-buffer-name` 三个负向模式；C++ 侧 `TestCSharpStructuredBuffer` 读同一份值来核对错误码 | 空串 |
| `CASCADED_CSHARP_BUFFER_FALLBACK=1` | **仅 Debug 演练**：7.1 的"回退到图内 `AllocBuffer`"红样开关；设置后结构化缓冲探针走按名分配、第二张图提交必败（`E_SB_CREATEBUFFER`）。生产脚本不设 | 不设 |
| `CASCADED_CSHARP_TEX_PROBE=1` | 启用纹理采样探针；只有 `TestCSharpTextureSampling` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_TEX_SELFTEST` | **仅 Debug**：`bad-texture-color` / `unbound-image` / `unbound-sampler` 三个负向模式；C++ 侧 `TestCSharpTextureSampling` 读同一份值来核对错误码 | 空串 |
| `CASCADED_CSHARP_SCENE_PROBE=1` | 启用场景用例驱动（场景注册 / 切场景 / 读数行）；只有 `TestCSharpScene` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_SCENE_SELFTEST` | **仅 Debug**：`skip-onupdate` / `bad-color` 两个负向模式（场景用例的红态构造）；模式名写错时 `Tick` 明确报错 | 空串 |
| `CASCADED_CSHARP_RESOURCE_PROBE=1` | 启用反向接口探针（C# 实现 `ResourceFactoryBase` / `ResourceBase`）；只有 `TestCSharpResourceFactory` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_RESOURCE_PHASE` | 反向接口探针阶段机（`create-factory` / `create-failing-factories` / `after-create` / `make-serialize-throw` / `after-throw` / `dispose-resource` / `dispose-factory` / `report-counters` / `report-resource-keepalive`），由测试在两次 `Tick` 之间设置 | 空串 |
| `CASCADED_CSHARP_RESOURCE_READINGS` | 反向接口探针把读数（句柄 / 错误码 / 计数）追加到这个文件，测试读同一份；不设时探针不写任何东西 | 不设 |
| `CASCADED_CSHARP_IMPORTER_PROBE=1` | 启用**导入器**反向探针（C# 实现 `ResourceImporterBase` / `ResourceImporterProductionsBase`，拉取式产两条资源）；只有 `TestCSharpImporter` 在自己的 Tick 之前设置、之后清理 | 不设 |
| `CASCADED_CSHARP_IMPORTER_PHASE` | 导入器探针阶段机（`create-importer` / `after-first-scan` / `after-second-scan` / `report-serialize`），由测试在两次 `Tick` 之间设置 | 空串 |
| `CASCADED_CSHARP_IMPORTER_READINGS` | 导入器探针把读数（句柄 / 保活计数 / Produce 次数 / 序列化计数）追加到这个文件，测试读同一份 | 不设 |
| `CASCADED_CSHARP_IMPORTER_SOURCE` | 导入器探针把 `GetSourceDirectory()` 声明成这个目录（C# 反向实现读不到管线传入的扫描根，必须自己声明） | 空串 |
| `CASCADED_EDITOR` | `1` 时启用编辑器面板服务（单窗口分栏 + WebView2 + WebSocket 命令服务）；不设时零行为（不监听、不建线程） | 不设 |
| `CASCADED_EDITOR_PORT` | 编辑器服务监听端口（只绑 localhost） | `18390`（`1`-`65535`） |
| `CASCADED_EDITOR_WEBVIEW2_DATA` | WebView2 用户数据目录 | `%TEMP%\cascaded-editor-webview2` |
| `CASCADED_EDITOR_PANEL_WIDTH` | 右侧面板宽（像素） | `320`（`1`..`客户区宽-1`） |
| `CASCADED_EDITOR_SELFTEST=no-runtime` | **仅 Debug**：模拟 WebView2 运行时缺失，验证"明确禁用、不崩"路径 | 不设 |
| `CASCADED_EDITOR_STOP=1` | 主动停服务（测试收尾隔离后续用例；`EditorService.Stop` 的入口） | 不设 |
| `CASCADED_EDITOR_READINGS_FILE` | 把 `[编辑器 读数]` 与服务标记行同时追加到该文件（测试读，不依赖 stdout 重定向） | 不设 |
| `CASCADED_EDITOR_DISPLAY_PROOF=1` | 可选屏幕捕获诊断（WGC/BitBlt/DXGI，只打印不断言；本机虚拟显示抓不到硬件 flip 内容） | 不设 |
| `CASCADED_READBACK_FORCE_NO_BB_TRANSFER` | **仅 Debug**：强制窗口 backbuffer 回读走"能力不支持"拒绝分支（红态构造） | 不设 |

## 失败语义（五条，全部实测过）

初始化失败时 `CSharpHost` 打印一行错误并**让进程以退出码 3 退出** —— 引擎里没有错误回传通道，带着"没有 C# 支持"继续跑正是规范里 MUST NOT 的静默降级。

| 情形 | 实测输出（节选） |
|---|---|
| 托管程序集缺失 | `CSharpHost 初始化失败（退出码 3）：解析托管入口 Boot 失败，返回 -2146233079（程序集 …\bin\Scripting.Managed.dll）；找过的路径：…\hostfxr.dll、…\Scripting.Managed.runtimeconfig.json` |
| 配置路径不存在 | `CSharpHost 初始化失败（退出码 3）：找不到托管侧的 runtimeconfig.json：…\bad.runtimeconfig.json；找过的路径：C:\Program Files\dotnet\host\fxr\10.0.2\hostfxr.dll、…\bad.runtimeconfig.json` |
| 配置要的运行时版本本机没有 | `hostfxr_initialize_for_runtime_config 返回 -2147450730（…；配置里要的框架版本：7.0.0；本机已装的运行时版本：10.0.2、8.0.23、8.0.6、9.0.12）` |
| 函数表 ABI 版本不符 | `CSharpHost 自检开关已生效：故意把 abi_version 改成 13（真实值 12）` + `[托管侧] ABI 版本不符：引擎 13，托管侧期望 12` + `CSharpHost 初始化失败（退出码 3）：托管侧 Boot 返回错误码 1（引擎侧 ABI 版本 12、函数表 56 字节；0 = 成功）；找过的路径：…` |
| 托管方法抛异常 | `[托管侧] Tick 抛异常：InvalidOperationException: …` + 引擎侧收到错误码 **4**；进程**不崩**。当前测试操作算失败，`GPUBackendTester` 按既有约定返回 **1**（不是无条件 0）；`Tools/check_csharp_host_failures.py` 按此断言 |

## 关闭

`hostfxr_close` 在 `CSharpHost` 实例析构时调用（早于模块卸载），并打印 `[CSharpHost] 运行时已关闭（hostfxr_close 已调用）`。**不 `FreeLibrary(hostfxr)`**：卸载它会让仍在运行的托管代码崩（实测 `0xC0000005`，两次）；`hostfxr` 的生命周期就是进程的生命周期，退出时由系统回收。模块的 `TryRelease` 导出会再调一次关闭，是幂等空转。

## 可直接复制运行的检查

跑一次测试宿主（`--headless 2` 表示跑 2 帧后退出），看托管侧启动与引擎侧读数：

```powershell
& 'out\build\x64-debug\bin\GPUBackendTester.exe' --backend vulkan --headless 2 2> $env:TEMP\csharp.err *> $env:TEMP\csharp.out
Select-String -LiteralPath $env:TEMP\csharp.out -Pattern '托管侧|IScriptHost|CSharpHost|Tick #'
```

实测输出（节选）：

```
[托管侧] 托管侧已启动：ABI 版本 12、函数表 56 字节
[托管侧] 回调引擎 Echo(42) 得到 43
CSharpHost：托管运行时已就绪（加载耗时 = 43 ms；函数表 56 字节，ABI v12）
IScriptHost 已取到：IsAvailable = true
```

跑一次场景用例，看场景读数行与像素证据（两个后端各跑一次；`test_output\TestCSharpScene_<后端>_fresh.png` /
`_loaded.png` 是离屏捕获的 PNG，可以直接打开对照）：

```powershell
& 'out\build\x64-debug\bin\GPUBackendTester.exe' --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_vulkan.json
Select-String -LiteralPath 'test_output\TestCSharpScene.log' -Pattern '\[托管 读数\]|\[场景 断言\]|TestCSharpScene'
```

实测输出（vulkan，节选；逐字见 `openspec/changes/csharp-scene-foundation/evidence/3.2-读数-vulkan.log`）：

```
[托管 读数] probe=scene-idle tick=1 phase=0 active=0 onStart=0 onUpdate=0 lastDtMicros=0 … renderFrames=0 entities=0 …
[场景 断言] 截断文件 → SceneFormatException：场景文件解析失败：test_output/scene_probe_truncated.json（JSON 不完整或格式错：…）
[场景 断言] 版本999 → SceneFormatException：formatVersion 不支持：文件是 999，本程序只支持 1（…）
[场景 断言] 缺id → SceneFormatException：缺少必填字段 entities[0].id：…
[场景 断言] 保存到不存在的目录 → IOException：保存场景失败：test_output/不存在目录/scene_probe_io.json（DirectoryNotFoundException: …）
[托管 读数] probe=scene-format rtChecked=54 rtMismatch=0 file1Bytes=1734 file2Bytes=1734 diffBytes=1
             reloadDeltaMicros=500000 trunc=SceneFormatException version=SceneFormatException
             missingId=SceneFormatException badType=SceneFormatException missingVersion=SceneFormatException
             saveIo=IOException badResLoad=ok badResEntities=1 keysTotal=13 keysUnknown=0
[场景 断言] 重复注册同一场景的 OnStart 增量：0（期望 0）
[托管 读数] probe=scene-state tick=1 phase=1 active=1 onStart=1 onUpdate=1 lastDtMicros=125000 startScene=场景甲
             updateScene=场景甲 repeatDelta=0 renderFrames=1 entities=3 rendered=2 vertices=6 shaderGroups=1
             firstEntityId=50500213-3831-4c7b-8d39-ee17303fb376 graph=4 graphSelf=… capture=3 captureSelf=…
             size=320x240 pass=2 present=1 v0xMicros=-250000 v0yMicros=-500000 v0zMicros=0
TestCSharpScene 像素：tag=fresh size=320x240 bg=(0,0,255,255) center=(255,0,0,255) png=test_output/TestCSharpScene_vulkan_fresh.png
[托管 读数] probe=scene-state tick=2 phase=2 active=1 onStart=2 onUpdate=2 lastDtMicros=250000 …（加载回来的场景）
TestCSharpScene 像素：tag=loaded size=320x240 bg=(0,0,255,255) center=(255,0,0,255) png=…_loaded.png
TestCSharpScene：坏资源名在渲染期被明确拒绝（Tick 回值 4，错误文本点名 Shaders/Test/csharp_scene_missing_shader）
[托管 读数] probe=scene-state tick=4 phase=4 active=0 onStart=3 onUpdate=3 … renderFrames=2 entities=0 … pass=0 present=0
TestCSharpScene 通过：vulkan 后端序列化往返 / 失败通道 / 生命周期 / 场景渲染像素 / 坏资源名报错 / 注销零行为 全部满足
```

红态（Debug 自检开关，跑完不设即恢复）：

```powershell
$env:CASCADED_CSHARP_SCENE_SELFTEST='skip-onupdate'   # 期望 status=fail，error 含 E_SCENE_DT
& 'out\build\x64-debug\bin\GPUBackendTester.exe' --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_red_skip_onupdate.json
$env:CASCADED_CSHARP_SCENE_SELFTEST='bad-color'       # 期望 status=fail，error 含 E_SCENE_PIXEL
& 'out\build\x64-debug\bin\GPUBackendTester.exe' --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_red_bad_color.json
Remove-Item Env:\CASCADED_CSHARP_SCENE_SELFTEST
```

**已知限制（醒目）**：

- 托管侧只认每个接口的**默认实例**（`get_interface_api` 的实例名传 `nullptr`）；按名寻址目前只有引擎与测试在用 —— 同一个接口的多份实现要能被脚本分别拿到，还得等托管表面的那一轮（`csharp-window-creation` 任务 5.6）。**例外**：编号 4 / 5 / 6 / 7 是目标类型表，本来就没有默认实例，取表时 `instanceOut` 允许为空。
- 阻塞调用（`WaitIdle`、回读的 `Wait`）会卡住引擎主线程：渲染探针的 `WaitIdle` 就是这样同步提交的；窗口路径不碰这些。
- 托管侧只在主线程被调用；线程拓扑、多脚本、热重载都不在本 change 的范围。
- `nethost` 找不到时构建不失败、运行期报错 —— 这是有意的（让缺 .NET 的机器也能编引擎），但意味着"构建成功"不等于"C# 可用"。
