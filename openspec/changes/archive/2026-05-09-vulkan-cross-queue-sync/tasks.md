## 1. Queue Family Index 映射

- [x] 1.1 `VulkanGraphExecutor.h`: 新增成员 `int m_GraphicsQueueFamily = -1` 和 `int m_ComputeQueueFamily = -1`，以及私有辅助方法 `GetQueueFamilyIndex(EGPUQueueType)` 将 `eDirect` 映射为 graphics family、`eCompute` 映射为 compute family
- [x] 1.2 `VulkanGraphExecutor.cpp`: 在 `CompileAndExecute()` 入口处从 `QueueContext` 查询并缓存 queue family index 到成员变量

## 2. cachedState 从资源对象读回（对齐 D3D12）

- [x] 2.1 `VulkanWindowHandle.h`: 新增 `GetCurrentBackBufferResourceState()` 方法，返回 `m_BackBufferResourceStates[m_CurrentImageIndex]`
- [x] 2.2 `VulkanGraphExecutor.cpp`: 在 `PrepareBatchResourceBarriers()` 中，对 External Image 从 `texture->GetResourceState()` 获取 `cachedState`；对 Backbuffer 从 `pWindow->GetCurrentBackBufferResourceState()` 获取；对 Internal 仍用 `InitializedState()`。Buffer 同理：External buffer 从 `buffer->GetResourceState()` 获取。

## 3. PrepareBatchResourceBarriers 增加 QFOT 逻辑

- [x] 3.1 在 `PrepareBatchResourceBarriers()` 的 image 遍历循环中，state 比较的 `continue` 跳过逻辑之前，增加 QFOT 判定：若 `lastState.queueType != currentState.queueType`，生成 release barrier（src batch）+ acquire barrier（dst batch），正确设置 `srcQueueFamilyIndex` / `dstQueueFamilyIndex`
- [x] 3.2 **Barrier 变体路由**: Direct→Compute 时 release 写入 `releaseBarriers`、acquire 写入 `computeAquireBarriers`；Compute→Direct 时 release 写入 `computeReleaseBarriers`、acquire 写入 `aquireBarriers`；同队列保持现有路由。QFOT barrier 作为独立 barrier 追加（不影响原有的 access/imageLayout 变化 barrier）
- [x] 3.3 对 buffer 遍历循环做同样的 QFOT 判定、barrier 生成和变体路由
- [x] 3.4 处理 QFOT 与 `stateHaveGap` 的兼容：gap 场景下 release 放入 `lastBatch` 对应变体，acquire 放入 `currentBatch` 对应变体
- [x] 3.5 **跨帧 QFOT**: 当 `isFirstState == true` 且 `cachedState.queueType != currentState.queueType` 时（帧间队列转移），仅生成 acquire barrier（在当前 batch 对应变体中），不生成 release barrier

## 4. RecordBatchCommands 增加 compute 变体 barrier 执行

- [x] 4.1 在 `RecordBatchCommands()` 中，检测 `computeAquireBarriers` / `computeReleaseBarriers` 或 `anyComputeQueueOperations` 时，从 `CommandListManager` 分配 `batch.computeCommandBuffer`（`cmdListManager.ComputeCommand()`），begin 录制
- [x] 4.2 在 `computeCommandBuffer` 上按顺序执行：computeAquireBarriers → compute 工作 → computeReleaseBarriers，然后 end 录制
- [x] 4.3 `SubmitBatches()` 中，若 batch 有 `computeCommandBuffer`（已 begin/end），提交到 compute queue 并设置跨队列 semaphore 同步
- [x] 4.4 处理 `anyComputeQueueOperations == true` 但 `computeCommandBuffer` 未单独分配的 batch：回退到 direct command buffer（现有行为）。已验证 `RecordComputePass` 和 `RecordBatchCommands` 中已有 fallback 逻辑

## 5. 编译验证

- [x] 5.1 运行 build.bat 验证编译通过，若失败则分析并修复直到 BUILD SUCCESSFUL

## 6. Cross-queue semaphore synchronization

- [x] 6.1 `VulkanFrameBoundResourceManager`: 新增 `castl::vector<vk::Semaphore> m_CrossQueueSemaphores` 池和 `vk::Fence m_ComputeFence` / `vk::Fence m_DirectFence`（如尚未存在），以及分配/复位方法。Semaphore 按需从池中分配，`Reset()` 时回收复用
- [x] 6.2 `VulkanGraphExecutor::SubmitBatches()`: 重构为 batch-by-batch 的同步提交模型。对每个 batch，若同时存在 `computeCommandBuffer` 和 `directCommandBuffer` 且 batch 有 cross-queue barriers（`computeAquireBarriers` 非空或 `computeReleaseBarriers` 非空），则在两个 submit 之间插入 semaphore：src queue submit signal + dst queue submit wait
- [x] 6.3 确保 batch 间顺序：batch N 的两个 queue 提交完成后（通过 fence），才能提交 batch N+1
- [x] 6.4 `VulkanFrameBoundResourceManager::Reset()`: 回收 cross-queue semaphores 复用（调用 `ResetFences` 对应 fence）

## 7. Per-pass compute command buffer routing

