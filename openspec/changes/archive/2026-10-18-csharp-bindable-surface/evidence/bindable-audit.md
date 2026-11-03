# 可绑定面审计表

**口径**：按**类体**数每个接口的纯虚方法（定位 `class X` 到它自己配平的 `}`，**先剥掉注释再解析**；纯虚析构 `virtual ~X() = 0;` 不算方法）。两条实测过的坑，脚本与这张表都按修好的口径来：

- 逐文件 `git grep -c 'virtual .*= *0;'` 会把同文件里的其它类算进来（`CRenderBackend.h` 里 13 条中 `CRenderBackend` 本类只有 10 条、另 3 条属 `IReadbackToken`；`WindowSystem.h` 里 39 条分属 `IWindow`（23）与 `IWindowSystem`（16））。
- **注释里出现的示例签名会被当成真声明**（`CRenderBackend.h` 的注释里写着 `query`、`IResource.h` 的注释里引用了 `Serialize` 的原文）；不剥注释就会凭空多出方法名。

**范围**：9 个模块级接口 + `IReadbackToken`（出现在 `Readback` 的返回值里），合计 **75** 条纯虚方法：

| 接口 | 方法条数 | 所在头文件 |
|---|---|---|
| `CRenderBackend` | 10 | `Interface/RenderInterface/header/CRenderBackend.h` |
| `IReadbackToken` | 3 | `Interface/RenderInterface/header/CRenderBackend.h` |
| `IWindow` | 23 | `WindowSystem/header/CAWindow/WindowSystem.h` |
| `IWindowSystem` | 16 | `WindowSystem/header/CAWindow/WindowSystem.h` |
| `IShaderCompilerManager` | 3 | `Interface/ShaderCompiler/header/Compiler.h` |
| `IOManager` | 3 | `Interface/IOManager/header/IOManager/IOManager.h` |
| `IResource` | 2 | `GeneralResources/header/CAResource/IResource.h` |
| `IScriptRenderTarget` | 3 | `Interface/RenderInterface/header/ScriptRenderBridge.h` |
| `IScriptRenderGraph` | 6 | `Interface/RenderInterface/header/ScriptRenderBridge.h` |
| `IScriptRenderBackend` | 6 | `Interface/RenderInterface/header/ScriptRenderBridge.h` |
| **合计** | **75** | |

**列定义**：`拷贝` = 该调用是否需要一次额外的内存拷贝（视图与按值不算）；`谁分配` = 过界内存的分配方（调用方给缓冲区 / 投影层分配 + 配对释放 / 引擎持有 + 只读视图 / `—` 表示不过界内存）；`可绑性` = `可绑` / `需快路` / `不可绑`；`不可绑` 必须在"理由或建议"列给出理由与建议。

