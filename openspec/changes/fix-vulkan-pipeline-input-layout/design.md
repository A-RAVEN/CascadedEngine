# Design: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Created**: 2026-07-12

---

## Context

`VulkanGraphExecutor::BuildPipelineStates()` 当前存在八项关键缺口（对抗验证后更新）：

| #  | 缺口                                                       | 位置                                    | 严重度   |
|----|-------------------------------------------------------------|-----------------------------------------|----------|
| C1 | vertexInputState 的 binding/attribute 为空                  | `BuildPipelineStates` line 1369         | CRITICAL |
| C2 | InterfaceTranslator.h 缺少 ECompareOp/EStencilOp/EBlendFactor/EBlendOp 转换函数 | `InterfaceTranslator.h`                    | CRITICAL |
| C3 | Monolithic 路径 pDepthStencilState 未设置（与 C4 同源）     | `BuildPipelineStates` line 1439         | CRITICAL |
| C4 | GPL 路径 depth-stencil 传 nullptr                           | `BuildPipelineStates` line 1401         | CRITICAL |
| M1 | Blend state 仅设置 colorWriteMask，遗漏其余9个字段          | `BuildPipelineStates` line 1386-1392    | MAJOR    |
| M2 | Stencil state front/back 完全忽略                           | `BuildPipelineStates`（未实现）         | MAJOR    |
| M3 | PipelineLibraryCache::GenerateHashKey 未包含 vertexInputDesc hash | `PipelineLibraryCache.cpp` line 19-63       | MAJOR    |
| M4 | GPL LinkPipeline 传 VK_NULL_HANDLE                          | `BuildPipelineStates` line 1410-1411    | MEDIUM   |
| M5 | 无 VkPipelineCache                                          | `BuildPipelineStates` 全局              | MEDIUM   |

D3D12 参考实现（`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp::Init`）正确构建了 `D3D12_INPUT_ELEMENT_DESC` 数组（从 `vertexInputDescs` 映射 vertex buffer slots 到 shader semantic names）、完整的 depth-stencil state、和 per-attachment blend states。

## Goals / Non-Goals

**Goals:**
- 从 `DrawCallBatch::m_VertexInputDescs` 和 shader reflection 数据构建完整的 `VkPipelineVertexInputStateCreateInfo`
- GPL `CreateFragmentOutputLibrary` 和 Monolithic `createInfo.pDepthStencilState` 均传入实际 depth-stencil state（两路径共享）
- 为每个 color attachment 创建独立的 `VkPipelineColorBlendAttachmentState`，包含全部 10 个字段
- Stencil state front/back 完整映射到 `VkStencilOpState`
- GPL `LinkPipeline` 传入实际 `VkRenderPass`
- 创建 `vk::PipelineCache` 并用于所有 pipeline 创建
- 在 `InterfaceTranslator.h` 中新增 4 个枚举转换函数（ECompareOp/EStencilOp/EBlendFactor/EBlendOp）

**Non-Goals:**
- 不改动 Compute Pipeline
- 不实现 Pipeline Derivatives
- 不实现 Pipeline Statistics
- 不优化 pipeline creation 性能

---

## Decisions

### D1: Vertex Input 从 DrawCallBatch + Shader Reflection 构建

**选择**: 从 `batch.m_VertexInputDescs`（`VertexInputDescMap`，key 为 NameHash）获取 `VertexInputsDescriptor`（含 stride、perInstance、attributes[]），结合 `ShaderReflectionData::m_VertexAttributes`（含 location、semantic name/index）的 location 信息，构建 Vulkan 的 `vertexBindingDescriptions` 和 `vertexAttributeDescriptions`。

**原理**:
- `VertexInputsDescriptor` 描述了顶点 buffer 的布局（stride、perInstance、每个 attribute 的 offset/format/semanticName）
- `ShaderReflectionData::m_VertexAttributes` 描述了 shader 中顶点属性的 location（从 Slang 编译器反射）
- 需要将两者按 semantic name 匹配：`vertexAttributeDescriptions` 的 `location` 来自 shader reflection，`binding` 来自 buffer slot index，`format` 和 `offset` 来自 `VertexInputsDescriptor`
- `vertexBindingDescriptions` 直接来自 `VertexInputsDescriptor`：`binding` = buffer slot index，`stride` = `desc.stride`，`inputRate` = perInstance ? `eInstance` : `eVertex`