- [x] 7.1 修改 `RecordComputePass()` 签名：增加 `bool asyncCompute` 参数
- [x] 7.2 在 `RecordComputePass()` 内部根据 `asyncCompute` + `batch.computeCommandBuffer` 可用性决定 `targetCmdBuf`：`asyncCompute && computeCmdBuf` → `computeCmdBuf`，否则 → `directCmdBuf`
- [x] 7.3 更新 `RecordBatchCommands()` 调用处：传入 `graph.GetComputePasses()[computePassID].asyncCompute`

## 8. Image aspect mask from texture format

- [x] 8.1 新增静态辅助函数 `GetImageAspectMask(ImageHandle const&, GPUGraph const&)` 在 `VulkanGraphExecutor.cpp` 中，根据 `GetDescriptor()` 返回的 `ETextureFormat` 映射到 `vk::ImageAspectFlags`
- [x] 8.2 在 `PrepareBatchResourceBarriers()` 中所有 image barrier 生成点（QFOT release、QFOT acquire、跨帧 QFOT acquire、regular transition barrier 共 4 处）使用 `GetImageAspectMask()` 替代硬编码的 `vk::ImageAspectFlagBits::eColor`

## 9. 编译验证

- [x] 9.1 运行 build.bat 验证编译通过，若失败则分析并修复直到 BUILD SUCCESSFUL

## 10. [CRITICAL] SubmitBatches Direct→Compute 路径遗漏 window semaphore 同步

- [x] 10.1 `VulkanGraphExecutor.cpp` `SubmitBatches()`: 在 `hasCrossQueueSync` 分支的 Direct→Compute 路径（`computeAquireBarriers.AnyBarrier()` 为 true）中，direct queue submit 前增加 `isLastBatch && batch.hasFinalizePass` 检查，添加 swapchain acquire/present semaphore 同步，与 Compute→Direct 路径的 window sync 逻辑对齐
- [x] 10.2 提取 window semaphore 同步逻辑为独立 lambda 或函数，避免 Direct→Compute / Compute→Direct / 无 cross-queue 三条路径重复代码

## 11. [IMPORTANT] Regular transition barrier 按 queueType 路由

- [x] 11.1 `VulkanGraphExecutor.cpp` `PrepareBatchResourceBarriers()` image 循环: regular barrier 的路由从固定 `aquireBarriers`/`releaseBarriers` 改为根据 `currentState.queueType` 选择变体（`eCompute` → `computeAquireBarriers`/`computeReleaseBarriers`，`eDirect` → `aquireBarriers`/`releaseBarriers`）
- [x] 11.2 同上，buffer 循环做同样的路由修复
- [x] 11.3 当 QFOT acquire barrier 已处理同一 layout transition（QFOT acquire 的 `oldLayout`/`newLayout` 与 regular barrier 相同）时，跳过 regular barrier 的追加，避免 `VUID-VkImageMemoryBarrier-oldLayout-01197` 校验错误
- [x] 11.4 `stateHaveGap` 场景下 release 和 acquire 分别按 `lastState.queueType` 和 `currentState.queueType` 路由到正确变体

## 12. [IMPORTANT] Internal 资源跨帧 QFOT 误触发

- [x] 12.1 `VulkanGraphExecutor.cpp` `PrepareBatchResourceBarriers()`: image 循环 `isFirstState && qfotNeeded` 分支中，增加 `image.GetType() == ImageHandle::ImageType::Internal` 检查，Internal 资源跳过跨帧 QFOT（因其每帧新建，从未被上一帧的队列族拥有）
- [x] 12.2 buffer 循环同理：External 以外的 buffer 跳过跨帧 QFOT

## 13. [IMPORTANT] AllocCrossQueueSemaphore off-by-one 修复

- [x] 13.1 `VulkanFrameManager.cpp` `AllocCrossQueueSemaphore()`: 新创建 semaphore 后 `m_CrossQueueSemaphoreIndex` 从 `size()` 改为 `size() - 1`，确保刚创建的 semaphore 可被后续调用复用，避免帧内 semaphore 无限增长

## 14. [MINOR] GetQueueFamilyIndex 显式使用 Vulkan 常量

- [x] 14.1 `VulkanGraphExecutor.cpp` `GetQueueFamilyIndex()`: default 分支返回 `static_cast<int>(vk::QueueFamilyIgnored)` 或 `VK_QUEUE_FAMILY_IGNORED` 替代裸 `-1`，避免依赖 int→uint32_t 转换巧合

## 15. [MINOR] Compute→Direct wait stage 精确化

- [x] 15.1 `VulkanGraphExecutor.cpp` `SubmitBatches()`: Compute→Direct 路径的 `waitStage` 从 `eAllCommands` 改为 `eVertexShader | eFragmentShader | eColorAttachmentOutput`（或更精确的管线阶段组合），改善 GPU 调度效率

## 16. [MINOR] 清理未使用成员变量

- [x] 16.1 `VulkanGraphExecutor.h`: 移除未使用的成员 `m_DirectFenceCounter`、`m_ComputeFenceCounter`（或补充其使用逻辑）
- [x] 16.2 `VulkanGPUExecutionBatch` 中移除或注释说明未使用的 `acquireBarriersEmitFenceQueues`、`bodyCommandsEmitFenceQueues`、`releaseBarriersEmitFenceQueues`、`computeWaitingDirectBatches`、`directWaitingComputeBatches`

## 17. 编译验证

- [x] 17.1 运行 build.bat 验证所有修复编译通过，若失败则分析并修复直到 BUILD SUCCESSFUL
