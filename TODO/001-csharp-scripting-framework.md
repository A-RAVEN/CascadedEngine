# 001 · C# 脚本运行时框架设计

> **状态**：探索中（未达 OpenSpec proposal 水位）
> **日期**：2026-09-06
> **关联**：`E:\Projects\CACodeGen`（既有绑定生成工具）、CascadedEngine 接入 C#
> **来源**：本文件为一次探索讨论（`/opsx:explore`）的沉淀，期间用 workflow 做了 6 面研究 + 对抗验证（72 agent，8/64 条 claim 被驳回）+ 完整性审查（判 REVISE）。
> 结论为「设计笔记」，不是 spec；升级为 openspec change 的条件见文末「升级建议」。

---

## 1. 目标与边界

用户拍板的目标：给引擎定一套 **C# 运行时方案**，让开发者用 C# 调用引擎的主要模块：

1. **IMGUI** —— 绘制自定义编辑器窗口
2. **RenderBackend + GPUGraph** —— 自定义渲染管线
3. **ThreadManager** —— 多线程系统
4. **资源管理系统** —— 加载/查询

**硬性边界**：
- **C# 侧不手写 extern**：在 C++ 侧定义接口 +（可选）自定义 attribute/元数据，一个 CodeGen 工具自动为这个 C++ 接口生成对应的 C# 函数和包装（顺带生成 C++ thunk 层）。single source of truth = 带标注的接口头。
- **完整游戏 Entity/Component 层不进本框架**（DEFERRED，见 §3.12）。

## 2. 引擎侧现状（2026-09-06 核实）

- 纯渲染引擎地基：模块化框架（`CACore` 服务定位器 + **逆序 teardown**）、双后端（Vulkan/D3D12）、RenderGraph、IMGUI、Readback、Timer/Thread/IO。
- **无游戏层**：无 Scene/Entity/Component/Behaviour；帧循环在 tester 里手写 `UpdateSystem → NewScheduler → ExecuteGraph → WaitIdle`（Main.cpp:196-213）。
- **已有反射元数据**：`CA_PRIVATE_REFLECTION`（`ShaderResourceHandle.h:116/175` 的 ImageHandle/BufferHandle 上）——可能让 CodeGen **读现有反射**而非 clang 解析，**待核实**（见 §8）。
- 已建立的生命周期纪律：逆序 teardown（`CAModuleManager.cpp:58-81`）、L1（shared_ptr 归零即释放）/L3a（VMA allocation 登记扫残留）、Readback dst 契约（`GetImageByteSize` 单一来源）、双后端 readback/subresource 门。
- 跨模块先例：组件经 `extern "C"` `CA_LIBRARY_API` factory（`LibraryExportCommon.h:18-40`、`CAModuleImplementation.h:99`）跨边界，而非直接 C++ 链接。

## 3. 关键设计决策

> 标记：`[✓]` = 已核实站得住；`[修正]` = 对抗审查修正后采纳。

**[✓] 3.1 接缝 = C-ABI 函数指针表，锚在 `CRenderBackend` 纯虚 vtable + `Common.h` 枚举 + `GPUGraph`。**
`CRenderBackend` 的成员全是纯虚（ExecuteGraph :61 / Release :62 / CreateGPUBuffer :63 / CreateGPUTexture :70 / CreateShaderStruct :71 / GetWindowHandle :72 / WaitIdle :74 / Readback :86-87），本身已是 vtable 契约。合同是 C-ABI 表，**不是 C++ 类 ABI**（`castl::shared_ptr`/`castl::string`/EASTL 容器/类 vtable 布局都是工具链/版本相关、不可 blittable）。C# 持有不透明 `nint` 胶囊，绝不持 `CRenderBackend*`。
> 引擎自身就证明这点：组件跨模块都走 `extern "C"` factory，不靠直接 C++ 链接；C# 入口必须指向 `extern "C"` 符号，否则 EntryPointNotFoundException。

