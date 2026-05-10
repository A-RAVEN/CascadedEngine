## Context

当前 `PrepareBatchResourceBarriers()` 为所有 barrier 生成 `VK_QUEUE_FAMILY_IGNORED`。在 async compute pass 场景下，资源在 direct queue batch 和 compute queue batch 之间传递时需要 Queue Family Ownership Transfer (QFOT)。

已有基础设施：
- `VulkanResourceState::queueType` — 每个状态的所属队列类型
- `VulkanGPUExecutionBatch::anyComputeQueueOperations` — 标记 batch 是否含 async compute
- `VulkanGPUExecutionBatch::aquireBarriers` / `releaseBarriers` — Direct 队列 barrier 容器
- `VulkanGPUExecutionBatch::computeAquireBarriers` / `computeReleaseBarriers` — Compute 队列 barrier 容器（**当前未被录制执行**）
- `QueueContext` — 已有 `GetGraphicsQueueFamily()` / `GetComputeQueueFamily()` 查询方法
- `RenderBackend_Vulkan::GetQueueContext()` — executor 可通过 `GetApp()` 访问

**注意**: `computeWaitingDirectBatches` / `directWaitingComputeBatches` 是 batch 间依赖追踪字段，当前 `BuildDependencyFreeBatches()` 未填充它们。本 change 不使用这两个字段做 QFOT 判定，仅使用 per-resource 的 `queueType` 比较。

D3D12 不存在等效概念（D3D12 的 queue 模型不同），需独立设计。

## Goals / Non-Goals

**Goals:**
- 当 image/buffer 在 Direct 和 Compute queue 之间转移时，生成符合 Vulkan 规范的 QFOT barrier（release + acquire），并路由到正确的 Direct/Compute 变体容器
- 同一 queue family 内的状态转换保持 `VK_QUEUE_FAMILY_IGNORED`（不引入不必要的 overhead）
- 修复 `cachedState` 初始化：对 External/Backbuffer 资源从对象读回当前状态（对齐 D3D12），确保跨帧状态连续性
- 补齐 `computeAquireBarriers` / `computeReleaseBarriers` 在 `RecordBatchCommands()` 中的执行路径
- 处理跨帧 QFOT：当读回的 `cachedState.queueType` 与帧首 batch 的 `queueType` 不同时，生成单边 acquire barrier

**Non-Goals:**
- 不实现 Transfer queue 的 QFOT（当前所有 transfer 在 Direct queue 上执行）
- 不修改 `QueueContext` 接口
- 不实现 D3D12 的 "queue local layout" 概念（Vulkan 中无对应物 — Vulkan 的 `vk::ImageLayout` 是全局枚举，不存在 per-queue 最优 layout）

## Decisions

### Decision 1: QFOT 触发判定

**选择**: 在 `PrepareBatchResourceBarriers()` 中比较相邻 batch 的 `VulkanResourceState::queueType`，若不相等则触发 QFOT。

**理由**: `queueType` 字段在 `SetImageRWState()` / `SetBufferRWState()` 中随 batch 一起记录，天然反映每个 batch 中该资源在哪个队列上使用。不需要额外遍历 batch 依赖图。

**替代方案**: 遍历 `computeWaitingDirectBatches` 和 `directWaitingComputeBatches` — 但这是 batch 级别的依赖，不是 per-resource 级别。同一个 batch 内可能有些资源跨队列、有些不跨。

### Decision 2: EGPUQueueType → QueueFamilyIndex 映射

**选择**: 在 `VulkanGraphExecutor` 中新增私有方法 `GetQueueFamilyIndex(EGPUQueueType)`，在 `CompileAndExecute()` 入口从 `QueueContext` 查询并缓存到成员变量。

**理由**: 不需要每次 barrier 生成时都通过 `GetApp()` 链式查询。Queue family index 在设备生命周期内不变，缓存一次即可。

**替代方案**: 直接在 barrier 生成时调用 `GetApp()->GetQueueContext().GetGraphicsQueueFamily()` — 可行但啰嗦，每次调用链较长。

### Decision 3: QFOT barrier 结构

**选择**: 
- Release barrier（在 src batch 的 releaseBarriers 中）：`srcQueueFamilyIndex = srcFamily, dstQueueFamilyIndex = dstFamily`
- Acquire barrier（在 dst batch 的 aquireBarriers 中）：`srcQueueFamilyIndex = srcFamily, dstQueueFamilyIndex = dstFamily`
- 两个 barrier 的 access/stage/imageLayout 与非 QFOT 场景相同（release 用 src state → 0 / acquire 用 0 → dst state）

