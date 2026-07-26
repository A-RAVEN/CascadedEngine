## 说明

此 change 为纯 bug 修复，不引入新 capability，不修改已有 spec 的行为契约。

### 修复的 bug

1. **`graphicsPipelineLibrary` feature 未启用**：extension 已请求但 `VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT` 未加入 DeviceCreateInfo pNext 链，导致 `VK_PIPELINE_CREATE_LIBRARY_BIT_KHR` 非法（VUID-06606、VUID-06642）
2. **`dynamicRendering` feature 未启用**：Vulkan 1.3 core feature `VkPhysicalDeviceVulkan13Features.dynamicRendering` 未开启，导致 GPL library stage 的 renderPass=NULL 非法（VUID-06576）
3. **`CreateFragmentLibrary` 缺少 pDepthStencilState**：Vulkan spec 将 pDepthStencilState 归类为 Fragment Shader State 子集成员。renderPass=NULL 且无 dynamic depth/stencil states 时，VUID-09035 要求 pDepthStencilState 为非 NULL。给 CreateFragmentLibrary 添加参数传入 depthStencilState 即可修复

### 已有关联 spec（行为不变，仅修复实现）
- （无——不涉及任何已有 spec）
