# csharp-render-backend-triangle 设计

## Context

`csharp-render-binding-foundations` 已提供公开记录、文本与路径值、同步字节数组、跨接口目标句柄及 `shared_ptr` 入参重建。本 change 复用这些能力，不再修改生成器。

已核对的现状：

- `Interface/RenderInterface/header/CRenderBackend.h` 的提交签名是“`virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) = 0;`”，调度器裸指针不能进入公开脚本表面。
- `Interface/RenderInterface/header/GPUGraph.h` 的具体建图类包含容器、智能指针和链式返回；`DrawCall`、`DrawCallBatch`、`RenderPass` 与 `GPUGraph` 都不是纯虚窄接口。
- `Test/GPUBackendTester/Main.cpp` 的 `TestSimpleTriangle` 已给出最小绘制路径：命名内部缓冲、`GPUBufferDescriptor::Create`、`ScheduleData` 复制字节、两个 `float3` 顶点属性、一个 `RenderPass`、一次 `Draw` 和 `Present`；像素验证使用独立离屏目标，而不是交换链后台缓冲。
- 同一份测试的原生复制最终进入 `GPUGraph::ScheduleData` 的“`m_DataHolder.AddData(data, size)`”，因此同步调用返回后不再引用托管数组。
- `CSharpHost/private/CSharpHost.cpp` 与 `Scripting/Managed/EngineApi.cs` 当前是手写 ABI v5，只下发窗口两个接口编号；生成接口表本身仍使用版本 2，本 change 不提升生成表版本。
- `Scripting/Managed/Host.cs` 已在主线程执行绑定、脚本调用和关闭清理。新渲染调用继续沿这条主线程路径，不能把等待或提交转移到脚本线程。
- `Tools/CodeGen/check_bindable_audit.py` 只认 `openspec/changes/**/evidence/bindable-audit.md` 中的唯一审计表；新增生产接口必须逐方法进入该表，不能只加 `CA_BIND`。

## Goals / Non-Goals

**Goals:**

- C# 用公开记录、字符串、字节数组和资源表面对象构建并提交简单三角形。
- 原生适配层负责具体 `GPUGraph` 构件、调度器创建、执行和资源保活，两个现有后端实现不变。
- 原生测试读取 C# 绘制产生的离屏结果，在 D3D12 与 Vulkan 上分别验证真实像素。
- 生命周期、错误路径、宿主 ABI、关闭清理和旧窗口行为都有可失败的判据。

**Non-Goals:**

- 不公开完整 `CRenderBackend` / `GPUGraph`，不实现通用图框架、计算、异步队列、通用多 pass、材质或着色器参数。
- 不实现托管回读、`std::span`、异步字节生命周期、GPU 计时或新生成器形状。
- 不修改 D3D12 / Vulkan 后端，不公开 `TaskScheduler*`、`CRenderBackend::Release` 或原生资源地址。

## Decisions

### 一、只增加三个窄纯虚接口

在 `Interface/RenderInterface/header/ScriptRenderBridge.h` 声明并标记 `CA_BIND`：

- `IScriptRenderTarget`
  - `uint32_t GetWidth() const`
  - `uint32_t GetHeight() const`
  - `bool IsBackbuffer() const`
- `IScriptRenderGraph`
  - `int32_t AddBuffer(cacore::NameHash const& name, GPUBufferDescriptor const& descriptor, castl::vector<uint8_t> const& data)`
  - `int32_t SetPositionColorStream(cacore::NameHash const& streamName, uint32_t stride, uint32_t positionOffset, uint32_t colorOffset)`
  - `int32_t AddTrianglePass(castl::shared_ptr<IScriptRenderTarget> const& target, cacore::PathHash const& shaderPath, cacore::NameHash const& streamName, cacore::NameHash const& bufferName, uint32_t vertexCount, float red, float green, float blue, float alpha)`
  - `int32_t Present(castl::shared_ptr<IScriptRenderTarget> const& target)`
  - `uint32_t GetPassCount() const`
  - `castl::string GetLastError() const`
- `IScriptRenderBackend`
  - `castl::shared_ptr<IScriptRenderTarget> GetWindowTarget(castl::shared_ptr<cawindow::IWindow> const& window)`
  - `castl::shared_ptr<IScriptRenderTarget> CreateCaptureTarget(uint32_t width, uint32_t height)`
  - `castl::shared_ptr<IScriptRenderGraph> CreateGraph()`
  - `int32_t SubmitGraph(castl::shared_ptr<IScriptRenderGraph> const& graph)`
  - `void WaitIdle()`
  - `castl::string GetLastError() const`

