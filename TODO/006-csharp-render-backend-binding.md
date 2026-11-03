# 006 · 渲染后端 C# binding

> **状态**：已升级到 `openspec/changes/csharp-render-backend-triangle/`
> **日期**：2026-11-01
> **关联**：`openspec/changes/csharp-render-backend-triangle/`、`docs/ROADMAP.md`、`TODO/001-csharp-scripting-framework.md`、`TODO/005-web-editor.md`、`Interface/RenderInterface/header/CRenderBackend.h`
> **性质**：设计笔记，不是 OpenSpec 规格，也不构成开工授权。

## 1. 背景与目标

窗口接口的 C# 生成与调用已经打通，下一个核心大类是让 C# 使用渲染后端。用户在 2026-11-01 明确要求：**这个方向优先于网页界面与 C# 编辑器服务方向，在 TODO 索引和路线图中排在前面。**

首个目标不是投影整个 `GPUGraph`，而是形成一个能验证真实渲染结果的纵向切片：C# 创建窗口、关联渲染后端、构建并提交一张简单三角形图，D3D12 与 Vulkan 都跑通。

## 2. 现状核实

- C# 已经能绑定并调用 `IWindow` / `IWindowSystem`。来源：`Scripting/Managed/Host.cs`；逐字引文：“`IWindow created = s_system.NewWindow`”。同一条语句创建窗口，后续代码调用 `GetWindowSize`、`CloseWindow` 和 `Dispose`。
- 既有审计已经把 `CreateGPUBuffer`、`CreateGPUTexture`、`CreateShaderStruct`、`Readback`、`WaitIdle` 判为可绑，并给出句柄、镜像与配对释放的投影建议。来源：`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`；此处是转述、非逐字引文。
- `ExecuteGraph` 不能原样暴露给 C#。来源：`Interface/RenderInterface/header/CRenderBackend.h`；逐字引文：“`virtual void ExecuteGraph(TaskScheduler* scheduler, castl::shared_ptr<GPUGraph> const& graph) = 0;`”。裸指针的线程归属与图的跨边界所有权都需要重新设计。
- `GetWindowHandle` 当前接收 `castl::shared_ptr<cawindow::IWindow>`，需要改成能消费现有窗口句柄的投影形状。此处是转述、非逐字引文。
- `CRenderBackend` 类声明目前没有 `CA_BIND`；已读整个 `Interface/RenderInterface/header/CRenderBackend.h` 确认。新增绑定面时需要同步可绑定面审计，不能只加标记让生成器碰巧出码。
- 新加的 GPU 计时入口是非纯虚方法，不在旧的纯虚方法审计范围内。普通图提交打通之前，不把计时绑定纳入首个切片。

## 3. 关键决策轴与结论

### 3.1 以真实三角形为终点，不以“生成了多少方法”为终点

生成绑定只是手段。首个验收必须证明 C# 的调用真正到达两个原生后端，并产生可以核对的渲染输出；只检查函数表或生成文本不算跑通。

### 3.2 图与资源走句柄，调度器留在引擎内部

首版需要建图与提交的句柄化接口，不让 C# 接触 `TaskScheduler*` 或 `castl::shared_ptr<GPUGraph>`。C# 只释放自己拥有的资源句柄，不暴露 `CRenderBackend::Release`，后端销毁仍由引擎模块生命周期管理。

### 3.3 只投影首个切片所需的图能力

首版只考虑窗口附件、顶点数据、一个渲染 pass、一次绘制和提交。完整计算图、异步队列、全部资源类型与 GPU 计时绑定留到普通图提交跑通之后。

## 4. 候选纵向切片

1. C# 使用现有窗口表面创建窗口，并取得可交给渲染后端的窗口句柄。
2. C# 创建首个切片所需的资源，通过最小建图接口描述简单三角形。
3. C# 提交图，引擎内部选择调度器并执行。
4. 在 D3D12 与 Vulkan 上分别检查真实执行结果；需要读回时，使用既有回读能力核对输出，不以“没有崩溃”代替渲染判据。

## 5. 被排除的首版方案

- **原样投影 `ExecuteGraph`**：会把调度器裸指针和图智能指针的所有权问题带进 C#，既有审计也建议改成建图与提交句柄。
- **向 C# 暴露 `Release`**：后端销毁不属于脚本权限范围。
- **首版覆盖整个 `GPUGraph`**：会同时引入资源、依赖、队列和生命周期问题，无法先用一个小结果验证核心通路。

## 6. 对抗验证结论

尚未进行对抗验证。本笔记只记录方向、现状和候选切片，尚未达到 proposal 水位。

## 7. 未决问题

- 最小建图接口的具体类型、句柄所有权与失效规则。
- 现有 `IWindow` 句柄如何安全转换为 `GetWindowHandle` 所需输入。
- 顶点数据上载与资源句柄如何进入建图描述。
- 双后端验收采用可见结果、回读结果还是两者组合，以及各自能失败的判据。
- 可绑定面审计需要新增哪些条目，旧的“不可绑”结论哪些可以凭窗口句柄基础重新裁定。

## 8. 升级建议

先解决上述接口形状与验收决策，再创建 OpenSpec proposal。首个 change 应限定为“C# 创建窗口并提交简单三角形图，双后端验证通过”，不得在 proposal 未获批准前开始实现。
