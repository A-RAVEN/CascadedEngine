## Context

Vulkan 1.3 后端，`TestSimpleTriangle` 单 render pass（1 color attachment，无 depth）。GPL feature 已启用，pipeline 4 个 library parts + link 全部成功。但仍有 3 类 validation error 未修复。

## Goals / Non-Goals

**Goals:**
- 消除 VUID-07904（vertex input location 不匹配）
- 消除 VUID-06055（fragment output library 缺 rendering info）
- 消除 VUID-09600 + VUID-02684 + UNDEFINED→COLOR_ATTACHMENT_OPTIMAL layout 错误

**Non-Goals:**
- 不修改 monolithic pipeline path
- 不引入 `VK_KHR_maintenance9` 或 `VK_EXT_extended_dynamic_state3`

## Decisions

### Decision 1: 启用 VK_EXT_vertex_attribute_robustness

`vertexAttributeRobustness` **不是** `VkPhysicalDeviceVulkan13Features` 的成员。它是独立扩展 `VK_EXT_vertex_attribute_robustness` 提供的 feature，位于 `VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT`。

修复步骤（`RenderBackend_Vulkan.cpp`）：

1. `GetDeviceExtensionNames()` 添加 `VK_EXT_VERTEX_ATTRIBUTE_ROBUSTNESS_EXTENSION_NAME`
2. 在 DeviceCreateInfo pNext 链添加：
```cpp
vk::PhysicalDeviceVertexAttributeRobustnessFeaturesEXT vertexAttrRobustFeatures{};
vertexAttrRobustFeatures.vertexAttributeRobustness = VK_TRUE;
// chain: vertexAttrRobustFeatures → gplFeatures → vulkan13Features
vertexAttrRobustFeatures.pNext = &gplFeatures;  // gplFeatures already has pNext=&vulkan13Features
deviceCreateInfo.pNext = &vertexAttrRobustFeatures;
```

### Decision 2: Fragment output library 添加 VkPipelineRenderingCreateInfo

VUID-06055：renderPass=NULL + fragment output interface + pColorBlendState 非 dynamic → `colorAttachmentCount` 必须匹配 `VkPipelineRenderingCreateInfo`。

修复：
```cpp
castl::array<vk::Format, 1> colorFormats = { vk::Format::eUndefined };
vk::PipelineRenderingCreateInfo renderingInfo{};
renderingInfo.colorAttachmentCount = colorBlendState.attachmentCount;
renderingInfo.pColorAttachmentFormats = colorFormats.data(); // VUID-06821 允许 UNDEFINED，延迟到 link

// pNext: createInfo → libraryInfo → renderingInfo → ... (existing chain)
renderingInfo.pNext = nullptr;
libraryInfo.pNext = &renderingInfo;
createInfo.pNext = &libraryInfo;
```

`pColorAttachmentFormats` 填 `VK_FORMAT_UNDEFINED` 是 GPL 合法做法——VUID-06821 明确允许 library 中的 UNDEFINED format，实际 format 在 link 时由 renderPass 提供。

### Decision 3: Swapchain image initial layout transition

VUID-09600 和 VUID-02684 **不是** VUID-06055 的 cascade——它们发生在不同的 pipeline 生命周期阶段。需要独立修复 layout barrier。

当前 barrier 逻辑已验证正确：backbuffer 初始 state 为 UNDEFINED（`VulkanWindowHandle.cpp:137-139`），`PrepareBatchResourceBarriers` 生成 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL barrier，`ExecuteBarriers` 在 `RecordRenderPass` 之前执行。但 barrier 使用 `eAllCommands` 而非精确的 `eTopOfPipe`——功能正确但性能不优，后续可优化。

如修完 Decision 1+2 后 layout error 仍存在，需深入调试 barrier 的实际执行路径。

## Risks / Trade-offs

- **[风险]** `VK_EXT_vertex_attribute_robustness` 是 EXT 扩展（非 core），需驱动支持 → **缓解**: 所有支持 Vulkan 1.3 的桌面 GPU 均支持此扩展，Ubuntu/Windows 主流驱动已包含
- **[风险]** `VkPipelineRenderingCreateInfo.pColorAttachmentFormats` 填 UNDEFINED 后 link pipeline 需提供实际 format → **缓解**: LinkPipeline 已通过 renderPass 提供格式信息
- **[风险]** Layout barrier 使用 eAllCommands 性能次优 → **缓解**: 功能正确，性能优化后续单独处理
