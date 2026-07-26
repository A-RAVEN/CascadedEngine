# Design: 修复 Vulkan Pipeline 创建关键缺口

**Change ID**: fix-vulkan-pipeline-input-layout
**Created**: 2026-07-12

---

## Context

`VulkanGraphExecutor::BuildPipelineStates()` 当前存在九项关键缺口（对抗验证后更新）：

| #  | 缺口                                                       | 位置                                    | 严重度   |
|----|-------------------------------------------------------------|-----------------------------------------|----------|
| C1 | vertexInputState 的 binding/attribute 为空                  | `BuildPipelineStates` line 1369         | CRITICAL |
| C2 | InterfaceTranslator.h 缺少 ECompareOp/EStencilOp/EBlendFactor/EBlendOp/EVertexInputFormatToVkFormat 转换函数 | `InterfaceTranslator.h`                    | CRITICAL |
| C3 | Monolithic 路径 pDepthStencilState 未设置（与 C4 同源）     | `BuildPipelineStates` line 1439         | CRITICAL |
| C4 | GPL 路径 depth-stencil 传 nullptr                           | `BuildPipelineStates` line 1401         | CRITICAL |
| C5 | **VkPipelineDynamicStateCreateInfo 完全缺失**（对抗验证 2026-07-14 BLOCKING 发现） | `BuildPipelineStates`（未实现）         | **BLOCKING** |
| M1 | Blend state 仅设置 colorWriteMask，遗漏其余9个字段          | `BuildPipelineStates` line 1386-1392    | MAJOR    |
| M2 | Stencil state front/back 完全忽略                           | `BuildPipelineStates`（未实现）         | MAJOR    |
| M3 | PipelineLibraryCache::GenerateHashKey 未包含 vertexInputDesc hash、stencil字段、完整blend字段、primitiveRestartEnable、多colorFormat | `PipelineLibraryCache.cpp` line 19-63       | MAJOR    |
| M4 | GPL LinkPipeline 传 VK_NULL_HANDLE                          | `BuildPipelineStates` line 1410-1411    | MEDIUM   |
| M5 | 无 VkPipelineCache，且覆盖不全（8 个调用点仅覆盖 2 个）     | `BuildPipelineStates` 全局              | MEDIUM   |

D3D12 参考实现（`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp::Init`）正确构建了 `D3D12_INPUT_ELEMENT_DESC` 数组（从 `vertexInputDescs` 映射 vertex buffer slots 到 shader semantic names）、完整的 depth-stencil state、和 per-attachment blend states。

## Goals / Non-Goals

**Goals:**
- 从 `DrawCallBatch::m_VertexInputDescs` 和 shader reflection 数据构建完整的 `VkPipelineVertexInputStateCreateInfo`，使用确定性单阶段算法
- 创建 `VkPipelineDynamicStateCreateInfo`（VK_DYNAMIC_STATE_VIEWPORT + VK_DYNAMIC_STATE_SCISSOR），GPL + Monolithic 双路径均传入
- GPL `CreateFragmentOutputLibrary` 和 Monolithic `createInfo.pDepthStencilState` 均传入实际 depth-stencil state（两路径共享）
- 为每个 color attachment 创建独立的 `VkPipelineColorBlendAttachmentState`，包含全部 10 个字段（8 per-attachment + 2 pipeline-level）
- Stencil state front/back 完整映射到 `VkStencilOpState`
- GPL `LinkPipeline` 传入实际 `VkRenderPass`
- 创建 `vk::PipelineCache` 并用于**全部 8 个** pipeline 创建调用点
- 在 `InterfaceTranslator.h` 中新增 5 个枚举转换函数（ECompareOp/EStencilOp/EBlendFactor/EBlendOp/EVertexInputFormatToVkFormat）
- 补全 `PipelineLibraryCache::GenerateHashKey` 至覆盖全部 pipeline state 字段

**Non-Goals:**
- 不改动 Compute Pipeline
- 不实现 Pipeline Derivatives
- 不实现 Pipeline Statistics
- 不优化 pipeline creation 性能

---

## Decisions

### D1: Vertex Input 从 DrawCallBatch + Shader Reflection 构建（确定性单阶段算法）

**选择**: 以 shader reflection 的 `m_VertexAttributes`（`vector`，确定性顺序）为外循环遍历。对每个 shader attribute，从 `batch.m_VertexInputDescs` 中按 `NameHash`（semantic name）查找匹配的 slot。维护 `vector<NameHash>` 记录已遇到的 slot key —— 首次遇到新 slot 时 append，binding 索引 = 该 key 在 vector 中的位置。只对至少有一个 shader attribute 匹配的 slot 创建 `VkVertexInputBindingDescription`。