**[✓] 3.2 句柄跨 ABI 留紧凑值，C# 不拥有。**
`ImageHandle`/`BufferHandle` 核心是 `ResourceHandleKeyData { NameHash name(uint64); uint32 uniqueID; uint32 threadID }` + 类型枚举（ShaderResourceHandle.h:23-36，:40-117，:119-176）。只有这个固定三元组 + 类型枚举以 ~16B blittable struct 跨边界；真正的 `shared_ptr<GPUTexture/WindowHandle>`（:112-113，:172）留在 C++ 侧、以该三元组为键。ABI 暴露**不透明资源 ID + 显式 AddRef/Release**，绝不跨 `shared_ptr` 值。

**[✓] 3.3 错误不跨 C++ 异常。**
JIT runtime 与 MSVC runtime 的异常机制不同，跨它 = UB。引擎本就以 `CA_LOG_ERR_BREAK`/`CA_ASSERT`（GPUGraph.h:549/:572）和返回 null handle/token（Readback 拒读返回 null token，CRenderBackend.h:84-87）报错。所以 C ABI 返回 int32 status（HRESULT 样式）+ out-param 错误码/字符串；C# 包装器转成 C# 异常（`Marshal.ThrowExceptionForHR` 语义）。VUID 通过 C# 注册 `[UnmanagedCallersOnly]` log 回调，后端 debug-utils 回调在每个 VUID/警告时调用它，编辑期即可诊断。

**[✓] 3.4 单向 AOT 安全 + 反向 callback。**
C#→native 用 **`[LibraryImport]`**（`.NET 7+` 编译期 source-gen，非 `[DllImport]`——后者的 IL 桩在运行时生成，AOT 剪不掉所以不可用）。native→C# 用 **`[UnmanagedCallersOnly]`**（Native AOT 恰好把这类方法导出成公开 C 入口点）。热路径 C# 把收到的 `IntPtr` cast 成 `delegate* unmanaged[Cdecl]<...>` 直调（零 marshal）。**绝不**在可被 native 调用的路径放 `Delegate`/`MulticastDelegate` 字段——`GetFunctionPointerForDelegate` 不 root，须 `GC.KeepAlive` 或静态字段保活。

**[修正] 3.5 单一帧循环模型：native tick 调注册的 per-frame callback。**
对抗审查发现原稿自相矛盾（一处说"C# host 游戏线程/每帧 build graph"，另一处说"native 跑帧、C# 是 callback"）。**选定一个**：native 主循环 `tick` 调引擎注册的 `[UnmanagedCallersOnly]` per-frame 回调（即 IMGUI 模式），回调在**引擎游戏线程**上跑。C# `Thread.CurrentThread` == 游戏线程；**C# 不 host 周期**。删除"C# hosts the game thread"表述。

**[✓] 3.6 恰好两个 crossing 点 + C# 永不上渲染 worker。**
(1) `build-graph → ExecuteGraph(TaskScheduler*, shared_ptr<GPUGraph> const&)`（游戏线程）；(2) `readback token->Wait() → consume`（游戏线程，同步后）。引擎 worker 线程（scheduler 节点/后端 upload+execute）只跑 C++。hostfxr 返回的 fnptr 不自动开线程——你在哪个 native 线程调用它，`Thread.CurrentThread` 就解析为那个线程（managed 身份懒附加）。`NewScheduler()` 每帧一个新的（`ThreadManager.h:114`，Impl.cpp:298-306 new-per-call 无缓存），所以 scheduler 从不跨帧共享。

