> **归档标注（2026-08-17，as-superseded）**：本 change **从未实施**（git 历史仅 proposal 新增 commit 01a851d，无实现提交）。核心前提（移除逐 batch `waitForFences` 阻塞 → 异步非阻塞提交）被 fix-vulkan-api-correctness 任务 10.1[F22] **明确冻结**（代码注释 "frozen by design ... do not remove the serialization first"，GraphExecutor.cpp:2579-2623 仍逐 batch wait+reset）。归档前经对抗核对（workflow w49tvyea9，10 条判定全 confirmed）逐条吸收/取代关系如下；delta spec（`specs/vulkan-execution-sync/`）**作废不提升**——其 "Batch 间非阻塞 Submit" 与 "isSharedBetweenQueues SHALL 返回 true" 两条需求与当前设计直接冲突。
>
> - 1.1-1.3 / 1.5（per-batch fence pool）：**absorbed** —— F22 保留的逐 batch 串行化使每队列 fence 每次 submit 前 unsignaled（wait+reset），满足 VUID-vkQueueSubmit-pFences-00013，无需 fence pool
> - 1.4（移除 batch 间 waitForFences）：**superseded** —— 被 F22 冻结决策取代
> - 2.1-2.3（MarkFenceSubmitted 全覆盖）：**absorbed** —— d7148da 删 m_FenceSubmitted 三件套，改局部 `directSubmitted`/`computeSubmitted` 门控（GraphExecutor.cpp:2479-2602），6 处 submit 全覆盖
> - 3.1-3.2（split barrier access mask 分离）：**implemented（早于本 proposal）** —— 归档 change 2026-05-09-vulkan-cross-queue-sync（fc39f63d）已实现
> - 4.1-4.4（ExecuteBarriers 精确 stage flags）：**absorbed/superseded** —— 被 fix-vulkan-api-correctness 7.1[F32] 的 `AccessToPipelineStages` 机制取代（f555be4）
> - 5.1-5.4（EGPUQueueTypeFlags bitmask）：**absorbed** —— bitmask 已在 pass/batch 层落地；`VulkanResourceState::isSharedBetweenQueues()` 仍为无调用死代码（恒 false）
> - 6.1-6.3（跨 batch QFOT semaphore）：**absorbed** —— 跨 batch happens-before 由保留的逐 batch waitForFences 保证，无需 semaphore
> - 7.1-7.2（编译验证）：**not-implemented** —— 无代码可构建

## 1. Per-batch Fence Pool

- [ ] 1.1 `VulkanFrameBoundResourceManager.h`: 添加 `castl::vector<vk::Fence> m_BatchFences` + `size_t m_BatchFenceIndex` + `vk::Fence AllocBatchFence()` + `vk::Fence GetLastBatchFence()`
- [ ] 1.2 `VulkanFrameBoundResourceManager.cpp`: 实现 `AllocBatchFence()`——复用已创建 fence 或 `vkCreateFence`，`Reset()` 中重置所有 fence 并将 index 归零
- [ ] 1.3 `SubmitBatches()`: 每 batch 调用 `AllocBatchFence()` 获取独立 fence，替换所有对 `GetDirectFence()`/`GetComputeFence()` 的 fence 参数
- [ ] 1.4 移除 `SubmitBatches()` 中 batch 间的 `waitForFences` + `resetFences`（lines 2170-2184）
- [ ] 1.5 `VulkanFrameContext::Aquire()`: 改为等待 `GetLastBatchFence()` 而非固定 fence

## 2. MarkFenceSubmitted 全覆盖

- [ ] 2.1 在 `SubmitBatches()` 的每个 `queue.submit(info, fence)` 后立即调用 `resourceManager.MarkFenceSubmitted()`
- [ ] 2.2 覆盖 cross-queue sync 分支中的 4 个 submit 调用（lines 2087, 2095, 2109, 2139）
- [ ] 2.3 覆盖 no-cross-queue-sync 分支中的 2 个 submit 调用（lines 2150, 2165）

## 3. Split Barrier Access Mask 分离

- [ ] 3.1 `PrepareBatchResourceBarriers()` stateHaveGap 路径（lines 1142-1151 image, lines 1268-1277 buffer）：release barrier `dstAccessMask=0`，acquire barrier `srcAccessMask=0`
- [ ] 3.2 Release barrier 的 `newLayout` 设为资源当前 layout（不做转换），acquire barrier 的 `oldLayout` 匹配 release 的 newLayout

## 4. ExecuteBarriers 精确 Stage Flags

- [ ] 4.1 `VulkanRenderStateBarriers.h`：`VulkanImageBarrier`/`VulkanBufferBarrier` 增加 `srcStageMask`/`dstStageMask` 字段
- [ ] 4.2 `AddImageBarrier`/`AddBufferBarrier` 增加 stage 参数并存储到 barrier 结构
- [ ] 4.3 `ExecuteBarriers()` 使用 `barrier.srcStageMask`/`dstStageMask` 替代硬编码的 `eAllCommands`
- [ ] 4.4 所有 `PrepareBatchResourceBarriers` 中调用 `Add*Barrier` 处传入对应 `VulkanResourceState::stageFlags` 和 `accessFlags`

## 5. EGPUQueueTypeFlags Bitmask

- [ ] 5.1 `VulkanResourceState.h`：`queueType` 从 `EGPUQueueType` 改为 `EGPUQueueTypeFlags`（或新增 flags 字段）
- [ ] 5.2 `Combine()` 增加 `queueTypes |= other.queueTypes`
- [ ] 5.3 `isSharedBetweenQueues()` 改为检查 bitmask 是否同时含 direct 和 compute
- [ ] 5.4 所有构造 `VulkanResourceState` 处或 `Set*RWState` 调用处：`EGPUQueueType` → `EGPUQueueTypeFlags`

## 6. 跨 Batch QFOT Semaphore

- [ ] 6.1 在 `SubmitBatches()` 中检测相邻 batch 间是否有 QFOT release→acquire 依赖
- [ ] 6.2 有依赖时：从 `resourceManager.AllocCrossQueueSemaphore()` 获取 binary semaphore
- [ ] 6.3 Release batch 的 submit 增加 signal semaphore，acquire batch 的 submit 增加 wait semaphore（匹配正确的 stage mask）

## 7. 编译验证

- [ ] 7.1 运行 `build.py`，验证 BUILD SUCCESSFUL
- [ ] 7.2 若编译失败：逐项修复类型不匹配（尤其 EGPUQueueType→Flags 转换、stage flags VkPipelineStageFlags 类型）