**理由**: 遵循 Vulkan 规范要求的 release + acquire barrier pair。Release barrier 保证 src queue 上的写入在 transfer 前完成；Acquire barrier 保证 dst queue 上的后续操作在 transfer 后才开始。Barrier 的 accessMask 在 release 侧用 srcAccess→0，acquire 侧用 0→dstAccess。

**替代方案**: 使用 `vk::PipelineStageFlagBits::eAllCommands` + `eAllCommands` 宽阶段 — 太保守，浪费 GPU 性能。

### Decision 4: stateHaveGap 的 QFOT 处理

**选择**: 当 `stateHaveGap == true` 且 `lastState.queueType != currentState.queueType` 时，release barrier 同时携带 QFOT 字段（srcQueueFamilyIndex/dstQueueFamilyIndex 不为 IGNORED）。

**理由**: gap 意味着中间 batch 不访问该资源，但所有权转移仍需在 release 和 acquire 边界完成。同时需要 gap 的 release + QFOT 是正交的两个需求，不应互斥。

### Decision 5: 只有 accessMask 变化时的同步队列处理

**选择**: 如果 `lastState.accessFlags == currentState.accessFlags` 且 queueType 不同，仍需要 QFOT（即使没有 access/state 变化也要做所有权转移）。

**理由**: 即使资源状态不变，一旦在另一个 queue family 上使用就必须完成 ownership transfer。不过当前代码中 `if (lastState.accessFlags == currentState.accessFlags && lastState.imageLayout == currentState.imageLayout) continue;` 会跳过屏障生成，QFOT 检测需要在此 continue 之前。

### Decision 6: cachedState 从资源对象读回（对齐 D3D12）

**选择**: 在 `PrepareBatchResourceBarriers()` 中，对 External Image 从 `image.GetTexturePtr<VulkanTexture>()->GetResourceState()` 获取 `cachedState`；对 Backbuffer 从 `image.GetWindowPtr<VulkanWindowHandle>()->GetCurrentBackBufferResourceState()` 获取（需新增 `GetCurrentBackBufferResourceState()` 方法）；对 Internal 资源仍用 `InitializedState()`。

**理由**: 当前 Vulkan 侧始终用 `InitializedState()` 作为 `cachedState`，这意味着 `ApplyExternalResourceStates()` 在帧末写入的状态在下帧 barrier 生成时被忽略。结果是每帧对同一 External 资源都从 `eUndefined` 开始生成 transition，产生冗余的 layout transition barrier。D3D12 通过 `pImage->GetResourceState()` 和 `pWindow->GetCurrentBackBufferResourceState()` 读回上次写入的状态，确保跨帧状态连续性。

**替代方案**: 不修复 — 导致每帧对 External 资源都生成虚假的 undefined→currentLayout transition，validation layer 会报错（image 实际不在 undefined layout）。

### Decision 7: QFOT barrier 的 Direct/Compute 变体路由

**选择**: 根据 src/dst 的 `queueType` 决定 barrier 写入哪个变体容器：
- **Direct → Compute**: release barrier 写入 `releaseBarriers`（src batch），acquire barrier 写入 `computeAquireBarriers`（dst batch）
- **Compute → Direct**: release barrier 写入 `computeReleaseBarriers`（src batch），acquire barrier 写入 `aquireBarriers`（dst batch）
- **同队列**: 按现有逻辑写入对应变体（Direct→Direct 用 `aquireBarriers`/`releaseBarriers`，Compute→Compute 用 compute 变体）

**理由**: QFOT 的 release barrier 必须在 src queue family 上执行，acquire barrier 必须在 dst queue family 上执行。`VulkanGPUExecutionBatch` 已有四组 barrier 容器（`aquireBarriers`/`releaseBarriers` for Direct + `computeAquireBarriers`/`computeReleaseBarriers` for Compute），QFOT barrier 需要路由到正确的容器才能被对应 queue 的 command buffer 录制执行。

**替代方案**: 全部写入 `aquireBarriers`/`releaseBarriers` — 但这意味着在 Direct command buffer 上执行 Compute queue family 的 barrier，违反 Vulkan 规范中 "如果 barrier 的 srcQueueFamilyIndex 和 dstQueueFamilyIndex 不同，barrier 必须在 src queue family（release）或 dst queue family（acquire）上录制" 的要求。

