## Why

Vulkan 后端 `TestSimpleTriangle` 存在多层 validation error 和运行时 bug。逐层修复后暴露更深层问题：第一层（vertex input / GPL renderPass）→ 第二层（command buffer 单例 / semaphore / fence）→ 第三层（hash 碰撞 / frame 3 死锁）→ 第四层（semaphore 复用 / teardown 顺序）。

## What Changes

1. ~~修复 vertex input attribute lookup bug~~ ✅（VUID-07904）
2. ~~GPL library 传入实际 VkRenderPass~~ ✅（VUID-06055 + VUID-02684）
3. ~~修复 command buffer 单例别名~~ ✅（draw 从未执行）
4. ~~修复 acquire semaphore wait~~ ✅（VUID-09600 + UNASSIGNED，第一帧）
5. ~~修复 compute fence 死锁~~ ✅（阻塞所有验证）
6. ~~修复 `hash_combine` 返回值丢弃~~ ✅（framebuffer/renderpass/pipeline layout cache 全部 key=0 碰撞，frame 2+ 渲染到错误 swapchain image）
7. ~~修复 frame 3 死锁~~ ✅（`Aquire` 里 redundant fence wait 在已 reset 的 fence 上死锁）
8. **修复 present semaphore 复用**（VUID-vkQueueSubmit-pSignalSemaphores-00067）：当前 2 个 frame context 各 1 套 semaphore，但 swapchain 有 3 个 image。present semaphore 在 image 被重新 acquire 前就被 re-signal → 改为按 swapchain image index 分配 semaphore
9. **修复 teardown 销毁顺序**：`Release()` 先销毁 window handle（含 image view）再销毁 framebuffer（引用那些 view）→ crash。改为先销毁 framebuffer cache

## Capabilities

### New Capabilities
- _（纯 bug 修复）_

### Modified Capabilities
- _（无 spec 变更）_

## Impact

- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/.cpp`：renderPass 参数；回退 VkPipelineRenderingCreateInfo
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp`：per-call 分配
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：GPL 调用点；SubmitBatches semaphore/fence
- `RenderBackend_Vulkan.cpp` + `VulkanGraphExecutor.cpp` + `ShaderLibrary.h`：hash_combine 返回值修复
- `VulkanFrameManager.cpp`：移除 Aquire 里 redundant fence wait
- `VulkanFrameManager.cpp/.h`：WindowSync 从 per-frame-context 改为 per-swapchain-image
- `RenderBackend_Vulkan.cpp`：Release() 销毁顺序修复
