# Tasks: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Created**: 2026-07-12
**Updated**: 2026-07-14（对抗验证后扩展：新增 Dynamic State、顶点算法重写、Hash/PipelineCache 补全）

---

## 1. 前置调研与数据确认

- [x] 1.1 ~~确认 `CPipelineStateObject` 中是否已有 depth-stencil 字段~~ **已确认**：`CPipelineStateObject` 包含完整的 `depthStencilStates`（`depthTestEnable`/`depthWriteEnable`/`depthCompareOp`/`stencilTestEnable`/`stencilStateFront`/`stencilStateBack`）和 `colorAttachments.attachmentBlendStates`（每个 attachment 含 `blendEnable`/`sourceColorBlendFactor`/`destColorBlendFactor`/`sourceAlphaBlendFactor`/`destAlphaBlendFactor`/`colorBlendOp`/`alphaBlendOp`/`channelMask` 共 8 个字段）。参考文件：`Interface/RenderInterface/header/CPipelineStateObject.h` line 50-137。
- [x] 1.2 确认 `VertexInputFormat` → `vk::Format` 的转换函数是否已有实现：新后端 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 无此函数，旧后端 `VulkanRenderBackend/private/InterfaceTranslator.h` line 17-30 有 `VertexInputFormatToVkFormat`。需移植。
- [x] 1.3 确认 `DrawCallBatch::m_VertexInputDescs` 的 key（`NameHash`）与 `ShaderReflectionData::m_VertexAttributes` 的 `m_SematicName`（`castl::string`）匹配机制——验证 `NameHash(castl::string const&)` 隐式构造函数和 `NameHash::operator==`（string 内容比较，非 hash 值比较）。确认 `ShaderVertexAttributeData` 的 `m_SematicIndex` 字段存在。

## 2. Enum 转换函数新增 [US4]

