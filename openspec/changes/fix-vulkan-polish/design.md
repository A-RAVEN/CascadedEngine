## Context

Vulkan 后端 `VulkanRenderBackendNew` 已完成核心功能实现（descriptor set、pipeline library、command list 管理），但对抗验证发现了以下问题：

- **Descriptor Pool 每帧 Reset 阻断缓存**: `VulkanFrameContext::Aquire()` 每帧调用 `vkResetDescriptorPool` 销毁并重建所有 descriptor set。新分配的 set 内容为空白——即使底层 buffer/image view/sampler 句柄未变，仍必须重新写入。原提议的 descriptor 写入缓存在此架构下**不可实现**，需记录约束并标注为 future work。
- **CommandList Handle 泄漏**: `Reset()` 调用 `vkResetCommandPool` reset 池后，将 `m_GraphicsCommand` 等成员置零，下次 `Command()` 懒加载时重新 `allocateCommandBuffers` 分配新 handle——旧 handle 未被 `vkFreeCommandBuffers` 释放导致泄漏。正确做法是复用已 reset 的 handle。
- **PipelineLibrary Hash 碰撞**: 当前签名从三个 32-byte SHA-256 中各取首 8 字节 XOR 得 64-bit。碰撞概率在 birthday bound ~2^32 条目时约 50%，碰撞后退化为链表查找且可能返回错误结果。
- **Debug 命名**: `VulkanDebug.h` 已有 `SetVKObjectDebugName` 模板函数，但未在资源创建点调用。`ShaderModule`、`Framebuffer`、`RenderPass` 的创建点同样缺少命名。


## Goals / Non-Goals

**Goals:**
- 修复 `VulkanCommandListManager::Reset()` 中的 command buffer handle 泄漏：Reset() 后复用已有 handle 而非重新分配
- 在 `VulkanResourceBindingInstance` / `VulkanFrameContext` 中添加 descriptor pool 生命周期约束注释，标注写入缓存为 future work
- 在 Buffer/Image/Pipeline/CommandBuffer/DescriptorSet/ShaderModule/Framebuffer/RenderPass 创建点为对象设置 debug name
- 将 PipelineLibrary 缓存改为 `castl::unordered_map` hash 查找，并增加碰撞回退逻辑
- 审计并记录 `VulkanFrameContext::Aquire()` 中 `vkResetDescriptorPool` 对缓存策略的架构约束

**Non-Goals:**
- 不实现 descriptor 写入缓存（因 pool 每帧 reset 阻断，标记为 future work）
- 不改动 descriptor pool 的分配/重置策略（跨帧复用等留给 future work）
- 不改动任何功能逻辑或 shader binding 行为
- 不引入新的外部依赖
- 不实现 `vkCmdBeginDebugUtilsLabelEXT`（留作未来可选项）

## Decisions

### D1: Descriptor Pool 生命周期约束记录 — 替代写入缓存

**对抗验证发现**: 原提议的 descriptor 写入缓存在当前架构下不可行。`VulkanFrameContext::Aquire()` 每帧调用 `vkResetDescriptorPool`，该操作隐式释放池内所有 descriptor set 并将 pool 重置为初始状态。下一帧分配的 descriptor set 为全新对象，不继承任何内容——即使底层 `VkBuffer`/`VkImageView`/`VkSampler` 句柄与上一帧完全相同，descriptor set 内容仍为空白。因此比较句柄并跳过 `vkUpdateDescriptorSets` 的策略**零收益**。

**选择**: 此 change 中不实现 descriptor 写入缓存。改为：
1. 在 `VulkanResourceBindingInstance::BuildDescriptors()` 中添加注释，说明每帧全量重写 descriptor set 是因为 pool 每帧 reset 导致 set 内容为空
2. 在 `VulkanFrameContext::Aquire()` 中注释记录 `vkResetDescriptorPool` 的语义：隐式释放池内所有 set，新 set 需全量写入
3. 标记 descriptor 写入缓存为 future work

