## Why

VulkanGraphExecutor 在构建 RenderPassCacheKey 时，depth attachment 和 color attachment 的格式分别硬编码为 `vk::Format::eD32Sfloat` 和 `vk::Format::eR8G8B8A8Unorm`。这导致：
- 任何非 D32Sfloat 深度格式或非 R8G8B8A8Unorm 颜色格式的 RenderPass 都会错误命中缓存，返回格式不匹配的 RenderPass/Framebuffer
- GPUGraph 中不同格式的 attachment 共用同一个缓存的 RenderPass，造成渲染结果错误或驱动层验证错误

实际上 `GPUTextureDescriptor`（包含正确的 `ETextureFormat`）就在上方已获取，且 `VulkanTexture::ConvertFormat()` 转换函数早已存在，只需替换两行即可消除硬编码。

## What Changes

- **VulkanGraphExecutor::RecordBatchCommands**: 将 `rpKey.depthFormat` 从 `vk::Format::eD32Sfloat` 改为 `VulkanTexture::ConvertFormat(desc.format)`
- **VulkanGraphExecutor::RecordBatchCommands**: 将 `rpKey.colorFormats.back()` 从 `vk::Format::eR8G8B8A8Unorm` 改为 `VulkanTexture::ConvertFormat(desc.format)`
- 移除两处 `// TODO: Proper format conversion` 注释

## Capabilities

### New Capabilities
- `vulkan-renderpass-format-conversion`: RenderPass 构建时从 attachment descriptor 正确转换格式到 vk::Format，确保不同格式的 RenderPass 不会错误共享缓存

### Modified Capabilities
- `vulkan-backend-alignment`: 关闭 Phase 1 项目 7（RenderPass 格式转换）

## Impact

- **VulkanGraphExecutor.cpp** (~4 行变更): `RecordBatchCommands()` 中 RenderPassCacheKey 构建逻辑
- **对齐文档**: Phase 1 完成后更新 `vulkan-backend-alignment` spec 状态
