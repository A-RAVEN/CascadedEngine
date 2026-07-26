## Context

Vulkan 后端使用 Vulkan 1.3（`VK_API_VERSION_1_3`），Device 创建时已请求 `VK_EXT_graphics_pipeline_library` extension。`VulkanPipelineLibrary::IsSupported()` 检查 extension 可用性并返回 true，因此 `BuildPipelineStates` 走 GPL 路径（创建 4 个 library parts + LinkPipeline）。

但 `RenderBackend_Vulkan::Init()` 中 `DeviceCreateInfo` 的 pNext 链为空——既没有 `VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT` 也没有 `VkPhysicalDeviceVulkan13Features`。两个 feature 都是默认 VK_FALSE，导致 GPL pipeline 创建触发 4 个 VUID 并崩溃。

## Goals / Non-Goals

**Goals:**
- 启用 `graphicsPipelineLibrary` feature → 修复 VUID-06606、VUID-06642
- 启用 `dynamicRendering` feature → 修复 VUID-06576
- 修复 `CreateFragmentLibrary` 缺少 `pDepthStencilState` → 修复 VUID-09035

**Non-Goals:**
- 不修改 GPL monolithic fallback 路径
- 不引入 `VK_KHR_dynamic_rendering` extension（1.3 已内建）
- 不引入 `VK_EXT_extended_dynamic_state3`（VUID-09035 改用 pDepthStencilState 方案）
- 不处理 pre-existing 的 `pEnabledFeatures` 为空问题（Vulkan 1.0 base features）

## Decisions

### Decision 1: 在 DeviceCreateInfo pNext 链中添加两个 feature struct

**位置**：`RenderBackend_Vulkan.cpp` line 227，当前代码：

```cpp
vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, deviceExts);
```

**修改后**：

```cpp
// Vulkan 1.3 core features: dynamicRendering
vk::PhysicalDeviceVulkan13Features vulkan13Features{};
vulkan13Features.dynamicRendering = VK_TRUE;

// GPL extension feature
vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gplFeatures{};
gplFeatures.graphicsPipelineLibrary = VK_TRUE;
gplFeatures.pNext = &vulkan13Features;

vk::DeviceCreateInfo deviceCreateInfo({}, queueCreationInfo.queueCreateInfoList, {}, deviceExts);
deviceCreateInfo.pNext = &gplFeatures;
```

**无需条件化**：`GPL` extension 在 `GetDeviceExtensionNames()` 中无条件请求，`dynamicRendering` 是 Vulkan 1.3 core feature 始终可用。GPU 不支持 GPL 时 `vkCreateDevice` 直接返回 `VK_ERROR_EXTENSION_NOT_PRESENT`，无需额外 fallback。

**VUID 映射**：

| VUID | 需要的修复 | 位置 |
|------|-----------|------|
| VUID-06606 | `graphicsPipelineLibrary` feature | `VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT` |
| VUID-06642 | `graphicsPipelineLibrary` feature | 同上（连锁反应）|
| VUID-06576 | `dynamicRendering` feature | `VkPhysicalDeviceVulkan13Features` |
| VUID-09035 | `pDepthStencilState` | `CreateFragmentLibrary` 代码修复（见 Decision 2）|

**为什么是 Vulkan13Features 而不是 DynamicRenderingFeaturesEXT**：
Vulkan 1.3 将 `dynamicRendering` 提升为 core feature，位于 `VkPhysicalDeviceVulkan13Features`。项目使用 `VK_API_VERSION_1_3`，不需要也不应该使用 extension 版本。

### Decision 2: 给 CreateFragmentLibrary 添加 pDepthStencilState 参数

**根因**：VUID-09035 的触发条件：
- renderPass=NULL + fragment shader state 但无 fragment output interface + 无 depth/stencil dynamic states → pDepthStencilState 必须为非 NULL

注意：Vulkan spec 明确将 `pDepthStencilState` 归类为 **Fragment Shader State** 子集成员（见 spec 9.2 章 "Fragment Shader State" 定义），而非 Fragment Output Interface。因此在 `FRAGMENT_SHADER_BIT-only` library 中设置 pDepthStencilState 完全合法——VUID-09035 本身也证实了这一点（它要求 pDepthStencilState，而非禁止）。

`CreateFragmentLibrary()`（VulkanPipelineLibrary.cpp:124-156）当前函数签名不包含 pDepthStencilState 参数，renderPass 默认为 NULL——命中全部触发条件。

**修复方案**：给 `CreateFragmentLibrary` 添加 `vk::PipelineDepthStencilStateCreateInfo const*` 参数，调用方传入 depthStencilState 指针。改动点：
- `VulkanPipelineLibrary.h`：函数声明加参数
- `VulkanPipelineLibrary.cpp`：`createInfo.pDepthStencilState = pDepthStencilState;`
- `VulkanGraphExecutor.cpp`：调用处传入 `&depthStencilState`

**备选方案**（未采用）：
- (a) 合并 fragment shader + fragment output 为一个 library → 正确但架构退步（失去独立缓存），且回避而非修复 VUID
- (b) 启用 VK_EXT_extended_dynamic_state3 → 引入新 extension 依赖

### Decision 3: 不修改 IsPipelineLibrarySupported 逻辑

`IsPipelineLibrarySupported()` 仅在 extension 层面检查可用性，不检查 feature enablement。feature 在 Device 创建时启用后，所有通过该 Device 创建的 pipeline 自动拥有 feature 能力，无需额外检查。

## Risks / Trade-offs

- **[风险]** `dynamicRendering` feature 启用后，spec 允许 renderPass=NULL，但代码始终使用传统 render pass（vkCmdBeginRenderPass + VkRenderPass + VkFramebuffer）。dynamicRendering 仅用于消除 GPL library parts 的 VUID-06576，不改变实际渲染路径 → **缓解**: GPL library parts 的 renderPass 被 spec 忽略（在 link 时指定实际 renderPass），library 不需要 `VkPipelineRenderingCreateInfo`
- **[风险]** TestSimpleTriangle 的 render pass 无 depth attachment（`hasDepth=false`），depthStencilState 为零初始化（depthTestEnable=FALSE, depthWriteEnable=FALSE），传给 CreateFragmentLibrary 的 pDepthStencilState 指向无操作状态 → **缓解**: 零初始化 depth stencil state 在 Vulkan spec 中是完全合法的有效状态，不会触发额外 VUID

## Open Questions

- _无_
