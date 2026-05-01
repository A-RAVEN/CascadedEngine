## Context

VulkanRenderBackendNew 当前无法渲染任何帧。核心断点在 `VulkanGraphExecutor::GetOrCreateShaderModule()` — 该函数记录日志后直接返回 `nullptr`，并将 `nullptr` 缓存到 `m_ShaderModuleCache`。这导致 `BuildPipelineStates` 中 shader stages 为空、pipeline layout 为 `VK_NULL_HANDLE`，整条渲染管线无法串联。

同时，`GetOrCreatePipelineLayout()` 虽已实现，但它独立从 reflection 数据重新解析 descriptor layout，没有消费 `ConstructShaderDescriptorInfo` 已生成的 `VulkanShaderResourceBindingInfo`，存在重复逻辑和潜在不一致。`VulkanShaderStruct::Init()` 中创建的 descriptor set layout 也是空占位（0 bindings），需要从 BindingInfo 构建。

已有的基础设施：
- `ShaderLibrary` 可加载 SPIR-V 并存储为 `VulkanShaderCode`（含 `spirvCode: vector<uint32_t>`）
- `ConstructShaderDescriptorInfo` 可生成完整的 `VulkanShaderResourceBindingInfo`（含 `setLayoutInfos`）
- `VulkanPipelineLibrary` 和 `PipelineLibraryCache` 已完整实现，等待正确的 shader module 和 pipeline layout 输入
- D3D12 后端的对应实现可作为参考（`CShaderModuleObject::Create`、`RootSignatureManager`）

## Goals / Non-Goals

**Goals:**
- 实现 ShaderModule 创建与缓存，使 `GetOrCreateShaderModule` 返回有效的 `vk::ShaderModule`
- 实现 PipelineLayout 正确构建，消费已有的 `VulkanShaderResourceBindingInfo` 生成 `vk::DescriptorSetLayout` 和 `vk::PipelineLayout`
- 修复 `VulkanShaderStruct` 中的空 descriptor set layout 占位
- 使 `BuildPipelineStates` 能成功创建图形/计算管线（shader stages + pipeline layout 可用）

**Non-Goals:**
- 不实现 GPUFrameManager / 多帧重叠（属于 Phase 2 性能优化）
- 不实现跨队列同步 / Queue Family ownership transfer
- 不实现 LinearMemoryManager / staging buffer 复用
- 不实现 BuildResources / BuildDescriptors 完整流程（属于下一个 change）
- 不实现 CBuffer 初始化上传流程（属于下一个 change）
- 不重构 VulkanResourceAliasing 的 VMA 生命周期 bug

## Decisions

### Decision 1: ShaderModule 缓存策略 — 按 SPIR-V SHA hash 缓存

**选择**: 使用 `VulkanShaderCode` 对应的 `sha256_hash::result_type`（即 `ShaderLibrary::m_ShaderPrograms` 的 map key）作为缓存 key，在 `VulkanGraphExecutor` 内维护 `m_ShaderModuleCache`。

**备选**: 在 `ShaderLibrary` 层面缓存（让 ShaderLibrary 持有 `vk::ShaderModule`）。

**理由**: ShaderModule 是 device-dependent 对象，与 VkDevice 绑定。ShaderLibrary 是 device-agnostic 的数据层，不应持有 Vulkan 对象。GraphExecutor 已有 `m_ShaderModuleCache` 成员和 device 引用，保持现有架构。

**缓存 key 类型**: 将 `m_ShaderModuleCache` 的 key 类型从 `size_t` 改为 `cahash::sha256_hash::result_type`。`sha256_hash::result_type` 已实现 `operator<=>`，可直接用于 `unordered_map`（需提供 hash 特化，或使用 `result_type` 的 `toString()` 作为 string key）。避免将 256-bit hash 截断为 `size_t`（64-bit）导致碰撞风险。

### Decision 2: PipelineLayout 构建策略 — 消费 VulkanShaderResourceBindingInfo

**选择**: 重写 `GetOrCreatePipelineLayout`，从 `VulkanShaderResourceBindingInfo.setLayoutInfos` 创建 `vk::DescriptorSetLayout`，组装 `vk::PipelineLayout`。

**备选 A**: 保持现有独立解析 reflection 数据的方式，仅修复 bug。
**备选 B**: 让 `VulkanShaderStruct::Init` 创建 descriptor set layout 并缓存，GraphExecutor 查找复用。

**理由**: `VulkanShaderResourceBindingInfo` 已包含完整的 descriptor set layout 信息（`VulkanDescriptorSetLayoutInfo` 含 `vk::DescriptorSetLayoutBinding` 向量），且已在 shader import 时计算好。复用这些数据避免重复解析、减少不一致风险。备选 B 虽合理但会引入 VulkanShaderStruct → VkDescriptorSetLayout 的额外所有权问题。

