# Vulkan Pipeline State

**Version**: 1.0
**Created**: 2026-07-12

---

## ADDED Requirements

### Requirement: 顶点输入状态完整构建

`VulkanGraphExecutor::BuildPipelineStates` SHALL 从 `DrawCallBatch::m_VertexInputDescs` 和 `VulkanShaderFileInfo::reflectionData.m_VertexAttributes` 构建完整的 `VkPipelineVertexInputStateCreateInfo`，包含正确的 `vertexBindingDescriptions` 和 `vertexAttributeDescriptions`。

#### Scenario: 从 DrawCallBatch 构建 binding descriptions

- **WHEN** `DrawCallBatch::m_VertexInputDescs` 包含顶点 buffer 布局描述（stride、perInstance）
- **THEN** 系统为每个 buffer slot 创建 `VkVertexInputBindingDescription`，binding 为 slot 索引，stride 和 inputRate 从 `VertexInputsDescriptor` 读取

#### Scenario: 从 shader reflection 构建 attribute descriptions

- **WHEN** `ShaderReflectionData::m_VertexAttributes` 包含顶点属性信息（location、semantic name）
- **THEN** 系统按 semantic name 匹配 `VertexInputsDescriptor` 中的属性，为每个属性创建 `VkVertexInputAttributeDescription`，指定 location、binding、format 和 offset

#### Scenario: 空顶点输入

- **WHEN** `DrawCallBatch::m_VertexInputDescs` 为空（shader 不使用顶点输入）
- **THEN** `vertexInputState` 的 `vertexBindingDescriptionCount` 和 `vertexAttributeDescriptionCount` 均为 0（合法状态）

#### Scenario: GPL 路径使用 vertex input state

- **WHEN** GPL 扩展可用且创建 pipeline
- **THEN** `CreateVertexInputLibrary` 接收的 `vertexInputState` 包含从上述数据源构建的完整 binding/attribute descriptions

#### Scenario: Monolithic 回退路径使用 vertex input state

- **WHEN** GPL 扩展不可用，使用 Monolithic 回退路径创建 pipeline
- **THEN** `VkGraphicsPipelineCreateInfo::pVertexInputState` 指向包含完整 binding/attribute descriptions 的结构

### Requirement: GPL 路径深度模板状态

`VulkanGraphExecutor::BuildPipelineStates` 在 GPL 路径中 SHALL 将实际的 depth-stencil state 传入 `CreateFragmentOutputLibrary`，而非 `nullptr`。Monolithic 回退路径中 `VkGraphicsPipelineCreateInfo::pDepthStencilState` SHALL 同样指向该 state。两个路径共享同一 `VkPipelineDepthStencilStateCreateInfo` 对象。

#### Scenario: 深度测试启用

- **WHEN** pipeline state 中 depth test 启用
- **THEN** `CreateFragmentOutputLibrary`（GPL）和 `createInfo.pDepthStencilState`（Monolithic）均接收非空的 `VkPipelineDepthStencilStateCreateInfo` 指针，其中 `depthTestEnable = VK_TRUE`、`depthWriteEnable = VK_TRUE`、`depthCompareOp` 为正确的比较操作

#### Scenario: 深度测试禁用

- **WHEN** pipeline state 中 depth test 禁用
- **THEN** 两个路径均接收非空的 `VkPipelineDepthStencilStateCreateInfo` 指针，其中 `depthTestEnable = VK_FALSE`

#### Scenario: 无深度附件

- **WHEN** RenderPass 不包含深度附件
- **THEN** 可安全传入 `nullptr`（当前行为）或有显式注释说明无深度附件的场景

#### Scenario: Depth-Stencil 构建在条件分支之前

- **WHEN** BuildPipelineStates 构建 shared pipeline state
- **THEN** `VkPipelineDepthStencilStateCreateInfo` 的构建代码 SHALL 位于 `if (pipelineLibrary.IsSupported())` / `else` 之前，GPL 和 Monolithic 两个路径引用同一对象

### Requirement: MRT 每 Attachment 独立且完整的 Blend State

`VulkanGraphExecutor::BuildPipelineStates` SHALL 为每个 color attachment 创建独立的 `VkPipelineColorBlendAttachmentState`，填充全部 10 个字段（blendEnable、srcColorBlendFactor、dstColorBlendFactor、srcAlphaBlendFactor、dstAlphaBlendFactor、colorBlendOp、alphaBlendOp、colorWriteMask），而非仅 colorWriteMask。

