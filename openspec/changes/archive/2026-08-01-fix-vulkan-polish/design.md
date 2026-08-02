<!-- 审计标注: 2026-08-01 baseline audit — D4 大面积过时(架构已变), D2 文件路径修正, 新增 D5/D6/D7, 详见审计记录 -->

## Context

Vulkan 后端 `VulkanRenderBackendNew` 已完成核心功能实现（descriptor set、pipeline library、command list 管理），但对抗验证发现了以下问题：

- **Descriptor Pool 每帧 Reset 阻断缓存**: `VulkanFrameContext::Aquire()` 每帧调用 `ResetDescriptorPool()` → `vkResetDescriptorPool` 销毁并重建所有 descriptor set。新分配的 set 内容为空白——即使底层 buffer/image view/sampler 句柄未变，仍必须重新写入。原提议的 descriptor 写入缓存在此架构下**不可实现**，需记录约束并标注为 future work。
- **PipelineLibrary Hash 碰撞**: 当前签名从三个 32-byte SHA-256 中各取首 8 字节 XOR 得 64-bit。碰撞概率在 birthday bound ~2^32 条目时约 50%，碰撞后退化为链表查找且可能返回错误结果。`m_LibraryCache` 使用 `castl::vector` 线性搜索（O(n)），需改为 `castl::unordered_map`。
- **Debug 命名**: `VulkanDebug.h` 已有 `SetVKObjectDebugName` 模板函数，但仅在 `VulkanBuffer::SetName()` / `VulkanTexture::SetName()` 中调用。`ShaderModule`、`Framebuffer`、`RenderPass` 的创建点在 `RenderBackend_Vulkan.cpp`（非 `VulkanGraphLocalResourceManager`），`PipelineLayout`、`Semaphore`、`Fence` 完全缺少命名。且 `SetVKObjectDebugName` 无 `#ifndef NDEBUG` 守卫——Release build 中调用 `setDebugUtilsObjectNameEXT` 为 UB。
- **CommandList 架构已变**: 原 proposal 描述的 handle 泄漏路径（`m_GraphicsCommand`/`m_ComputeCommand` 单句柄懒加载 + Reset 置零）已不存在。当前 `VulkanCommandListManager` 使用每次分配 + `m_AllocatedGraphicsCmdBufs`/`m_AllocatedComputeCmdBufs` vector 追踪 + Reset 时 `freeCommandBuffers` 的正确模式。但 `Reset()` 缺少 GPU fence 完成断言，`Release()` 在 `destroyCommandPool` 后仅 `clear()` vector（防御性不足）。


## Goals / Non-Goals

**Goals:**
- 在 `VulkanResourceBindingInstance` / `VulkanFrameContext` / `ResetDescriptorPool()` 中添加 descriptor pool 生命周期约束注释，标注写入缓存为 future work
- 修复 `SetVKObjectDebugName` 的 `#ifndef NDEBUG` 守卫缺失（Release build UB 风险）
- 在 Pipeline/CommandBuffer/DescriptorSet/ShaderModule/Framebuffer/RenderPass/PipelineLayout/Semaphore/Fence 创建点为对象设置 debug name
- 将 PipelineLibrary 缓存改为 `castl::unordered_map` hash 查找，并增加碰撞回退逻辑
- 在 `VulkanCommandListManager::Reset()` 中添加 GPU fence 完成断言 + `Release()` 中添加防御性 `freeCommandBuffers`

