# Tasks: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Created**: 2026-07-12
**Updated**: 2026-07-12（对抗验证后扩展）

---

## 1. 前置调研与数据确认

- [ ] 1.1 ~~确认 `CPipelineStateObject` 中是否已有 depth-stencil 字段~~ **已确认**：`CPipelineStateObject` 包含完整的 `depthStencilStates`（`depthTestEnable`/`depthWriteEnable`/`depthCompareOp`/`stencilTestEnable`/`stencilStateFront`/`stencilStateBack`）和 `colorAttachments.attachmentBlendStates`（每个 attachment 含 `blendEnable`/`sourceColorBlendFactor`/`destColorBlendFactor`/`sourceAlphaBlendFactor`/`destAlphaBlendFactor`/`colorBlendOp`/`alphaBlendOp`/`channelMask` 共 8 个字段）。参考文件：`Interface/RenderInterface/header/CPipelineStateObject.h` line 50-137。
- [ ] 1.2 确认 `VertexInputFormat` → `vk::Format` 的转换函数是否已有实现：新后端 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 无此函数，旧后端 `VulkanRenderBackend/private/InterfaceTranslator.h` line 17-30 有 `VertexInputFormatToVkFormat`。需移植。
- [ ] 1.3 确认 `DrawCallBatch::m_VertexInputDescs` 的 key（`NameHash`）与 `ShaderReflectionData::m_VertexAttributes` 的 `m_SematicName` 匹配机制——验证 semantic name hash 一致性。

## 2. Enum 转换函数新增 [US4]