#### Scenario: 单个 color attachment（完整字段）

- **WHEN** RenderPass 包含 1 个 color attachment
- **THEN** `colorBlendState` 的 `attachmentCount = 1`，`pAttachments` 指向 1 个 blend attachment state，包含全部 10 个字段

#### Scenario: 多个 color attachment（完整字段）

- **WHEN** RenderPass 包含 N 个 color attachment（N > 1）
- **THEN** `colorBlendState` 的 `attachmentCount = N`，`pAttachments` 指向 N 个独立的 blend attachment state，每个从 `attachmentBlendStates[i]` 读取全部 10 个字段

#### Scenario: 数据源不足

- **WHEN** `attachmentBlendStates` 中定义的 blend state 数量少于实际的 color attachment 数量
- **THEN** 超出部分使用默认 blend state（blendEnable=VK_FALSE, srcColorBlendFactor=eOne, dstColorBlendFactor=eZero, srcAlphaBlendFactor=eOne, dstAlphaBlendFactor=eZero, colorBlendOp=eAdd, alphaBlendOp=eAdd, colorWriteMask=RGBA）

#### Scenario: LogicOp 设置

- **WHEN** 构建 colorBlendState
- **THEN** `logicOpEnable = VK_FALSE`，`logicOp = vk::LogicOp::eClear`

#### Scenario: 单字段回退被替换

- **WHEN** 对抗验证发现当前代码仅设置了 colorWriteMask（line 1387-1388）
- **THEN** 代码 SHALL 替换为读取 `SingleColorAttachmentBlendStates` 的全部 8 个源字段并转换为 8 个 Vulkan 字段

### Requirement: GPL Pipeline 链接使用实际 RenderPass

`VulkanGraphExecutor::BuildPipelineStates` 在 GPL 路径中 SHALL 使用 `GetOrCreateRenderPass` 返回的实际 `VkRenderPass` 进行 pipeline linking，而非 `VK_NULL_HANDLE`。

#### Scenario: GPL 链接使用真实 RenderPass

- **WHEN** GPL 扩展可用且要进行 pipeline linking
- **THEN** `LinkPipeline` 接收从 `GetOrCreateRenderPass(RenderPassCacheKey)` 返回的非空 `VkRenderPass`，以及正确的 subpass index

#### Scenario: RenderPassCacheKey 构建复用

- **WHEN** GPL 路径和非 GPL 路径都需要 RenderPass
- **THEN** RenderPass 构建逻辑（RenderPassCacheKey 填充和 GetOrCreateRenderPass 调用）提升到 `if/else` 之前，两个路径共享

### Requirement: Stencil State Front/Back 完整映射

`VulkanGraphExecutor::BuildPipelineStates` SHALL 从 `CPipelineStateObject::depthStencilStates.stencilStateFront` 和 `stencilStateBack` 读取 stencil 状态，并填充 `VkPipelineDepthStencilStateCreateInfo::front` 和 `back` 的 `VkStencilOpState`。

#### Scenario: Stencil 启用时填充 front/back

- **WHEN** `stencilTestEnable == true`
- **THEN** `depthStencilState.front` 和 `depthStencilState.back` 各包含从对应 `StencilStates` 映射的 7 个字段：`failOp`→`EStencilOpToVkStencilOp`、`passOp`→`EStencilOpToVkStencilOp`、`depthFailOp`→`EStencilOpToVkStencilOp`、`compareOp`→`ECompareOpToVkCompareOp`、`compareMask`、`writeMask`、`reference`

#### Scenario: Stencil 禁用时仍填充默认值

- **WHEN** `stencilTestEnable == false`
- **THEN** `front` 和 `back` 填充默认值（保持与引擎层结构体一致），以免未初始化内存进入 Vulkan 调用

### Requirement: 枚举转换函数完整性

`VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` SHALL 包含以下 constexpr 转换函数，用于将引擎层枚举映射到 Vulkan API 枚举。

#### Scenario: Depth-Stencil 枚举转换

- **WHEN** 构建 depth-stencil state
- **THEN** `ECompareOpToVkCompareOp(ECompareOp)` 和 `EStencilOpToVkStencilOp(EStencilOp)` 可用，覆盖 ECompareOp 的 8 种值和 EStencilOp 的 3 种值

#### Scenario: Blend 枚举转换