**[修正] 3.7-3.8 运行时双通道：都【进程内】。**
- 主（编辑器/工具链）= **CoreCLR native hosting（hostfxr/nethost）JIT**。官方序列：`get_hostfxr_path → LoadLibrary 三个导出 hostfxr_initialize_for_runtime_config / hostfxr_get_runtime_delegate / hostfxr_close → load_assembly_and_get_function_pointer`。**走 runtime_config 路径**，不走 `hostfxr_initialize_for_dotnet_command_line`（它单次/进程、会接管调用线程、且有 threadpool 停排缺陷 dotnet/runtime#3741）。
- 发布 = **NativeAOT**。但对抗审查抓出一个关键修正：ABI 在同一地址空间传 C++ 对象指针（`shared_ptr<GPUGraph>&`、`TaskScheduler*`、`IReadbackToken*`），所以 **NativeAOT 模块必须进程内**——引擎 `dlopen` 一个导出 `[UnmanagedCallersOnly]` 的 AOT native 库，**不能是"另一个进程"**。**否决"AOT 独立进程"**（不同地址空间传不了这些指针）。
- 因此：**JIT host = 进程内 hostfxr；AOT host = 引擎 dlopen 的进程内 AOT native 库**，二者共享同一条 C ABI + 同一批 C++ 对象。若发行的游戏不需要进程内 graph/readback/task 互操作，这些面就只在编辑器 host，AOT 面需裁掉。
- 硬约束：**每进程只能加载一个 CoreCLR**；native hosting 与 trim 不兼容；同进程 side-by-side 自包含 runtime 不支持。

**[修正] 3.9 所有权：SafeHandle 是 C# 拥有 native 的唯一入口；恰好一侧拥有释放，另一侧 borrow。**
- `SafeHandle(ownsHandle)` 覆盖 `ReleaseHandle()` 调引擎释放、**幂等**、`Dispose` 确定性；`CriticalFinalizerObject` finalizer 是兜底。对引擎拥有的资源（window backbuffer、借来句柄）用 `ownsHandle=false`——borrow 永不 free。
- `GCHandle` 在 native 侧保一个托管对象（跨界的读回载体、持久 callback）；必须显式 `Free()`。
- **pin** 任何原地址跨界的托管内存（Pinned GCHandle 或 fixed span）——GC 压缩后裸指针失效，裸 `IntPtr` 不是 GC root 无存活保证。
- `GC.KeepAlive` 回调源 delegate 全长保存。
- `ConditionalWeakTable` **只用于托管→托管辅助状态**（ephemeron），**绝不用于拥有 native handle**（其 clean 由 key 回收驱动，非 native 释放点，不具确定性）。
- **double-ownership 是首要危害**：引擎内部对象（VulkanBuffer/Texture/WindowHandle，last shared_ptr 掉即释放）与一个 C# 包装器可能都认为自己在 free 同一 handle。**恰好一侧 free，另一侧 borrow 或持有不释放的 key 三元组**。
- **finalizer 只是安全网，不用于正确性**：释放定在 `WaitIdle`/readback 帧边界；只靠 finalizer 会推迟到 GC 时间。**注意**：把释放绑定到帧边界修 leak，但**不修 UAF**——UAF（悬垂指针指向托管内存）只能靠 pin/root 托管缓冲，与何时释放无关。两者都需要。

**[✓] 3.10 生成式绑定（build-time codegen）。**
一个生成器读 C++ 元数据/attribute（倾向复用现有 `CA_PRIVATE_REFLECTION`）+ 枚举 + `GPUGraph`，发两个产物：`<Module>.Bindings.cpp`（extern-"C" C-ABI shim：扁平 struct-of-function-pointers 包每个 virtual + per-pass C 胶水 + raw 句柄三元组结构体）和 `<Module>.Bindings.cs`（`[LibraryImport]` 壳 + blittable 句柄结构体）。两者**同一次构建一起编**，注释里就写明"生成的 C# 签名必须精确镜像 C shim——由 ABI-schema 测试强制"。镜像 Flax.Build 的做法（BindingsGenerator 发 `.Bindings.Gen.cpp/.cs` 进 IntermediateFolder 并编译；API 未变时跳过重生成，配 on-disk LoadCache/SaveCache）。（它是统一 CodeGen 框架的一个 emitter，与反射/序列化共享 parser + 类型表，见 [[002-reflection-via-codegen]]。）

