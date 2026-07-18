# Proposal: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Status**: Proposed
**Created**: 2026-07-12

---

## Why

`VulkanGraphExecutor::BuildPipelineStates()` 中 Vulkan Graphics Pipeline 的创建存在九项关键缺口（对抗验证 2026-07-14 发现 Dynamic State 缺失等多项额外缺陷），导致图形管线无法正常工作：

1. 顶点输入状态完全为空（顶点着色器无法接收属性数据）
2. GPL 路径深度模板状态缺失（传 nullptr）
3. **Monolithic 路径同样缺少深度模板状态**（`VkGraphicsPipelineCreateInfo::pDepthStencilState` 未设置）——对抗验证 CRITICAL 发现
4. 多渲染目标混合只处理单个 attachment，且 **Blend state 仅设置了 colorWriteMask，遗漏 blendEnable、src/dstColorBlendFactor、src/dstAlphaBlendFactor、colorBlendOp、alphaBlendOp**——对抗验证 MAJOR 发现
5. **Stencil state front/back 完全忽略**，`VkPipelineDepthStencilStateCreateInfo::front/back`（`VkStencilOpState`）未填充——对抗验证 MAJOR 发现
6. GPL 链接使用 VK_NULL_HANDLE 忽略实际 RenderPass 格式
7. 缺少 VkPipelineCache 导致每帧重建管线
8. **InterfaceTranslator.h 缺少 ECompareOp→vk::CompareOp、EStencilOp→vk::StencilOp、EBlendFactor→vk::BlendFactor、EBlendOp→vk::BlendOp、EVertexInputFormat→vk::Format 的转换函数**——对抗验证 CRITICAL 发现（旧 Vulkan 后端 `VulkanRenderBackend/private/InterfaceTranslator.h` 包含这些函数的参考实现，新后端需移植）
9. **VkPipelineDynamicStateCreateInfo 完全缺失**——对抗验证 2026-07-14 BLOCKING 发现：`RecordRenderPass` 中调用 `cmdBuf.setViewport()`/`setScissor()` 动态设置 viewport/scissor，但 `BuildPipelineStates` 中既没有创建 `VkPipelineDynamicStateCreateInfo` 声明 `VK_DYNAMIC_STATE_VIEWPORT` + `VK_DYNAMIC_STATE_SCISSOR`，也没有在 `VkPipelineViewportStateCreateInfo` 中填充静态 viewport/scissor 数据（`pViewports`/`pScissors` 均为 nullptr）。违反 Vulkan 规范 VUID-VkGraphicsPipelineCreateInfo-pDynamicStates-00749/00750。即使前 8 项修复全部实施，viewport 仍为 0 大小 → 渲染画面必为空。

D3D12 后端在 `PipelineStatesObject.cpp` 中正确设置了全部 10 个 blend state 字段、完整的 stencil front/back、以及 depth-stencil state。新 Vulkan 后端需全面补齐。

## What Changes