**Future Work 实现路径** (不在本 change 范围内):
- 方案 A: 使用 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` 创建 pool，帧间通过 `vkFreeDescriptorSets` 显式释放不再需要的 set，保持常驻 set 的写入状态
- 方案 B: 跨帧复用 descriptor set——pool 不每帧 reset，而是按需分配/释放 set，利用 set 内容的持久性实现自然缓存
- 方案 C: 多 pool 轮转——使用 N 个 pool 轮转（类似 swapchain image count），每个 pool 的 set 在 N 帧内保持有效

这些方案均需较大架构改动（descriptor pool 生命周期管理、跨帧复用策略、与 FrameContext 的集成），留待后续独立 change 处理。

### D2: Debug 命名 — 扩展现有 SetVKObjectDebugName 并在创建点调用

**选择**: 利用已有的 `SetVKObjectDebugName<T>` 模板（`VulkanDebug.h`），在以下创建点添加调用：

| 对象类型 | 调用位置 | 命名规范 |
|----------|----------|----------|
| `vk::Buffer` | `VulkanMemoryManager::CreateBuffer()` / `VulkanLinearMemoryManager` | `"Buffer:<purpose>"` |
| `vk::Image` | `VulkanMemoryManager::CreateImage()` / `VulkanGraphLocalResourceManager` | `"Image:<purpose>"` |
| `vk::Pipeline` | `VulkanPipelineLibrary` 各 Create/LinkPipeline 方法 | `"Pipeline:<shader_name>"` |
| `vk::CommandBuffer` | `VulkanCommandListManager` 各 `Command()` 方法 | `"CmdBuf:<Graphics/Compute/Transfer>"` |
| `vk::DescriptorSet` | `VulkanResourceBindingInstance::AllocateDescriptorSets()` | `"DescSet:<shader_name>:set<N>"` |
| `vk::ShaderModule` | `VulkanPipelineLibrary` 各 `CreateShaderModule()` | `"ShaderMod:<shader_name>:<stage>"` |
| `vk::Framebuffer` | `VulkanGraphLocalResourceManager` framebuffer 创建点 | `"Framebuf:<render_pass_name>"` |
| `vk::RenderPass` | `VulkanGraphLocalResourceManager` render pass 创建点 | `"RenderPass:<name>"` |

**前提条件**: 需要确认 Vulkan 设备支持 `VK_EXT_debug_utils`（Vulkan 1.2 标准通常包含）。在 `VulkanApp` 初始化时检查并设置 `m_DebugUtilsEnabled` 标志。

**替代方案**:
- 方案 A: 使用 `VK_EXT_debug_marker`（旧扩展）。被否决：`VK_EXT_debug_utils` 功能更强大，且在 Vulkan 1.2+ 上普遍支持。
- 方案 B: 在 RenderInterface 层添加通用 SetName 接口。被否决：超出本 change 范围，且跨后端统一接口设计需更多讨论。

### D3: PipelineLibrary Hash — castl::unordered_map + 自定义 Hash

**选择**: 将 `m_LibraryCache` 从 `castl::vector<castl::pair<PipelineLibraryKey, PipelineLibraryParts>>` 改为 `castl::unordered_map<PipelineLibraryKey, PipelineLibraryParts, PipelineLibraryKeyHash>`。

**Hash 函数设计**:
`PipelineLibraryKey` 包含三个 `VKHashVal`（`cahash::hash256`，即 32-byte SHA-256）。自定义 hash functor 从三个 hash 值的首 8-byte 各取 XOR 得到一个 `size_t`：
```cpp
struct PipelineLibraryKeyHash {
    size_t operator()(PipelineLibraryKey const& k) const {
        return reinterpret_cast<size_t const*>(&k.vertexInputHash)[0]
             ^ reinterpret_cast<size_t const*>(&k.fragmentOutputHash)[0]
             ^ reinterpret_cast<size_t const*>(&k.shaderHash)[0];
    }
};
```

**碰撞风险与缓解**:
当前 hash 仅使用每个 32-byte SHA-256 的 8-byte 前缀 XOR 为 64-bit。在 birthday bound 约 2^32 条目时碰撞概率约 50%。虽然实际 pipeline library 条目数通常 < 100（远低于碰撞边界），但为安全起见，`unordered_map` 的 `key_eq` 使用 `PipelineLibraryKey` 的完整三字段 `operator==` 作为回退比较——hash 碰撞时退化为一次完整 key compare，确保**绝不返回错误的 pipeline**。额外开销可忽略（仅碰撞时触发）。

**替代方案**:
- 方案 A: 使用 `castl::shared_dic<VKHashVal, ...>`（即 `HashContainer.h` 中的 `shared_dic`）。被否决：`shared_dic` 的 key 类型是单一 `VKHashVal`，而 `PipelineLibraryKey` 是三个 hash 的组合。
- 方案 B: 将三个 hash 合并为一个新的 `VKHashVal`。被否决：增加 hash 计算开销，且原始三个 hash 值已经足够唯一。
- 方案 C: 从每个 SHA-256 取更多字节（如 16-byte）组合 hash。暂不采用：当前条目数下 64-bit 足矣，且 `unordered_map` 的完整 key compare 提供安全网。

### D4: CommandList Handle 泄漏修复

**对抗验证发现**: 原审计结论错误——`Reset()` 后将 `vk::CommandBuffer` 成员变量置零再重新分配会导致 handle 泄漏。

**泄漏机制分析**:
1. 首次 `GraphicsCommand()` 调用 `device.allocateCommandBuffers(...)` 分配句柄 `H1`，存入 `m_GraphicsCommand`
2. 帧结束调用 `Reset()` → `vkResetCommandPool(pool, ...)`，池内所有 command buffer 被**重置到 initial 状态**，但句柄 `H1` 仍有效
3. `Reset()` 将 `m_GraphicsCommand = nullptr`（Vulkan-Hpp 默认构造函数产生空句柄）
4. 下一帧 `GraphicsCommand()` 检测到 `m_GraphicsCommand` 为空，再次调用 `allocateCommandBuffers` 分配新句柄 `H2`
5. 句柄 `H1` 从未被 `vkFreeCommandBuffers` 释放 —— **泄漏**

**选择**: `Reset()` 后不复用已 reset 的 handle，避免重新分配。

**具体实现**:
- 移除 `Reset()` 中 `m_GraphicsCommand = nullptr` / `m_ComputeCommand = nullptr` / `m_TransferCommand = nullptr` 的置零逻辑
- 改为在 `Reset()` 之后第一次访问时，检查 handle 是否有效且已 reset（通过判空或 flag），直接对其调用 `beginCommandBuffer` 复用
- 或在 `Reset()` 中对每个已有的 command buffer 调用 `vkResetCommandBuffer`（individual reset），保持 handle 不变
- **推荐方案**: 池级 `vkResetCommandPool` 已经隐式 reset 池内所有 command buffer。`Reset()` 后保留 `m_GraphicsCommand` 等成员不变，在 `Command()` 访问器中通过标志位（如 `m_CurrentState == Reset`）判断是否需要重新 begin

**防重入保护**: 在 `Reset()` 中添加断言/注释，确认调用方已在 GPU fence 等待完成后才调用。若在 GPU 执行中调用 `Reset()` 会导致未定义行为。

## Risks / Trade-offs

- [Descriptor 缓存推迟] 将 descriptor 写入缓存标记为 future work 意味着当前每帧仍有冗余的 `vkUpdateDescriptorSets` 调用。但 pool 每帧 reset 架构下这些调用是**必需的**（descriptor set 内容为空），性能影响已内化在每帧 pool reset 的开销中。真正的优化需要更大力度的 pool 生命周期重构。
- [CommandList 修复风险] `Reset()` 中保留已 reset handle 的改动需确保与现有 `Command()` 懒加载逻辑兼容。特别是 `GraphicsCommand()` 等方法中 `if (!m_GraphicsCommand)` 的判空逻辑需替换为状态标志机。若引入竞争条件（GPU 仍在执行时 Reset）将导致未定义行为。
- [Debug 命名性能] `vkSetDebugUtilsObjectNameEXT` 是轻量级 API（仅存储字符串指针），无 GPU 操作 → 几乎无性能影响
- [PipelineLibrary 碰撞] `unordered_map` 的 hash 碰撞 + 完整 key compare 回退机制确保正确性。rehash 可能导致偶尔的 allocation spike → Pipeline library 条目数通常 < 100，影响微乎其微
- [ShaderModule/RenderPass/Framebuffer 命名依赖] 这些对象的创建点可能在 `VulkanPipelineLibrary` 或 `VulkanGraphLocalResourceManager` 内部，需要确认创建路径上可用的描述字符串来源（shader name、render pass name 等）