- [x] 2.1 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp(ECompareOp)` → `vk::CompareOp`，覆盖 `eAlways`/`eNever`/`eLEqual`/`eGEqual`/`eLess`/`eGreater`/`eEqual`/`eUnequal` 共 8 种。参考 `VulkanRenderBackend/private/InterfaceTranslator.h` line 443-457。
- [x] 2.2 在 `InterfaceTranslator.h` 中新增 `EStencilOpToVkStencilOp(EStencilOp)` → `vk::StencilOp`，覆盖 `eKeep`/`eReplace`/`eZero` 共 3 种。参考旧文件 line 432-441。
- [x] 2.3 在 `InterfaceTranslator.h` 中新增 `EBlendFactorToVkBlendFactor(EBlendFactor)` → `vk::BlendFactor`，覆盖 `eZero`/`eOne`/`eSrcAlpha`/`eOneMinusSrcAlpha`/`eDstAlpha`/`eOneMinusDstAlpha`/`eSrcColor`/`eOneMinusSrcColor`/`eDstColor`/`eOneMinusDstColor` 共 10 种。参考旧文件 line 459-474。
- [x] 2.4 在 `InterfaceTranslator.h` 中新增 `EBlendOpToVkBlendOp(EBlendOp)` → `vk::BlendOp`，覆盖 `eAdd`/`eSubtract`/`eReverseSubtract`/`eMin`/`eMax` 共 5 种。参考旧文件 line 477-488。
- [x] 2.5 在 `InterfaceTranslator.h` 中新增 `EVertexInputFormatToVkFormat(VertexInputFormat)` → `vk::Format`，覆盖 `eR32_SFloat`/`eR32G32_SFloat`/`eR32G32B32_SFloat`/`eR32G32B32A32_SFloat`/`eR8G8B8A8_UNorm`/`eR32_UInt`/`eR32_SInt` 共 7 种。参考旧文件 line 17-30。

## 3. Vertex Input 完整构建 [US1]

- [x] 3.1 在 `BuildPipelineStates` 中，将 `vertexInputState` 从空 `{}` 初始化为从 `batch.m_VertexInputDescs` 和 `pFileInfo->reflectionData.m_VertexAttributes` 构建的完整 `VkPipelineVertexInputStateCreateInfo`。**使用确定性单阶段算法**：以 `pFileInfo->reflectionData.m_VertexAttributes`（`vector`，确定性顺序）为外循环。对每个 shader attribute，通过 `NameHash(reflAttr.m_SematicName)` 在 `batch.m_VertexInputDescs` 中查找匹配的 buffer slot。维护 `castl::vector<NameHash> seenSlotKeys` —— 首次遇到新 slot 时 append，binding 索引 = 该 key 在 vector 中的位置。从匹配的 slot 的 `VertexInputsDescriptor` 创建 `VkVertexInputBindingDescription`（binding=deterministic_index, stride=desc.stride, inputRate=perInstance?eInstance:eVertex），从匹配的 `VertexAttribute` 创建 `VkVertexInputAttributeDescription`（location=refl.m_Location, binding=deterministic_index, format=EVertexInputFormatToVkFormat(attr.format), offset=attr.offset）。只对至少有一个 shader attribute 匹配的 slot 创建 binding（不产生孤儿 binding）。若 `m_VertexAttributes` 非空但 `m_VertexInputDescs` 为空，记录 CACore 警告日志。详细算法见 design.md D1。
- [x] 3.2 GPL 路径：确保 `CreateVertexInputLibrary` 接收的 `vertexInputState` 已填充完整（替换当前空 `{}` 初始化）。
- [x] 3.3 Monolithic 回退路径：确保 `createInfo.pVertexInputState` 指向已填充的 `vertexInputState`。
- [x] 3.4 **NEW（对抗验证 BLOCKING 发现）**：在 `BuildPipelineStates` 的 if/else 之前（与 depth-stencil、RenderPass、blendAttachments 同级）创建 `vk::PipelineDynamicStateCreateInfo dynamicState{}`，声明 `VK_DYNAMIC_STATE_VIEWPORT` 和 `VK_DYNAMIC_STATE_SCISSOR`。
- [x] 3.5 **NEW**：GPL 路径：将 `&dynamicState` 传入 `CreatePreRasterizationLibrary`（需新增 `pDynamicState` 参数，见 task 6.7）。同时传入 `pipelineCache`（见 task 6.7）。
- [x] 3.6 **NEW**：Monolithic 路径：设置 `createInfo.pDynamicState = &dynamicState`。

## 4. Depth-Stencil + RenderPass 提升（GPL + Monolithic 共享）

- [x] 4.1 在 `BuildPipelineStates` 中，将 `VkPipelineDepthStencilStateCreateInfo depthStencilState{}` 的构建放在 `if (pipelineLibrary.IsSupported())` / `else` 之前（建议在 line 1382 `multisampleState` 构建之后）。从 `pipelineStateData.depthStencilStates` 读取各字段：
  - `depthTestEnable` / `depthWriteEnable` 转 `VK_TRUE`/`VK_FALSE`
  - `depthCompareOp` 通过 `ECompareOpToVkCompareOp` 转换
  - `stencilTestEnable` 转 `VK_TRUE`/`VK_FALSE`
- [x] 4.2 Stencil front/back 映射：从 `pipelineStateData.depthStencilStates.stencilStateFront` 和 `.stencilStateBack`（`StencilStates` 各含 7 个字段：`failOp`/`passOp`/`depthFailOp`/`compareOp`/`compareMask`/`writeMask`/`reference`）映射到 `VkStencilOpState`，填充 `depthStencilState.front` 和 `depthStencilState.back`。辅助函数签名：
  ```cpp
  static vk::StencilOpState FillVkStencilOpState(DepthStencilStates::StencilStates const& src);
  ```
- [x] 4.3 将 RenderPass 构建逻辑（`RenderPassCacheKey` 填充 + `GetOrCreateRenderPass` 调用）从 else 分支提升到 `if/else` 之前（与 depth-stencil state 同级），两个路径共享 `VkRenderPass renderPass` 变量。
- [x] 4.4 GPL 路径：`CreateFragmentOutputLibrary` 传入 `&depthStencilState`（替换 `nullptr`）；`LinkPipeline` 传入实际的 `renderPass`（替换 `VK_NULL_HANDLE`）。
- [x] 4.5 Monolithic 路径：`createInfo.pDepthStencilState = &depthStencilState`；`createInfo.renderPass = renderPass`（使用提升后的变量）。**同时删除 else 分支中残留的 RenderPass 构建代码**（`RenderPassCacheKey` 填充 + `GetOrCreateRenderPass` 调用，原 lines 1415-1432），避免重复构建。

## 5. MRT Blend 完整 10 字段 [US3]

- [x] 5.1 将 `VkPipelineColorBlendAttachmentState colorBlendAttachment{}`（单个）改为 `castl::vector<vk::PipelineColorBlendAttachmentState> blendAttachments`（长度为 `rasterPass.GetColorAttachmentCount()`）。此 vector 也需放在 if/else 之前（与 depth-stencil state 共享同一层级）。
- [x] 5.2 遍历每个 color attachment（`i < attachmentCount`），从 `pipelineStateData.colorAttachments.attachmentBlendStates[i]` 读取并填充以下字段：
  - `blendEnable` → `VK_TRUE`/`VK_FALSE`
  - `srcColorBlendFactor` → `EBlendFactorToVkBlendFactor(src.sourceColorBlendFactor)`
  - `dstColorBlendFactor` → `EBlendFactorToVkBlendFactor(src.destColorBlendFactor)`
  - `srcAlphaBlendFactor` → `EBlendFactorToVkBlendFactor(src.sourceAlphaBlendFactor)`
  - `dstAlphaBlendFactor` → `EBlendFactorToVkBlendFactor(src.destAlphaBlendFactor)`
  - `colorBlendOp` → `EBlendOpToVkBlendOp(src.colorBlendOp)`
  - `alphaBlendOp` → `EBlendOpToVkBlendOp(src.alphaBlendOp)`
  - `colorWriteMask` → `EColorChannelMaskToVkColorComponentFlags(src.channelMask)`
- [x] 5.3 更新 `colorBlendState`：`pAttachments = blendAttachments.data()`，`attachmentCount = attachmentCount`。同时设置 `logicOpEnable = VK_FALSE`，`logicOp = vk::LogicOp::eClear`。
- [x] 5.4 若 `attachmentBlendStates` 定义不足（数据源 < attachmentCount），剩余 attachment 使用默认 blend state（blend disabled, write mask RGBA）。

## 6. VkPipelineCache [US5]

- [x] 6.1 在 `RenderBackend_Vulkan.h` 中新增 `vk::PipelineCache m_PipelineCache` 成员变量。
- [x] 6.2 在 `RenderBackend_Vulkan::Init` 中创建 `vk::PipelineCache`：尝试从文件 `pipeline_cache.bin` 加载 initialData。缓存文件路径使用应用工作目录。
- [x] 6.3 在 `RenderBackend_Vulkan::Release` 中通过 `getPipelineCacheData` 序列化 `m_PipelineCache` 到文件 `pipeline_cache.bin`，然后 `destroyPipelineCache`。
- [x] 6.4 修改 `VulkanPipelineLibrary::CreateMonolithicPipeline` 签名，新增 `vk::PipelineCache cache = nullptr` 参数，传入底层 `device.createGraphicsPipeline(cache, createInfo)`。
- [x] 6.5 修改 `VulkanPipelineLibrary::LinkPipeline` 签名，新增 `vk::PipelineCache cache = nullptr` 参数，传入底层 `device.createGraphicsPipeline(cache, linkingCreateInfo)`。
- [x] 6.6 在 `BuildPipelineStates` 中 GPL 路径：所有 library part 创建调用处传入 `pipelineCache = GetApp()->GetPipelineCache()`（`CreateVertexInputLibrary`、`CreatePreRasterizationLibrary`、`CreateFragmentLibrary`、`CreateFragmentOutputLibrary`、`LinkPipeline`）。Monolithic 路径：传入 `CreateMonolithicPipeline`。
- [x] 6.7 **NEW（对抗验证 2026-07-14）**：修改 4 个 GPL library part 创建函数签名，各新增 `vk::PipelineCache cache = nullptr` 参数：
  - `CreateVertexInputLibrary`: 新增 `vk::PipelineCache cache` 参数，`device.createGraphicsPipeline(cache, createInfo)`
  - `CreatePreRasterizationLibrary`: 新增 `const vk::PipelineDynamicStateCreateInfo* pDynamicState = nullptr` + `vk::PipelineCache cache = nullptr` 两个参数；`createInfo.pDynamicState = pDynamicState`；`device.createGraphicsPipeline(cache, createInfo)`
  - `CreateFragmentLibrary`: 新增 `vk::PipelineCache cache` 参数，`device.createGraphicsPipeline(cache, createInfo)`
  - `CreateFragmentOutputLibrary`: 新增 `vk::PipelineCache cache` 参数，`device.createGraphicsPipeline(cache, createInfo)`
- [x] 6.8 **NEW**：修改 `VulkanGraphExecutor.cpp` line 1504 compute pipeline 创建：`auto cache = GetApp()->GetPipelineCache(); device.createComputePipeline(cache, createInfo)` 替代 `device.createComputePipeline(nullptr, createInfo)`。
- [x] 6.9 **NEW**：修改 `VertexInputStateManager.cpp` line 44 GPL vertex input 创建：`GetDevice().createGraphicsPipeline(GetApp()->GetPipelineCache(), pipelineCreateInfo)` 替代 `createGraphicsPipeline(nullptr, ...)`。
- [x] 6.10 在 `RenderBackend_Vulkan.h` 中新增 `vk::PipelineCache const& GetPipelineCache() const { return m_PipelineCache; }` 访问器（注意与已有 `GetPipelineLibraryCache()` 区分命名）。

## 7. PipelineLibraryCache 完善 [US6]

- [x] 7.1 在 `RenderStateCombination` 结构体（`PipelineLibraryCache.h` line 10-28）中新增以下字段（显式、完整，禁用"等"模糊描述）：
  - **完整 depth/stencil 状态**: 存储整个 `DepthStencilStates` 结构体（含 `depthTestEnable`、`depthWriteEnable`、`stencilTestEnable`、`depthCompareOp`、`stencilStateFront`（7 fields: failOp/passOp/depthFailOp/compareOp/compareMask/writeMask/reference）、`stencilStateBack`（同上 7 fields），共 17 个独立状态值）
  - **`bool primitiveRestartEnable`**: 从 `inputAssemblyState` 的 `primitiveRestartEnable` 读取
  - **修正 colorFormat → `castl::vector<vk::Format> colorFormats`**: 替代当前单 `colorFormat` 字段（与 `RenderPassCacheKey::colorFormats` 一致）
- [x] 7.2 更新 `GenerateHashKey`（`PipelineLibraryCache.cpp` line 19-63），逐字段哈希以下内容：
  - **blend 循环扩展**：对每个 `blendAttachment`，hash 全部 8 个字段（`blendEnable`、`srcColorBlendFactor`、`dstColorBlendFactor`、`srcAlphaBlendFactor`、`dstAlphaBlendFactor`、`colorBlendOp`、`alphaBlendOp`、`colorWriteMask`）。注意：这些字段已在 `blendAttachments` 中，仅扩展 hashing loop，不新增 struct 字段。
  - **stencil/depth**：hash `depthTestEnable`、`depthWriteEnable`、`stencilTestEnable`、`depthCompareOp`，以及 front/back 各 7 个 stencil 字段（`failOp`/`passOp`/`depthFailOp`/`compareOp`/`compareMask`/`writeMask`/`reference`）。
  - **`primitiveRestartEnable`**
  - **修正 colorFormat**：遍历 `colorFormats` vector 逐个 `static_cast<uint32_t>(fmt)` hash（替代单 `colorFormat` 字段）
- [x] 7.3 在 `BuildPipelineStates` 中 pipeline 创建完成后，将完整的 `RenderStateCombination` 写入 `PipelineLibraryCache`（调用 `CachePipeline`），并在创建前尝试 `TryGetCachedPipeline`。确保 `RenderStateCombination` 在创建前已正确填充所有新增字段。

## 8. 编译验证与修复

- [x] 8.1 运行 `build.py`，若编译失败则分析并修复直到 BUILD SUCCESSFUL。
