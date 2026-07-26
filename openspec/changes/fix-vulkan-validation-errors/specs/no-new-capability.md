## 说明

纯 bug 修复，不引入新 capability，不修改已有 spec。

### 修复的 bug

1. ~~`VulkanGraphExecutor.cpp` vertex input attribute lookup 用 semantic name 查 stream-name map 永远 miss → vertex attributes 为空（VUID-07904）~~ ✅
2. GPL library 创建时 renderPass=NULL → 06055 + 02684 同一根因。修法：传入实际 VkRenderPass，回退 VkPipelineRenderingCreateInfo
3. `VulkanCommandListManager::GraphicsCommand()` 返回单例 → 所有 batch 共用一个 VkCommandBuffer → draw 被擦掉从未执行
4. Acquire semaphore 未被包含 swapchain image 操作的 batch（batch 1）wait → 09600 + UNASSIGNED 同一根因。修法：预计算 firstSwapchainBatch，精确定位 wait
