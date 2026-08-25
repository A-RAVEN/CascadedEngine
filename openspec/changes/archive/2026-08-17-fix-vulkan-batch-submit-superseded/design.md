## Context

`SubmitBatches()` 当前在每 batch 后 `waitForFences(UINT64_MAX)` + `resetFences`（line 2170-2184）。对抗验证发现移除阻塞后引入 3 个 Vulkan spec 违规。

## Goals / Non-Goals

**Goals:**
- Per-batch 独立 fence，消除单 fence 复用违规
- MarkFenceSubmitted 在 cross-queue 和 no-cross-queue 路径全覆盖
- Split barrier access mask 正确分离
- ExecuteBarriers 使用精确 pipeline stage flags
- VulkanResourceState 改为 EGPUQueueTypeFlags bitmask
- 跨 batch QFOT 有 semaphore happens-before 保证

**Non-Goals:**
- 不实现 D3D12 的三阶段 fence chain（acquire/body/release）
- 不改动窗口 semaphore sync 逻辑

## Decisions

### D1: Per-batch Fence Pool

在 `VulkanFrameBoundResourceManager` 中维护 `castl::vector<vk::Fence> m_BatchFences` 和 `size_t m_BatchFenceIndex`。每 batch 调用 `AllocBatchFence()` 获取独立 fence。`VulkanFrameContext::Aquire()` 等待 `GetLastBatchFence()`。

### D2: Split Barrier Access Mask 分离

Gapped 资源在 stateHaveGap 路径（lines 1142-1151, 1268-1277）：
- release barrier: srcAccessMask=state.accessFlags, dstAccessMask=0
- acquire barrier: srcAccessMask=0, dstAccessMask=nextState.accessFlags

### D3: ExecuteBarriers 精确 Stage Flags

`VulkanImageBarrier`/`VulkanBufferBarrier` 增加 `srcStageMask`/`dstStageMask` 字段，`AddBarrier` 增加 stage 参数。`ExecuteBarriers` 不再硬编码 `eAllCommands`。

### D4: EGPUQueueTypeFlags Bitmask

```cpp
// VulkanResourceState.h
- EGPUQueueType queueType = EGPUQueueType::eDirect;
+ EGPUQueueTypeFlags queueTypes{}; // bitmask
// Combine():
+ queueTypes |= other.queueTypes;

// isSharedBetweenQueues():
+ return (queueTypes & EGPUQueueType::eDirect) && (queueTypes & EGPUQueueType::eCompute);
```

### D5: 跨 Batch QFOT Semaphore

相邻 batch 间有 QFOT 依赖时（前 batch 有 releaseOnCompute/LayoutTransitionOnCompute，后 batch 需要 acquire），分配临时 binary semaphore 连接两次 submit。

## Risks

- **[Risk] Fence pool 耗尽** → 按 batch 数量预分配，单帧 batch 数 < 64 安全
- **[Risk] Stage flags 传递链长** → 所有 AddBarrier 调用点需修改，编译期检查
- **[Risk] Semaphore 数量爆炸** → 仅在最坏情况每 batch 间分配 1 个，总量 < batch count
