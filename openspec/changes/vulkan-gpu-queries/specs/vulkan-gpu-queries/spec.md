## ADDED Requirements

### Requirement: Timestamp 查询必须能创建、写入并回读

Vulkan 后端的查询管理器 SHALL 创建 `VK_QUERY_TYPE_TIMESTAMP` 的 `vk::QueryPool`，在 render pass 前后插入 `vkCmdWriteTimestamp`，并通过 `vkCmdCopyQueryPoolResults` 把时间戳回读到 CPU 可见的 buffer。

#### Scenario: 一帧的时间戳从查询池回读到 CPU

- **WHEN** 一帧在 render pass 前后各写入一次 timestamp，随后触发一次回读
- **THEN** CPU 可见 buffer 里拿到这两个时间戳的值（不是未初始化数据）

### Requirement: Occlusion 查询必须支持 begin/end 区间

查询管理器 SHALL 支持 `VK_QUERY_TYPE_OCCLUSION`，并用 `vkCmdBeginQuery` / `vkCmdEndQuery` 划定统计区间。

#### Scenario: 遮挡查询的区间被记录并可回读

- **WHEN** 对一段绘制调用调用 `vkCmdBeginQuery` 与 `vkCmdEndQuery`，随后回读该查询索引
- **THEN** 拿到这段区间的采样通过数

### Requirement: Pipeline Statistics 查询以设备特性为前提

查询管理器 SHALL 支持 `VK_QUERY_TYPE_PIPELINE_STATISTICS`，但只有在设备支持 `pipelineStatisticsQuery` 特性时才创建该类型的查询池。

#### Scenario: 设备不支持时不创建该类型的查询池

- **WHEN** 设备不支持 `pipelineStatisticsQuery` 特性
- **THEN** 请求 pipeline statistics 查询不会创建查询池，调用方拿到明确的"不支持"结果（不静默返回成功）

### Requirement: timestamp 数据必须交给引擎的 profiling 层

查询管理器 SHALL 把回读到的 timestamp 数据暴露给引擎的 profiling 层。

#### Scenario: profiling 层能取到刚回读的那一帧时间戳

- **WHEN** 一帧结束、timestamp 已回读之后，profiling 层请求这一帧的时间戳
- **THEN** 拿到的是这次回读出来的值，不是空值或上一次的残留
