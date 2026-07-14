## Why

Vulkan 后端完全缺少 GPU 查询（Queries）体系。D3D12 通过 `D3D12_QUERY_HEAP` + `EndQuery`/`ResolveQueryData` 实现了 Timestamp（GPU profiling）、Occlusion、Pipeline Statistics 等查询。这些是 GPU 性能分析和调试的基础设施，任何需要 profiling 或 occlusion culling 的场景都依赖它们。

当前 Vulkan 后端无 `vk::QueryPool`、无 `vkCmdWriteTimestamp`/`vkCmdBeginQuery`/`vkCmdEndQuery`、无 `vkCmdCopyQueryPoolResults` 调用。

## What Changes

- **Timestamp Queries**: 创建 `vk::QueryPool`（`VK_QUERY_TYPE_TIMESTAMP`），在 render pass 前后插入 `vkCmdWriteTimestamp`，通过 `vkCmdCopyQueryPoolResults` 回读到 CPU 可见 buffer
- **Occlusion Queries**: `VK_QUERY_TYPE_OCCLUSION` + `vkCmdBeginQuery`/`vkCmdEndQuery`
- **Pipeline Statistics**: `VK_QUERY_TYPE_PIPELINE_STATISTICS`（需 `pipelineStatisticsQuery` feature）
- **GPU Profiling 集成**: 将 timestamp 数据暴露给引擎的 profiling 层

## Capabilities

### New Capabilities
- `vulkan-gpu-queries`: Vulkan GPU 查询体系——Timestamp、Occlusion、Pipeline Statistics

## Impact

- 新增 `VulkanGPUQueryManager` 类（query pool 创建/管理/回读）
- `VulkanGraphExecutor` — 插入 `vkCmdWriteTimestamp` 调用点
- `RenderBackend_Vulkan` — 集成 QueryManager
- 对应 D3D12 的 `GPUQueryHeap` / `D3D12_QUERY_HEAP_TYPE`