- **WHEN** 构建 blend state
- **THEN** `EBlendFactorToVkBlendFactor(EBlendFactor)` 和 `EBlendOpToVkBlendOp(EBlendOp)` 可用，覆盖 EBlendFactor 的 10 种值和 EBlendOp 的 5 种值

#### Scenario: Vertex Format 枚举转换

- **WHEN** 构建 vertex input state
- **THEN** `EVertexInputFormatToVkFormat(VertexInputFormat)` 可用，覆盖 VertexInputFormat 的 7 种值

#### Scenario: 参考旧实现

- **WHEN** 新 InterfaceTranslator.h 缺失以上函数
- **THEN** 应参考 `VulkanRenderBackend/private/InterfaceTranslator.h` 中 `ECompareOpTranslate`（line 443）、`EStencilOpTranslate`（line 432）、`EBlendFactorTranslate`（line 459）、`EBlendOpTranslate`（line 477）、`VertexInputFormatToVkFormat`（line 17）的参考实现

### Requirement: PipelineLibraryCache Hash Key 覆盖

`PipelineLibraryCache::GenerateHashKey` SHALL 将 vertex input descriptor 的完整数据纳入哈希计算，`RenderStateCombination` 结构体 SHALL 包含足够的 stencil 状态字段以区分不同 stencil 配置的管线。

#### Scenario: Vertex Input Desc 纳入哈希

- **WHEN** 两个 pipeline 的 `DrawCallBatch::m_VertexInputDescs` 不同但 vertexBindings/vertexAttributes 相同
- **THEN** `GenerateHashKey` 返回不同的 hash 值（通过纳入 VertexInputsDescriptor 的整体序列化数据）

#### Scenario: Stencil 状态纳入缓存键

- **WHEN** 两个 pipeline 的区别仅在于 stencilTestEnable 或 stencil front/back 参数不同
- **THEN** `RenderStateCombination` 包含区分这些差异的字段，`GenerateHashKey` 返回不同的 hash 值

### Requirement: VulkanPipelineLibrary 接受 PipelineCache 参数

`VulkanPipelineLibrary::CreateMonolithicPipeline` 和 `LinkPipeline` SHALL 接受可选的 `vk::PipelineCache` 参数并传入底层 `createGraphicsPipeline` 调用。

#### Scenario: CreateMonolithicPipeline 传入 PipelineCache

- **WHEN** 调用 `CreateMonolithicPipeline(createInfo, cache)`
- **THEN** 底层调用 `device.createGraphicsPipeline(cache, createInfo)`，而非 `device.createGraphicsPipeline(nullptr, createInfo)`

#### Scenario: LinkPipeline 传入 PipelineCache

- **WHEN** 调用 `LinkPipeline(libraries, layout, renderPass, subpass, cache)`
- **THEN** 底层调用 `device.createGraphicsPipeline(cache, linkingCreateInfo)`，而非 `device.createGraphicsPipeline(nullptr, linkingCreateInfo)`

#### Scenario: 默认参数保持向后兼容

- **WHEN** 未传入 `cache` 参数
- **THEN** `cache` 默认为 `nullptr`，行为与当前一致

### Requirement: VkPipelineCache 创建和使用

`RenderBackend_Vulkan` SHALL 创建并管理 `vk::PipelineCache` 对象，用于所有 pipeline 创建以加速重复运行时的 pipeline 编译。

#### Scenario: PipelineCache 初始化

- **WHEN** `RenderBackend_Vulkan::Init` 执行
- **THEN** 创建 `vk::PipelineCache` 对象。如果磁盘上存在缓存文件（`pipeline_cache.bin`），将其数据作为 initialData 加载

#### Scenario: Pipeline 创建使用 Cache

- **WHEN** 任何 `createGraphicsPipeline` 或 `createComputePipeline` 调用
- **THEN** `pCreateInfo->pNext` 链中的 `VkPipelineCache` 字段或直接作为参数传入，指向 `RenderBackend_Vulkan::m_PipelineCache`（如果 pipeline 创建 API 支持）

#### Scenario: PipelineCache 持久化

- **WHEN** `RenderBackend_Vulkan::Release` 执行
- **THEN** 序列化 `m_PipelineCache` 数据到磁盘文件 `pipeline_cache.bin`，然后销毁 cache 对象

#### Scenario: 缓存文件不存在

- **WHEN** 首次运行且磁盘上无缓存文件
- **THEN** 正常创建空的 `VkPipelineCache`（initialDataSize = 0），无错误