**对抗验证发现（2026-07-14）**: 原 proposal 以 `unordered_map` 迭代为外循环分配 binding 索引，存在两个 CRITICAL 缺陷：(1) `unordered_map` 迭代顺序由 hash 值决定，同组数据跨次运行可能产生不同 binding 编号 → PipelineLibraryCache hash 不稳定；(2) 遍历所有 m_VertexInputDescs 条目会为未被 shader 使用的 slot 创建"孤儿 binding"（有 binding description 但无 attribute description 指向它）。D3D12 和旧 Vulkan 后端均以 shader reflection vector 为外循环。

**算法伪代码**:
```
vector<NameHash> seenSlotKeys;      // deterministic order = binding index
vector<VkVertexInputBindingDescription> bindings;
vector<VkVertexInputAttributeDescription> attributes;

for (auto& reflAttr : pFileInfo->reflectionData.m_VertexAttributes) {
    NameHash semanticNameHash(reflAttr.m_SematicName);  // castl::string → NameHash
    auto slotIt = batch.m_VertexInputDescs.find(semanticNameHash);
    if (slotIt == batch.m_VertexInputDescs.end()) continue;
    
    auto& slotDesc = slotIt->second.Get();
    
    int bindingIdx = indexOf(seenSlotKeys, semanticNameHash);
    if (bindingIdx < 0) {
        bindingIdx = seenSlotKeys.size();
        seenSlotKeys.push_back(semanticNameHash);
        bindings.push_back({bindingIdx, slotDesc.stride,
            slotDesc.perInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex});
    }
    
    for (auto& attr : slotDesc.attributes) {
        if (attr.semanticIndex == reflAttr.m_SematicIndex) {
            attributes.push_back({
                reflAttr.m_Location,
                bindingIdx,
                EVertexInputFormatToVkFormat(attr.format),
                attr.offset
            });
        }
    }
}
```

**匹配机制**: `ShaderVertexAttributeData::m_SematicName`（`castl::string`）通过 `NameHash` 隐式构造转换为 key。`NameHash::operator==` 通过 string 内容比较（非 hash 值），确保匹配正确性。

**空顶点输入判断**: 以 `m_VertexAttributes` 是否为空为准。若 `m_VertexAttributes` 非空但 `m_VertexInputDescs` 为空，记录 CACore 警告（配置错误），vertexInputState 仍声明为空（Vulkan 允许但输出不可预期）。

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

### D5: VkPipelineCache 在 RenderBackend_Vulkan 中管理（覆盖全部 8 个调用点）

**选择**: 在 `RenderBackend_Vulkan` 中添加 `vk::PipelineCache m_PipelineCache` 成员和 `GetPipelineCache()` 访问器。Init 时创建（尝试从文件加载缓存数据，失败时回退到空 cache），Release 时序列化到文件并销毁。**关键修正（对抗验证 2026-07-14）**: 原 tasks 仅覆盖 2 个调用点（`CreateMonolithicPipeline` + `LinkPipeline`），遗漏了 4 个 GPL library part 创建函数、compute pipeline 创建、和 `VertexInputStateManager` 中的 pipeline 创建。需覆盖全部 8 个 `createGraphicsPipeline`/`createComputePipeline` 调用点。

**覆盖的全部 8 个调用点**:
| # | 位置 | 当前代码 | 修复 |
|---|------|---------|------|
| 1 | `VulkanPipelineLibrary::CreateVertexInputLibrary` line 62 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 2 | `VulkanPipelineLibrary::CreatePreRasterizationLibrary` line 108 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 3 | `VulkanPipelineLibrary::CreateFragmentLibrary` line 141 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 4 | `VulkanPipelineLibrary::CreateFragmentOutputLibrary` line 177 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 5 | `VulkanPipelineLibrary::CreateMonolithicPipeline` line 251 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 6 | `VulkanPipelineLibrary::LinkPipeline` line 232 | `createGraphicsPipeline(nullptr, ...)` | 传入 `cache` 参数 |
| 7 | `VulkanGraphExecutor::BuildPipelineStates` line 1504 | `createComputePipeline(nullptr, ...)` | 传入 `GetApp()->GetPipelineCache()` |
| 8 | `VertexInputStateManager` line 44 | `createGraphicsPipeline(nullptr, ...)` | 传入 `GetApp()->GetPipelineCache()` |

