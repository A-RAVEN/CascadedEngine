## 说明

纯 bug 修复，不引入新 capability，不修改已有 spec。

### 修复的 bug

1. `VK_EXT_vertex_attribute_robustness` extension + feature 未启用（VUID-07904）
2. `CreateFragmentOutputLibrary` 缺少 `VkPipelineRenderingCreateInfo`（VUID-06055）
3. Swapchain image 缺少 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL layout transition（VUID-09600、VUID-02684）
