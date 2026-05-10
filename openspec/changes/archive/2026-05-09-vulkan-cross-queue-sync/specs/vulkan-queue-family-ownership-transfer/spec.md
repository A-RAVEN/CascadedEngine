## ADDED Requirements

### Requirement: 跨队列资源所有权转移
当资源的 `VulkanResourceState::queueType` 在相邻 batch 间发生变化时（Direct ↔ Compute），系统 SHALL 生成 Queue Family Ownership Transfer (QFOT) barrier pair，即 release barrier（在 src batch）和 acquire barrier（在 dst batch），正确设置 `srcQueueFamilyIndex` 和 `dstQueueFamilyIndex` 为实际的 queue family index。Barrier SHALL 路由到与 src/dst queue family 对应的变体容器。

#### Scenario: Image 从 Direct queue 转移到 Compute queue
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个 image 在 batch `N-1` 的 `queueType` 为 `eDirect`、batch `N` 的 `queueType` 为 `eCompute`
- **THEN** 在 batch `N-1` 的 `releaseBarriers`（Direct 变体）中生成 release barrier，`srcQueueFamilyIndex` = graphics family, `dstQueueFamilyIndex` = compute family, `srcAccessMask` = lastState.accessFlags, `dstAccessMask` = 0
- **AND** 在 batch `N` 的 `computeAquireBarriers`（Compute 变体）中生成 acquire barrier，`srcQueueFamilyIndex` = graphics family, `dstQueueFamilyIndex` = compute family, `srcAccessMask` = 0, `dstAccessMask` = currentState.accessFlags

#### Scenario: Image 从 Compute queue 转移到 Direct queue
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个 image 在 batch `N-1` 的 `queueType` 为 `eCompute`、batch `N` 的 `queueType` 为 `eDirect`
- **THEN** 在 batch `N-1` 的 `computeReleaseBarriers`（Compute 变体）中生成 release barrier，`srcQueueFamilyIndex` = compute family, `dstQueueFamilyIndex` = graphics family, `srcAccessMask` = lastState.accessFlags, `dstAccessMask` = 0
- **AND** 在 batch `N` 的 `aquireBarriers`（Direct 变体）中生成 acquire barrier，`srcQueueFamilyIndex` = compute family, `dstQueueFamilyIndex` = graphics family, `srcAccessMask` = 0, `dstAccessMask` = currentState.accessFlags

#### Scenario: Buffer 从 Direct queue 转移到 Compute queue
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个 buffer 在 batch `N-1` 的 `queueType` 为 `eDirect`、batch `N` 的 `queueType` 为 `eCompute`
- **THEN** 在 batch `N-1` 的 `releaseBarriers` 中生成 release barrier，`srcQueueFamilyIndex` = graphics family, `dstQueueFamilyIndex` = compute family
- **AND** 在 batch `N` 的 `computeAquireBarriers` 中生成 acquire barrier，`srcQueueFamilyIndex` = graphics family, `dstQueueFamilyIndex` = compute family

#### Scenario: Buffer 从 Compute queue 转移到 Direct queue
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个 buffer 在 batch `N-1` 的 `queueType` 为 `eCompute`、batch `N` 的 `queueType` 为 `eDirect`
- **THEN** 在 batch `N-1` 的 `computeReleaseBarriers` 中生成 release barrier，`srcQueueFamilyIndex` = compute family, `dstQueueFamilyIndex` = graphics family
- **AND** 在 batch `N` 的 `aquireBarriers` 中生成 acquire barrier，`srcQueueFamilyIndex` = compute family, `dstQueueFamilyIndex` = graphics family

### Requirement: 同队列 family 内不生成 QFOT
同一 queue family 内的资源状态转换 SHALL 保持 `VK_QUEUE_FAMILY_IGNORED`，不生成不必要的 QFOT barrier。

#### Scenario: Direct 队列内状态转换不触发 QFOT
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个资源在相邻 batch 间 `queueType` 相同（如均为 `eDirect`）
- **THEN** barrier 的 `srcQueueFamilyIndex` 和 `dstQueueFamilyIndex` 均设为 `VK_QUEUE_FAMILY_IGNORED`
- **AND** barrier 路由到 Direct 变体容器（`aquireBarriers` / `releaseBarriers`）

#### Scenario: Compute 队列内状态转换不触发 QFOT
- **WHEN** `PrepareBatchResourceBarriers()` 检测到某个资源在相邻 batch 间 `queueType` 相同（均为 `eCompute`）
- **THEN** barrier 的 `srcQueueFamilyIndex` 和 `dstQueueFamilyIndex` 均设为 `VK_QUEUE_FAMILY_IGNORED`
- **AND** barrier 路由到 Compute 变体容器（`computeAquireBarriers` / `computeReleaseBarriers`）