**数据结构映射**:
```
DrawCallBatch::m_VertexInputDescs: unordered_map<NameHash, HashObj<VertexInputsDescriptor>>
  ├─ key: 语义名称的 hash（如 "POSITION" 的 NameHash）
  └─ value: VertexInputsDescriptor { stride, perInstance, attributes: [{offset, format, semanticName, semanticIndex}] }

ShaderReflectionData::m_VertexAttributes: vector<ShaderVertexAttributeData>
  └─ each: { m_Name, m_SematicName, m_SematicIndex, m_Location }

output vertexBindingDescriptions: vector<VertexInputBindingDescription>
  └─ each: { binding=i, stride=desc.stride, inputRate=perInstance ? INSTANCE : VERTEX }

output vertexAttributeDescriptions: vector<VertexInputAttributeDescription>
  └─ each: { location=refl.m_Location, binding=i, format=ConvertVertexFormat(attr.format), offset=attr.offset }
```

**关键实现细节**:
- 需要 `ConvertVertexFormat(VertexInputFormat) → vk::Format` 转换函数（或复用已有转换）
- 如果 `m_VertexInputDescs` 为空（shader 不使用顶点输入），则 vertexInputState 保持空（合法——用于 procedural generation 等场景）
- 代码需在 line 1369 处构建此状态，并在 GPL（line 1396）和 Monolithic（line 1439）两个路径中使用

**备选方案**: 仅从 shader reflection 构建——被拒绝，因为 shader reflection 不包含 stride 和 perInstance 信息，这些来自 buffer 布局。

### D2: Depth-Stencil 从 CPipelineStateObject 读取（GPL + Monolithic 共享）

**选择**: 从 `pipelineStateData`（`CPipelineStateObject`）的 `depthStencilStates` 字段读取 depth/stencil 状态，构造 `VkPipelineDepthStencilStateCreateInfo`。**关键修正**：将 depth-stencil 构建提升到 `if (pipelineLibrary.IsSupported())` / `else` 之前，GPL `CreateFragmentOutputLibrary` 和 Monolithic `createInfo.pDepthStencilState` 共同使用。

**原理**:
- `CPipelineStateObject::depthStencilStates`（`DepthStencilStates`）包含 `depthTestEnable`、`depthWriteEnable`、`depthCompareOp`、`stencilTestEnable`、`stencilStateFront`、`stencilStateBack`——均已在 `Interface/RenderInterface/header/CPipelineStateObject.h` 中确认存在
- GPL 路径当前在 line 1401 传 `nullptr`，需改为 `&depthStencilState`
- Monolithic 路径在 line 1439 创建 `VkGraphicsPipelineCreateInfo` 时完全未设置 `pDepthStencilState`，需新增
- 将 `VkPipelineDepthStencilStateCreateInfo depthStencilState{}` 的构建逻辑放在 `if/else` 之前（line 1382 之后），两个路径共享同一对象
- stencil state 的 front/back 映射见 D7

**数据映射**:
```cpp
vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
depthStencilState.depthTestEnable = pipelineStateData.depthStencilStates.depthTestEnable ? VK_TRUE : VK_FALSE;
depthStencilState.depthWriteEnable = pipelineStateData.depthStencilStates.depthWriteEnable ? VK_TRUE : VK_FALSE;
depthStencilState.depthCompareOp = ECompareOpToVkCompareOp(pipelineStateData.depthStencilStates.depthCompareOp);
depthStencilState.stencilTestEnable = pipelineStateData.depthStencilStates.stencilTestEnable ? VK_TRUE : VK_FALSE;
depthStencilState.front = /* 见 D7 */;
depthStencilState.back = /* 见 D7 */;
depthStencilState.minDepthBounds = 0.0f;
depthStencilState.maxDepthBounds = 1.0f;
```

**备选方案**: 仅修复 GPL 路径忽略 Monolithic 路径——被拒绝，因为两个路径均需 depth-stencil state 以保证管线正确性。

### D3: MRT Blend 为每个 Attachment 独立构建（完整 10 字段）

