## Context

D3D12 `AliasedMemoryAllocator` 使用 D3D12MA `VirtualBlock`（512MB）做虚拟地址子分配 + per-batch alloc/free + `CommitBlock`（`CreatePlacedResource` 物理映射）。Vulkan 对应技术：VMA `VmaPool` + `vmaCreateBuffer`/`vmaCreateImage` with pool 参数。

## Goals / Non-Goals

**Goals:** VmaPool 子分配，per-batch 生命周期，多 pool 自动扩展，跨帧复用，buffer/image 分离

**Non-Goals:** 不实现 virtual addressing（VMA 无此概念），不实现 heap type 多态

## Decisions

### D1: VmaPool 子分配

`vmaCreateBuffer(allocator, &bufferInfo, &allocInfo_with_pool, &buffer, &allocation, nullptr)` 自动在 pool 内子分配。`vmaFreeMemory` 回收空间。VMA 内部使用 buddy/linear algorithm 管理空闲区间。

### D2: 跨帧复用

创建时设 `VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT`（staging 类）或不设（通用 buddy）。帧末 `vmaResetPool` 或依赖 per-resource `vmaFreeMemory` 回收。

### D3: Buffer/Image 分离

查询 `VkPhysicalDeviceMemoryProperties` 的 `memoryTypeBits`，若 buffer 和 image 支持不同 memory types，使用不同 pool。集成 GPU 通常统一。

## Risks

- VMA pool 的 buddy allocator 碎片化 → 监控 pool 利用率
- Linear algorithm 不支持部分释放 → 使用 buddy algorithm + 帧末整体 reset
