## Context

D3D12 使用 `ID3D12QueryHeap` + `EndQuery`/`ResolveQueryData`。Vulkan 对应 `vk::QueryPool` + `vkCmdWriteTimestamp`/`vkCmdCopyQueryPoolResults`。

## Goals / Non-Goals

**Goals:** Timestamp 查询 + Occlusion 查询 + 结果回读 + Profiling 层集成

**Non-Goals:** Pipeline Statistics（需 feature 检查）、Breadcrumb debugging、Performance Query（VK_KHR_performance_query）

## Decisions

### D1: Query Pool 大小

每帧预分配固定数量 timestamp slot（如 64），覆盖所有 render/compute pass。不足时动态扩展。

### D2: 回读延迟

Timestamp 结果有 1-2 帧延迟（GPU 异步执行）。使用 ring buffer 存储，在第 N+2 帧时读取第 N 帧的数据。

### D3: GPU Timestamp → 时间转换

使用 `VkPhysicalDeviceLimits::timestampPeriod`（nanoseconds per tick）。

## Risks

- `timestampValidBits` 可能非全 64-bit → 处理 wrap-around
- Integrated GPU 的 `timestampPeriod` 可能较大（精度低）→ profiling 时标注