**选择**: 构建 `castl::vector<VkPipelineColorBlendAttachmentState>`，长度为 `rasterPass.GetColorAttachmentCount()`，每个元素从 `pipelineStateData.colorAttachments.attachmentBlendStates[i]` 读取**全部 10 个字段**。

**对抗验证发现**: 当前代码仅设置了 `colorWriteMask`，遗漏了 `blendEnable`、`srcColorBlendFactor`、`dstColorBlendFactor`、`srcAlphaBlendFactor`、`dstAlphaBlendFactor`、`colorBlendOp`、`alphaBlendOp` 七个关键字段。D3D12 后端在 `PipelineStatesObject.cpp` line 70-77 正确设置了所有字段。

**原理**:
- 当前代码仅创建一个 `colorBlendAttachment`，对所有 color attachment 共享同一 blend state，且只设置了 `colorWriteMask`
- 正确做法是为每个 color attachment 创建独立 blend state，支持不同 attachment 使用不同混合模式
- 如果 `attachmentBlendStates` 的数据不足（如仅定义了一个），剩余 attachment 使用默认 blend state（blend disabled, write mask RGBA）

**数据映射（参考 D3D12 line 70-77）**:
```cpp
castl::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
blendAttachments.resize(attachmentCount);
for (uint32_t i = 0; i < attachmentCount; ++i) {
    auto& src = pipelineStateData.colorAttachments.attachmentBlendStates[i];
    blendAttachments[i].blendEnable         = src.blendEnable ? VK_TRUE : VK_FALSE;
    blendAttachments[i].srcColorBlendFactor = EBlendFactorToVkBlendFactor(src.sourceColorBlendFactor);
    blendAttachments[i].dstColorBlendFactor = EBlendFactorToVkBlendFactor(src.destColorBlendFactor);
    blendAttachments[i].srcAlphaBlendFactor = EBlendFactorToVkBlendFactor(src.sourceAlphaBlendFactor);
    blendAttachments[i].dstAlphaBlendFactor = EBlendFactorToVkBlendFactor(src.destAlphaBlendFactor);
    blendAttachments[i].colorBlendOp        = EBlendOpToVkBlendOp(src.colorBlendOp);
    blendAttachments[i].alphaBlendOp        = EBlendOpToVkBlendOp(src.alphaBlendOp);
    blendAttachments[i].colorWriteMask      = EColorChannelMaskToVkColorComponentFlags(src.channelMask);
}
colorBlendState.attachmentCount = attachmentCount;
colorBlendState.pAttachments = blendAttachments.data();
colorBlendState.logicOpEnable = VK_FALSE;
colorBlendState.logicOp = vk::LogicOp::eClear;
```


**注意**: `attachmentBlendStates` 是 `castl::array<T,8>`（定长数组），`.size()` 永远返回 8。正确长度来源是 `rasterPass.GetColorAttachmentCount()`。

### D4: RenderPass 构建逻辑提升（if/else 前共享）

**选择**: 将 RenderPass 构建逻辑（`RenderPassCacheKey` 填充 + `GetOrCreateRenderPass` 调用）从 else 分支（Monolithic）提升到 `if (pipelineLibrary.IsSupported())` / `else` 之前。GPL `LinkPipeline` 使用提升后的 `VkRenderPass`（替换 `VK_NULL_HANDLE`），Monolithic `createInfo.renderPass` 同样使用。

**原理**:
- 当前 GPL 路径 line 1410-1411 使用 `dummyRenderPass = VK_NULL_HANDLE`
- 当前 Monolithic 路径（else 分支 line 1415-1432）正确构建了 `RenderPassCacheKey` 并获取了真实 `VkRenderPass`
- 将 RenderPass 构建逻辑提升到 if/else 之前（与 depth-stencil state 一起），两个路径共享同一 render pass 对象
- 这一优化与 D2（depth-stencil 提升）共同将 3 个共享状态（depth-stencil、RenderPass、blendAttachments vector）都放在条件分支之前

**GPL 与 RenderPass 兼容性**: GPL library parts 创建时未指定 render pass（当前行为），链接时使用真实 render pass 是合法且推荐的。

### D5: VkPipelineCache 在 RenderBackend_Vulkan 中管理

**选择**: 在 `RenderBackend_Vulkan` 中添加 `vk::PipelineCache m_PipelineCache` 成员。Init 时创建（尝试从文件加载缓存数据），Release 时序列化到文件并销毁。

