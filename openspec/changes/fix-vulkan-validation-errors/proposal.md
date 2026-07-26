## Why

Vulkan 后端 `TestSimpleTriangle` 运行时产生 3 个 VUID + 1 个 layout error（VUID-07904 x2、VUID-06055、VUID-02684、VUID-09600、UNDEFINED→COLOR_ATTACHMENT_OPTIMAL），导致窗口模式下渲染不正确。

## What Changes

1. 启用 `vertexAttributeRobustness`：添加 `VK_EXT_vertex_attribute_robustness` extension 到 `GetDeviceExtensionNames()`，在 DeviceCreateInfo pNext 链添加 `VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT` → 修复 VUID-07904（注意：此 feature 不属于 Vulkan13Features，它是 `VK_EXT_vertex_attribute_robustness` 扩展）
2. `CreateFragmentOutputLibrary` 添加 `VkPipelineRenderingCreateInfo`：renderPass=NULL 时 pNext 链提供 rendering info，`colorAttachmentCount` 取自 colorBlendState，`pColorAttachmentFormats` 填 `VK_FORMAT_UNDEFINED`（VUID-06821 允许 GPL library 延迟到 link 时指定）→ 修复 VUID-06055
3. Swapchain image initial layout barrier：确认并修复 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL 的 transition → 修复 VUID-09600 + VUID-02684 + layout error

## Capabilities

### New Capabilities
- _（纯 bug 修复）_

### Modified Capabilities
- _（无 spec 变更）_

## Impact

- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`：`GetDeviceExtensionNames()` 加 extension，DeviceCreateInfo pNext 链加 `VertexAttributeRobustnessFeaturesEXT`
- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.cpp`：`CreateFragmentOutputLibrary` 添加 `VkPipelineRenderingCreateInfo`
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：layout barrier 验证/修复
