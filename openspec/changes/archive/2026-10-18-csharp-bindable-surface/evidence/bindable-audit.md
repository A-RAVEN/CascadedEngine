# 可绑定面审计表

**口径（csharp-native-direct-binding 任务 2.1 换面）**：审计面 = **进入绑定面的公开方法**——`CA_BIND` 接口里的公开成员函数（含非纯虚 virtual 与非虚公开方法；构造 / 析构 / 运算符不算），逐条取类体按访问标号切段后**只数 public 段**；模板方法只在同文件写了 `CA_BIND_INSTANTIATE(方法名, …)` 时计一次（生成器按声明展开具体槽，审计按方法名一行）。`CA_BIND_SKIP` 的方法仍要有行（结论写"不可绑 + 本轮未实现/理由"）。解析实现见 `Tools/CodeGen/check_bindable_audit.py` 的 `header_methods`。

**范围（本批换面后）**：**8** 个接口 = 原生五类（`CRenderBackend` / `GPUGraph` / `ShaderStruct` / `GPUTexture` / `WindowHandle`）+ `IReadbackToken` + 窗口两个，合计 **91** 条公开方法。过渡期桥五个接口的 28 条方法随 5.1 退役移出审计面（行同时删除，不是"放过"）。旧表的 `IShaderCompilerManager` / `IOManager` / `IResource` 三块随换面移出（它们不是本批绑定面的接口，行同时删除；不是"放过"）。

| 接口 | 方法条数 | 所在头文件 |
|---|---|---|
| `CRenderBackend` | 16 | `Interface/RenderInterface/header/CRenderBackend.h` |
| `IReadbackToken` | 3 | `Interface/RenderInterface/header/CRenderBackend.h` |
| `GPUGraph` | 20 | `Interface/RenderInterface/header/GPUGraph.h` |
| `ShaderStruct` | 7 | `Interface/RenderInterface/header/ShaderStruct.h` |
| `GPUTexture` | 3 | `Interface/RenderInterface/header/GPUTexture.h` |
| `WindowHandle` | 3 | `Interface/RenderInterface/header/WindowHandle.h` |
| `IWindow` | 23 | `WindowSystem/header/CAWindow/WindowSystem.h` |
| `IWindowSystem` | 16 | `WindowSystem/header/CAWindow/WindowSystem.h` |
| **合计** | **91** | |

**列定义**：`拷贝` = 该调用是否需要一次额外的内存拷贝（视图与按值不算）；`谁分配` = 过界内存的分配方（调用方给缓冲区 / 投影层分配 + 配对释放 / 引擎持有 + 只读视图 / `—` 表示不过界内存）；`可绑性` = `可绑` / `需快路` / `不可绑`；`不可绑` 必须在"理由或建议"列给出理由与建议。