**接口变更**: `GetOrCreatePipelineLayout` 签名从 `(ShaderReflectionData const&)` 改为 `(VulkanShaderResourceBindingInfo const&)`。调用方需先从 `ShaderLibrary` 获取 `VulkanShaderFileInfo::shaderBindingInfo`。

### Decision 3: DescriptorSetLayout 生命周期 — GraphExecutor 拥有并缓存

**选择**: `GetOrCreatePipelineLayout` 内部创建 `vk::DescriptorSetLayout`，缓存在 `m_DescriptorSetLayoutCache` 中，PipelineLayout 销毁时一并销毁。

**理由**: PipelineLayout 直接引用 DescriptorSetLayout，两者生命周期应一致。当前 GraphExecutor 的 `CleanupCaches` 已负责清理 pipeline cache，扩展为同时清理 descriptor set layout cache 是自然演进。

**缓存 key 类型**: `m_DescriptorSetLayoutCache` 和 `m_PipelineLayoutCache` 的 key 类型从 `size_t` 改为基于 `VulkanShaderResourceBindingInfo` 内容的 hash 值。hash 输入应包含：每个 set 的 `setIndex` + 所有 binding 的 `descriptorType`、`binding`、`descriptorCount`、`stageFlags`。setIndex 必须包含在 hash 中以确保 set 顺序不同时产生不同的 key。

### Decision 4: VulkanShaderStruct 的 descriptor set layout — 延迟到 BuildResources 阶段

**选择**: `VulkanShaderStruct::Init` 中移除空 descriptor set layout / pipeline layout / descriptor pool / descriptor set 的创建，改为在 `BuildResources` 阶段从 GraphExecutor 获取已缓存的 layout。

**备选**: 在 `Init` 中直接调用 `ConstructShaderDescriptorInfo` 重建 layout。

**理由**: `Init` 阶段没有足够的上下文（不知道自己属于哪个 pass 的哪个 binding instance）。延迟到 `BuildResources` 阶段可以复用 GraphExecutor 已缓存的 layout，避免同一 shader 重复创建。当前 `Init` 中的空 layout 没有任何实际用途，移除比保留更安全。

**过渡说明**: 移除后 VulkanShaderStruct 将不持有任何 descriptor 相关的 Vulkan 对象。本 change 的目标是使 BuildPipelineStates 能创建有效的 Pipeline（需要 ShaderModule + PipelineLayout），DescriptorSet 的创建和资源绑定属于下一个 change（BuildResources / BuildDescriptors）。VulkanShaderStruct 在本 change 结束后，其 descriptor 功能处于不可用状态，直到下一个 change 补充 BuildResources。

### Decision 5: 空 descriptor set 的处理 — 创建空 DescriptorSetLayout 保持 setIndex 连续性

**选择**: 当 `setLayoutInfos` 中某个 set 的 bindings 为空时，仍然为该 set 创建一个空 DescriptorSetLayout（0 bindings），以保持 PipelineLayout 中 set index 与 shader 中的 `layout(set=N)` 声明一致。

**备选**: 跳过空 set，不创建 DescriptorSetLayout。

**理由**: Vulkan 的 `vk::PipelineLayoutCreateInfo::pSetLayouts` 是一个连续数组，索引隐含 set number。如果 set 0 有 bindings、set 1 为空、set 2 有 bindings，跳过 set 1 会导致 `pSetLayouts[1]` 实际对应 set 2 的 layout，shader 中 `layout(set=2)` 的绑定错位。创建空 DescriptorSetLayout 是 Vulkan 的标准做法，开销极小。

## Risks / Trade-offs

- [Risk] PipelineLayout 缓存可能导致 shader 热重载时使用旧 layout → 缓存 key 应包含 layout hash，热重载时清理缓存。缓解：当前不支持热重载，后续实现时再处理。
- [Risk] 移除 VulkanShaderStruct::Init 中的 descriptor 创建可能影响其他代码路径 → 检查所有调用点，确认这些对象从未被使用（当前返回空 layout，实际无消费者）。
- [Risk] VulkanShaderStruct 在本 change 后无法绑定 descriptor → 这是预期的，DescriptorSet 创建属于下一个 change。确保所有调用者不依赖 VulkanShaderStruct 的 descriptor 成员。
- [Risk] 缓存 key 类型从 `size_t` 改为 `sha256_hash::result_type` 可能需要添加 hash 特化 → `result_type` 已有 `operator<=>`，可基于其 `data` 数组实现 `std::hash` 特化或使用 `toString()` 作为中间 key。