- [ ] 2.1 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp(ECompareOp)` → `vk::CompareOp`，覆盖 `eAlways`/`eNever`/`eLEqual`/`eGEqual`/`eLess`/`eGreater`/`eEqual`/`eUnequal` 共 8 种。参考 `VulkanRenderBackend/private/InterfaceTranslator.h` line 443-457。
- [ ] 2.2 在 `InterfaceTranslator.h` 中新增 `EStencilOpToVkStencilOp(EStencilOp)` → `vk::StencilOp`，覆盖 `eKeep`/`eReplace`/`eZero` 共 3 种。参考旧文件 line 432-441。
- [ ] 2.3 在 `InterfaceTranslator.h` 中新增 `EBlendFactorToVkBlendFactor(EBlendFactor)` → `vk::BlendFactor`，覆盖 `eZero`/`eOne`/`eSrcAlpha`/`eOneMinusSrcAlpha`/`eDstAlpha`/`eOneMinusDstAlpha`/`eSrcColor`/`eOneMinusSrcColor`/`eDstColor`/`eOneMinusDstColor` 共 10 种。参考旧文件 line 459-474。
- [ ] 2.4 在 `InterfaceTranslator.h` 中新增 `EBlendOpToVkBlendOp(EBlendOp)` → `vk::BlendOp`，覆盖 `eAdd`/`eSubtract`/`eReverseSubtract`/`eMin`/`eMax` 共 5 种。参考旧文件 line 477-488。
- [ ] 2.5 在 `InterfaceTranslator.h` 中新增 `EVertexInputFormatToVkFormat(VertexInputFormat)` → `vk::Format`，覆盖 `eR32_SFloat`/`eR32G32_SFloat`/`eR32G32B32_SFloat`/`eR32G32B32A32_SFloat`/`eR8G8B8A8_UNorm`/`eR32_UInt`/`eR32_SInt` 共 7 种。参考旧文件 line 17-30。

## 3. Vertex Input 完整构建 [US1]

- [ ] 3.1 在 `BuildPipelineStates` 中，将 `vertexInputState` 从空 `{}` 初始化为从 `batch.m_VertexInputDescs` 和 `pFileInfo->reflectionData.m_VertexAttributes` 构建的完整 `VkPipelineVertexInputStateCreateInfo`。遍历 `batch.m_VertexInputDescs`（`unordered_map<NameHash, HashObj<VertexInputsDescriptor>>`），对每个 buffer slot 创建 `VkVertexInputBindingDescription`（binding = slot index, stride = desc.stride, inputRate = perInstance ? eInstance : eVertex）。遍历 `pFileInfo->reflectionData.m_VertexAttributes`（`vector<ShaderVertexAttributeData>`），按 semantic name 匹配 descriptor 中的 attribute，创建 `VkVertexInputAttributeDescription`（location = refl.m_Location, binding = slot index, format = EVertexInputFormatToVkFormat(attr.format), offset = attr.offset）。
- [ ] 3.2 GPL 路径：确保 `CreateVertexInputLibrary` 接收的 `vertexInputState` 已填充完整（替换当前空 `{}` 初始化）。
- [ ] 3.3 Monolithic 回退路径：确保 `createInfo.pVertexInputState` 指向已填充的 `vertexInputState`。

## 4. Depth-Stencil + RenderPass 提升（GPL + Monolithic 共享）

- [ ] 4.1 在 `BuildPipelineStates` 中，将 `VkPipelineDepthStencilStateCreateInfo depthStencilState{}` 的构建放在 `if (pipelineLibrary.IsSupported())` / `else` 之前（建议在 line 1382 `multisampleState` 构建之后）。从 `pipelineStateData.depthStencilStates` 读取各字段：
  - `depthTestEnable` / `depthWriteEnable` 转 `VK_TRUE`/`VK_FALSE`
  - `depthCompareOp` 通过 `ECompareOpToVkCompareOp` 转换
  - `stencilTestEnable` 转 `VK_TRUE`/`VK_FALSE`
- [ ] 4.2 Stencil front/back 映射：从 `pipelineStateData.depthStencilStates.stencilStateFront` 和 `.stencilStateBack`（`StencilStates` 各含 7 个字段：`failOp`/`passOp`/`depthFailOp`/`compareOp`/`compareMask`/`writeMask`/`reference`）映射到 `VkStencilOpState`，填充 `depthStencilState.front` 和 `depthStencilState.back`。辅助函数签名：
  ```cpp
  static vk::StencilOpState FillVkStencilOpState(DepthStencilStates::StencilStates const& src);
  ```
- [ ] 4.3 将 RenderPass 构建逻辑（`RenderPassCacheKey` 填充 + `GetOrCreateRenderPass` 调用）从 else 分支提升到 `if/else` 之前（与 depth-stencil state 同级），两个路径共享 `VkRenderPass renderPass` 变量。
- [ ] 4.4 GPL 路径：`CreateFragmentOutputLibrary` 传入 `&depthStencilState`（替换 `nullptr`）；`LinkPipeline` 传入实际的 `renderPass`（替换 `VK_NULL_HANDLE`）。
- [ ] 4.5 Monolithic 路径：`createInfo.pDepthStencilState = &depthStencilState`；`createInfo.renderPass = renderPass`（使用提升后的变量，不再在 else 内重复构建）。

## 5. MRT Blend 完整 10 字段 [US3]

- [ ] 5.1 将 `VkPipelineColorBlendAttachmentState colorBlendAttachment{}`（单个）改为 `castl::vector<vk::PipelineColorBlendAttachmentState> blendAttachments`（长度为 `rasterPass.GetColorAttachmentCount()`）。此 vector 也需放在 if/else 之前（与 depth-stencil state 共享同一层级）。
- [ ] 5.2 遍历每个 color attachment（`i < attachmentCount`），从 `pipelineStateData.colorAttachments.attachmentBlendStates[i]` 读取并填充以下字段：
  - `blendEnable` → `VK_TRUE`/`VK_FALSE`
  - `srcColorBlendFactor` → `EBlendFactorToVkBlendFactor(src.sourceColorBlendFactor)`
  - `dstColorBlendFactor` → `EBlendFactorToVkBlendFactor(src.destColorBlendFactor)`
  - `srcAlphaBlendFactor` → `EBlendFactorToVkBlendFactor(src.sourceAlphaBlendFactor)`
  - `dstAlphaBlendFactor` → `EBlendFactorToVkBlendFactor(src.destAlphaBlendFactor)`
  - `colorBlendOp` → `EBlendOpToVkBlendOp(src.colorBlendOp)`
  - `alphaBlendOp` → `EBlendOpToVkBlendOp(src.alphaBlendOp)`
  - `colorWriteMask` → `EColorChannelMaskToVkColorComponentFlags(src.channelMask)`
- [ ] 5.3 更新 `colorBlendState`：`pAttachments = blendAttachments.data()`，`attachmentCount = attachmentCount`。同时设置 `logicOpEnable = VK_FALSE`，`logicOp = vk::LogicOp::eClear`。
- [ ] 5.4 若 `attachmentBlendStates` 定义不足（数据源 < attachmentCount），剩余 attachment 使用默认 blend state（blend disabled, write mask RGBA）。

## 6. VkPipelineCache [US5]

- [ ] 6.1 在 `RenderBackend_Vulkan.h` 中新增 `vk::PipelineCache m_PipelineCache` 成员变量。
- [ ] 6.2 在 `RenderBackend_Vulkan::Init` 中创建 `vk::PipelineCache`：尝试从文件 `pipeline_cache.bin` 加载 initialData。缓存文件路径使用应用工作目录。
- [ ] 6.3 在 `RenderBackend_Vulkan::Release` 中通过 `getPipelineCacheData` 序列化 `m_PipelineCache` 到文件 `pipeline_cache.bin`，然后 `destroyPipelineCache`。
- [ ] 6.4 修改 `VulkanPipelineLibrary::CreateMonolithicPipeline` 签名，新增 `vk::PipelineCache cache = nullptr` 参数，传入底层 `device.createGraphicsPipeline(cache, createInfo)`。
- [ ] 6.5 修改 `VulkanPipelineLibrary::LinkPipeline` 签名，新增 `vk::PipelineCache cache = nullptr` 参数，传入底层 `device.createGraphicsPipeline(cache, linkingCreateInfo)`。
- [ ] 6.6 在 `BuildPipelineStates` 中所有 pipeline 创建调用处传入 `GetApp()->GetPipelineCache()`（需在 `RenderBackend_Vulkan` 中提供 `GetPipelineCache()` 访问器）。

## 7. PipelineLibraryCache 完善 [US6]

- [ ] 7.1 在 `RenderStateCombination` 结构体（`PipelineLibraryCache.h` line 10-28）中新增字段：
  - Vertex input desc 整体哈希值（如 `uint64_t vertexInputDescHash`，通过对 `VertexInputsDescriptor` 的 stride/perInstance/attributes[] 整体序列化后计算）
  - Stencil state 相关字段：`ECompareOp stencilFrontCompareOp`、`ECompareOp stencilBackCompareOp`、`EStencilOp stencilFrontFailOp` 等（或直接存整个 `DepthStencilStates`）
- [ ] 7.2 更新 `GenerateHashKey`（`PipelineLibraryCache.cpp` line 19-63）：将新增字段纳入哈希计算。
- [ ] 7.3 在 `BuildPipelineStates` 中 pipeline 创建完成后，将完整的 `RenderStateCombination` 写入 `PipelineLibraryCache`（调用 `CachePipeline`），并在创建前尝试 `TryGetCachedPipeline`。

## 8. 编译验证与修复

- [ ] 8.1 运行 `build.py`，若编译失败则分析并修复直到 BUILD SUCCESSFUL。