### Decision 8: RecordBatchCommands 中增加 compute 变体 barrier 执行

**选择**: 在 `RecordBatchCommands()` 中，当 batch 的 compute 变体 barrier 非空时，分配或复用 `batch.computeCommandBuffer` 并执行它们。compute 侧 barrier 执行顺序为：computeAquireBarriers → compute 工作 → computeReleaseBarriers。

**理由**: 当前 `RecordBatchCommands()` 仅在 `directCommandBuffer` 上执行 `aquireBarriers` 和 `releaseBarriers`，`computeAquireBarriers` 和 `computeReleaseBarriers` 完全未被录制。不补齐此执行路径则 QFOT 的 compute 侧 barrier 不会生效。

**注意**: 当前 `batch.computeCommandBuffer` 从未被分配（无代码从 `CommandListManager` 获取 compute command buffer）。本 decision 需要同时补齐 compute command buffer 的分配和 begin/end 录制逻辑。如果当前阶段 compute queue 的实际调度尚未就绪，至少需要确保 compute 变体 barrier 有合法的 command buffer 承载执行。

**替代方案**: 暂时将所有 QFOT barrier 都写入 Direct 变体，在 Direct command buffer 上执行 — 这在资源从 Direct 转到 Compute 时是正确的（acquire 在 dst queue family 执行），但从 Compute 转到 Direct 时 release 应该在 Compute queue family 执行，此处有语意不匹配。仅作为过渡方案可接受，但最终需要补齐 compute 侧执行路径。

### Decision 9: 跨帧 QFOT 处理

**选择**: 当 `cachedState` 从 External/Backbuffer 对象读回后，若 `cachedState.queueType`（帧 N-1 最后一笔 batch 的队列类型）与帧 N 第一个 batch 的 `queueType` 不同，则 **只生成 acquire barrier**（在 dst batch 的 acquire 变体中），不生成 release barrier（因无上一帧 batch 可承载）。

**理由**: 跨帧时上一帧的 batch 已提交完毕，无法追加 release barrier。在 Vulkan 中，如果能够证明上一帧的操作已经通过 fence/semaphore 完成了同步，则只需要在 dst queue 上做 acquire 即可完成所有权转移。当前 `GPUFrameManager` 通过 `FrameContext` 的 fence 机制保证了帧间串行化，上一帧的执行结果在帧 N 开始时已完成。

**替代方案**: 
- 在帧 N 第一个 batch 前插入一个虚拟 batch 承载 release barrier — 引入不必要的复杂度
- 假设跨帧不会发生 QFOT（即 External/Backbuffer 资源的上一帧最终状态总是在 Direct queue 上）— 当前可能成立，但未来 async compute 后不再成立，不能依赖此假设

- **风险**: 当前 async compute 功能尚未在 GPU 上实际测试，QFOT 的正确性需要端到端验证。但 barrier 逻辑的正确性可以从 Vulkan validation layer 得到保证。`computeCommandBuffer` 当前未被分配，compute 变体 barrier 的执行路径需要补齐，这是本 change 的一部分。
- **影响面**: 修改 `PrepareBatchResourceBarriers()` 内部逻辑（barrier 变体路由 + cachedState 修复 + 跨帧 QFOT），以及 `RecordBatchCommands()`（compute 变体 barrier 执行 + compute command buffer 分配）。不改变 barrier 的对外接口和后续的提交/提交流程。

### Decision 10: Cross-queue semaphore synchronization for QFOT

**选择**: 在 `SubmitBatches()` 中，当相邻 batch 之间存在 QFOT 时，在 src queue 的 submit 上 signal 一个 semaphore，在 dst queue 的 submit 上 wait 同一个 semaphore。Semaphore 由 `VulkanFrameBoundResourceManager` 管理（新增 `m_CrossQueueSemaphores` 池，按需分配和回收）。

**理由**: Vulkan 规范要求 QFOT 的 release barrier 和 acquire barrier 之间必须有 memory dependency（通过 semaphore 或 fence）。当前 `SubmitBatches()` 对 compute 和 direct queue 各自独立提交，仅使用 fence 做 host 端同步，缺少 GPU 端的跨队列执行顺序保证。没有 semaphore 同步时，acquire barrier 可能在 release barrier 之前执行，属于未定义行为。