**原理**:
- `VkPipelineCache` 是 Vulkan 标准机制，允许多次运行时复用 pipeline 编译结果
- 在 `BuildPipelineStates` 中所有 `createGraphicsPipeline`/`createComputePipeline` 调用传入 `pPipelineCache = &m_PipelineCache`
- `VulkanPipelineLibrary::CreateMonolithicPipeline` 和 `LinkPipeline` 需接受可选的 `VkPipelineCache*` 参数（或直接通过 `GetApp()` 访问）

**缓存文件路径**: 使用应用工作目录下的 `pipeline_cache.bin` 文件。
**实现细节**:
```cpp
// Init:
vk::PipelineCacheCreateInfo cacheInfo{};
// 尝试从文件加载 initialData
std::ifstream cacheFile("pipeline_cache.bin", std::ios::binary | std::ios::ate);
if (cacheFile.is_open()) {
    size_t fileSize = cacheFile.tellg();
    cacheFile.seekg(0);
    std::vector<uint8_t> cacheData(fileSize);
    cacheFile.read(reinterpret_cast<char*>(cacheData.data()), fileSize);
    cacheInfo.initialDataSize = fileSize;
    cacheInfo.pInitialData = cacheData.data();
}
m_PipelineCache = m_Device.createPipelineCache(cacheInfo);

// Release:
auto cacheData = m_Device.getPipelineCacheData(m_PipelineCache);
std::ofstream cacheFile("pipeline_cache.bin", std::ios::binary);
cacheFile.write(reinterpret_cast<const char*>(cacheData.data()), cacheData.size());
m_Device.destroyPipelineCache(m_PipelineCache);
```

### D6: InterfaceTranslator.h 新增枚举转换函数

**选择**: 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 函数。

**对抗验证发现**: 当前 InterfaceTranslator.h 仅有 6 个转换函数（ETopology/EPolygonMode/ECullMode/EFrontFace/EColorChannelMask），缺少深度模板和混合相关的 4 个枚举转换函数。旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 在 line 432-488 已包含 `EStencilOpTranslate`、`ECompareOpTranslate`、`EBlendFactorTranslate`、`EBlendOpTranslate` 的参考实现。

**原理**: 所有枚举转换集中在一个头文件中，保持命名风格一致（`E<Source>ToVk<Dest>`）。函数均为 `constexpr`，编译期展开无运行时开销。

**参考实现（旧 Vulkan 后端）**:
```cpp
constexpr vk::CompareOp ECompareOpToVkCompareOp(ECompareOp inCompareOp)
{
    switch (inCompareOp)
    {
    case ECompareOp::eAlways:   return vk::CompareOp::eAlways;
    case ECompareOp::eNever:    return vk::CompareOp::eNever;
    case ECompareOp::eLEqual:   return vk::CompareOp::eLessOrEqual;
    case ECompareOp::eGEqual:   return vk::CompareOp::eGreaterOrEqual;
    case ECompareOp::eLess:     return vk::CompareOp::eLess;
    case ECompareOp::eGreater:  return vk::CompareOp::eGreater;
    case ECompareOp::eEqual:    return vk::CompareOp::eEqual;
    case ECompareOp::eUnequal:  return vk::CompareOp::eNotEqual;
    default: return vk::CompareOp::eAlways;
    }
}

constexpr vk::StencilOp EStencilOpToVkStencilOp(EStencilOp inStencilOp)
{
    switch (inStencilOp)
    {
    case EStencilOp::eKeep:     return vk::StencilOp::eKeep;
    case EStencilOp::eReplace:  return vk::StencilOp::eReplace;
    case EStencilOp::eZero:     return vk::StencilOp::eZero;
    default: return vk::StencilOp::eKeep;
    }
}

constexpr vk::BlendFactor EBlendFactorToVkBlendFactor(EBlendFactor inBlendFactor)
{
    switch (inBlendFactor)
    {
    case EBlendFactor::eZero:                return vk::BlendFactor::eZero;
    case EBlendFactor::eOne:                 return vk::BlendFactor::eOne;
    case EBlendFactor::eSrcAlpha:            return vk::BlendFactor::eSrcAlpha;
    case EBlendFactor::eOneMinusSrcAlpha:    return vk::BlendFactor::eOneMinusSrcAlpha;
    case EBlendFactor::eDstAlpha:            return vk::BlendFactor::eDstAlpha;
    case EBlendFactor::eOneMinusDstAlpha:    return vk::BlendFactor::eOneMinusDstAlpha;
    case EBlendFactor::eSrcColor:            return vk::BlendFactor::eSrcColor;
    case EBlendFactor::eOneMinusSrcColor:    return vk::BlendFactor::eOneMinusSrcColor;
    case EBlendFactor::eDstColor:            return vk::BlendFactor::eDstColor;
    case EBlendFactor::eOneMinusDstColor:    return vk::BlendFactor::eOneMinusDstColor;
    default: return vk::BlendFactor::eZero;
    }
}

constexpr vk::BlendOp EBlendOpToVkBlendOp(EBlendOp inBlendOp)
{
    switch (inBlendOp)
    {
    case EBlendOp::eAdd:              return vk::BlendOp::eAdd;
    case EBlendOp::eSubtract:         return vk::BlendOp::eSubtract;
    case EBlendOp::eReverseSubtract:  return vk::BlendOp::eReverseSubtract;
    case EBlendOp::eMin:              return vk::BlendOp::eMin;
    case EBlendOp::eMax:              return vk::BlendOp::eMax;
    default: return vk::BlendOp::eAdd;
    }
}
```