**[✓] 3.11 调用约定 / 平台细节。**
x64/ARM/ARM64 只有一种规范调用约定（Stdcall==Cdecl），主 x64 目标无需显式选；只有 Windows x86 需要选。`char_t` = Windows 上 `wchar_t`(UTF-16)，其它平台 `char`(UTF-8)。hostfxr 用 `HOSTFXR_CALLTYPE=__cdecl`，返回的 runtime delegate 用 `CORECLR_DELEGATE_CALLTYPE=__stdcall`（仅 32 位 x86 上 ABI 可见）。

**[✓] 3.12 DEFERRED：完整游戏 Entity/Component 层。**
它引入动态跨帧 managed 状态、per-entity 存活、脚本驱动变更，会让 reload/生命周期契约指数级变难；属于**高于**本脚本层的上层，不放进接缝。四个目标面（tooling/编辑器 + 管线构建 + 任务提交 + 资源加载查询）都是静态、数据驱动、适合跨窄 C ABI。

## 4. 四个目标模块的真实表面（workflow 核实 file:line）

### 4.1 IMGUI 编辑器窗口 —— `imgui_display::IMGUIContext`
`GetInstance<imgui_display::IMGUIContext>()`（Main.cpp:1440）。因为 native 持有帧循环 + ImGui context，**C# 编辑器是 callback 而非 driver**：native 跑帧，C# 跑编辑器 draw/窗口代码经一个注册的 `[UnmanagedCallersOnly]` 函数指针，内部调：
- `IMGUIContext::PrepareDrawData(GPUGraph*)` :85，`Draw(GPUGraph*, ImageHandle captureTarget=ImageHandle())` :90 / `Draw(GPUGraph*, vector<ImageHandle> const&)` :96，`GetWindowHandles()` :104
- 表面方法：`Init(IModuleManager*)` :78、`Initialize(string editorConfigPath, shared_ptr<IWindow>, GPUGraph*)` :79、`UpdateIMGUI()` :84、`LogViewports()` :99、`GetRenderBackend()/GetWindowSystem()` :102-103

### 4.2 RenderBackend + GPUGraph 自定义管线
C# 在游戏线程 build 帧：
- `GPUGraph::AddPass(RenderPass)/Rast`（实为 :607/:608）、`AddPass(ComputeBatch)/Comp`（:609/:610）、`AllocImage/AllocBuffer`（:627/:629，仅 Internal handle，:842-855）、`Present(ImageHandle)` :630（仅 Backbuffer，:632-636）、`Finalize(ImageHandle, ETextureAccessType)` :638
- `RenderPass::New(...)` :271/:280/:298 和 `Name/SafeSetPipelineState/SetShaderInfo/SetParam/SetAttachmentConfig`；`DrawCallBatch::New/...`；`DrawCall::New/SetVertexBuffer/SetIndexBuffer/DrawIndexed/Draw/ViewPort/Scissor`；`ComputeBatch + ComputeDispatch`
- `ShaderStruct` 经 `CRenderBackend::CreateShaderStruct(NameHash)` :71 + `SetValue/SetImage/SetBuffer/SetSampler/SetStruct`
- 提交点：`ExecuteGraph(TaskScheduler*, shared_ptr<GPUGraph> const&)`（CRenderBackend.h:61）。
- Readback：`CRenderBackend::Readback(ImageHandle const&, span<uint8_t>)` :86 / `Readback(BufferHandle const&, span<uint8_t>)` :87 返回 `IReadbackToken`（CRenderBackend.h:49-56），`Wait()` :53（唯一阻塞同步，之后 dst 填充）、`IsReady()` :54、`Reset()` :55。
- **载体尺寸必须来自 `GetImageByteSize(w,h,format)`**（Common.h:274-311，唯一公开来源），**不能** `width*height*GetFormatBlockSize`（default=4 会给未知格式发明 `W*H*4`，:237-259）。读回源必须是**调用方创建的 External texture**，`ETextureAccessType::eTransferSrc|eTransferDst|eRT`（CreateCaptureRT，Main.cpp:100-106）；presented swapchain backbuffer 无 eTransferSrc，不能读回（Main.cpp:97-99）。