**缓存文件路径**: 使用应用工作目录下的 `pipeline_cache.bin` 文件（注：若 CWD 跨运行变化则缓存永远 miss，可后续优化为可执行文件目录）。
**实现细节**:
```cpp
// RenderBackend_Vulkan.h 新增:
vk::PipelineCache const& GetPipelineCache() const { return m_PipelineCache; }

// Init (带异常保护):
vk::PipelineCacheCreateInfo cacheInfo{};
std::ifstream cacheFile("pipeline_cache.bin", std::ios::binary | std::ios::ate);
if (cacheFile.is_open()) { /* ...加载 initialData... */ }
try {
    m_PipelineCache = m_Device.createPipelineCache(cacheInfo);
} catch (vk::SystemError const& e) {
    CA_LOG_WARN("Failed to load pipeline cache, creating empty: {}", e.what());
    m_PipelineCache = m_Device.createPipelineCache({}); // 空 cache 回退
}

// Release:
auto cacheData = m_Device.getPipelineCacheData(m_PipelineCache);
if (!cacheData.empty()) {
    std::ofstream cacheFile("pipeline_cache.bin", std::ios::binary);
    cacheFile.write(reinterpret_cast<const char*>(cacheData.data()), cacheData.size());
}
m_Device.destroyPipelineCache(m_PipelineCache);
```

### D6: InterfaceTranslator.h 新增枚举转换函数

**选择**: 在 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 中新增 `ECompareOpToVkCompareOp`、`EStencilOpToVkStencilOp`、`EBlendFactorToVkBlendFactor`、`EBlendOpToVkBlendOp`、`EVertexInputFormatToVkFormat` 五个 constexpr 函数。

**对抗验证发现**: 当前 InterfaceTranslator.h 仅有 5 个转换函数（ETopology/EPolygonMode/ECullMode/EFrontFace/EColorChannelMask），缺少深度模板和混合相关的 5 个枚举转换函数。旧 `VulkanRenderBackend/private/InterfaceTranslator.h` 在 line 17-30（`VertexInputFormatToVkFormat`）和 line 432-488（`EStencilOpTranslate`、`ECompareOpTranslate`、`EBlendFactorTranslate`、`EBlendOpTranslate`）已包含参考实现。

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

### D8: PipelineLibraryCache::GenerateHashKey 哈希字段补全

**选择**: 全面补全 `RenderStateCombination` 结构体和 `GenerateHashKey` 的哈希字段。

**对抗验证发现（2026-07-14）**: `GenerateHashKey` 存在严重哈希不足：
- blend 循环仅 hash 2/8 字段（`colorWriteMask` + `blendEnable`），遗漏 `srcColorBlendFactor`、`dstColorBlendFactor`、`srcAlphaBlendFactor`、`dstAlphaBlendFactor`、`colorBlendOp`、`alphaBlendOp`（这 6 个字段已存储在 `blendAttachments` struct 中，仅需扩展 hashing loop）
- `RenderStateCombination` 零 stencil/depth 字段（17 个管线状态值不在 hash 中）
- 单 `colorFormat` 字段无法表达 MRT 多 attachment 格式（与 `RenderPassCacheKey::colorFormats` vector 不一致）
- 缺失 `primitiveRestartEnable`

**RenderStateCombination 新增字段**:
```cpp
struct RenderStateCombination {
    // 现有字段...
    
    // 新增: 完整 depth/stencil 状态 (17 fields)
    bool depthTestEnable;
    bool depthWriteEnable;
    bool stencilTestEnable;
    ECompareOp depthCompareOp;
    DepthStencilStates::StencilStates stencilFront;   // 7 fields
    DepthStencilStates::StencilStates stencilBack;    // 7 fields
    
    // 新增: primitive restart
    bool primitiveRestartEnable;
    
    // 修正: colorFormats vector (替代单 colorFormat)
    castl::vector<vk::Format> colorFormats;  // was: vk::Format colorFormat;
};
```

**GenerateHashKey 补全**:
- blend 循环扩展为 hash 全部 8 个字段：`srcColorBlendFactor`、`dstColorBlendFactor`、`srcAlphaBlendFactor`、`dstAlphaBlendFactor`、`colorBlendOp`、`alphaBlendOp`（+ 已有的 `colorWriteMask`、`blendEnable`）
- 新增 depth/stencil hash：`depthTestEnable`、`depthWriteEnable`、`stencilTestEnable`、`depthCompareOp`、以及 front/back 各 7 个 stencil 字段
- 新增 `primitiveRestartEnable`
- 修正 `colorFormat` → 遍历 `colorFormats` vector 逐个 hash
- 注意：blend factor/op 字段**已在 `blendAttachments` 中**，不需要新增 struct 字段——仅需扩展 hashing loop

