# csharp-render-backend-triangle

## Why

生成器与句柄基础能力已经归档，但 C# 仍只能创建和操作窗口，不能关联渲染后端、构建图或提交绘制。`CRenderBackend::ExecuteGraph` 暴露 `TaskScheduler*`，`GPUGraph` 又包含具体建图类、容器和链式返回，不能直接整类绑定；现在需要一个窄适配面，把首个真实渲染结果走通，而不是继续只验证生成文本。

## What Changes

- 新增脚本专用窄渲染适配面：C# 能取得窗口渲染目标、创建离屏验证目标和图对象，提供顶点字节、顶点布局、着色器路径、清除颜色与绘制次数，构建并提交简单三角形。
- 原生适配实现继续使用现有 `GPUGraph`、`CRenderBackend` 和窗口句柄；每次提交由原生侧创建调度器。脚本不得接触 `TaskScheduler*`、`CRenderBackend::Release`、具体建图类或原生资源指针。
- 图、窗口目标和离屏目标使用现有强句柄与目标类型解析规则；智能指针入参重建真实对象。提交期间由原生适配器保活图及其资源，等待完成后允许释放，已释放或错误类型句柄不得进入原生实现。
- 顶点数据使用现有 `byte[]` 同步复制投影，原生图持有复制后的字节；缓冲描述、名称、路径和其他参数使用已归档的绑定能力，不为本切片另造生成器形状。
- `CSharpHost` 登记渲染适配接口，并在手写宿主表末尾追加渲染接口编号，托管侧按编号取得函数表。公开 C# 示例实际构建并提交三角形，不只是委托签名或日志声明。
- 新增 `TestCSharpTriangle`：在 D3D12 与 Vulkan 上执行由 C# 构建的图，原生侧读取独立离屏目标，逐像素检查清除色和三角形实色，并用改变顶点颜色证明插值结果来自脚本数据。空画面、跳过绘制、错误顶点数据或错误目标都必须让测试失败。
- 扩充可绑定面审计、生成物检查、适配器单元测试、宿主与脚本说明及测试清单，保持现有窗口、回调和双后端回归通过。

## Non-goals

- 不完整绑定 `CRenderBackend`、`GPUGraph`、具体建图类、静态工厂、模板方法或链式返回，也不向 C# 暴露调度器、原生指针或后端销毁权限。
- 不实现通用材质、计算图、多队列、异步提交、通用多 pass 依赖、纹理采样、着色器参数、GPU 计时或完整资源框架。
- 不实现 `std::span`、托管回读或跨调用异步字节缓冲区；离屏像素由原生测试通过既有 `Readback` 读取，不经过 C#。
- 不改变既有 `GPUGraph` 执行语义、原生线程调度策略或函数表 ABI 版本；不为本切片修改生成器转换规则。
- 不实现相机、网格、场景、ECS、网页编辑器或后续可复用 C# 渲染框架。

## Capabilities

### New Capabilities

- `csharp-rendering`：脚本专用窄渲染适配面、图与目标资源生命周期、同步建图和提交，以及双后端真实像素验收。

### Modified Capabilities

- `csharp-host`：宿主登记表与手写 ABI 增加渲染接口接线，托管启动、关闭和重绑必须覆盖新增渲染表面。

## Impact

- 适配接口与实现：`Interface/RenderInterface/header/ScriptRenderBridge.h`、`Interface/RenderInterface/private/ScriptRenderBridge.*`、对应 CMake 与 `build.py` 绑定清单。
- 宿主与托管：`CSharpHost/private/CSharpHost.cpp`、`CSharpHost/CMakeLists.txt`、`CSharpHost/README.md`、`Scripting/Managed/EngineApi.cs`、`Host.cs`、`UserScript.cs`、`SurfaceSignatureCheck.cs` 与布局检查。
- 审计与生成判据：`Tools/CodeGen/check_bindable_audit.py`、`Tools/CodeGen/check_bindings.py`、`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`（当前唯一审计表）。
- 测试与文档：`Test/CoreTests/ScriptRenderBridgeTest.cpp`、`Test/CoreTests/SerializerTest.cpp`、`Test/GPUBackendTester/Main.cpp`、`docs/testing.md`、本 change 的 `evidence/`。
- 明确依赖：已归档的 `csharp-render-binding-foundations`；不引入新的外部依赖，不修改 D3D12 / Vulkan 后端实现。
