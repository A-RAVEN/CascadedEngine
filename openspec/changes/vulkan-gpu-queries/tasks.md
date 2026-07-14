## 1. VulkanGPUQueryManager 基础架构

- [ ] 1.1 新建 `VulkanGPUQueryManager.h/.cpp`，继承 `VulkanSubobjectBase`
- [ ] 1.2 实现 `CreateTimestampPool(maxCount)` —— `vk::QueryPool`（`VK_QUERY_TYPE_TIMESTAMP`）
- [ ] 1.3 实现 `CreateOcclusionPool(maxCount)` —— `VK_QUERY_TYPE_OCCLUSION`
- [ ] 1.4 实现 `CreatePipelineStatsPool(maxCount)` —— `VK_QUERY_TYPE_PIPELINE_STATISTICS`（需 feature 检查）

## 2. Timestamp 查询

- [ ] 2.1 在 `RecordRenderPass` 前后和 `RecordComputePass` 前后插入 `vkCmdWriteTimestamp`
- [ ] 2.2 实现 `ResolveTimestamps(dstBuffer)` —— `vkCmdCopyQueryPoolResults` 回读到 host-visible buffer
- [ ] 2.3 实现 `ReadTimestampData()` —— 从回读 buffer 解析 GPU ticks 并转换为时间

## 3. 与 Profiling 层集成

- [ ] 3.1 在 `VulkanGraphExecutor::CompileAndExecute` 的 profiling 输出中使用 timestamp 数据替代 CPU 端 `std::chrono` 计时
- [ ] 3.2 暴露 GPU timestamp 数据供上层 profiling 系统使用

## 4. Occlusion Queries

- [ ] 4.1 支持 `vkCmdBeginQuery`/`vkCmdEndQuery` 包围 draw call
- [ ] 4.2 回读 occlusion 结果（pass/fail count）

## 5. 编译验证

- [ ] 5.1 运行 `build.py`，验证 BUILD SUCCESSFUL
- [ ] 5.2 确认 `VK_QUERY_TYPE_TIMESTAMP` 在 target GPU 上可用（几乎所有 GPU 支持）

## 6. 不在此 Change 范围

- Pipeline Statistics 查询（依赖 `pipelineStatisticsQuery` feature，非所有 GPU 支持）
- GPU Crash 诊断 breadcrumb（AMD/NV vendor extension）