**Non-Goals:**
- 不实现 descriptor 写入缓存（因 pool 每帧 reset 阻断，标记为 future work）
- 不改动 descriptor pool 的分配/重置策略（跨帧复用等留给 future work）
- 不改动 CommandList 的 per-call 分配策略为 handle 复用（当前架构已正确，优化留待 future work）
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
| `vk::Buffer` | `VulkanMemoryManager::CreateBuffer()` / `VulkanLinearMemoryManager`（补充已有 `VulkanBuffer::SetName()`） | `"Buffer:<purpose>"` |
| `vk::Image` | `VulkanMemoryManager::CreateImage()` / `VulkanGraphLocalResourceManager`（补充已有 `VulkanTexture::SetName()`） | `"Image:<purpose>"` |
| `vk::Pipeline` | `VulkanPipelineLibrary` 各 Create/LinkPipeline 方法 | `"Pipeline:<shader_name>"` |
| `vk::CommandBuffer` | `VulkanCommandListManager` 各 `Command()` 方法 | `"CmdBuf:<Graphics/Compute/Transfer>"` |
| `vk::DescriptorSet` | `VulkanResourceBindingInstance::AllocateDescriptorSets()` | `"DescSet:<shader_name>:set<N>"` |
| `vk::ShaderModule` | `RenderBackend_Vulkan::GetOrCreateShaderModule()` | `"ShaderMod:<hash>"` |
| `vk::Framebuffer` | `RenderBackend_Vulkan::GetOrCreateFramebuffer()` | `"Framebuf:<size>"` |
| `vk::RenderPass` | `RenderBackend_Vulkan::GetOrCreateRenderPass()` | `"RenderPass:<hash>"` |
| `vk::PipelineLayout` | `RenderBackend_Vulkan::GetOrCreatePipelineLayout()` | `"PipelineLayout:<hash>"` |
| `vk::Semaphore` | `VulkanFrameBoundResourceManager::AllocCrossQueueSemaphore()` | `"Semaphore:cross_queue"` |
| `vk::Fence` | `VulkanFrameBoundResourceManager::CreateFences()` | `"Fence:direct"/"Fence:compute"` |

**注意**: ShaderModule/Framebuffer/RenderPass 的创建点在 `RenderBackend_Vulkan.cpp`（跨帧缓存），不在 `VulkanPipelineLibrary` 或 `VulkanGraphLocalResourceManager`。

**前提条件**: `VK_EXT_debug_utils` 在 instance 层始终启用（`GetInstanceExtensionNames()` 无 `#ifndef NDEBUG` 守卫），但 `SetVKObjectDebugName` 中的 `setDebugUtilsObjectNameEXT` 调用需加 `#ifndef NDEBUG` 守卫以避免 Release build 中的 UB。

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

### D4: CommandList 防御性加固（替代原 handle 泄漏修复）

**2026-08-01 审计发现**: 原 D4 描述的设计（`m_GraphicsCommand`/`m_ComputeCommand`/`m_TransferCommand` 单句柄懒加载 + Reset 置零 → handle 泄漏）已不再适用。当前 `VulkanCommandListManager` 架构已演变为：

```
GraphicsCommand()/ComputeCommand(): 每次调用 AllocateCommandBuffer() → push 到 vector
Reset(): freeCommandBuffers(vector) → resetCommandPool(pool)
TransferCommand(): 单句柄懒加载 (m_TransferCommand)
Reset(): resetCommandPool(transferPool) → m_TransferCommand = nullptr (隐式回收, 符合 Vulkan spec)
```

handle 泄漏路径已不存在。但仍有防御性加固点：

- **Reset() 缺少 GPU fence 完成断言**: 若 GPU 仍在执行中调用 `resetCommandPool` 是 UB。应在 `Reset()` 头部添加 `CA_ASSERT` 或注释引用 `VulkanFrameContext::Aquire()` 中的 fence 同步文档。
- **Release() 防御性不足**: `destroyCommandPool` 隐式释放池内所有 CB，但 `m_AllocatedGraphicsCmdBufs`/`m_AllocatedComputeCmdBufs` 在 `Release()` 中仅 `.clear()`（`VulkanCommandListManager.cpp:76-77`），未先显式 `freeCommandBuffers`。虽然符合 Vulkan spec，但防御性编程建议先 free 再 destroy。

**选择**: 不改动架构，仅添加防御性措施：

1. `Reset()` 中添加 `CA_ASSERT` 注释，引用 `Aquire()` 中的 fence sync 排序
2. `Release()` 中在 `destroyCommandPool` 前先调用 `freeCommandBuffers` 清理 vector（或添加断言验证 vector 已空）
3. 保留 `m_TransferCommand` 的隐式回收模式（`resetCommandPool` 已正确处理）