### D9: VulkanPipelineLibrary 签名变更

**选择**: 全部 6 个 pipeline 创建函数均新增 `vk::PipelineCache cache = nullptr` 参数。`CreatePreRasterizationLibrary` 额外新增 `const vk::PipelineDynamicStateCreateInfo* pDynamicState = nullptr` 参数。

**原理**: 当前仅 `CreateMonolithicPipeline` 和 `LinkPipeline` 需要签名变更。对抗验证发现还需覆盖 4 个 GPL library part 创建函数（它们也调用 `createGraphicsPipeline(nullptr, ...)`）。此外 `CreatePreRasterizationLibrary` 需要接收 Dynamic State 参数（见 D10）。

**变更后的签名**:
```cpp
vk::Pipeline CreateVertexInputLibrary(
    vk::PipelineVertexInputStateCreateInfo const& vertexInputState,
    vk::PipelineInputAssemblyStateCreateInfo const& inputAssemblyState,
    vk::PipelineCache cache = nullptr);                           // 新增

vk::Pipeline CreatePreRasterizationLibrary(
    vk::PipelineShaderStageCreateInfo const& vertexShader,
    vk::PipelineShaderStageCreateInfo const* tessControlShader,
    vk::PipelineShaderStageCreateInfo const* tessEvalShader,
    vk::PipelineShaderStageCreateInfo const* geometryShader,
    vk::PipelineViewportStateCreateInfo const& viewportState,
    vk::PipelineRasterizationStateCreateInfo const& rasterizationState,
    const vk::PipelineDynamicStateCreateInfo* pDynamicState = nullptr,  // 新增
    vk::PipelineCache cache = nullptr);                           // 新增

vk::Pipeline CreateFragmentLibrary(
    vk::PipelineShaderStageCreateInfo const& fragmentShader,
    vk::PipelineCache cache = nullptr);                           // 新增

vk::Pipeline CreateFragmentOutputLibrary(
    vk::PipelineMultisampleStateCreateInfo const& multisampleState,
    vk::PipelineDepthStencilStateCreateInfo const* depthStencilState,
    vk::PipelineColorBlendStateCreateInfo const& colorBlendState,
    vk::PipelineCache cache = nullptr);                           // 新增

vk::Pipeline CreateMonolithicPipeline(
    vk::GraphicsPipelineCreateInfo const& createInfo,
    vk::PipelineCache cache = nullptr);                           // 新增

vk::Pipeline LinkPipeline(
    PipelineLibraryParts const& libraries,
    vk::PipelineLayout layout,
    vk::RenderPass renderPass,
    uint32_t subpass,
    vk::PipelineCache cache = nullptr);                           // 新增
```

调用侧在 `BuildPipelineStates` 中传入 `GetApp()->GetPipelineCache()`（按值传递 `vk::PipelineCache`，非指针）。

### D10: Dynamic State 声明（对抗验证 BLOCKING 发现）

**选择**: 在 `BuildPipelineStates` 中创建 `vk::PipelineDynamicStateCreateInfo`，声明 `VK_DYNAMIC_STATE_VIEWPORT` 和 `VK_DYNAMIC_STATE_SCISSOR`。GPL 路径传入 `CreatePreRasterizationLibrary`（需 D9 新增的 `pDynamicState` 参数）；Monolithic 路径设置 `createInfo.pDynamicState`。

**对抗验证发现（2026-07-14）**: 当前代码在 `RecordRenderPass` 中实际调用 `cmdBuf.setViewport()` / `cmdBuf.setScissor()` 动态设置 viewport/scissor，但 `BuildPipelineStates` 中既没有创建 `VkPipelineDynamicStateCreateInfo` 声明这两个动态状态，也没有在 `VkPipelineViewportStateCreateInfo` 中填充静态 viewport/scissor 数据（`pViewports`/`pScissors` 均为 nullptr）。这违反 Vulkan 规范 VUID-VkGraphicsPipelineCreateInfo-pDynamicStates-00749/00750。验证层开启时 pipeline 创建必然失败；关闭验证层时 viewport 行为未定义（0 大小 viewport → 画面为空）。这是 proposal 原 8 项修复之外的 BLOCKING 级缺口——即使其余修复全部正确实施，viewport 仍为 0 大小，渲染画面必为空。

