## Why

`CRenderBackend` 现在只有提交图、创建资源和读回，没有 GPU 计时入口；`Interface/` 里搜索 `Query`、`Timestamp`、`Occlusion`、`Profiling` 是零命中。两个后端也都没有查询设施：`D3D12RenderBackend/` 里唯一命中 `Query` 的是 COM 的 `QueryInterface`，没有 `QueryHeap` / `EndQuery` / `ResolveQueryData`；Vulkan 后端没有 `vk::QueryPool`。

原工件写成「D3D12 已实现、Vulkan 补齐」，与代码事实不符。实际是两个后端都没有，得先补一条公共的、可测试的图执行计时通路：一次图提交后，能回答每个渲染 pass（图里的一次绘制阶段）和计算 pass 在 GPU 上花了多久。

本 change 的目录名仍叫 `vulkan-gpu-queries`，能力名改成 `gpu-graph-timing`。

## What Changes

- **公共接口**：新增 `Interface/RenderInterface/header/GPUTiming.h`，定义每队列的计时能力、单条 pass 的 GPU 时间、一次图执行的结果对象。结果对象提供 `IsReady()` / `Wait()` / `Passes()`，不暴露 `vk::QueryPool` 或 `ID3D12QueryHeap`。
- **后端入口**：`CRenderBackend` 新增 `GetTimingCapabilities(queue)` 与 `ExecuteGraphTimed(graph)`；原 `ExecuteGraph` 保持不变，普通渲染不承担查询开销。
- **D3D12 实现**：按图实际使用的 direct / compute 队列创建时间戳查询堆和回读缓冲，由结果对象持有；未使用的队列不分配。每个 pass 的起止记录在实际命令列表上，关闭命令列表前解析。
- **Vulkan 实现**：按图实际使用的队列创建 `vk::QueryPool` 和回读缓冲，由结果对象持有；未使用的队列不分配。查询先初始化/重置，复制放在 render pass 外，并显式保证结果可用后才让 CPU 读取。
- **结果定位**：每条结果带 pass 类型、该类型内的原始序号、实际队列和名称；批次重排不影响定位。渲染 pass 复用现有 `RenderPass::GetName()`，计算 pass 用 `compute[<序号>]` 兜底。
- **测试**：`GPUBackendTester` 新增 `TestGPUTiming`，覆盖异步计算、direct 回退、双后端 pass 键一致，以及“读取结果后再执行另一张图，原结果逐字段不变”。

## Capabilities

### New Capabilities

- `gpu-graph-timing`: 一次图执行的逐 pass GPU 计时、能力查询与结果生命周期。

### Modified Capabilities

- 无。

## Non-goals

- **不做遮挡查询（Occlusion）**：它是逐绘制调用的“有多少像素通过深度测试”，粒度和消费者都与 pass 时间不同，另开 change。
- **不做管线统计（Pipeline Statistics）**：顶点数、片元调用数这类硬件计数器需要 `pipelineStatisticsQuery` 特性，也另开。
- **不改 profiling 面板**：`catimer::TimerSystem` 现在只有 CPU 高精度时钟事件，GPU 历史与轨道需要单独设计，不能把 GPU 纳秒冒充成 CPU `time_point`。
- **不做 CPU / GPU 时钟校准**：本次只保证每条 pass 的时长和同队列内的时间先后；跨队列绝对时间与 CPU 对齐另开。
- **不做逐绘制调用计时**：本次粒度是图里的 pass。
- **不改 `ExecuteGraph` 的现有行为**：普通路径不创建查询对象、不等待回读。

## Impact

- 新增 `Interface/RenderInterface/header/GPUTiming.h`。
- `Interface/RenderInterface/header/CRenderBackend.h` 和两个后端实现新增计时入口。
- `Interface/RenderInterface/header/GPUGraph.h` 不需要新增查询对象；渲染 pass 复用现有 `Name()`，计算 pass 用序号兜底。
- `D3D12RenderBackend/private/GPUGraph/GPUGraphExecutor.cpp` 与 `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` 增加 pass 记录与结果解析。
- 两个后端各新增一个查询管理对象，文件名在实现时定；结果对象持有原生查询资源和 CPU 回读数据。
- `Test/GPUBackendTester/Main.cpp` 增加 `TestGPUTiming`；`docs/testing.md` 更新测试数与最小验证集合；新增 `Tools/check_gpu_timing.py`，守住普通 `ExecuteGraph` 不创建计时资源。
- `catimer::TimerSystem` 本次不改；profiling 面板接入留到后续 change。
