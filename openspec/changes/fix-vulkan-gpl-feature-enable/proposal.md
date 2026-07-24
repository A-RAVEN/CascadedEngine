## Why

Vulkan 后端在 Device 创建时未启用 `graphicsPipelineLibrary` 和 `dynamicRendering` feature，导致 GPL pipeline 创建时触发 4 个 VUID validation error 并崩溃。该问题在 `fix-vulkan-render-white-screen` 修复 RenderPassCacheKey 后被暴露——此前因 render pass 创建失败，pipeline 创建被跳过，GPL 代码路径从未被执行到。

## What Changes

1. **启用 `VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT`**：在 DeviceCreateInfo 的 pNext 链中添加 `.graphicsPipelineLibrary = VK_TRUE`（修复 VUID-06606、VUID-06642）
2. **启用 `VkPhysicalDeviceVulkan13Features.dynamicRendering`**：在 pNext 链中添加 `.dynamicRendering = VK_TRUE`（修复 VUID-06576）
3. **给 `CreateFragmentLibrary` 添加 `pDepthStencilState` 参数**：Vulkan spec 将 `pDepthStencilState` 归类为 Fragment Shader State 子集成员，在 FRAGMENT_SHADER_BIT library 中合法。renderPass=NULL 且无 dynamic depth/stencil states 时 VUID-09035 要求提供 pDepthStencilState（`VulkanPipelineLibrary.h/.cpp` + `VulkanGraphExecutor.cpp`）

## Capabilities

### New Capabilities
- _（纯 bug 修复，不引入新 capability）_

### Modified Capabilities
- _（不修改已有 spec 的行为契约）_

## Impact

- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`：`Init()` 函数中 DeviceCreateInfo 的 pNext 链（约 line 227），添加两个 feature struct
- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.h`：`CreateFragmentLibrary()` 声明添加 `pDepthStencilState` 参数
- `VulkanRenderBackendNew/private/PipelineLibrary/VulkanPipelineLibrary.cpp`：`CreateFragmentLibrary()` 实现中设置 `pDepthStencilState`
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：GPL 路径调用处传入 `&depthStencilState`
- 无 API 变更，无 breaking change
