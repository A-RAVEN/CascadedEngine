## Why

审计还发现了 13 个 medium 和 15 个 low 级别缺陷，虽不直接阻塞渲染管线，但涵盖性能退化、静默数据损坏、生产环境启动失败、缓存无限膨胀、未定义行为等问题。趁开发阶段一并修复，避免积累技术债务。

<!-- Audited 2026-07-31 against baseline (post vulkan-resource-aliasing-full commit 93da7cf): 18/19 findings confirmed STILL_PRESENT, 1 OBSOLETE (3.4: m_AllocatedCommandBuffers dead code — member already refactored into m_AllocatedGraphicsCmdBufs + m_AllocatedComputeCmdBufs, both actively used). 0 ALREADY_FIXED. -->

## What Changes

### A. Barrier 与管线效率（medium）
- `ExecuteBarriers` 使用最紧 stage mask 替代硬编码 `eAllCommands`，合并 image+buffer barrier 为单次调用
- 计算 pass 仅在实际写入 UAV 时注入 memory barrier，而非无条件注入
- `stateHaveGap` 路径移除冗余 release barrier（同队列无 QFOT 场景）
- 检测 compute queue family 为 -1 时禁用 asyncCompute 并输出警告

### B. 错误路径加固（medium/low）
- `RecordRenderPass`：任一 attachment 的 ImageView 为 null 时跳过整个 render pass，防止 framebuffer attachment 数量不匹配
- `AllocateCommandBuffer`：空向量返回时添加 `return vk::CommandBuffer{}` 防止无返回值 UB
- Validation layer 仅在非 NDEBUG 构建时启用，生产环境不因缺失 SDK 而崩溃
- `AllocateBuffer/AllocateImage` 失败时将 out 参数显式置 null
- `AllocUploadStagingBuffer` 失败时记录错误（而非静默 continue）
- Pipeline cache 回退创建添加二次 try-catch 防止嵌套异常崩溃
- 为 fence wait 添加超时（替代 UINT64_MAX），检测 DEVICE_LOST

### C. 内存与缓存清理（medium/low）
- `ReplanWithRealAlignment` 结束时重新计算 `m_TotalUnaliasedSize`
- GPL 路径 LinkPipeline 失败时立即销毁本次创建的 4 个 library part
- Swapchain 重建时清空 `m_FramebufferCache` 等依赖旧 imageView 的缓存
- 移除未使用的 `m_AllocatedCommandBuffers` 死代码
- `AllocatePage` 的 `push_back` 包在 try-catch 中防止 VMA 泄漏

### D. API 字段正确性（low）
- `InitializedState` 拆分为 `InitializedImageState` / `InitializedBufferState` 消除语义混淆
- `TextureSamplerDescriptor::Create` 修复 `integerFormat` 参数被忽略的 bug

## Capabilities

### New Capabilities
- `barrier-pipeline-efficiency`: Barrier 管线效率优化——紧 stage mask、合并调用、移除冗余 barrier
- `error-path-hardening`: 错误路径加固——避免 UB、静默失败、生产崩溃
- `memory-cache-cleanup`: 内存与缓存清理——泄漏修复、死代码移除、缓存失效
- `api-field-correctness`: API 字段正确性——sampler 字段映射、状态工厂语义修正

### Modified Capabilities
<!-- 此 change 不修改已有 spec -->

## Impact

- `VulkanGraphExecutor.cpp` — ExecuteBarriers（stage mask 合并）、RecordRenderPass（attachment null 检查）、ComputePass（条件 barrier）、stateHaveGap（移除冗余）、CompileAndExecute（asyncCompute 验证）、AllocUploadStagingBuffer（错误日志）、SubmitBatches（fence 超时）
- `VulkanResourceAliasing.cpp` — ReplanWithRealAlignment（总大小重算）
- `VulkanPipelineLibrary.cpp` — GPL LinkPipeline 失败清理
- `RenderBackend_Vulkan.cpp` — 条件 validation layer、pipeline cache 双重保护、swapchain 缓存清除、fence 超时
- `VulkanCommandListManager.cpp` — 空返回修复
- `VulkanLinearMemoryManager.cpp` — push_back 异常安全
- `VulkanMemoryManager.cpp` — out 参数初始化
- `TextureSampler.h` — integerFormat bug 修复
- `VulkanResourceState.h` — InitializedState 拆分
