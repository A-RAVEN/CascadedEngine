<!-- 审计标注: 2026-08-01 baseline audit — 4 个 Section 中 Section 4 大面积过时(架构已变), 新增 5 个 task, 详见 design.md 审计记录 -->

## Why

Vulkan 后端在功能对齐上已基本追平 D3D12，但对抗验证发现了若干 debug 与正确性问题：

- **Descriptor 写入缓存不可行**：`VulkanFrameContext::Aquire()` 每帧调用 `ResetDescriptorPool()` → `vkResetDescriptorPool` 清空所有 descriptor set 内容。新分配的 descriptor set 不含任何有效描述——即使底层 buffer handle 未变，仍必须重新写入。原提议的"比较 handle 跳过写入"策略在当前 pool 每帧 reset 架构下**零收益**，需标记为 future work 并改为记录 pool 生命周期约束。
- **PipelineLibrary hash 碰撞风险**：当前 hash 从 3 个 32-byte SHA-256 中各取首 8 字节 XOR 得到 64-bit，碰撞概率不可忽略（birthday bound ~2^32 条目即 ~50%），碰撞后退化为链表查找。`m_LibraryCache` 使用 `castl::vector` 线性搜索，需改为 `castl::unordered_map`。
- **Debug 命名覆盖不全**：`ShaderModule`、`Framebuffer`、`RenderPass`、`PipelineLayout`、`Semaphore`、`Fence` 缺少 debug 命名，在 RenderDoc 中显示为裸句柄。
- **所有 Vulkan 对象缺少 debug 名称**：现有 `SetVKObjectDebugName` 模板仅在 `VulkanBuffer::SetName()` / `VulkanTexture::SetName()` 中调用，RenderDoc/NSight 抓帧仅显示裸句柄。且该模板无 `#ifndef NDEBUG` 守卫，Release build 中有 UB 风险。
- **CommandList 防御性加固**：原 proposal 描述的 handle 泄漏路径（`m_GraphicsCommand`/`m_ComputeCommand` 单句柄懒加载 + Reset 置零）已不存在——当前架构改为每次分配 + vector 追踪 + Reset 统一释放。但 `Reset()` 缺少 GPU fence 完成断言，`Release()` 缺少防御性 `freeCommandBuffers`。

这些 gap 影响运行时正确性（UB 风险）、开发调测效率（debug 命名缺失）与运行时性能（hash 碰撞），应在其他功能 change 结束后统一处理。

## What Changes

- **Descriptor Pool 生命周期约束记录** (替代原 descriptor 缓存): 因 `VulkanFrameContext::Aquire()` 每帧 `vkResetDescriptorPool` 清空所有 descriptor set 内容，缓存策略在当前架构下不可行。改为在代码中显式记录 pool 生命周期约束，标注缓存为 future work。注意: pool 已使用 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` flag（允许单独 `vkFreeDescriptorSets`），但 `resetDescriptorPool` 仍清空所有 set。
- **SetVKObjectDebugName 安全性修复**: 模板函数当前无 `#ifndef NDEBUG` 守卫，Release build 中调用 `setDebugUtilsObjectNameEXT` 为 UB。添加编译期守卫或运行时 gating。
- **PipelineLibrary 查找优化**: 将 `VulkanPipelineLibrary` 的线性搜索 `castl::vector<castl::pair<...>>` 替换为 `castl::unordered_map` hash 查找；同时增加 hash 碰撞时的完整 key compare 回退逻辑，确保正确性
- **VK_EXT_debug_utils 资源命名**: 在 Pipeline/CommandBuffer/DescriptorSet/ShaderModule/Framebuffer/RenderPass/PipelineLayout/Semaphore/Fence 创建点为对象设置可读名称，使 RenderDoc/NSight 中可显示有意义的资源标识
- **CommandList 防御性加固**: 在 `Reset()` 中添加 GPU fence 完成断言；在 `Release()` 中添加防御性 `freeCommandBuffers` 调用

## Capabilities

### New Capabilities
- `vulkan-debug-tracing`: Vulkan debug 工具与资源命名 — 为所有 Vulkan 对象（Buffer、Image、Pipeline、CommandBuffer、DescriptorSet、ShaderModule、Framebuffer、RenderPass）设置 debug 名称，支持 RenderDoc/NSight 友好显示

### Modified Capabilities
<!-- descriptor 写入缓存因 pool 每帧 reset 被标记为 future work，不影响任何 spec 级行为 -->
无

## Impact

- `VulkanRenderBackendNew/private/ResourceManagement/VulkanFrameManager.cpp` — descriptor pool 生命周期注释 (`Aquire()` + `ResetDescriptorPool()`)
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/.cpp` — 添加 descriptor pool 生命周期约束注释，移除原缓存逻辑
- `VulkanRenderBackendNew/private/Utils/VulkanDebug.h` — `SetVKObjectDebugName` 添加 `#ifndef NDEBUG` 守卫
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` — ShaderModule/Framebuffer/RenderPass/PipelineLayout 创建点设置 debug name
- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h/.cpp` — hash map 替换线性查找 + 碰撞回退
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` — 临时资源创建点设置 debug name
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.cpp` — buffer/image 创建点设置 debug name（VMA 级别，补充已有 `VulkanBuffer::SetName`）
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp` — Reset() 断言 + Release() 防御性 free
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanFrameManager.cpp` — Fence/Semaphore 创建点设置 debug name