| 接口::方法 | C++ 形状 | 投影形状 | 拷贝 | 谁分配 | 可绑性 | 理由或建议 | 失败上报方式 |
|---|---|---|---|---|---|---|---|
| `CRenderBackend::ExecuteGraph` | `TaskScheduler*` + `castl::shared_ptr<GPUGraph> const&` | scheduler 经 `CA_BIND_INJECT(…, scheduler)` 由宿主钩子提供（不进 C ABI / 托管表面）；`shared_ptr` 参数 ⇒ 图对象句柄 | 无 | 投影层句柄 | 可绑（注入） | ② 的参数位置开的两条唯一通道之一；钩子名 `CAInject_CRenderBackend_ExecuteGraph_scheduler` | 错误码（句柄失效 / 空图） |
| `CRenderBackend::GetTimingCapabilities` | `EGPUQueueType` → `GPUTimingCapabilities`（单 bool 纯标量记录） | 枚举按 `int32_t`，记录按值镜像 `<T>C` | 无 | — | 可绑 | 纯标量记录走镜像快速路径 | 不失败 |
| `CRenderBackend::ExecuteGraphTimed` | `TaskScheduler*` + `shared_ptr<GPUGraph>` → `std::unique_ptr<IGPUGraphTiming>` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：返回位置是 `unique_ptr<IGPUGraphTiming>`，要让这条进表必须把第五个接口 `IGPUGraphTiming` 一并放进绑定面（连带它的容器返回）；脚本四个渲染用例不用计时。`CA_BIND_SKIP` 记本轮未实现 | 不暴露（无入口） |
| `CRenderBackend::Release` | 无参 | `—（不暴露）` | — | — | **不可绑** | ④ 语义越界：后端销毁由引擎模块生命周期决定；脚本侧释放走托管 `Dispose` 防线。`CA_BIND_SKIP` | 不暴露（无入口） |
| `CRenderBackend::CreateGPUBuffer` | `GPUBufferDescriptor const&`（两个 `uint64_t` 公开记录）+ `EBufferUsageFlags` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：返回位置是 `castl::shared_ptr<GPUBuffer>`，托管侧没有 `GPUBuffer` 接口的映射（本批绑定面不含它）。建缓冲的脚本路径是"名称 + `BufferHandle` + `GPUGraph::AllocBuffer`"（实测托管侧跳过原文见 evidence/2.2）。`CA_BIND_SKIP` 记本轮未实现 | 不暴露（无入口） |
| `CRenderBackend::CreateGPUBuffer` | `EBufferUsageFlags, uint64_t, uint64_t`（非纯虚便利重载） | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：同上（重载两条各占一行）。`CA_BIND_SKIP` | 不暴露（无入口） |
| `CRenderBackend::CreateGPUTexture` | `GPUTextureDescriptor const&` + `ETextureAccessTypeFlags` | 描述符按 blittable 指针传；返回句柄 | 无 | 投影层句柄 | 可绑 | 同 CreateGPUBuffer | 返回 0 + 错误码 |
| `CRenderBackend::CreateShaderStruct` | `cacore::NameHash const&` → `castl::shared_ptr<ShaderStruct>` | 名称走 UTF-8 两段；返回 `ShaderStruct` 句柄（目标类型表） | 一次（名称） | 投影层句柄 | 可绑 | 未知类型名的拒绝在托管前置校验（D6）里做，原生入口不接空元数据 | 返回 0 + 错误码 |
| `CRenderBackend::FindShaderStructData` | `cacore::NameHash const&` → `ShaderCompilerSlang::ShaderStructData const*` | 记录两段式：`uint64 <M>Len(...)` + `Copy<M>(...)`，托管得序列化只读副本；空命中 `null` | 一次（整记录序列化） | 调用方给缓冲区 | 可绑 | ② 的返回位置**唯一例外**（design D8）；原 ② 结论按两段式通道重新归类 | 错误码（块头/载荷不符） |
| `CRenderBackend::FindShaderRootStructData` | `cacore::PathHash const&` → 同上 | 同 `FindShaderStructData` | 一次 | 调用方给缓冲区 | 可绑 | 同上（按着色器路径查根结构） | 错误码 |
| `CRenderBackend::GetWindowHandle` | `castl::shared_ptr<cawindow::IWindow>` → `castl::shared_ptr<WindowHandle>` | 入参是已绑定接口 ⇒ 窗口对象句柄；返回 `WindowHandle` 是已绑定接口 ⇒ 按对象登记发句柄 | 无 | 投影层句柄 | 可绑 | 2.4（D12）改判：`WindowHandle` 进表后两条 `shared_ptr` 都走既有机制（参数按句柄、返回按对象登记），解 SKIP | 返回 0 + 错误码 |
| `CRenderBackend::AnyWindowRunning` | 返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `CRenderBackend::WaitIdle` | 无参，阻塞 | `void` | 无 | — | 可绑 | 阻塞语义写进契约：会卡住调用线程 | 不失败 |
| `CRenderBackend::RunTestCode` | 无参（非纯虚） | `—（不暴露）` | — | — | **不可绑** | ④ 语义越界：测试专用入口，脚本不该调。`CA_BIND_SKIP` | 不暴露（无入口） |
| `CRenderBackend::Readback` | `ImageHandle const&` + `std::span<uint8_t>` → `std::unique_ptr<IReadbackToken>` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：`std::span` 是 design Non-Goals 点名的 GPU 查询批形状（另立批）。`CA_BIND_SKIP` 记本轮未实现 | 不暴露（无入口） |
| `CRenderBackend::Readback` | `BufferHandle const&` + `std::span<uint8_t>` → 同上 | `—（不暴露）` | — | — | **不可绑** | 同上（重载两条各占一行） | 不暴露（无入口） |
| `IReadbackToken::Wait` | 无参，阻塞直到 GPU 写完 | `void crb_token_wait(uint64)` | 无 | — | 可绑 | 阻塞语义：文档必须写明"会卡住调用线程" | 错误码（超时 / 令牌失效） |
| `IReadbackToken::IsReady` | `const`，返回 `bool` | `int32` | 无 | — | 可绑 | 布尔用 `int32` | 不失败 |
| `IReadbackToken::Reset` | 无参（幂等释放） | `void crb_token_reset(uint64)` | 无 | — | 可绑 | 幂等语义写进契约（重复调不报错） | 不失败 |
| `GPUGraph::AddPass` | `RenderPass const&`（记录：私有成员 + `vector<ImageHandle>` 值类容器、`ShaderStructDic` 映射、`vector<DrawCallBatch>`） → 链式自返回 | 记录走序列化块（D11 三类元素）；C ABI 是 `uint8_t const* + len`；托管表面返回同一图对象（链式） | 一次（整记录序列化） | 调用方给缓冲区 | 可绑 | D11：`AddPass(RenderPass const&)` 是四个渲染用例的**唯一建图入口**，SKIP 即功能回退；记录需要 `CA_CODEGEN_FRIEND(RenderPass)`（2.2 加，不改签名） | 错误码（块头/载荷/对象句柄） |
| `GPUGraph::Rast` | 同 `AddPass(RenderPass const&)` | 同左 | 一次 | 调用方给缓冲区 | 可绑 | 同左（同一入口的别名） | 错误码 |
| `GPUGraph::AddPass` | `ComputeBatch const&`（记录：ShaderInfo + string + 三个 uint32 + vector<ComputeDispatch> + bool） → 链式自返回 | 记录走序列化块；托管返回同一图对象 | 一次 | 调用方给缓冲区 | 可绑 | 计算 pass 入口；记录字段若在生成期被判超界，本条改判并留修订记录（不在验收轮偷偷改） | 错误码 |
| `GPUGraph::Comp` | 同 `AddPass(ComputeBatch const&)` | 同左 | 一次 | 调用方给缓冲区 | 可绑 | 同左 | 错误码 |
| `GPUGraph::ScheduleData` | `ImageHandle const&, const void*, uint64_t, uint64_t, bool` → 链式自返回 | 内存窗投影（D2）：`uint8_t const* + uint64 长度`，托管收 `byte[]`；其余参数原样 | 无（同步拷贝） | 引擎图内上传持有区 | 可绑 | D2：桥的字节上传通道在原生面的等价形态 | 错误码 |
| `GPUGraph::ScheduleData` | `BufferHandle const&` 版本同上 | 同左 | 无 | 同左 | 可绑 | 同左 | 错误码 |
| `GPUGraph::AllocImage` | `ImageHandle const&, GPUTextureDescriptor const&` → 链式自返回 | 值类句柄 + 描述符镜像指针 | 无 | — | 可绑 | 图内图像分配 | 错误码 |
| `GPUGraph::AllocBuffer` | `BufferHandle const&, GPUBufferDescriptor const&` → 链式自返回 | 值类句柄 + 描述符镜像指针 | 无 | — | 可绑 | 图内缓冲分配 | 错误码 |
| `GPUGraph::Present` | `ImageHandle const&` → 链式自返回 | 值类句柄 | 无 | — | 可绑 | 呈现入口 | 错误码 |
| `GPUGraph::Finalize` | `ImageHandle const&, ETextureAccessType` → 链式自返回 | 值类句柄 + 枚举 `int32_t` | 无 | — | 可绑 | 纹理用途转换 | 错误码 |
| `GPUGraph::GetGraphStages` | 返回 `castl::vector<EGraphStageType> const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：返回位置容器的**托管**两段式读取（C ABI 侧有 `Len`/`Copy` 伴生，但托管表面没有对应通道；脚本渲染用例不需要它）。`CA_BIND_SKIP` 记本轮未实现 | 不暴露（无入口） |
| `GPUGraph::GetRenderPasses` | 返回 `castl::deque<RenderPass> const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：返回位置的记录容器（D11 只定义记录**输入**方向；C++ 写路径对值类容器/映射不产出，硬发会让托管拿到缺字段的字节）。`CA_BIND_SKIP` 记本轮未实现 | 不暴露（无入口） |
| `GPUGraph::GetRenderPassForWrite` | 返回 `RenderPass&`（非 const 引用 = 出参方向） | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：出参方向的记录（本批只定义记录输入方向）；且"写穿原生对象"的语义未定义。`CA_BIND_SKIP` | 不暴露（无入口） |
| `GPUGraph::GetComputePasses` | 返回 `castl::deque<ComputeBatch> const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：同 `GetRenderPasses`（返回位置记录容器） | 不暴露（无入口） |
| `GPUGraph::GetDataTransfers` | 返回 `castl::vector<GPUDataTransfers> const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：元素记录 `GPUDataTransfers` 的形状（含 `DataReference` 搬运记录）不在本批序列化批登记范围 | 不暴露（无入口） |
| `GPUGraph::GetPassIndices` | 返回 `castl::vector<uint32_t> const&` | 标量容器两段式（D13，1.6）：`Count` 查条数 + `Copy` 按定长缓冲逐字节拷；托管得 `uint[]`，空容器返回空数组 | 一次 | 调用方给缓冲区 | 可绑 | 1.6/2.4 改判：D13 开标量容器通道；3.1 的 `GetPassCount` 消费点由托管糖层 `GetPassIndices().Length` 承接 | 错误码（空容器不解引用） |
| `GPUGraph::GetUploadDataHolder` | 返回 `UploadDataHolder const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：记录含 `void const*` 成员（不可序列化）。`CA_BIND_SKIP` | 不暴露（无入口） |
| `GPUGraph::GetImageManager` | 返回 `GraphResourceManager<GPUTextureDescriptor> const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：模板化的内部资源管理器记录，无绑定形状。`CA_BIND_SKIP` | 不暴露（无入口） |
| `GPUGraph::GetBufferManager` | 返回 `GraphResourceManager<GPUBufferDescriptor> const&` | `—（不暴露）` | — | — | **不可绑** | 同 `GetImageManager` | 不暴露（无入口） |
| `GPUGraph::GetFinalizePass` | 返回 `FinalizePass const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：记录含 `map<ImageHandle, ETextureAccessType>`（键非 NameHash，D11 的 MUST NOT）。`CA_BIND_SKIP` | 不暴露（无入口） |
| `ShaderStruct::SetValue` | `template<typename T> SetValue(NameHash const&, T const&, uint32_t)`；本批声明 `float3` / `float` 两形态 | 显式实例化声明（D9）：`ShaderStruct_SetValue_float3` / `_float` 两个槽；名称两段 + 值 + `uint32`；托管出同名重载 | 一次（名称） | — | 可绑（实例化声明） | ① 的唯一通道：`CA_BIND_INSTANTIATE(SetValue, float3, float)`；未声明形态不进任何表面 | 错误码 |
| `ShaderStruct::SetImage` | `NameHash const&, ImageHandle const&, GPUTextureView const&, uint32_t` → 链式自返回 | 名称两段 + 值类句柄 + 视图记录（纯标量）+ `uint32` | 一次（名称） | — | 可绑 | 纹理字段绑定 | 错误码 |
| `ShaderStruct::SetImage` | `NameHash const&, castl::shared_ptr<GPUTexture> const&, uint32_t`（便利重载） | 名称两段 + 纹理对象句柄 + `uint32` | 一次 | 投影层句柄 | 可绑 | 指向已绑定接口 `GPUTexture` 的智能指针 ⇒ 句柄 | 错误码 |
| `ShaderStruct::SetBuffer` | `NameHash const&, BufferHandle const&, uint32_t` → 链式自返回 | 名称两段 + 值类句柄 + `uint32` | 一次 | — | 可绑 | 缓冲字段绑定 | 错误码 |
| `ShaderStruct::SetSampler` | `NameHash const&, TextureSamplerDescriptor const&, uint32_t` → 链式自返回 | 名称两段 + 采样器记录镜像指针 + `uint32` | 一次 | — | 可绑 | 采样器字段绑定（记录是 8 字段公开记录） | 错误码 |
| `ShaderStruct::SetStruct` | `NameHash const&, castl::shared_ptr<ShaderStruct> const&, uint32_t` → 链式自返回 | 名称两段 + 子结构句柄 + `uint32` | 一次 | 投影层句柄 | 可绑 | 子结构按目标类型解析（同一接口的句柄） | 错误码 |
| `ShaderStruct::GetStructTypeName` | 返回 `cacore::NameHash const&` | `—（不暴露）` | — | — | **不可绑** | 本轮未实现：返回位置的包装值（`NameHash const&`）——本批只定义包装值的入参方向；这条是诊断用，脚本不需要。`CA_BIND_SKIP` | 不暴露（无入口） |
| `GPUTexture::GetDescriptor` | 返回 `GPUTextureDescriptor const&` | 记录镜像按值返回（`<T>C`） | 无 | — | 可绑 | 描述符是纯标量记录 | 不失败 |
| `GPUTexture::SetName` | `castl::string const&` | `char const* + uint64 len`（UTF-8） | 一次 | 调用方 | 可绑 | 文本入参 | 错误码 |
| `GPUTexture::GetName` | 返回 `castl::string const&` | 两段式（`Len` + `Copy`） | 一次 | 调用方给缓冲区 | 可绑 | 文本返回不许把内部指针交给 C# | 不失败 |
| `WindowHandle::GetSizeSafe` | 返回 `uint2`（两个 `uint32_t` 的公开记录） | 记录镜像按值返回（`uint2C`） | 无 | — | 可绑 | 2.4（D12）：窗口尺寸从纯标量记录读，无句柄无容器 | 不失败 |
| `WindowHandle::GetBackbufferDescriptor` | 返回 `GPUTextureDescriptor const&` | 序列化只读副本（`Len` + `Copy`，与 D8 同族；`const&` 即非拥有别名） | 一次（整记录序列化） | 调用方给缓冲区 | 可绑 | 2.4（D12）：后台缓冲描述符，托管拿到副本 | 错误码（块头/载荷不符） |
| `WindowHandle::IsValid` | 非纯虚，返回 `bool` | `int32` | 无 | — | 可绑 | 2.4（D12）：非纯虚公开方法，批 A 既有形态 | 不失败 |
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

## 汇总：不可绑清单（本批换面后 11 条：④ 2 条 + 本轮未实现 9 条）

| 接口::方法 | 属于哪一类不可投影 | 处置建议 | 归属哪个后续 change |
|---|---|---|---|
| `CRenderBackend::Release` | ④ 语义越界 | 不暴露；脚本侧释放走托管 `Dispose` | 无（规则即结论） |
| `CRenderBackend::RunTestCode` | ④ 语义越界（测试专用入口） | 不暴露 | 无（规则即结论） |
| `CRenderBackend::ExecuteGraphTimed` | 本轮未实现（返回 `unique_ptr<IGPUGraphTiming>` 要连带第五接口一起放门） | 不暴露；计时批另立时连 `IGPUGraphTiming` 一起进表 | 计时/查询批 |
| `CRenderBackend::Readback`（图像重载） | 本轮未实现（`std::span` 是 Non-Goal 点名的 GPU 查询批形状） | 不暴露 | GPU 查询批 |
| `CRenderBackend::Readback`（缓冲重载） | 同上 | 不暴露 | GPU 查询批 |
| `GPUGraph::GetRenderPasses` | 本轮未实现（返回位置的记录容器；D11 只定义输入方向） | 不暴露；需要读回时另立出参批 | 出参/查询批 |
| `GPUGraph::GetRenderPassForWrite` | 本轮未实现（出参方向记录） | 不暴露 | 出参批 |
| `GPUGraph::GetComputePasses` | 本轮未实现（返回位置的记录容器） | 不暴露 | 出参/查询批 |
| `GPUGraph::GetDataTransfers` | 本轮未实现（元素记录形状未登记） | 不暴露 | 出参/查询批 |
| `GPUGraph::GetUploadDataHolder` | 本轮未实现（记录含 `void const*` 成员） | 不暴露 | —— |
| `GPUGraph::GetImageManager` / `GetBufferManager` | 本轮未实现（模板化内部资源管理器无绑定形状） | 不暴露 | —— |
| `GPUGraph::GetFinalizePass` | 本轮未实现（记录含键非 NameHash 的映射，D11 的 MUST NOT） | 不暴露 | 映射键扩展批 |
| ~~`CRenderBackend::ExecuteGraph`~~ | **已移出不可绑清单**（本批 2.2） | 参数位置裸指针经 `CA_BIND_INJECT` 合法化，见主表那一行 | csharp-native-direct-binding |
| ~~`CRenderBackend::FindShaderStructData`~~ | **已移出不可绑清单**（本批 1.2） | 返回位置裸指针经"记录两段式"唯一例外合法化 | csharp-native-direct-binding |
| ~~`CRenderBackend::FindShaderRootStructData`~~ | **已移出不可绑清单**（本批 1.2） | 同上 | csharp-native-direct-binding |
| ~~`CRenderBackend::GetWindowHandle`~~ | **已移出不可绑清单**（本批 2.2） | `IWindow` 已是绑定面接口 ⇒ 参数可解析为窗口句柄 | csharp-native-direct-binding |
| ~~`IWindow::GetWindowSystem`~~ | **已移出不可绑清单**（2026-10-25） | 改判为可绑的借用句柄，见主表那一行 | `csharp-window-creation` |
| ~~`IWindowSystem::NewWindow`~~ | **已移出不可绑清单**（2026-10-20） | 改判为可绑的弱句柄，见主表那一行 | `csharp-window-creation` |
| ~~`IShaderCompilerManager::*` / `IOManager::*` / `IResource::*`~~ | **随换面移出审计范围**（本批 2.1） | 三个接口不在本批绑定面（主表行同时删除，不是"放过"）；需要时另立审计面 | —— |

**四类不可投影分别落到谁身上（换面后）**：① 模板方法 / 泛型接口 —— `ShaderStruct::SetValue` 走 `CA_BIND_INSTANTIATE` 通道合法化（2.2 声明形态），`GPUGraph` 的三个模板维持排除（`ScheduleData` 声明单形态、`AllocAndUploadBuffer` 排除）；② 所有权不明的裸指针 —— 参数位置由 `CA_BIND_INJECT`（`ExecuteGraph`）与内存窗配对（`ScheduleData` 系）两条通道合法化，返回位置由"记录两段式"（两个 `Find*`）开唯一例外，其余维持不可绑；③ 依赖隐式转换的重载 —— 本批**暂无实例**（`Readback` 两条的参数类型 `ImageHandle` / `BufferHandle` 无隐式互转；规则保留）；④ 语义越界 —— 2 条（`Release` / `RunTestCode`）。

## 汇总：需快路清单（0 条）

这 8 个接口里**没有**"每帧多次调用且参数是容器"的方法，因此"需快路"为空。这不是规则失效，而是**审计范围不含热路径所在的位置**：

- 每帧的容器密集调用实际发生在 `GPUGraph`（提交 pass / binding）与 `IMGUIContext`（每帧编辑树）上，这两个不在本次审计范围（见 design 的 Open Questions）。
- 已观察到的容器参数（`IWindow::SetWindowName` / 已退役的桥的 `AddBuffer(byte[])`）都是低频结构化调用，走"一次拷贝"那档即可；11 个 `Set*Callback` 是**回调注册**（每帧不走注册），走"函数指针 + 保活表"那档 ✓。

结论：快路规则保留，但**本 change 找不到真实消费者**；第一批消费者要在审计范围扩到 `GPUGraph` / `IMGUIContext` 时才出现。