**注意**: 原 proposal 6 个 task 中 4.1-4.4（审计泄漏路径、移除置零、添加状态标志、Init/Destroy 管理）全部过时，已从 tasks.md 中移除。仅保留修订版 4.5（断言）和标记 4.6（已文档化）。

### D5: SetVKObjectDebugName 守卫修复

**审计发现**: `SetVKObjectDebugName`（`VulkanDebug.h:11-24`）调用 `device.setDebugUtilsObjectNameEXT()` 无 `#ifndef NDEBUG` 守卫。`VK_EXT_debug_utils` 扩展虽在 instance 层始终启用（`GetInstanceExtensionNames()` 无 #ifndef NDEBUG），但 device 层函数指针在 release build 中可能无效。此外 `debugUtilsMessengerCallback`（`RenderBackend_Vulkan.cpp:74`）已用 `#if !defined(NDEBUG)` 守卫，不一致。

**选择**: 在 `SetVKObjectDebugName` 函数体内添加 `#ifndef NDEBUG` / `#endif` 包裹 `setDebugUtilsObjectNameEXT` 调用。与 `m_DebugMessenger` 创建（`RenderBackend_Vulkan.cpp:234-236`）的守卫策略一致。

### D6: PipelineLayout / Semaphore / Fence debug 命名（扩展 D2）

**审计发现**: `vk::PipelineLayout`、`vk::Semaphore`、`vk::Fence` 三类对象完全无 debug 命名。在 RenderDoc/NSight 中均显示为裸句柄。

**选择**: 在 D2 表中追加这三类对象的命名策略。Semaphore/Fence 的创建点在 `VulkanFrameBoundResourceManager`，PipelineLayout 在 `RenderBackend_Vulkan::GetOrCreatePipelineLayout()`。

### D7: ResetDescriptorPool() 方法文档

**审计发现**: `VulkanFrameBoundResourceManager::ResetDescriptorPool()`（`VulkanFrameManager.cpp:83-89`）方法本身无注释说明 `vkResetDescriptorPool` 的语义（隐式释放池内所有 descriptor set，新 set 需全量写入）。原 task 1.1 仅覆盖 `Aquire()` 中的调用点。

**选择**: 在 `ResetDescriptorPool()` 方法定义处添加注释，说明 Vulkan spec 行为及其对 descriptor 写入缓存策略的影响。

---

## Risks / Trade-offs

- [Descriptor 缓存推迟] 将 descriptor 写入缓存标记为 future work 意味着当前每帧仍有冗余的 `vkUpdateDescriptorSets` 调用。但 pool 每帧 reset 架构下这些调用是**必需的**（descriptor set 内容为空），性能影响已内化在每帧 pool reset 的开销中。真正的优化需要更大力度的 pool 生命周期重构。
- [CommandList 架构已变] 原 D4 描述的 handle 泄漏路径已不存在。当前架构（per-call alloc + vector 追踪 + Reset 时 free）正确且安全。防御性加固仅添加断言/注释，风险极低。
- [Debug 命名性能] `vkSetDebugUtilsObjectNameEXT` 是轻量级 API（仅存储字符串指针），无 GPU 操作 → 几乎无性能影响
- [Debug 命名 UB 风险] `SetVKObjectDebugName` 无守卫是**真实 UB**——Release build 中调用 `setDebugUtilsObjectNameEXT` 可能 crash。必须在任何命名 task 之前修复（task 2.10 [AUDIT]）。
- [PipelineLibrary 碰撞] `unordered_map` 的 hash 碰撞 + 完整 key compare 回退机制确保正确性。rehash 可能导致偶尔的 allocation spike → Pipeline library 条目数通常 < 100，影响微乎其微
- [ShaderModule 命名限制] `GetOrCreateShaderModule` 的入参是 `programHash`（SHA-256），无可读 shader 名称。命名只能使用 hash 值或从 ShaderLibrary 反查名称
