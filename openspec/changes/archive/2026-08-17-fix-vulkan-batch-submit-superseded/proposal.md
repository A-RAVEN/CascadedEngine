## Why

`VulkanGraphExecutor::SubmitBatches()` 在每 batch 之间用 `waitForFences(UINT64_MAX)` 阻塞 CPU。移除阻塞后暴露出三个 Vulkan spec 违规：(1) 同一个 `VkFence` 在单帧内多次传入 `queue.submit()`——Vulkan spec 要求 fence 必须是 unsignaled；(2) `MarkFenceSubmitted` 仅在 no-cross-queue-sync 分支调用，cross-queue 路径遗漏；(3) split barrier 将含完整 src+dst access mask 的同一 barrier 复制到 release/acquire 两边而非分离。

此外 `ExecuteBarriers` 对所有 barrier 硬编码 `eAllCommands`、`isSharedBetweenQueues` 恒返回 false（且从未被调用）、跨 batch QFOT transition 移除阻塞 fence 后缺少 `happens-before` 保证。

## What Changes

- **Per-batch fence pool**: 替代单 fence 复用，每个 batch 从 pool 获取独立 fence，仅最后一个 batch 的 fence 用于下一帧 sync
- **MarkFenceSubmitted 全覆盖**: cross-queue 和 no-cross-queue 两分支均在 `queue.submit` 后调用
- **Split barrier access mask 正确分离**: release barrier dstAccessMask=0, acquire barrier srcAccessMask=0
- **ExecuteBarriers 精确 stage flags**: 使用 `VulkanResourceState` 的 stage/access flags 替代 `eAllCommands`
- **EGPUQueueTypeFlags bitmask**: `VulkanResourceState` 的 `queueType` 从单值改为 bitmask，`Combine()` 累加
- **跨 batch QFOT semaphore 依赖**: 为有 QFOT 依赖的相邻 batch 分配 binary semaphore 保证 happens-before

## Capabilities

### Modified Capabilities
- `vulkan-execution-sync`: 批量提交从"基本正确"扩展为"无 spec 违规、精确 barrier、跨 batch QFOT 安全"

## Impact

- `VulkanGraphExecutor.cpp` `SubmitBatches()` + `PrepareBatchResourceBarriers()` + `ExecuteBarriers()`
- `VulkanFrameBoundResourceManager.h/.cpp` — batch fence pool
- `VulkanResourceState.h` — queueType bitmask
- `VulkanPassRWState.h/.cpp` — barrier stage flags 传递