| 接口::方法 | C++ 形状 | 投影形状 | 拷贝 | 谁分配 | 可绑性 | 理由或建议 | 失败上报方式 |
|---|---|---|---|---|---|---|---|
| `CRenderBackend::ExecuteGraph` | 裸指针 `TaskScheduler*` + `castl::shared_ptr<GPUGraph> const&` | 拆成 `BuildGraph(builder句柄) → uint64 graph` 与 `SubmitGraph(uint64 graph)`；scheduler 不进签名 | 一次（构件走序列化块） | 投影层句柄 | **不可绑** | ② 所有权不明裸指针类：裸 `TaskScheduler*` 的线程归属不能让脚本决定，`shared_ptr` 参数需句柄化。建议拆成"建图（返回 graph 句柄）+ 提交（引擎决定 scheduler）" | 返回 0 + 错误码 |
| `CRenderBackend::Release` | 无参 | `—（不暴露）` | — | — | **不可绑** | ④ 语义越界类：后端销毁由引擎模块生命周期（`TryRelease`）决定，脚本不该也不能销毁后端。建议不暴露，脚本只能释放它自己创建的句柄 | 不暴露（无入口） |
| `CRenderBackend::CreateGPUBuffer` | `GPUBufferDescriptor const&`（实测定宽：`uint64_t count` / `uint64_t stride`，无容器）+ `EBufferUsageFlags` | `uint64 crb_create_buffer(uint64 self, GPUBufferDescriptorC const*, uint32 flags)` + `crb_release_buffer(uint64)` | 无 | 投影层句柄 | 可绑 | 描述符是 blittable ⇒ 按指针传镜像 + 布局自检；返回 `shared_ptr` ⇒ 句柄 + 配对释放 | 返回 0 + 错误码 |
| `CRenderBackend::CreateGPUTexture` | `GPUTextureDescriptor const&`（实测：两个枚举 + 四个 `uint32_t`，无容器）+ `ETextureAccessTypeFlags` | `uint64 crb_create_texture(uint64 self, GPUTextureDescriptorC const*, uint32 access)` + `crb_release_texture(uint64)` | 无 | 投影层句柄 | 可绑 | 同上 | 返回 0 + 错误码 |
| `CRenderBackend::CreateShaderStruct` | `cacore::NameHash const&`（native 类型，内部含字符串）+ 返回 `castl::shared_ptr<ShaderStruct>`（多态类） | `uint64 crb_create_shader_struct(uint64 self, char const* name, uint64 len)` + 配对释放 | 一次（字符串） | 投影层句柄 | 可绑 | `NameHash` 在 C ABI 面就是 UTF-8 字符串；返回值是智能指针 ⇒ 句柄 | 返回 0 + 错误码 |
| `CRenderBackend::GetWindowHandle` | 参数 `castl::shared_ptr<cawindow::IWindow>`（另一模块的接口实例） | `—（不暴露）` | — | — | **不可绑** | ② 所有权不明裸指针类：脚本侧没有"窗口"句柄可传（窗口归 `IWindowSystem`/`IWindow` 管）。建议改成接收引擎登记的窗口 id | 不暴露（无入口） |
| `CRenderBackend::AnyWindowRunning` | 返回 `bool` | `int32` | 无 | — | 可绑 | 布尔在 C ABI 面用 `int32` 表示 | 不失败 |
| `CRenderBackend::WaitIdle` | 无参 | `void crb_wait_idle(uint64 self)` | 无 | — | 可绑 | 阻塞语义：文档必须写明"会卡住调用线程" | 不失败 |
| `CRenderBackend::Readback` | 参数 `ImageHandle const&`（实测：含 `m_Key` / `m_ExternalManagedTexture` 指针 / `m_Type` 的类，无虚函数）+ `std::span<uint8_t>`；返回 `std::unique_ptr<IReadbackToken>` | `uint64 crb_readback_image(uint64 self, ImageHandleC const*, uint8_t* dst, uint64 len)` → token 句柄；`crb_release_token(uint64)` | 无 | 调用方给 dst；token 由投影层给句柄 | 可绑 | `span` 是最理想形状；`ImageHandle` 按 blittable 镜像传 + 布局自检；`unique_ptr` 按独占智能指针规则 ⇒ 句柄，释放即作废 | 返回 0 + 错误码 |
| `CRenderBackend::Readback` | 同上，参数换成 `BufferHandle const&`（同类结构） | `uint64 crb_readback_buffer(uint64 self, BufferHandleC const*, uint8_t* dst, uint64 len)` → token 句柄 | 无 | 调用方给 dst；token 由投影层给句柄 | 可绑 | 同上（`Readback` 的重载，两条各占一行） | 返回 0 + 错误码 |
| `IReadbackToken::Wait` | 无参，阻塞直到 GPU 写完 | `void crb_token_wait(uint64)` | 无 | — | 可绑 | 阻塞语义：文档必须写明"会卡住调用线程" | 错误码（超时 / 令牌失效） |
| `IReadbackToken::IsReady` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IReadbackToken::Reset` | 无参（幂等释放） | `void crb_token_reset(uint64)` | 无 | — | 可绑 | 幂等语义写进契约（重复调不报错） | 不失败 |
| `IWindow::CloseWindow` | 无参 | `void` | 无 | — | 可绑 | 无形状问题 | 不失败 |
| `IWindow::WindowShouldClose` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::ShowWindow` | 无参 | `void` | 无 | — | 可绑 | 无形状问题 | 不失败 |
| `IWindow::SetWindowPos` | 两个 `int` | `int32, int32` | 无 | — | 可绑 | `int` 必须钉成 `int32_t` | 不失败 |
| `IWindow::GetWindowPos` | `const`，两个 `int&` 出参 | `int32* outX, int32* outY` | 无 | 调用方 | 可绑 | 出参引用 ⇒ 指针 | 不失败 |
| `IWindow::SetWindowSize` | 两个 `int` | `int32, int32` | 无 | — | 可绑 | 同 `SetWindowPos` | 不失败 |
| `IWindow::GetWindowSize` | `const`，两个 `int&` 出参 | `int32* outX, int32* outY` | 无 | 调用方 | 可绑 | 同 `GetWindowPos` | 不失败 |
| `IWindow::Focus` | 无参 | `void` | 无 | — | 可绑 | 无形状问题 | 不失败 |
| `IWindow::GetWindowFocus` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::GetWindowMinimized` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::SetWindowName` | `castl::string const&` | `char const* + uint64 len`（UTF-8，不要求 NUL 结尾） | 一次 | 调用方 | 可绑 | 字符串走 UTF-8 + 长度 | 错误码（空指针 / 长度不合理） |
| `IWindow::GetWindowName` | `const`，返回 `castl::string_view`（指向实现内部） | 两段式：`uint64 get_name_len()` + `copy_name(uint8_t* dst, uint64 cap)` | 一次 | 调用方给缓冲区 | 可绑 | `string_view` 的生命周期不受调用方控制 ⇒ **不许**把裸指针直接交给 C#；用两段式拷出 | 错误码 |
| `IWindow::SetWindowAlpha` | `float` | `float` | 无 | — | 可绑 | 浮点按值 | 不失败 |
| `IWindow::GetDpiScale` | `const`，返回 `float` | `float` | 无 | — | 可绑 | 浮点按值 | 不失败 |
| `IWindow::GetNativeWindowHandle` | 返回 `void*` | `uint64`（不透明令牌） | 无 | — | 可绑 | 语义是"借出的窗口句柄"，C# 只能当不透明数、不得解引用 | 不失败 |
| `IWindow::GetWindowSystem` | 返回 `IWindowSystem*`（裸指针，自引用/工厂语义不明） | `uint64`（**借用句柄**） | 无 | — | 可绑 | 2026-10-25 改判：返回的是**已 `CA_BIND` 接口**的裸指针，所有权在模块、本质是借用 ⇒ 投成借用句柄（kind 4），释放只清槽不销毁。见 `csharp-window-creation` 任务 5.3 / design D9 | 解出的指针不可用时报 1001 |
| `IWindow::GetKeyState` | `int` + `int` → `bool` | `int32, int32` → `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::GetMouseX` | `const`，返回 `float` | `float` | 无 | — | 可绑 | 浮点按值 | 不失败 |
| `IWindow::GetMouseY` | `const`，返回 `float` | `float` | 无 | — | 可绑 | 浮点按值 | 不失败 |
| `IWindow::IsKeyDown` | `int` → `bool` | `int32` → `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::IsKeyTriggered` | `int` → `bool` | `int32` → `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::IsMouseDown` | `int` → `bool` | `int32` → `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindow::IsMouseUp` | `int` → `bool` | `int32` → `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IWindowSystem::NewWindow` | `castl::weak_ptr<IWindow>` 返回 | `uint64`（**弱句柄**：句柄表存弱引用，不抬高 `use_count`；对象析构后旧句柄按 1001 判失效） | 无 | 投影层持弱引用 | 可绑 | 原判 ② 有误：② 的定义是所有权不明的**裸指针**，`weak_ptr` 不是裸指针；弱引用语义可以由代际号 + `lock()` 表达（`csharp-window-creation` 1.4 已实现并在夹具上验过） | 错误码（1002 空指针 / 1001 句柄失效） |
| `IWindowSystem::GetWindowCount` | `const`，返回 `int` | `int32` | 无 | — | 可绑 | `int` 钉成 `int32_t` | 不失败 |
| `IWindowSystem::GetMonitorCount` | `const`，返回 `int` | `int32` | 无 | — | 可绑 | 同上 | 不失败 |
| `IWindowSystem::GetMonitor` | `const`，`int` → `MonitorInfo`（实测：两个 `FloatRect` + 一个 `float`，递归纯标量记录） | `int32` → `MonitorInfoC`（递归镜像 + 嵌套字段路径的布局自检 + 按值返回） | 无 | — | 可绑 | 两个矩形与 DPI 逐字段生成（`FloatRect` / `MonitorInfo` 公开记录）；绑定层在调用原生实现**之前**用 `GetMonitorCount` 校验编号，负数/越界走错误码 1006，不许进原生 `clamp` | 错误码（无效 id，1006） |
| `IWindowSystem::UpdateSystem` | 无参 | `void` | 无 | — | 可绑 | 每帧调一次的事件泵，由引擎调；脚本调也不破坏语义（幂等） | 不失败 |
| `IWindowSystem::SetWindowFocusCallback` | `castl::function<void(IWindow*, bool)>` 回调 | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow` | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 异常 / 1004 重入 / 1005 跨线程） |
| `IWindowSystem::SetCursorEnterCallback` | `castl::function<void(IWindow*, bool)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，`bool` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetCursorPosCallback` | `castl::function<void(IWindow*, float, float)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，两个 `float` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetMouseButtonCallback` | `castl::function<void(IWindow*, int, int, int)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，三个 `int32` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetScrollCallback` | `castl::function<void(IWindow*, float, float)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，两个 `float` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetWindowCloseCallback` | `castl::function<void(IWindow*)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，只有一个借用窗口参数 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetWindowPosCallback` | `castl::function<void(IWindow*, float, float)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，两个 `float` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetWindowSizeCallback` | `castl::function<void(IWindow*, float, float)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，两个 `float` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetKeyCallback` | `castl::function<void(IWindow*, int, int, int, int)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，四个 `int32` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::SetCharCallback` | `castl::function<void(IWindow*, uint32_t)>` | 函数指针 + `void* user` + 内部注册号 + 注销伴生；回调首参是回调期间借用的 `IWindow`，`uint32` 按值 | 无 | 投影层保活表（GCHandle）+ 原生注册表 | 可绑 | 替换先装后销、置空/释放同时清原生注册与保活；回调期间 `IWindow` 可调用、返回即失效；重入与跨线程拒绝；异常下次主线程上报 | 统一错误通道（1003 / 1004 / 1005） |
| `IWindowSystem::GetSystemNativeHandle` | 返回 `void*` | `uint64`（不透明令牌） | 无 | — | 可绑 | 借出的系统句柄，C# 只能当不透明数 | 不失败 |
| `IShaderCompilerManager::AquireShaderCompiler` | 返回 `IShaderCompiler*`（池借出，与 `ReturnShaderCompiler` 配对） | `uint64` 句柄（投影层句柄表持裸指针），配对 `ReturnShaderCompiler(uint64)` | 无 | 投影层句柄 | 可绑 | 裸指针但**有明确配对语义** ⇒ 句柄表即可覆盖。注意 `Aquire` 是既有拼写，生成器不许改名 | 返回 0 + 错误码 |
| `IShaderCompilerManager::ReturnShaderCompiler` | `IShaderCompiler*` 参数 | `uint64` 句柄 → 反查 | 无 | 投影层句柄 | 可绑 | 与 `AquireShaderCompiler` 配对；无效句柄必须报错 | 错误码（无效句柄） |
| `IShaderCompilerManager::InitializePoolSize` | `uint32_t` | `uint32` | 无 | — | 可绑 | 定宽整数 | 错误码 |
| `IOManager::Initialize` | `thread_management::CThreadManager*`（初始化期依赖注入） | `—（不暴露）` | — | — | **不可绑** | ④ 语义越界类：模块初始化期的依赖注入，脚本既不该也不能提供线程管理器。建议不暴露 | 不暴露（无入口） |
| `IOManager::Batch` | `castl::string_view` → `castl::shared_ptr<IOBatch>` | `char const* + uint64 len` → `uint64` 句柄 + 配对释放 | 一次（路径字符串） | 投影层句柄 | 可绑 | `string_view` 参数与 `string const&` 同档；返回智能指针 ⇒ 句柄 | 返回 0 + 错误码 |
| `IOManager::WriteBatch` | `castl::string_view` → `castl::shared_ptr<WBatch>` | 同上 | 一次 | 投影层句柄 | 可绑 | 同上 | 返回 0 + 错误码 |
| `IResource::Serialize` | `ca_io::WBatch*`（另一个接口的裸指针） | `—（不暴露）` | — | — | **不可绑** | ② 所有权不明裸指针类：参数是 `WBatch` 接口实例且没有所有权说明。建议改成 `uint64 wbatch 句柄`（由 `IOManager::WriteBatch` 产出、共用同一张句柄表），在此之前不暴露 | 不暴露（无入口） |
| `IResource::Deserialize` | `ca_io::IOBatch*`（另一个接口的裸指针） | `—（不暴露）` | — | — | **不可绑** | ② 所有权不明裸指针类：同上，`IOBatch` 句柄化后再暴露 | 不暴露（无入口） |
| `IScriptRenderTarget::GetWidth` | `const`，返回 `uint32_t` | `uint32` | 无 | — | 可绑 | 定宽整数；目标尺寸只读 | 不失败 |
| `IScriptRenderTarget::GetHeight` | `const`，返回 `uint32_t` | `uint32` | 无 | — | 可绑 | 同 `GetWidth` | 不失败 |
| `IScriptRenderTarget::IsBackbuffer` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔在 C ABI 面用 `int32` 表示 | 不失败 |
| `IScriptRenderGraph::AddBuffer` | `cacore::NameHash const&`（包装值）+ `GPUBufferDescriptor const&`（双 `uint64_t` 公开记录）+ `castl::vector<uint8_t> const&`（同步字节） | `char const* + uint64 len`、`GPUBufferDescriptorC const*`、`uint8_t const* + uint64 count`；返回 `int32` | 一次（名称 + 字节） | 投影层（图内上传持有区） | 可绑 | 字节同步复制、描述符按 blittable 指针传；失败走 0/非 0 状态 + 文本 | 返回 0 + 错误码 + `GetLastError()` |
| `IScriptRenderGraph::SetPositionColorStream` | 一个 `NameHash const&` + 三个 `uint32_t` | `char const* + uint64 len` + 三个 `uint32`；返回 `int32` | 一次（名称） | — | 可绑 | 首版只支持 `POSITION` / `COLOR` 两个 `float3`；越界即拒绝 | 返回 0 + 错误码 + `GetLastError()` |
| `IScriptRenderGraph::AddTrianglePass` | `castl::shared_ptr<IScriptRenderTarget> const&`（目标接口）+ `PathHash const&` + 两个 `NameHash const&` + `uint32_t` + 四个 `float` | `uint64 targetHandle` + 文本 + 两个文本 + `uint32` + 四个 `float`；返回 `int32` | 一次（三个名字） | 投影层句柄 | 可绑 | 窄绘制描述：目标/着色器/流/缓冲/顶点数/清除色都由调用方给，不是 C++ 代画 | 返回 0 + 错误码 + `GetLastError()` |
| `IScriptRenderGraph::Present` | `castl::shared_ptr<IScriptRenderTarget> const&` | `uint64 targetHandle`；返回 `int32` | 无 | 投影层句柄 | 可绑 | 只接受后台缓冲目标；不设呈现目标数量上限 | 返回 0 + 错误码 + `GetLastError()` |
| `IScriptRenderGraph::GetPassCount` | `const`，返回 `uint32_t` | `uint32` | 无 | — | 可绑 | 行为断言用，不替代像素判据 | 不失败 |
| `IScriptRenderGraph::GetLastError` | `const`，返回 `castl::string` | 两段式：`uint64 <M>Len()` + `Copy<M>(uint8_t* dst, uint64 cap)` | 一次 | 调用方给缓冲区 | 可绑 | `string` 返回必须两段式拷出，不许把内部指针交给 C# | 不失败（文本本身） |
| `IScriptRenderBackend::GetWindowTarget` | `castl::shared_ptr<cawindow::IWindow> const&`（另一模块接口） | `uint64 windowHandle` → `uint64`（强句柄） | 无 | 投影层句柄 | 可绑 | 窗口句柄由窗口接口发行；目标入口按目标类型解析，错误类型/失效句柄不得进入实现 | 返回 0 句柄 + 错误码 |
| `IScriptRenderBackend::CreateCaptureTarget` | 两个 `uint32_t` → `castl::shared_ptr<IScriptRenderTarget>` | `uint32, uint32` → `uint64`（强句柄） | 无 | 投影层句柄 | 可绑 | 固定 RGBA8 / 2D / 单层 / 单 mip / 单采样；尺寸必须非零 | 返回 0 句柄 + 错误码 |
| `IScriptRenderBackend::CreateGraph` | 无参 → `castl::shared_ptr<IScriptRenderGraph>` | `void` → `uint64`（强句柄） | 无 | 投影层句柄 | 可绑 | 图由引擎内部构造，脚本只拿句柄 | 返回 0 句柄 + 错误码 |
| `IScriptRenderBackend::SubmitGraph` | `castl::shared_ptr<IScriptRenderGraph> const&` → `int32_t` | `uint64 graphHandle` → `int32` | 无 | 投影层句柄 | 可绑 | 调度器由原生侧新建，不进签名；空图/已释放句柄必须在提交前拒绝 | 返回 0 + 错误码 + `GetLastError()` |
| `IScriptRenderBackend::WaitIdle` | 无参，阻塞到后端空闲 | `void crb_wait_idle(uint64 self)` | 无 | — | 可绑 | 阻塞语义：文档写明会卡住调用线程；等待后才清空待完成集合 | 不失败 |
| `IScriptRenderBackend::GetLastError` | `const`，返回 `castl::string` | 两段式：`uint64 <M>Len()` + `Copy<M>(uint8_t* dst, uint64 cap)` | 一次 | 调用方给缓冲区 | 可绑 | 同图对象的 `GetLastError` | 不失败（文本本身） |

## 汇总：不可绑清单（6 条 + 2 条已移出）

| 接口::方法 | 属于哪一类不可投影 | 处置建议 | 归属哪个后续 change |
|---|---|---|---|
| `CRenderBackend::ExecuteGraph` | ② 所有权不明的裸指针（+ `shared_ptr` 参数） | 拆成"建图（返回句柄）+ 提交（引擎决定 scheduler）" | `csharp-bindings-generator`（改接口形状） |
| `CRenderBackend::Release` | ④ 语义越界 | 不暴露；脚本只能释放自己创建的句柄 | 无（规则即结论） |
| `CRenderBackend::GetWindowHandle` | ② 所有权不明的裸指针（`shared_ptr<IWindow>` 参数没有对应句柄） | 改成接收引擎分配的窗口 id | `csharp-bindings-generator` |
| ~~`IWindow::GetWindowSystem`~~ | **已移出不可绑清单**（2026-10-25） | 改判为可绑的借用句柄，见主表那一行 | `csharp-window-creation` |
| ~~`IWindowSystem::NewWindow`~~ | **已移出不可绑清单**（2026-10-20） | 改判为可绑的弱句柄，见主表那一行 | `csharp-window-creation` |
| `IOManager::Initialize` | ④ 语义越界 | 不暴露 | 无（规则即结论） |
| `IResource::Serialize` | ② 所有权不明的裸指针（`WBatch*`） | `WBatch` 句柄化后暴露 | `csharp-bindings-generator` |
| `IResource::Deserialize` | ② 所有权不明的裸指针（`IOBatch*`） | `IOBatch` 句柄化后暴露 | `csharp-bindings-generator` |

**四类不可投影分别落到谁身上**：① 模板方法 / 泛型接口 —— 本批 10 个接口里**暂无实例**（依据：75 条纯虚方法里没有一条是 `template<...>` 成员，也没有类模板作参数；规则保留，等出现再套）；② 所有权不明的裸指针 —— 4 条（`ExecuteGraph` / `GetWindowHandle` / `Serialize` / `Deserialize`；**`NewWindow` 已移出本类** —— 它返回的是 `weak_ptr` 不是裸指针，2026-10-20 起改判为可绑的"弱句柄"；**`GetWindowSystem` 也已移出** —— 它返回的是已 `CA_BIND` 接口的裸指针，2026-10-25 起改判为可绑的"借用句柄"；两者都见主表那一行）；③ 依赖隐式转换的重载 —— 本批**暂无实例**（依据：全批只有一个重载对 `CRenderBackend::Readback` 的图像版与缓冲版，参数类型分别是 `ImageHandle` / `BufferHandle`、没有隐式互转；规则保留）；④ 语义越界 —— 2 条（`Release` / `Initialize`）。

## 汇总：需快路清单（0 条）

这 10 个接口里**没有**"每帧多次调用且参数是容器"的方法，因此"需快路"为空。这不是规则失效，而是**审计范围不含热路径所在的位置**：

- 每帧的容器密集调用实际发生在 `GPUGraph`（提交 pass / binding）与 `IMGUIContext`（每帧编辑树）上，这两个不在本次审计范围（见 design 的 Open Questions）。
- 已观察到的容器参数（`IWindow::SetWindowName` / `IOManager::Batch` / `WriteBatch`）都是低频结构化调用，走"一次拷贝"那档即可；11 个 `Set*Callback` 是**回调注册**（每帧不走注册），走"函数指针 + 保活表"那档 ✓。

结论：快路规则保留，但**本 change 找不到真实消费者**；第一批消费者要在审计范围扩到 `GPUGraph` / `IMGUIContext` 时才出现。