这些形状只使用已支持的定宽标量、公开记录、文本/路径值、同步字节和接口智能指针。`AddTrianglePass` 的较长参数列表同时验证统一转换描述能组合所有已支持形状；任何新增容器、回调或具体建图类型都先缩小接口，不改生成器。

可失败的建图与提交方法用 `int32_t` 返回状态，0 表示成功、非 0 表示拒绝；对应对象的 `GetLastError()` 返回包含参数名的错误文本，成功调用先清空旧错误。这样错误不会穿过原生异常边界，也不依赖生成物内部的静态错误写入函数。C# 必须检查返回值，不能因方法不抛异常就把拒绝当成成功。

`AddTrianglePass` 是窄绘制描述，不是 C++ 代为决定整个三角形：调用方仍提供目标、着色器、流名、缓冲名、顶点数和清除色。首版只内置 `POSITION` 与 `COLOR` 两个 `float3` 语义，因为当前着色器明确使用这两个语义；它不扩展成通用顶点布局系统。

**备选方案**：直接标记 `CRenderBackend` / `GPUGraph`，或提供单个 `DrawTriangle()`。前者会把裸指针、容器和链式类型带过边界，后者会让 C++ 代替 C# 建图，均不能证明本 change 的目标。

### 二、原生建图保持同步、可复制和可检查

`CScriptRenderGraph` 持有一份 `castl::shared_ptr<GPUGraph>`，以及本图已登记缓冲和顶点流的描述：

- `AddBuffer` 先检查名称唯一、描述中的字节数与数组长度一致，再调用 `AllocBuffer` 和以 `copy=true` 的 `ScheduleData`。原生上传持有区立即取得字节副本；单元测试在调用后修改输入数组，必须读不到变化。
- `SetPositionColorStream` 建立两个 `VertexAttribute`：`POSITION` 与 `COLOR` 都为 `eR32G32B32_SFloat`。两个属性各是 12 字节，来源是 `float3`；示例步长 24、颜色偏移 12 与 `TestSimpleTriangle` 一致。任一 `offset + 12` 超出步长即拒绝。
- `AddTrianglePass` 检查目标、流名、缓冲名与 `vertexCount`，并确保绘制数不超过对应缓冲描述的元素数，再用 `RenderPass::New`、`SetShaderInfo`、`VertexStream`、`SetVertexBuffer` 和 `Draw` 构造一个 pass。
- `Present` 只接受后台缓冲目标并写入 `GPUGraph` 的呈现清单；本切片不另设呈现目标数量上限，现有 `GPUGraph::Present` 可以保存多个目标，但示例只使用一个窗口。`GetPassCount` 供原生和托管行为断言，不作为“生成即成功”的像素替代判据。
- `CreateCaptureTarget` 固定使用当前原生验证路径的 `E_R8G8B8A8_UNORM`、二维、单层、单 mip、单采样和 `eTransferSrc | eTransferDst | eRT`，尺寸必须非零。`GetWindowTarget` 同时保存窗口描述快照，原生测试用它核对离屏目标的宽、高和格式；尺寸不一致必须失败，不能只检查像素颜色。

所有失败在进入具体后端前给出错误；重复名称、空目标、错误布局、越界绘制、非后台缓冲呈现和空图提交不得只记录日志后仍继续执行。

### 三、提交保活到等待结束，资源释放沿用目标句柄

`CScriptRenderBackend` 以非拥有指针读取模块管理器中的 `CRenderBackend` 与 `CThreadManager`，自己拥有适配状态。`SubmitGraph` 在主线程内调用 `NewScheduler()` 和 `ExecuteGraph`，并把图的强引用放入待完成集合；`WaitIdle` 调用后端等待后才清空集合。图内部的目标与上传数据继续由 `GPUGraph` 持有。适配器另记录 `WaitIdle` 调用次数与最近一次提交的弱图引用，原生测试必须同时核对等待确实发生、待完成集合已清理以及真实呈现清单，不能只依赖后端可能已有的内部同步。