### 4.3 ThreadManager 任务提交 —— `thread_management::CThreadManager`
`CThreadManager::NewScheduler()` :114、`AddTaskQueue(NameHash, array_ref<uint32_t>)` :112、`InitializeThreadCount` :111；`TaskScheduler::NewTask()/NewTaskParallelFor()/NewTaskGraph()` :33-35、`Execute(array_ref<TaskBase*>, bool wait=false)` :36、`WaitAll()` :37；`CTask::MainThread()/Thread(NameHash)/DependsOn/WaitOnEvent/SignalEvent/Functor` :48-57，`TaskParallelFor::Functor(function<void(uint32_t)>)/JobCount` :60-77，`CTaskGraph::Func(function<void(TaskScheduler*)>)` :97。
- **障碍**：`CTask::Functor` 收 `castl::function<void()>`（ThreadManager.h:57）——C# 不可寻址的 C++ 类型。shim 须把 managed delegate 编成 `[UnmanagedCallersOnly]` 函数指针（**不**在 struct 里放 Delegate 字段），并用 `GC.KeepAlive` root 到该 job 生命周期。

### 4.4 资源系统 —— `resource_management::ResourceSystemFactory`
`NewImportingSystem()` :11、`NewManagingSystem()` :13、`DeleteImportingSystem/DeleteManagingSystem` :12/:14、共享包装 `NewImportingSystemShared()` :16、`NewManagingSystemShared()` :21。`ResourceManagingSystem`（ResourceManagingSystem.h:13-68）：`Initialize(shared_ptr<IOManager>)` :16、`SetResourceRootPath/GetResourceRootPath/GetResourceFullPath` :18-20，模板 `GetOrLoadResource<TRes>(PathHash)` :28、`GetOrNewResource<TRes>(PathHash, TArgs&&...)` :50、`GetOrNewSubResource<TRes>` :64。（**注**：实际类名是 `ResourceFactory`，ResourceSystemFactory.h:8。）
- 这些是 `PathHash` 键 + `castl::function` 回调返回 `shared_ptr<TRes>` 的模板，**不可直接绑**。绑定须合成非泛型 C-ABI 入口（path 键、返回 opaque resource ID）或 per-TRes 薄 C++ 包装；C# 呈现 `GetOrLoadResource(path) → resource handle`。`ResourceImportingSystem::ScanSourceDirectory(string const&)` :14。

## 5. 绑定生成（用户核心痛点）方案

> **本 §5 是"统一引擎 CodeGen 框架"的一个消费端**：生成器出 `Gen.Bindings.cpp/.cs`（C ABI + C# 包装）。同一个 parser + 类型表还出 `Gen.Reflection`（反射，见 [[002-reflection-via-codegen]]）、`Gen.Serialization` 与 `Gen.Hash+Equality`（哈希/相等，见 [[003-hash-equality-via-codegen]]）。各消费端共享"带标注 C++ 类型 → parser → 类型表"的管线与**同一套 attribute**。

### 5.1 目标
**C# 零手写 extern**。C++ 侧定义接口 + 标注/元数据，CodeGen 自动生成 C# 函数和包装（+ C++ thunk 层）。单一事实来源 = 带标注接口头，每次构建两边重生成，漂移 = build error。

