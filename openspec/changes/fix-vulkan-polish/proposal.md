## Why

Vulkan 后端在功能对齐上已基本追平 D3D12，但对抗验证发现了若干 debug 与正确性问题：

- **Descriptor 写入缓存不可行**：`VulkanFrameContext::Aquire()` 每帧调用 `vkResetDescriptorPool` 清空所有 descriptor set 内容。新分配的 descriptor set 不含任何有效描述——即使底层 buffer handle 未变，仍必须重新写入。原提议的"比较 handle 跳过写入"策略在当前 pool 每帧 reset 架构下**零收益**，需标记为 future work 并改为记录 pool 生命周期约束。
- **CommandList handle 泄漏**：`vkResetCommandPool` 不释放 handle——仅重置状态为 initial。`VulkanCommandListManager::Reset()` 将 `m_GraphicsCommand` 等清零后，下次 `Command()` 调用重新 `allocateCommandBuffers` 产生新 handle，旧 handle 被泄漏。需修复为复用已 reset 的 handle。
- **PipelineLibrary hash 碰撞风险**：当前 hash 从 3 个 32-byte SHA-256 中各取首 8 字节 XOR 得到 64-bit，碰撞概率不可忽略（birthday bound ~2^32 条目即 ~50%），碰撞后退化为链表查找。
- **Debug 命名覆盖不全**：`ShaderModule`、`Framebuffer`、`RenderPass` 缺少 debug 命名，在 RenderDoc 中显示为裸句柄。
- **所有 Vulkan 对象缺少 debug 名称**：现有 `SetVKObjectDebugName` 模板未在资源创建点调用，RenderDoc/NSight 抓帧仅显示裸句柄。

这些 gap 影响运行时正确性（handle 泄漏）、开发调测效率（debug 命名缺失）与运行时性能（hash 碰撞），应在其他功能 change 结束后统一处理。

## What Changes

- **Descriptor Pool 生命周期约束记录** (替代原 descriptor 缓存): 因 `VulkanFrameContext::Aquire()` 每帧 `vkResetDescriptorPool` 清空所有 descriptor set 内容，缓存策略在当前架构下不可行。改为在代码中显式记录 pool 生命周期约束，标注缓存为 future work（需引入 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` + `vkFreeDescriptorSets` 或跨帧复用 strategy 后方可实现）
- **CommandList handle 泄漏修复**: `VulkanCommandListManager::Reset()` 中不复用已 reset 的 handle 而重新 `allocateCommandBuffers` 导致 handle 泄漏。修复为 Reset() 后将已有 `vk::CommandBuffer` 句柄重置到 initial 状态后直接复用，不再重新分配
- **PipelineLibrary 查找优化**: 将 `VulkanPipelineLibrary` 的线性搜索 `castl::vector<castl::pair<...>>` 替换为 `castl::unordered_map` hash 查找；同时增加 hash 碰撞时的完整 key compare 回退逻辑，确保正确性
- **VK_EXT_debug_utils 资源命名**: 在 Buffer/Image/Pipeline/CommandBuffer/DescriptorSet/ShaderModule/Framebuffer/RenderPass 创建点为对象设置可读名称，使 RenderDoc/NSight 中可显示有意义的资源标识

## Capabilities

### New Capabilities
- `vulkan-debug-tracing`: Vulkan debug 工具与资源命名 — 为所有 Vulkan 对象（Buffer、Image、Pipeline、CommandBuffer、DescriptorSet、ShaderModule、Framebuffer、RenderPass）设置 debug 名称，支持 RenderDoc/NSight 友好显示

### Modified Capabilities
<!-- descriptor 写入缓存因 pool 每帧 reset 被标记为 future work，不影响任何 spec 级行为 -->
无

## Impact

- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp` — command list handle 泄漏修复：Reset() 中复用已有 handle 而非重新 allocate
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/.cpp` — 添加 descriptor pool 生命周期约束注释，移除原缓存逻辑
- `VulkanRenderBackendNew/private/GPUGraph/VulkanFrameContext.h/.cpp` — 记录每帧 Aquire() 中 vkResetDescriptorPool 对缓存策略的阻断
- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/.cpp` — hash map 替换线性查找 + 碰撞回退
- `VulkanRenderBackendNew/private/Utils/VulkanDebug.h` — debug utils 命名辅助函数
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.cpp` — buffer/image 创建点设置 debug name
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` — 资源创建点设置 debug name，ShaderModule/Framebuffer/RenderPass 创建点设置 debug name