**VertexInputFormat 枚举值说明**: 当前引擎层 `VertexInputFormat` 枚举仅定义了 7 种格式（`eR32_SFloat`、`eR32G32_SFloat`、`eR32G32B32_SFloat`、`eR32G32B32A32_SFloat`、`eR8G8B8A8_UNorm`、`eR32_UInt`、`eR32_SInt`），design 中提到的 RGBA32F 等名称不存在。`EVertexInputFormatToVkFormat` 仅需覆盖这 7 个值。

### D7: Stencil State Front/Back 完整映射

**选择**: 从 `CPipelineStateObject::depthStencilStates.stencilStateFront` 和 `.stencilStateBack`（`StencilStates` 结构体各含 7 个字段）映射到 `VkStencilOpState`。

**对抗验证发现**: 当前代码完全未处理 stencil state。`VkPipelineDepthStencilStateCreateInfo` 的 `front` 和 `back` 成员（`VkStencilOpState`）需填充。

**数据映射**:
```cpp
vk::StencilOpState fillVkStencilOpState(DepthStencilStates::StencilStates const& src)
{
    return vk::StencilOpState{
        .failOp      = EStencilOpToVkStencilOp(src.failOp),
        .passOp      = EStencilOpToVkStencilOp(src.passOp),
        .depthFailOp = EStencilOpToVkStencilOp(src.depthFailOp),
        .compareOp   = ECompareOpToVkCompareOp(src.compareOp),
        .compareMask = src.compareMask,
        .writeMask   = src.writeMask,
        .reference   = src.reference,
    };
}
// ...
depthStencilState.front = fillVkStencilOpState(pipelineStateData.depthStencilStates.stencilStateFront);
depthStencilState.back  = fillVkStencilOpState(pipelineStateData.depthStencilStates.stencilStateBack);
```

如果 `stencilTestEnable` 为 `false`，则 `front` 和 `back` 可留默认值（Vulkan 规范允许），但仍显式填充以保持一致性。

### D8: PipelineLibraryCache::GenerateHashKey 纳入 vertexInputDesc

**选择**: 在 `RenderStateCombination` 中新增 `vulkanVertexInputDescHash` 字段（通过对 `DrawCallBatch::m_VertexInputDescs` 各描述符序列化后计算 sha256_hash），`GenerateHashKey` 中将其纳入哈希计算。

**对抗验证发现**: tasks 未覆盖此变更，但 `GenerateHashKey` 当前仅哈希 `vertexBindings`/`vertexAttributes`（line 24-39），其数据来源是 `RenderStateCombination`，而该 struct 的 `vertexBindings`/`vertexAttributes` 当前从未被实际填充（在 `BuildPipelineStates` 中未写入 PipelineLibraryCache）。需要：

