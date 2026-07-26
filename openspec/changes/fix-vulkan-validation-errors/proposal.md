## Why

Vulkan 后端 `TestSimpleTriangle` 存在多个 validation layer 问题。已修复 VUID-07904（vertex input lookup bug）。对抗验证发现 VUID-06055 的"修复"（添加 VkPipelineRenderingCreateInfo）实际上把 fragment output library 强制设为 dynamic-rendering 模式，导致 linked pipeline 的 renderPass=NULL → 引发 VUID-02684。06055 和 02684 是**同一根因**的两个症状：GPL library 创建时未传 VkRenderPass。此外 VUID-09600（image layout）的根因是 acquire semaphore 未被 wait（UNASSIGNED-non-acquired-swapchain-image-used），barrier 本身正确。

## What Changes

1. ~~修复 vertex input attribute lookup bug~~ ✅ 已完成（VUID-07904 消失）
2. **GPL library 传入实际 VkRenderPass**（统一修复 VUID-06055 + VUID-02684）：`CreateFragmentLibrary` 和 `CreateFragmentOutputLibrary` 添加 `vk::RenderPass` 参数，创建时设 `createInfo.renderPass`。**回退** task 2.1 的 `VkPipelineRenderingCreateInfo`（renderPass 非 NULL 时 06055 前提不成立，不需要 rendering info）。linked pipeline 变为 renderpass pipeline，与 `beginRenderPass` 兼容
3. **修复 command buffer 单例别名**：`VulkanCommandListManager::GraphicsCommand()` 返回同一个 VkCommandBuffer，所有 batch 共用 → 每个 batch 的 `begin()` 擦掉前一个的录制 → draw 从未执行。改为每次分配新 command buffer
4. **修复 acquire semaphore wait**（统一修复 VUID-09600 + UNASSIGNED）：barrier 正确但 batch 1（含 swapchain barrier + draw）的 submit 未 wait acquire semaphore。预计算 `firstSwapchainBatch`（扫 `batchRWStates.imageRWStates` 找 Backbuffer），仅在该 batch 的 submit 上 wait
5. **修复 compute fence 死锁**：`SubmitBatches` 每 batch 后 unconditional `waitForFences(computeFence)` 在无 compute 时死锁，阻塞所有测试验证。改为仅在有 compute submit 的 batch 后 wait

## Capabilities

### New Capabilities
- _（纯 bug 修复）_

### Modified Capabilities
- _（无 spec 变更）_

## Impact

- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/.cpp`：`CreateFragmentLibrary` + `CreateFragmentOutputLibrary` 添加 renderPass 参数；回退 VkPipelineRenderingCreateInfo
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp`：`GraphicsCommand()`/`ComputeCommand()` 从单例改为每次分配
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：GPL 调用点传入 renderPass；`SubmitBatches` semaphore wait 精确定位到 firstSwapchainBatch
