# Vulkan RenderPass 格式转换

**Version**: 1.0
**Created**: 2026-05-02
**Status**: Active

---

## Requirements

### Requirement: RenderPass 构建时正确转换 attachment 格式

`VulkanGraphExecutor` SHALL 在构建 `RenderPassCacheKey` 时，通过 `VulkanTexture::ConvertFormat()` 将 `GPUTextureDescriptor.format`（ETextureFormat）转换为 `vk::Format`，而非使用硬编码值。

#### Scenario: 不同深度格式的 RenderPass 正确缓存分离
- **WHEN** GPUGraph 包含两个 RenderPass，一个使用 `E_D32_SFLOAT` depth attachment，另一个使用 `E_D16_UNORM` depth attachment
- **THEN** `GetOrCreateRenderPass` 为两个格式分别创建独立的 `vk::RenderPass` 对象

#### Scenario: 不同颜色格式的 RenderPass 正确缓存分离
- **WHEN** GPUGraph 包含两个 RenderPass，一个使用 `E_R8G8B8A8_UNORM` color attachment，另一个使用 `E_B8G8R8A8_UNORM`
- **THEN** `GetOrCreateRenderPass` 为两个格式分别创建独立的 `vk::RenderPass` 对象

#### Scenario: 相同格式的 RenderPass 命中缓存
- **WHEN** GPUGraph 包含两个 RenderPass，均使用 `E_R8G8B8A8_UNORM` color attachment
- **THEN** 第二个 RenderPass 复用第一个已缓存的 `vk::RenderPass` 对象