- **Vertex Input**: 从 `DrawCallBatch::m_VertexInputDescs`（VertexInputsDescriptor：stride/perInstance/attributes[]）和 shader reflection `ShaderVertexAttributeData`（location/semantic name）构建 `VkPipelineVertexInputStateCreateInfo`，填充 `vertexBindingDescriptions` 和 `vertexAttributeDescriptions`。**关键修正（对抗验证 2026-07-14）**：算法必须以 shader reflection 的 `m_VertexAttributes`（`vector`，确定性顺序）为外循环，首次遇到新 slot 的 `NameHash` 时才分配 binding 索引（append 到 `vector<NameHash>`），避免 `unordered_map` 迭代顺序不确定性导致 PipelineLibraryCache hash 不稳定。只对至少有一个 shader attribute 匹配的 slot 创建 binding（跳过孤儿 binding）。GPL 路径和 Monolithic 回退路径均需修复。同时将旧 `VulkanRenderBackend` 的 `VertexInputFormatToVkFormat` 移植到新 `InterfaceTranslator.h`。
- **Depth-Stencil（两路径共享）**: 从 `CPipelineStateObject::depthStencilStates`（已确认存在 depthTestEnable/depthWriteEnable/depthCompareOp/stencilTestEnable/stencilStateFront/stencilStateBack）构建 `VkPipelineDepthStencilStateCreateInfo`。关键修正：depth-stencil 构建逻辑提升到 if/else 之前，GPL `CreateFragmentOutputLibrary` 和 Monolithic `createInfo.pDepthStencilState` 共同使用。
- **Stencil Front/Back**: 从 `CPipelineStateObject::depthStencilStates.stencilStateFront/Back` 的 7 个字段（failOp/passOp/depthFailOp/compareOp/compareMask/writeMask/reference）映射到 `VkStencilOpState`，分别填充 `VkPipelineDepthStencilStateCreateInfo::front` 和 `back`。
- **MRT Blend（完整 10 字段）**: 为每个 color attachment 构造独立的 `VkPipelineColorBlendAttachmentState`，从 `attachmentBlendStates[i]` 读取全部 10 个字段：blendEnable、srcColorBlendFactor、dstColorBlendFactor、srcAlphaBlendFactor、dstAlphaBlendFactor、colorBlendOp、alphaBlendOp、colorWriteMask。以及 `VkPipelineColorBlendStateCreateInfo` 的 `logicOpEnable=false`、`logicOp=eClear`。
- **GPL 链接**: GPL `LinkPipeline` 传入实际 `VkRenderPass`（从 `GetOrCreateRenderPass(rpKey)` 获取），替代当前 `VK_NULL_HANDLE`。RenderPass 构建逻辑提升到 if/else 之前。
- **Pipeline Cache**: 在 `RenderBackend_Vulkan` 中创建 `vk::PipelineCache`，所有 `createGraphicsPipeline`/`createComputePipeline` 调用传入该 cache；初始化时尝试从磁盘加载缓存数据，释放时序列化到磁盘。
- **Enum 转换函数**: 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 函数，参考旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 中的实现。
- **Dynamic State（新增，对抗验证 BLOCKING 发现）**: 在 `BuildPipelineStates` 中创建 `VkPipelineDynamicStateCreateInfo`，声明 `VK_DYNAMIC_STATE_VIEWPORT` 和 `VK_DYNAMIC_STATE_SCISSOR`。GPL 路径：传入 `CreatePreRasterizationLibrary`（需新增 `pDynamicState` 参数）。Monolithic 路径：设置 `createInfo.pDynamicState`。同时 `VulkanPipelineLibrary::CreatePreRasterizationLibrary` 签名需新增 `const vk::PipelineDynamicStateCreateInfo* pDynamicState = nullptr` 参数。

## Capabilities

### New Capabilities
- `vulkan-pipeline-state`: Vulkan graphics pipeline 创建的完整性和正确性——涵盖 vertex input、depth-stencil（GPL+Monolithic）、stencil front/back、MRT blend（完整10字段）、GPL RenderPass 链接、PipelineCache（全部 8 个调用点）、enum 转换函数（5 个）、Dynamic State（viewport+scissor）共九项修复

### Modified Capabilities
- （无）本变更不修改任何已有 spec 的需求

## Impact

- **VulkanGraphExecutor.cpp** `BuildPipelineStates()`：核心修改——构建 vertex input state（确定性单阶段算法）、depth-stencil state（提升到 if/else 前共享）、stencil front/back、MRT blend states（完整10字段）、**Dynamic State（VK_DYNAMIC_STATE_VIEWPORT + VK_DYNAMIC_STATE_SCISSOR）**、GPL RenderPass 链接
- **InterfaceTranslator.h**（`VulkanRenderBackendNew/private/Utils/`）：新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 转换函数，参考旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 实现
- **VulkanPipelineLibrary.h/.cpp**：`CreateFragmentOutputLibrary` 签名不变（已有 `const* depthStencilState` 参数），仅调用侧传入非 nullptr；`LinkPipeline` 签名不变（已有 `VkRenderPass` 参数），仅调用侧传入真实 render pass；`CreateMonolithicPipeline`、`LinkPipeline`、`CreateVertexInputLibrary`、`CreatePreRasterizationLibrary`、`CreateFragmentLibrary`、`CreateFragmentOutputLibrary` 新增 `VkPipelineCache` 参数；**`CreatePreRasterizationLibrary` 新增 `const vk::PipelineDynamicStateCreateInfo* pDynamicState` 参数**
- **RenderBackend_Vulkan.h/.cpp**：新增 `vk::PipelineCache m_PipelineCache` 成员和 `GetPipelineCache()` 访问器；Init 中创建 cache（尝试加载磁盘缓存，失败时回退到空 cache）；Release 中序列化到磁盘并销毁
- **PipelineLibraryCache.h/.cpp**：`RenderStateCombination` 新增完整 stencil/depth 字段（`DepthStencilStates` 整体存储）、`primitiveRestartEnable`、`colorFormats` vector（替代单 `colorFormat`）；`GenerateHashKey` 补全所有 blend attachment 字段（8/8）、stencil/depth 全字段、`primitiveRestartEnable`、多 color format
- **VertexInputStateManager.cpp**：line 44 `device.createGraphicsPipeline(nullptr, ...)` → 传入 `GetApp()->GetPipelineCache()`

## Non-goals

- 不改动 Compute Pipeline（已完整）
- 不实现 Pipeline Derivatives
- 不实现 Pipeline Statistics
- 不优化 pipeline creation 性能（本变更聚焦正确性）