**实现**:
```cpp
// 在 if/else 之前创建（与其他共享状态 peer）
vk::DynamicState dynamicStates[] = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
};
vk::PipelineDynamicStateCreateInfo dynamicState{};
dynamicState.dynamicStateCount = 2;
dynamicState.pDynamicStates = dynamicStates;

// GPL 路径 (line 1398 附近):
auto preRasterLib = pipelineLibrary.CreatePreRasterizationLibrary(
    vertexShaderStage, nullptr, nullptr, nullptr,
    viewportState, rasterizationState,
    &dynamicState,    // 新增参数
    pipelineCache);   // 新增参数

// Monolithic 路径 (line 1436 附近):
createInfo.pDynamicState = &dynamicState;
```

**注意**: `CreatePreRasterizationLibrary` 内部创建 `VkGraphicsPipelineCreateInfo` 时需设置 `createInfo.pDynamicState = pDynamicState`。D3D12 的 PSO 天然将 viewport/scissor 作为动态状态（不声明在 PSO 中），Vulkan 需要显式声明。

---

## Risks / Trade-offs

- **[Dynamic State 遗漏风险]**：如果未来新增其他动态状态（如 `VK_DYNAMIC_STATE_BLEND_CONSTANTS`），需同步更新 `dynamicStates` 数组和 `CreatePreRasterizationLibrary` 的参数。  
  → 缓解：当前仅需 viewport + scissor（与 D3D12 行为匹配），引擎不使用其他动态状态。

- **[Vertex Input 匹配失败]**：如果 `DrawCallBatch::m_VertexInputDescs` 的 slot key 与 shader reflection 的 semantic name 不匹配（大小写、前缀等差异），会导致 vertex input 为空。  
  → 缓解：添加 CACore 日志警告。算法以 shader reflection 为外循环，至少不会分配 orphan binding。

- **[Binding 索引确定性]**：依赖 `seenSlotKeys` vector 的 append 顺序保证确定性。`m_VertexAttributes` 是 `vector`（确定顺序），`m_VertexInputDescs` 是 `unordered_map`（仅用于查找，不用于迭代顺序）。  
  → 缓解：正确——查找操作不依赖迭代顺序，确定性由 vector 外循环保证。

- **[VertexFormat 转换缺失]**：引擎层 `VertexInputFormat` 枚举仅 7 种格式，转换函数仅需覆盖这 7 种即可。  
  → 缓解：映射表不会遗漏——枚举值有限。

- **[PipelineCache 序列化大小]**：首次运行时 pipeline cache 为空，多次运行后可能达到数 MB。  
  → 缓解：这是 Vulkan 标准机制，驱动程序管理缓存大小。

- **[GPL RenderPass 变更]**：使用真实 render pass 替代 VK_NULL_HANDLE 可能影响 GPL library part 的复用。  
  → 缓解：GPL library parts 创建时未绑定 render pass，链接时使用真实 render pass 确保正确性。

- **[PipelineCache 文件路径依赖 CWD]**：使用相对路径 `pipeline_cache.bin`，若 CWD 跨运行变化则缓存永远 miss。  
  → 缓解：可后续优化为可执行文件目录或可配置路径。首次运行空 cache 不会导致错误。

- **[PipelineLibraryCache hash 冲突]**：若 hash 字段补全不完整，两个不同的管线可能误命中缓存返回错误管线。  
  → 缓解：D8 显式列出全部需 hash 的字段，实施时逐项验证。

- **[AllocateCommandBuffer 崩溃（pre-existing）]**：此 crash 属于 Phase 2 `fix-vulkan-batch-submit`，若每帧必崩则 Phase 1 的渲染结果无法验证。  
  → 缓解：需在实施前确认崩溃触发条件。若每帧必崩则需优先修复。

---

## Open Questions

- Q1: ~~`CPipelineStateObject` 中是否已包含 depth-stencil state 字段~~  
  → **已确认**: `CPipelineStateObject` 包含完整的 `depthStencilStates`（含 `depthTestEnable`/`depthWriteEnable`/`depthCompareOp`/`stencilTestEnable`/`stencilStateFront`/`stencilStateBack`），以及完整的 `colorAttachments.attachmentBlendStates`（每个 attachment 10 个字段）。数据源完备，无需新增结构体字段。

- Q2: `VertexInputFormat` → `vk::Format` 的转换函数是否已有实现？  
  → **已确认**: 新后端 `VulkanRenderBackendNew/private/Utils/InterfaceTranslator.h` 无此函数，旧后端 `VulkanRenderBackend/private/InterfaceTranslator.h` line 17-30 有 `VertexInputFormatToVkFormat` 实现。需移植到新后端的 `InterfaceTranslator.h`。

