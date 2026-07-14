# Proposal: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Status**: Proposed
**Created**: 2026-07-12

---

## Why

`VulkanGraphExecutor::BuildPipelineStates()` 中 Vulkan Graphics Pipeline 的创建存在八项关键缺口（对抗验证发现三项额外缺陷），导致图形管线无法正常工作：

1. 顶点输入状态完全为空（顶点着色器无法接收属性数据）
2. GPL 路径深度模板状态缺失（传 nullptr）
3. **Monolithic 路径同样缺少深度模板状态**（`VkGraphicsPipelineCreateInfo::pDepthStencilState` 未设置）——对抗验证 CRITICAL 发现
4. 多渲染目标混合只处理单个 attachment，且 **Blend state 仅设置了 colorWriteMask，遗漏 blendEnable、src/dstColorBlendFactor、src/dstAlphaBlendFactor、colorBlendOp、alphaBlendOp**——对抗验证 MAJOR 发现
5. **Stencil state front/back 完全忽略**，`VkPipelineDepthStencilStateCreateInfo::front/back`（`VkStencilOpState`）未填充——对抗验证 MAJOR 发现
6. GPL 链接使用 VK_NULL_HANDLE 忽略实际 RenderPass 格式
7. 缺少 VkPipelineCache 导致每帧重建管线
8. **InterfaceTranslator.h 缺少 ECompareOp→vk::CompareOp、EStencilOp→vk::StencilOp、EBlendFactor→vk::BlendFactor、EBlendOp→vk::BlendOp 的转换函数**——对抗验证 CRITICAL 发现（旧 Vulkan 后端 `VulkanRenderBackend/private/InterfaceTranslator.h` 包含这些函数的参考实现，新后端需移植）

D3D12 后端在 `PipelineStatesObject.cpp` 中正确设置了全部 10 个 blend state 字段、完整的 stencil front/back、以及 depth-stencil state。新 Vulkan 后端需全面补齐。

## What Changes

- **Vertex Input**: 从 `DrawCallBatch::m_VertexInputDescs`（VertexInputsDescriptor：stride/perInstance/attributes[]）和 shader reflection `ShaderVertexAttributeData`（location/semantic name）构建 `VkPipelineVertexInputStateCreateInfo`，填充 `vertexBindingDescriptions` 和 `vertexAttributeDescriptions`。GPL 路径和 Monolithic 回退路径均需修复。同时将旧 `VulkanRenderBackend` 的 `VertexInputFormatToVkFormat` 移植到新 `InterfaceTranslator.h`。
- **Depth-Stencil（两路径共享）**: 从 `CPipelineStateObject::depthStencilStates`（已确认存在 depthTestEnable/depthWriteEnable/depthCompareOp/stencilTestEnable/stencilStateFront/stencilStateBack）构建 `VkPipelineDepthStencilStateCreateInfo`。关键修正：depth-stencil 构建逻辑提升到 if/else 之前，GPL `CreateFragmentOutputLibrary` 和 Monolithic `createInfo.pDepthStencilState` 共同使用。
- **Stencil Front/Back**: 从 `CPipelineStateObject::depthStencilStates.stencilStateFront/Back` 的 7 个字段（failOp/passOp/depthFailOp/compareOp/compareMask/writeMask/reference）映射到 `VkStencilOpState`，分别填充 `VkPipelineDepthStencilStateCreateInfo::front` 和 `back`。
- **MRT Blend（完整 10 字段）**: 为每个 color attachment 构造独立的 `VkPipelineColorBlendAttachmentState`，从 `attachmentBlendStates[i]` 读取全部 10 个字段：blendEnable、srcColorBlendFactor、dstColorBlendFactor、srcAlphaBlendFactor、dstAlphaBlendFactor、colorBlendOp、alphaBlendOp、colorWriteMask。以及 `VkPipelineColorBlendStateCreateInfo` 的 `logicOpEnable=false`、`logicOp=eClear`。
- **GPL 链接**: GPL `LinkPipeline` 传入实际 `VkRenderPass`（从 `GetOrCreateRenderPass(rpKey)` 获取），替代当前 `VK_NULL_HANDLE`。RenderPass 构建逻辑提升到 if/else 之前。
- **Pipeline Cache**: 在 `RenderBackend_Vulkan` 中创建 `vk::PipelineCache`，所有 `createGraphicsPipeline`/`createComputePipeline` 调用传入该 cache；初始化时尝试从磁盘加载缓存数据，释放时序列化到磁盘。
- **Enum 转换函数**: 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 函数，参考旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 中的实现。

## Capabilities

### New Capabilities
- `vulkan-pipeline-state`: Vulkan graphics pipeline 创建的完整性和正确性——涵盖 vertex input、depth-stencil（GPL+Monolithic）、stencil front/back、MRT blend（完整10字段）、GPL RenderPass 链接、PipelineCache、enum 转换函数共八项修复

### Modified Capabilities
- （无）本变更不修改任何已有 spec 的需求

## Impact

- **VulkanGraphExecutor.cpp** `BuildPipelineStates()`：核心修改——构建 vertex input state、depth-stencil state（提升到 if/else 前共享）、stencil front/back、MRT blend states（完整10字段）、GPL RenderPass 链接
- **InterfaceTranslator.h**（`VulkanRenderBackendNew/private/Utils/`）：新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 转换函数，参考旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 实现
- **VulkanPipelineLibrary.h/.cpp**：`CreateFragmentOutputLibrary` 签名不变（已有 `const* depthStencilState` 参数），仅调用侧传入非 nullptr；`LinkPipeline` 签名不变（已有 `VkRenderPass` 参数），仅调用侧传入真实 render pass；`CreateMonolithicPipeline` 和 `LinkPipeline` 新增 `VkPipelineCache` 参数
- **RenderBackend_Vulkan.h/.cpp**：新增 `vk::PipelineCache m_PipelineCache` 成员；Init 中创建 cache（尝试加载磁盘缓存）；Release 中序列化到磁盘并销毁
- **PipelineLibraryCache.h/.cpp**：`GenerateHashKey` 需包含 vertexInputDesc hash（当前仅含 vertexBindings/vertexAttributes 但未从实际数据填充）；需更新 `RenderStateCombination` 结构体以包含完整的 stencil/blend 状态字段

## Non-goals

- 不改动 Compute Pipeline（已完整）
- 不实现 Pipeline Derivatives
- 不实现 Pipeline Statistics
- 不优化 pipeline creation 性能（本变更聚焦正确性）