### Requirement: QFOT 与 accessMask 变化的独立判定
QFOT 判定 SHALL 独立于 accessMask / imageLayout 的比较。即使资源状态不变但 queueType 变化，仍需要执行所有权转移。当前 skip 逻辑（`continue` when accessFlags and imageLayout unchanged）SHALL 在 QFOT 判定之后执行。

#### Scenario: 跨队列且状态不变仍需 QFOT
- **WHEN** `lastState.accessFlags == currentState.accessFlags` 且 `lastState.imageLayout == currentState.imageLayout`，但 `lastState.queueType != currentState.queueType`
- **THEN** 仍生成 QFOT release + acquire barrier pair（不因状态不变而跳过）
- **AND** 由于 accessMask / imageLayout 不变，跳过后续的普通状态转换 barrier（仅 QFOT barrier）

### Requirement: QFOT 与 stateHaveGap 的兼容
当 `stateHaveGap == true`（batch 之间有间隔）且同时需要 QFOT 时，release barrier SHALL 同时携带 gap 语义（放在 lastBatch 对应变体的 releaseBarriers）和 QFOT 语义（正确设置 queue family index）。acquire barrier 同理放在 currentBatch 对应变体的 aquireBarriers。

#### Scenario: 跨队列且有 gap
- **WHEN** `stateHaveGap == true` 且 `lastState.queueType != currentState.queueType`
- **THEN** release barrier 放入 `lastBatch` 对应变体的 release 容器（Direct→Compute 用 `releaseBarriers`，Compute→Direct 用 `computeReleaseBarriers`），同时设置 QFOT 字段
- **AND** acquire barrier 放入 `currentBatch` 对应变体的 acquire 容器（Direct→Compute 用 `computeAquireBarriers`，Compute→Direct 用 `aquireBarriers`），同时设置 QFOT 字段

### Requirement: 跨帧 QFOT 处理
当 `cachedState` 从 External/Backbuffer 资源对象读回后，若 `cachedState.queueType`（上一帧最终状态所在队列）与帧 `N` 第一个 batch 的 `queueType` 不同，系统 SHALL 仅生成单边 acquire barrier（无 release barrier，因上一帧 batch 已不可达）。

#### Scenario: 跨帧队列转移（上一帧末 Compute → 本帧首 Direct）
- **WHEN** `PrepareBatchResourceBarriers()` 中 `isFirstState == true` 且 `cachedState` 从对象读回后 `cachedState.queueType == eCompute` 而 `currentState.queueType == eDirect`
- **THEN** 在第一个 batch 的 `aquireBarriers` 中生成 acquire barrier，`srcQueueFamilyIndex` = compute family, `dstQueueFamilyIndex` = graphics family, `srcAccessMask` = 0, `dstAccessMask` = currentState.accessFlags
- **AND** 不生成 release barrier

#### Scenario: 跨帧队列转移（上一帧末 Direct → 本帧首 Compute）
- **WHEN** `cachedState.queueType == eDirect` 而 `currentState.queueType == eCompute`
- **THEN** 在第一个 batch 的 `computeAquireBarriers` 中生成 acquire barrier，`srcQueueFamilyIndex` = graphics family, `dstQueueFamilyIndex` = compute family

#### Scenario: 跨帧同队列不触发 QFOT
- **WHEN** `cachedState.queueType == currentState.queueType`（跨帧但同一 queue family）
- **THEN** 不生成跨帧 QFOT barrier，按正常流程处理

### Requirement: Compute 变体 barrier 的执行录制
`RecordBatchCommands()` SHALL 在 `computeCommandBuffer` 上执行 `computeAquireBarriers` 和 `computeReleaseBarriers`（当它们非空时）。若 batch 需要 compute command buffer 但尚未分配，SHALL 从 `CommandListManager` 分配并 begin/end 录制。

#### Scenario: 有 compute 变体 barrier 时分配并执行
- **WHEN** `RecordBatchCommands()` 检测到 `batch.computeAquireBarriers.AnyBarrier()` 或 `batch.computeReleaseBarriers.AnyBarrier()` 为 true
- **THEN** 若 `batch.computeCommandBuffer` 未分配，从 `cmdListManager.ComputeCommand()` 获取并 begin
- **AND** 在 compute command buffer 上按顺序执行：computeAquireBarriers → compute 工作（compute pass 录制）→ computeReleaseBarriers
- **AND** end compute command buffer

#### Scenario: 无 compute 变体 barrier 且无 compute 工作时跳过
- **WHEN** `batch.anyComputeQueueOperations == false` 且 compute 变体 barrier 均为空
- **THEN** 不分配 compute command buffer，仅使用 direct command buffer（现有行为）

### Requirement: External 资源 cachedState 从对象读回
`PrepareBatchResourceBarriers()` SHALL 对 External Image 从 `VulkanTexture::GetResourceState()` 获取 `cachedState`；对 Backbuffer Image 从 `VulkanWindowHandle::GetCurrentBackBufferResourceState()` 获取；对 Internal 资源仍用 `InitializedState()`。Buffer 同理：External buffer 从 `VulkanBuffer::GetResourceState()` 获取。