1. 在 `GenerateHashKey` 中新增对 `VertexInputsDescriptor` 整体数据的哈希（如各 attribute 的 format/offset/semanticName）
2. 同时在 `RenderStateCombination` 中补充 stencil 相关字段（`compareOp`、`stencilFailOp`、`stencilPassOp`、`stencilDepthFailOp`、`compareMask`、`writeMask`、`reference` for front and back）
3. 或在 `BuildPipelineStates` 中构建完 vertex input 后，将数据写入 `RenderStateCombination` 并更新 hash

**备选方案**: 直接用整个 `VkPipelineVertexInputStateCreateInfo` 的序列化数据做 hash——更简单但耦合 Vulkan 结构体。

### D9: VulkanPipelineLibrary 签名变更

**选择**: `VulkanPipelineLibrary::CreateMonolithicPipeline` 和 `LinkPipeline` 接受可选的 `VkPipelineCache` 参数，传入底层 `createGraphicsPipeline` 调用。

**原理**: 当前两个函数的签名不包含 pipeline cache 参数：
```cpp
vk::Pipeline CreateMonolithicPipeline(vk::GraphicsPipelineCreateInfo const& createInfo);
vk::Pipeline LinkPipeline(PipelineLibraryParts const& libraries, vk::PipelineLayout layout, vk::RenderPass renderPass, uint32_t subpass);
```
需变更为：
```cpp
vk::Pipeline CreateMonolithicPipeline(vk::GraphicsPipelineCreateInfo const& createInfo, vk::PipelineCache cache = nullptr);
vk::Pipeline LinkPipeline(PipelineLibraryParts const& libraries, vk::PipelineLayout layout, vk::RenderPass renderPass, uint32_t subpass, vk::PipelineCache cache = nullptr);
```
调用侧在 `BuildPipelineStates` 中传入 `GetApp()->GetPipelineCache()`。

---

## Risks / Trade-offs

- **[Vertex Input 匹配失败]**：如果 `DrawCallBatch::m_VertexInputDescs` 为空但 shader 需要顶点输入，或 semantic name 不匹配，会导致 vertex input 为空。  
  → 缓解：添加 CACore 日志警告，并允许空 vertex input state（合法用于某些场景）。

- **[VertexFormat 转换缺失]**：引擎层 `VertexInputFormat` 枚举仅 7 种格式（`eR32_SFloat` 到 `eR32_SInt`），转换函数仅需覆盖这 7 种即可。  
  → 缓解：映射表不会遗漏——枚举值有限，可在 `EVertexInputFormatToVkFormat` 中完整覆盖。

- **[EStencilOp 枚举值有限]**：引擎层 `EStencilOp` 仅定义了 `eKeep`/`eReplace`/`eZero` 三种，转换函数仅需覆盖这三种。  
  → 缓解：不会遗漏。

- **[PipelineCache 序列化大小]**：首次运行时 pipeline cache 为空，多次运行后可能达到数 MB。  
  → 缓解：无重大风险——这是 Vulkan 标准机制，驱动程序管理缓存大小。

- **[GPL RenderPass 变更]**：使用真实 render pass 替代 VK_NULL_HANDLE 可能影响 GPL library part 的复用。  
  → 缓解：GPL library parts 创建时未绑定 render pass，链接时使用真实 render pass 确保正确性。

- **[Blend 枚举转换完整度]**：`EBlendFactor` 包含 10 种值，`EBlendOp` 包含 5 种值，转换函数需完整映射。  
  → 缓解：参考旧 Vulkan 后端的完整实现，不会遗漏。

---

## Open Questions

- Q1: ~~`CPipelineStateObject` 中是否已包含 depth-stencil state 字段~~  
  → **已确认**: `CPipelineStateObject` 包含完整的 `depthStencilStates`（含 `depthTestEnable`/`depthWriteEnable`/`depthCompareOp`/`stencilTestEnable`/`stencilStateFront`/`stencilStateBack`），以及完整的 `colorAttachments.attachmentBlendStates`（每个 attachment 10 个字段）。数据源完备，无需新增结构体字段。

- Q2: `VertexInputFormat` → `vk::Format` 的转换函数是否已有实现？  
  → **已确认**: 新后端 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 无此函数，旧后端 `VulkanRenderBackend/private/InterfaceTranslator.h` line 17-30 有 `VertexInputFormatToVkFormat` 实现。需移植到新后端的 `InterfaceTranslator.h`。