### 5.2 候选路线对比
| 路线 | 机制 | 优点 | 代价 |
|---|---|---|---|
| **复用现有 `CA_PRIVATE_REFLECTION`** | 读引擎已有反射元数据 | 最贴近"C++ 侧标注"；不新造 attribute | **待核实地形**（见 §8）：能否覆盖想暴露的全部方法/类型 |
| **attribute + `[[clang::annotate]]`** | MSVC 下宏空展开、clang 下可读 | 显式白名单 + marshal 提示；非侵入 | 需在接口头加宏；codegen 要 clang |
| **libclang / `clang -ast-dump=json`** | 扫接口头 AST 发两边 | 自动、精确、零反射 | 要写 marshal 规则；是个不小的工具 |
| **沿用现成生成器**（`CppSharp`/`ClangSharp`） | MIT 开源 | 最快到管线 | 继承其取舍；须验证能处理 fluent/span/handle 模式且能在 MSVC/Windows 跑 |

### 5.3 推荐路线（优先序）
1. **先核实现有反射元数据能否驱动**（最省、最贴合诉求）——见 §8 待确认项。
2. 不足则 **attribute（`CA_CLASS/CA_METHOD/CA_HANDLE/CA_PROPERTY`，MSVC 空展开）+ libclang `-ast-dump=json` → python codegen**。
3. headless 急需可先用 `CppSharp`/`ClangSharp` 打底，验证再决定换不换。

### 5.4 硬模式映射规则
| C++ 模式 | 生成器行为 |
|---|---|
| 模板 `GetInstance<T>()` | 关到 C# 侧：绑 `GetInstance(const char*)→void*`，C# 泛型 `T GetService<T>()` 传类型名强转 |
| fluent builder 返 `GPUGraph&`/`RenderPass&` | 当**不转移所有权**的透明 `IntPtr`；C# 生成 IDisposable 包装，方法改内部句柄并 `return this` 链式 |
| `std::span<uint8_t>` | `Span<byte>`/`ReadOnlySpan<byte>`；热路径 pin，非热 `byte[]` 拷贝 |
| 句柄 `ImageHandle/BufferHandle/WindowHandle` | `SafeHandle` 子类（ownsHandle 按归属）；native 侧持托管用 `GCHandle` |
| `enum class` | C# `enum`；flag 加 `[Flags]`；`EMultiSampleCount` 的 `e1=0` 在生成时处理 |
| `castl::string`/`glm::mat4`/数组 | UTF-8 buffer 拷贝 / 定长 struct / 小包装 |
| 回调委托 | `[UnmanagedCallersOnly]` 静态 + delegate 保活（root）+ 释放反注册 |

### 5.5 关键坑
- **行号锚点会漂**：对抗审查发现原稿引 `GPUGraph.h AddPass:760` 实为 `:607`——**codegen 必须读元数据标签，不读行号**。可立即抄走（CACodeGen 也适用）。
- 生成 C# 不能镜像 EASTL 容器值类型（不可 blittable，会逼你手重声明 C# surface）。RenderPass/DrawCallBatch/DrawCall **内容全部经 shim 调用改 opaque graph handle**，不做 managed side-car struct。
- **ABI-schema 漂移测试要可执行**：采样 sizeof()/字段偏移/函数指针表槽顺序+签名/枚举到 C# 值，从 C++ **元数据**生成，跑在 build.py/CI，漂移=硬失败。这是本方案头号维护风险，目前无落地 Guard。

## 6. 所有权与线程拓扑（合并版）

```
                     单一游戏线程
 native 主循环 ─tick─▶ [UnmanagedCallersOnly] C# per-frame callback
     │                        │  build（经 shim 改 opaque graph handle）
     │                        ▼
     │              crossing① build → ExecuteGraph(shared_ptr<GPUGraph>)
     ▼
 [C# 永不上渲染 worker]   crossing② ReadbackToken.Wait→consume → WaitIdle 边界
     │
 ┌───┴─────┬─────────────┐
 JIT host   AOT host        （都【进程内】，共享同一 C ABI + 同一批 C++ 对象指针
 hostfxr    dlopen AOT native 库   ← 不是两个进程）
 热重载      无热重载
```

## 7. 对抗验证结论（诚实记录）