因此 C# 可以在 `SubmitGraph` 后释放自己的图表面对象，但不能用释放代替 GPU 等待；最终像素读取前必须调用 `WaitIdle`。像素测试期间，脚本保留图、窗口目标和离屏目标，供原生测试核对真实图结构、呈现清单和描述快照，托管 `Shutdown` 再释放；适配器只保存测试钩子的弱引用，不额外无限期持有。

`GetWindowTarget` 通过现有窗口智能指针入参取得真实窗口，再调用 `GetWindowHandle`。强返回的图与目标句柄使用目标类型解析；错误类型、失效句柄和已释放表面对象不得进入实现。

### 四、宿主手写 ABI 只追加字段，目标接口允许无默认实例

`EngineApi` 末尾追加 `script_render_backend_interface_id`、`script_render_graph_interface_id`、`script_render_target_interface_id`，手写 ABI 从 v5 升到 v6。编号使用现有 1、2 之后的 3、4、5；大小由两侧 `sizeof` 实测，不手写常量。生成接口表继续是版本 2。

`CSharpHost` 链接 `Rendering` 与线程管理接口，在模块链接阶段用模块管理器中的真实后端和线程管理器创建适配器，并以默认实例登记接口 3。接口 4、5 是目标类型表，不需要默认实例：`OnGetInterfaceApi` 对这两类表允许 `instanceOut == nullptr`，返回的图或目标仍通过各自 `ResolveTargetHandle` 取得真实对象。缺少后端依赖时接口 3 不登记且保留明确错误；只有渲染探针要求该接口，现有窗口脚本不因此静默降级或被迫渲染。

`Host.Boot` 核对新增三个接口的布局清单；`EnsureBound` 在调用用户脚本前绑定渲染后端和两个目标表。`Shutdown` 先释放脚本持有的离屏目标、图和后端表面，再让原生适配器随宿主生命周期销毁。

### 五、C# 示例构建真实图，运行时开关只控制探针

`UserScript` 增加独立于 `OnEngineReady` 的 `OnRenderProbe`。`Host` 只在 `CASCADED_CSHARP_RENDER_PROBE=1` 且三个渲染表已绑好时调用它，并使用独立的探针调用标记；进程首次 `OnEngineReady` 不再消耗这次渲染机会。`TestCSharpTriangle` 在自己的 Tick 前设置开关并负责清理，其他窗口测试不设置开关，因此全量运行不会提前触发或漏掉渲染探针。正常示例顺序：

1. 用现有 `IWindowSystem.NewWindow` 创建窗口并取得窗口目标。
2. 创建同尺寸离屏目标和图。
3. 写入三个顶点的字节，每个顶点包含位置与颜色两个 `float3`；示例步长 24、位置偏移 0、颜色偏移 12。
4. 调用 `AddBuffer`、`SetPositionColorStream`、窗口目标的 `AddTrianglePass` 和 `Present`。
5. 用完全相同的逻辑绘制描述再调用一次离屏目标的 `AddTrianglePass`，与现有 `TestSimpleTriangle` 的验证镜像一致；这只是同一绘制的验证副本，不形成通用多 pass 能力。
6. 提交并 `WaitIdle`，保留图、窗口目标和离屏目标给原生测试核对，托管 `Shutdown` 再统一释放。

`CASCADED_CSHARP_RENDER_SELFTEST` 在 Debug 提供 `skip-draw`、`bad-vertex`、`skip-present` 与 `skip-wait`。`skip-draw` 省略窗口与离屏目标的两次 `AddTrianglePass`，使图 pass 数为 0；原生测试必须把它与正常模式的 2 个 pass 对比，以 `E_TRIANGLE_DRAW` 判红，不得用零面积三角形代替省略调用。其余模式分别改变一个顶点颜色、省略呈现和省略等待，用来证明像素、图结构和调用计数判据都会变红。生产脚本不依赖这些开关。

### 六、真实像素判据必须能区分背景、三角形和错误顶点

`TestCSharpTriangle` 在两个后端分别执行正常探针，取得适配器弱引用对应的离屏 `ImageHandle`，通过既有 `Readback` 和 `Wait` 读取 RGBA8 字节：

