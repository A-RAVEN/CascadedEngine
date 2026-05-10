## Why

Vulkan 后端当前的 barrier 生成全部使用 `VK_QUEUE_FAMILY_IGNORED`，未实现 Queue Family Ownership Transfer (QFOT)。当 async compute pass 在独立 compute queue 上执行、资源在 Direct queue batch 和 Compute queue batch 之间传递时，缺少 QFOT barrier 属于 Vulkan 规范的未定义行为。这是 Phase 2 最后一个未完成的 P1 项。

## What Changes

- 在 `PrepareBatchResourceBarriers()` 中，当 image/buffer 在相邻 batch 间的 `queueType` 发生变化时，生成 release barrier（在 src batch）+ acquire barrier（在 dst batch），正确设置 `srcQueueFamilyIndex` 和 `dstQueueFamilyIndex`。**根据 src/dst 队列类型将 barrier 路由到正确的变体**：Direct → Compute 的 acquire 写入 `computeAquireBarriers`，Compute → Direct 的 release 从 `computeReleaseBarriers` 读取
- 修复 `cachedState` 初始化：对 External/Backbuffer 资源从对象读回 `GetResourceState()`（而非始终用 `InitializedState()`），确保跨帧状态连续性——对齐 D3D12 的 `pImage->GetResourceState()` / `pWindow->GetCurrentBackBufferResourceState()` 行为。**同时处理跨帧 QFOT**：读回的 `cachedState.queueType` 可能与首 batch 的 `queueType` 不同，此时 release barrier 无可承载 batch，只生成单边 acquire barrier 完成所有权转移
- 在 `RecordBatchCommands()` 中增加 `computeAquireBarriers` / `computeReleaseBarriers` 的执行逻辑（当前仅执行了 direct 变体，compute 变体未被录制）
- 利用 `VulkanResourceState::queueType` 字段判定是否需要 QFOT（**注**：`computeWaitingDirectBatches` / `directWaitingComputeBatches` 是 batch 级依赖追踪字段，当前未填充，QFOT 判定不依赖它们；本 change 仅在注释/日志中使用 batch 的 `anyComputeQueueOperations` 作为辅助信息）
- 将 `QueueContext` 的 queue family index 查询能力暴露给 barrier 生成逻辑（已有 `GetGraphicsQueueFamily()` / `GetComputeQueueFamily()`）

## Capabilities

### New Capabilities

- `vulkan-queue-family-ownership-transfer`: 当资源在 direct queue 和 compute queue 之间传递时，生成正确的 Queue Family Ownership Transfer barrier

### Modified Capabilities

_(无已有 capability 被修改)_

## Impact

- `VulkanGraphExecutor.cpp` — 修改 `PrepareBatchResourceBarriers()`：① QFOT 判定逻辑和 release/acquire barrier 生成（含 barrier 变体路由）；② 修复 `cachedState` 从 External/Backbuffer 对象读回状态；③ 跨帧 QFOT 处理；④ 修复 image barrier 的 `aspectMask` 硬编码问题。修改 `RecordBatchCommands()`：⑤ 增加 compute 变体 barriers 的执行录制；⑥ 修复 compute pass 路由为 per-pass 判定。修改 `SubmitBatches()`：⑦ 增加跨队列 semaphore 同步
- `VulkanGraphExecutor.h` — 新增成员 `m_GraphicsQueueFamily` / `m_ComputeQueueFamily` 和辅助方法 `GetQueueFamilyIndex(EGPUQueueType)`
- `VulkanWindowHandle.h` / `.cpp` — 新增 `GetCurrentBackBufferResourceState()` 方法（已完成），`m_BackBufferResourceStates` 初始化
- `VulkanFrameBoundResourceManager` — 新增 cross-queue semaphore 池管理

## Code Review Fixes (2026-05-09)

Code review 发现以下问题，已作为新 tasks 追加到 tasks.md：

- **[CRITICAL]** `SubmitBatches()` Direct→Compute QFOT 路径遗漏 swapchain window semaphore 同步（acquire/present），导致呈现异常
- **[IMPORTANT]** Regular transition barrier 始终路由到 Direct 变体，跨队列后可能在错误队列上访问资源 + QFOT acquire 后 duplicate layout transition 触发 validation error
- **[IMPORTANT]** Internal 资源（每帧新建）误触发跨帧 QFOT acquire barrier
- **[IMPORTANT]** `AllocCrossQueueSemaphore()` off-by-one 导致 semaphore 无法复用、帧内无限增长
- **[MINOR]** `GetQueueFamilyIndex()` default 返回裸 `-1` 而非 Vulkan 常量
- **[MINOR]** Compute→Direct wait stage 使用 `eAllCommands` 过于保守
- **[MINOR]** 清理未使用的成员变量