### 7.1 完整性审查判 REVISE
核心站得住（接缝位置、blittable 句柄三元组 ABI、build-time codegen、异常-free 状态通道、对 Stride/.NET8/ExecuteGraph-fanout/gen0-finalizer 的驳回都验证正确），但**两处承重矛盾 / 一处缺口**必须先落：

- **[矛盾 a] 帧循环归属互斥** → 已按 §3.5 修正（native tick 调 per-frame callback）。
- **[矛盾 b] 双通道** → 已按 §3.7-3.8 修正（AOT 必须进程内，否决独立进程）。
- **[缺口 c] Phase 1 缺 Graph 创建/变更路径** → shim 加 `CreateGraph()` 返回不透明句柄；RenderPass/DrawCallBatch/DrawCall 内容全走 shim 调用（见 §5.5）。
- **[缺口 d] Readback token 所有权未映射** → 包成 `SafeHandle(ownsHandle=true)`，`ReleaseHandle` 调 `token->Reset()/delete`；加 status 通道区分"不支持→null token"与"成功"。
- **所有权现在钉死**：读回的外部 `eTransferSrc|eTransferDst|eRT` RT = C# 拥有的 SafeHandle(ownsHandle=true)，WaitIdle 边界释放；`CTask*` = **借用**（属 per-frame scheduler，WaitAll()+delete 释放），C# 不建 SafeHandle；window backbuffer = 借用(ownsHandle=false)，只在 WaitIdle/sync 后消费。

### 7.2 被驳回的 8 条 claim（诚实，不粉饰）
- **Stride 不是先例**：纯 C# 引擎，不能当"C++ 核心 + C# 绑定"样板。真正收敛的事实锚在 Flax + Godot。
- **`LoopFunction` 是 per-thread 循环体**：错，它只是 2 行 setter。
- **"gen0 压力不触发 finalizer → leak"**：技术错（finalizer 在收死对象的代跑）；但"须确定性释放"结论精神仍成立；且点明释放只修 leak 不修 UAF。
- **Godot 两条**：URL 假 / 细节不准（substance 真、URL 假，严格把关驳回）。
- **`.NET 8 hdt_load_assembly`**：bytes 加载器不用 AssemblyDependencyResolver，过度声称。
- **NativeAOT 泛型虚方法限制**：无 URL 无法确认。

### 7.3 引擎已确认的强事实（反哺 CACodeGen）
- 跨模块先例走 `extern "C"` factory；C# 入口须 `extern "C"`。
- 句柄在 ABI 上留紧凑三元组；C# 不拥有，显式 AddRef/Release。
- 帧循环 / worker 线程拓扑真实存在（scheduler 每帧 new、WaitIdle、readback carry）。

## 8. 未决问题（open questions）

1. **`CA_PRIVATE_REFLECTION` 反射真实形状**：能否覆盖想暴露的方法/类型？→ **待确认**（决定 codegen 读现有反射 vs 新 attribute vs clang）。
2. **单提交模型**：跨 batch readback 是否需把引擎改成**单 `vkQueueSubmit` 全批次**（或批间 semaphore）？跨 batch 的 pipeline barrier 只覆盖该 submit，读回跨 batch 不能靠跨批屏障（见记忆 `vulkan-batch-submit-memory-dependency`）。
3. **编辑/发布分离**：发行是否**永不需要 per-file 热重载**？若不需要，编辑 host 和游戏 host 可**进程分离**，从而解 AOT-vs-ALC 张力（靠进程分离而非同进程共存）。
4. **C# 拥有 vs 引擎拥有的资源清单**：C# 是否自己建 External GPUTexture/GPUBuffer 并以 SafeHandle 持有，还是所有 C# 可见资源都是 graph-local/key-triplet borrow（ownsHandle=false）？SafeHandle-owner 路径是最易 double-ownership 处。
5. **持久 IMGUI callback / 读回 buffer 的 rooting 策略**：静态字段 vs GCHandle vs 生命周期作用域——ALC 卸载要求 ALC 外无强 GC handle。
6. **game thread 的 async/await**：`runtime_config` 路径下 managed Task continuation 是否正确未验证；模型是否为纯同步单线程待定。
7. **reload 与进程退出 teardown 的边界**：脚本 ALC 的每帧 native 资源能否保证在 `CAModuleManager` 逆序销毁前释放；reload 是否需要独立于进程退出 teardown 的"drain and release"边界——C# 层能否把跨帧 SafeHandle 持有跨越一次 unload，机制未定义。

