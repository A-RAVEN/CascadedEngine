## ADDED Requirements

### Requirement: 双 bool 独立跟踪 fence 提交状态
系统 SHALL 使用 `m_DirectFenceSubmitted` 和 `m_ComputeFenceSubmitted` 两个独立标志跟踪 DirectFence 和 ComputeFence 的提交状态。每个提交路径 SHALL 只设置对应 fence 的标志。

#### Scenario: Direct 提交只标记 DirectFence
- **WHEN** SubmitBatches 中无跨 queue 路径执行 direct queue 提交
- **THEN** 仅 `m_DirectFenceSubmitted` 被设为 true，`m_ComputeFenceSubmitted` 不变

#### Scenario: Compute 提交只标记 ComputeFence
- **WHEN** SubmitBatches 中 compute 路径执行 compute queue 提交
- **THEN** 仅 `m_ComputeFenceSubmitted` 被设为 true，`m_DirectFenceSubmitted` 不变

#### Scenario: 跨 queue 提交标记双 fence
- **WHEN** SubmitBatches 中执行 cross-queue 提交（同时使用 direct + compute queue）
- **THEN** 两个标志都被设为 true

### Requirement: Batch 同步仅等待已提交的 fence
`SubmitBatches` 中的 batch-ordering sync 循环 SHALL 仅对已提交的 fence 调用 `waitForFences` + `resetFences`，wait+reset 后清除对应标志。

#### Scenario: 仅 direct fence 提交时不等 compute
- **WHEN** 当前 batch 仅提交了 direct queue 工作
- **THEN** 仅等待和重置 DirectFence，清除 m_DirectFenceSubmitted

#### Scenario: 等待后 Aquire 不阻塞
- **WHEN** batch sync 已 wait+reset DirectFence 且已清除标志
- **THEN** 下一帧 `Aquire()` 中 `IsFenceSubmitted()` 返回 false，直接复用帧上下文

### Requirement: waitForFences 返回值全量检查
所有 6 处 `waitForFences` 调用 SHALL 检查返回值。VK_ERROR_DEVICE_LOST 时 SHALL 记录错误并停止后续操作。

#### Scenario: Upload waitForFences 返回 DEVICE_LOST
- **WHEN** `UploadData` 中 `waitForFences` 返回 VK_ERROR_DEVICE_LOST
- **THEN** 函数记录错误并返回失败

#### Scenario: SubmitBatches waitForFences 返回 DEVICE_LOST
- **WHEN** batch-ordering sync 中 `waitForFences` 返回 VK_ERROR_DEVICE_LOST
- **THEN** 系统记录错误并设置内部 device-lost 标志

#### Scenario: FrameContext::Aquire waitForFences 返回 DEVICE_LOST
- **WHEN** 帧上下文 Aquire 中 `waitForFences` 返回 VK_ERROR_DEVICE_LOST
- **THEN** 系统记录错误并设置内部 device-lost 标志

#### Scenario: GPUFrameManager::WaitIdle 返回 DEVICE_LOST
- **WHEN** `WaitIdle` 的 `waitForFences` 返回 VK_ERROR_DEVICE_LOST
- **THEN** 系统记录错误，不继续执行后续渲染操作

### Requirement: Swapchain 致命错误传播
`acquireNextImageKHR` 和 `presentKHR` SHALL 检查 DEVICE_LOST、SURFACE_LOST 等致命错误并传播。

#### Scenario: acquireNextImageKHR 返回 DEVICE_LOST
- **WHEN** `acquireNextImageKHR` 返回 VK_ERROR_DEVICE_LOST
- **THEN** 系统记录错误并返回失败状态