**设计细节**:
- 遍历 batch 时，记录上一个有 compute submit 的 batch index 和上一个有 direct submit 的 batch index
- 检测当前 batch 的 `computeAquireBarriers` 非空且上个 direct batch 有 `releaseBarriers` 相关资源的 QFOT：compute submit 需 wait semaphore（由 direct submit signal）
- 检测当前 batch 的 `aquireBarriers` 非空且上个 compute batch 有 `computeReleaseBarriers` 相关资源的 QFOT：direct submit 需 wait semaphore（由 compute submit signal）
- 不在 batch 内直接追踪 semaphore 依赖，而是简化：**若同一个 batch 内同时有 compute 和 direct command buffer，且存在 QFOT 关系，则在两个 submit 之间插入 semaphore**

**简化策略**: 考虑到当前 async compute 场景还在早期阶段，先实现最核心的同步——同一个 batch 内若 `computeCommandBuffer` 和 `directCommandBuffer` 都存在且 batch 含有 cross-queue barriers，则 compute submit signal + direct submit wait（或反之，取决于 barrier 方向）。

**替代方案**: 
- 使用 timeline semaphore 做更灵活的跨队列同步 — 功能更强但引入更多复杂度，当前阶段不需要
- 将所有 QFOT barrier 的 release 和 acquire 合并到同一个 queue 执行 — 违反 Vulkan 规范对 queue family ownership transfer 的要求

### Decision 11: Per-pass compute command buffer routing

**选择**: 修改 `RecordComputePass()` 使其根据每个 compute pass 的 `asyncCompute` 属性独立决定 target command buffer，而非依赖 batch 级的 `anyComputeQueueOperations`。

**理由**: 同一个 batch 内可能同时包含 sync compute pass（`asyncCompute = false`，应在 Direct queue 上执行）和 async compute pass（`asyncCompute = true`，应在 Compute queue 上执行）。当前 `RecordComputePass()` 使用 `batch.anyComputeQueueOperations` 做判定，这是一个 batch 级标志——只要 batch 内有任何一个 async compute pass，所有 compute pass 都会被路由到 `computeCommandBuffer`，导致 sync compute pass 在错误的 queue family 上执行。

**实现**: 向 `RecordComputePass()` 传递 `computePass.asyncCompute` 参数，在函数内部决定 targetCmdBuf：
- `asyncCompute == true` 且 `batch.computeCommandBuffer` 已分配 → 使用 `computeCommandBuffer`
- 否则 → 使用 `batch.directCommandBuffer`（回退）

**影响**: `RecordBatchCommands()` 调用 `RecordComputePass()` 时需额外传入 `graph.GetComputePasses()[computePassID].asyncCompute`。

**替代方案**: 在 batch 构建阶段将 sync 和 async compute pass 分到不同 batch — 但这改变了 `BuildDependencyFreeBatches()` 的语义，影响面更大。

### Decision 12: Image aspect mask from texture format

**选择**: 在 `PrepareBatchResourceBarriers()` 中所有 image barrier 生成处，使用 descriptor format 推导正确的 `vk::ImageAspectFlags`，替代硬编码的 `eColor`。利用已有的 `VulkanTexture::GetImageAspect()` 逻辑（根据 `ETextureFormat` 返回 `eDepth` / `eDepth | eStencil` / `eColor`）。

**理由**: 当前所有 image barrier（包括 QFOT 和普通 transition barrier）的 `subresourceRange.aspectMask` 都硬编码为 `vk::ImageAspectFlagBits::eColor`。对 depth/stencil 纹理（如 `E_D24_UNORM_S8_UINT`, `E_D32_SFLOAT`），这会产生错误的 barrier，validation layer 会报 `VUID-vkImageMemoryBarrier-image-XXXXX` 错误。

**实现**: 
- 新增一个静态辅助函数 `GetImageAspectMask(ImageHandle const&, GPUGraph const&)`，根据 image 的 descriptor format 返回正确的 aspect mask
- 对所有 image barrier 构建点（QFOT release/acquire、跨帧 QFOT acquire、regular transition barrier）统一使用该函数

**注意**: 这是已有问题，不是本 change 引入的。但本 change 在 QFOT barrier 的新增代码中复制了这个模式，应在本次一并修复。

**替代方案**: 在 `VulkanResourceState` 中新增 `aspectMask` 字段 — 侵入性大，当前仅 barrier 生成需要此信息，不值得扩展状态结构体。