- 清除色使用 `(0, 1, 0, 1)`，背景像素必须精确为 `(0, 255, 0, 255)`，避免半值量化歧义。
- 三个顶点位置沿用现有简单三角形的平面坐标，深度统一为 0；三个顶点颜色都为红色。屏幕中心位于三角形内部，正常结果必须精确为 `(255, 0, 0, 255)`。实色顶点使判据不依赖窗口尺寸、像素角点或插值容差。
- 同时用窗口描述快照核对离屏目标的宽、高和格式，并核对真实 `GPUGraph` 的呈现清单包含窗口后台缓冲、`WaitIdle` 调用次数增加且待完成集合已清理，禁止只凭任意非零字节通过。
- C++ 测试与 C# 示例分别写独立夹具值，不共享一个可被同时改错的常量表。`skip-draw` 必须通过上述 `E_TRIANGLE_DRAW` 结构判据失败，不得对没有绘制和清除的离屏目标编造绿色像素期望；`bad-vertex` 把一个顶点改为蓝色后必须让中心不再是纯红而失败；`skip-present` 与 `skip-wait` 分别由图结构和调用计数判红。两个后端都运行正常与负向探针。
- `Tools/check_csharp_triangle.py` 检查正常报告为 `pass`、负向报告为 `fail` 及对应错误码，保存本次日志。PNG 仅作为辅助证据，不能代替字节断言。

### 七、审计、单元测试和文档与实现同步

三个接口的每条纯虚方法都加入当前唯一 `bindable-audit.md` 与 `INTERFACES`。`build.py` 只增加新主头和期望产物前缀，不新增转换矩阵。`check_bindable_audit.py` 同时拒绝重复审计行，并用头文件实算方法数与接口数核对表内汇总，防止只增行数而留下旧合计。`check_bindings.py` 继续用真实接口无桩、两侧方法对齐和公开表面判据检查新产物；`GPUBufferDescriptor` 登记为共享公开记录，两个 `uint64_t` 字段允许使用 C# `ulong`，但公开方法中的原生句柄和其他未登记 `ulong` 仍按原判据拒绝。

`Test/CoreTests/ScriptRenderBridgeTest.cpp` 检查原生图结构、复制后的字节、两个属性、绘制次数、清除色、呈现清单、等待调用和全部参数拒绝路径；`TestCSharpTriangle` 检查真实模块、宿主接线、托管调用和双后端像素。`CSharpHost/README.md` 更新手写 ABI 与编号表，`docs/testing.md` 更新文件数、测试数、命令和红态方法，`TODO/006-csharp-render-backend-binding.md` 标为已升级，Roadmap 只标记本 change 已建立。

## Risks / Trade-offs

- **窄接口仍可能退化成 C++ 代画** → 参数必须来自 C#；原生结构断言和 `bad-vertex` 证明顶点字节确实影响输出。
- **提交后资源提前释放** → 原生待完成集合持有图强引用，`WaitIdle` 后清理；离屏目标由脚本保留到像素读取。
- **目标接口没有默认实例导致取表失败** → 只对目标类型表允许空实例，后端接口仍必须有真实适配器；新增取表测试覆盖两种路径。
- **两个后端坐标方向或量化不同** → 背景选角落、三角形使用三个实色顶点，中心期望精确红色，不依赖插值容差；`bad-vertex` 仍证明插值来自脚本数据。
- **验证镜像被误当成通用多 pass** → 只重复同一逻辑绘制到离屏目标，接口不支持 pass 依赖、计算或通用附件组合。
- **宿主新增依赖影响现有窗口测试** → 渲染探针显式启用；普通窗口路径不提交 GPU 图，完整旧回归必须重跑。

## Migration Plan

1. 先增加适配接口、审计行、原生结构测试与未实现红态证据。
2. 实现原生建图和资源生命周期，生成三份接口产物并通过 CoreTests 与绑定检查。
3. 追加宿主 ABI、登记适配器并完成托管绑定与关闭清理。
4. 接入 C# 示例和 `TestCSharpTriangle`，先证明 `skip-draw` / `bad-vertex` 变红，再取得双后端正常像素。
5. 更新文档并运行完整 CoreTests、绑定检查、窗口回调、两个后端全套测试和最终 Debug 构建。
6. 若需撤回，必须同时撤回三个接口、宿主 v6 字段、托管绑定、脚本和测试，重新生成全部产物；不得混用 v5/v6 手写宿主表或新旧目标句柄。

## Open Questions

本 change 范围内没有未决范围决策。通用着色器参数、更多顶点格式、异步提交、托管回读和后续可复用渲染框架均留给后续 change，不影响本切片的验收。