## 9. 参考与来源

- **主机序列**：Microsoft "Write a custom .NET runtime host"（hostfxr/nethost）。**hostfxr 函数指针生命周期**：设计文档明说返回的 fnptr "has the lifetime of the process"，"currently there's no way to unload the managed component"。因此热重载用 `load_assembly_and_get_function_pointer` **一次**进入一个固定 bootstrap，再由 bootstrap（全 C#）自建 collectible `AssemblyLoadContext`（`isCollectible:true`）+ `LoadFromAssemblyPath` + `alc.Unload()`（协作式，GC.Collect/WaitForPendingFinalizers 直到 `WeakReference(alc)` 为 null）；`[UnmanagedCallersOnly]` 指针若其定义程序集已卸载则绝不能再调（崩溃）。
- **hosting 禁忌**：不能在 Windows `DllMain/DLL_PROCESS_ATTACH` 里 LoadLibrary/hostfxr/跑 managed（死锁/崩溃）；标准修法 = CreateThread worker 做 init 回传结果。host 上下文句柄**非线程安全**须串行。
- **4 引擎对照**：Flax（C++ 核心 + 生成绑定，`BindingsGenerator` 发 `.Bindings.Gen.cpp/.cs`）+ Godot（C-ABI bridge + ClassDB MethodBind + GCHandle/thunk 身份）为收敛点；**Stride 被剔除**（纯 C#）。
- **相关工具**：`E:\Projects\CACodeGen`（分层 = MetaParser→ParserLibrary(libclang)→CInterfaceCodeGen→CSharpBindingsCodeGen；问题在"值装箱/New/Release"语义模型，见 §10）。
- **workflow 记录**：`openspec/` 无；转存脚本 `design-csharp-scripting-framework.js`（`<repo>/.claude/workflows/scripts/`），输出 journal 在 session transcript 目录。

## 10. 与 CACodeGen 的关系（诊断）

你的 CACodeGen 分层思路正确（C++→C 接口→C# OO），真正的失败在**语义模型**：
- 它把 C++ 对象当"不透明句柄 + 值装箱"（`H_Type`/`New_Type`/`New_Type_Copy`/`Release_Type`）。对真引擎表达不了"从注册表借一个已有对象"（单例服务、fluent builder 返回引用、shared_ptr 句柄），也表达不了"共享所有权"。
- 它没有**跨边界共管 + 失效**桥 → 当 C++ 和 C# 同时持同一共享资源时 double-free/leak；模块卸载时 C# 包装器变悬垂没人管。
- **修正方向**：分层保留；把语义从"New/Release 值装箱"改成"**注册表借对象 + 跨边界引用计数 + 卸载失效通知**"；同时让 CodeGen 从**一次解析**发两层（C 函数表 + C# OO 包装）。绑定到**接口层**（`CRenderBackend` 等），不绑后段私有实现。

## 11. 升级建议

- **当前未达 proposal 水位**（有未决问题 + `CA_PRIVATE_REFLECTION` 未核实）。
- 若推进，先做两件事：
  1. 开一个小的 follow-up workflow/调查：**核实 `CA_PRIVATE_REFLECTION` 真实形状**，定死 codegen 的元数据来源。
  2. 打通一条**纵向切片**（IMGUI 编辑窗口的 C-ABI shim），验证运行时模型（托管 + 线程拓扑 + 所有权），再谈生成器铺开。
- 满足这两点后，才用 `/opsx:propose` 建 OpenSpec change，把本文件升级成 proposal/design/specs/tasks。