#### Scenario: External Image 跨帧状态连续
- **WHEN** 上一帧 `ApplyExternalResourceStates()` 已将最终状态写入 `VulkanTexture::m_LastResourceState`
- **THEN** 本帧 `PrepareBatchResourceBarriers()` 中该 External image 的首次 `cachedState` 从 `texture->GetResourceState()` 读回，而非 `InitializedState()`

#### Scenario: Backbuffer 跨帧状态连续
- **WHEN** 上一帧已将最终状态写入 `m_BackBufferResourceStates[m_CurrentImageIndex]`
- **THEN** 本帧 `PrepareBatchResourceBarriers()` 中该 Backbuffer 的首次 `cachedState` 从 `windowHandle->GetCurrentBackBufferResourceState()` 读回

#### Scenario: Internal 资源仍用 InitializedState
- **WHEN** `image.IsIntternal()` 为 true
- **THEN** `cachedState = VulkanResourceState::InitializedState()`（Internal 资源每帧重新创建，无跨帧状态）

### Requirement: Cross-queue semaphore synchronization
当同一个 batch 内同时存在 compute 和 direct command buffer 且 batch 包含跨队列 barrier 时，`SubmitBatches()` SHALL 在 compute submit 和 direct submit 之间插入 semaphore 同步，保证 QFOT release barrier 在 acquire barrier 之前完成。

#### Scenario: Direct→Compute QFOT 提交顺序
- **WHEN** batch 包含 compute command buffer（含 `computeAquireBarriers`）和 direct command buffer（含 `releaseBarriers`），且存在 Direct→Compute QFOT
- **THEN** direct submit signal semaphore → compute submit wait 同一 semaphore，确保 release barrier 在 acquire barrier 之前执行

#### Scenario: Compute→Direct QFOT 提交顺序
- **WHEN** batch 包含 compute command buffer（含 `computeReleaseBarriers`）和 direct command buffer（含 `aquireBarriers`），且存在 Compute→Direct QFOT
- **THEN** compute submit signal semaphore → direct submit wait 同一 semaphore，确保 release barrier 在 acquire barrier 之前执行

#### Scenario: consecutive batches cross-queue ordering
- **WHEN** Batch N 的 compute submit 和 Batch N+1 的 compute submit 之间有资源依赖（前者的产出被后者消费）
- **THEN** 在 `SubmitBatches()` 层面保证 batch 间提交顺序：batch N 的两个 queue 提交完成后才提交 batch N+1

#### Scenario: 无 QFOT 时不引入额外 semaphore
- **WHEN** batch 虽有 compute 和 direct command buffer 但不含跨队列 barrier（无 QFOT）
- **THEN** 不引入额外 semaphore 同步（两个 queue 独立提交即可）

### Requirement: Per-pass compute command buffer routing
`RecordComputePass()` SHALL 根据每个 compute pass 的 `asyncCompute` 属性独立决定 target command buffer，而非依赖 batch 级的 `anyComputeQueueOperations` 标志。

#### Scenario: Sync compute pass 始终在 direct command buffer 上执行
- **WHEN** `RecordComputePass()` 处理 `asyncCompute == false` 的 compute pass
- **THEN** 使用 `batch.directCommandBuffer` 录制，忽略 `batch.anyComputeQueueOperations` 和 `batch.computeCommandBuffer`

#### Scenario: Async compute pass 路由到 compute command buffer（若可用）
- **WHEN** `RecordComputePass()` 处理 `asyncCompute == true` 的 compute pass，且 `batch.computeCommandBuffer` 已分配
- **THEN** 使用 `batch.computeCommandBuffer` 录制

#### Scenario: Async compute pass 回退到 direct command buffer
- **WHEN** `RecordComputePass()` 处理 `asyncCompute == true` 的 compute pass，但 `batch.computeCommandBuffer` 未分配
- **THEN** 回退到 `batch.directCommandBuffer` 录制（keep existing fallback behavior）

### Requirement: Image aspect mask from texture format
所有在 `PrepareBatchResourceBarriers()` 中生成的 image memory barrier SHALL 根据 image 的 descriptor format 推导正确的 `vk::ImageAspectFlags`，替代硬编码的 `vk::ImageAspectFlagBits::eColor`。

#### Scenario: Depth texture uses eDepth aspect
- **WHEN** image 的 `ETextureFormat` 为 `E_D32_SFLOAT` 或 `E_D16_UNORM`
- **THEN** `barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth`

#### Scenario: Depth-stencil texture uses eDepth | eStencil
- **WHEN** image 的 `ETextureFormat` 为 `E_D24_UNORM_S8_UINT`
- **THEN** `barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil`

#### Scenario: Color texture uses eColor
- **WHEN** image 的 `ETextureFormat` 为其他值（color formats）
- **THEN** `barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor`
