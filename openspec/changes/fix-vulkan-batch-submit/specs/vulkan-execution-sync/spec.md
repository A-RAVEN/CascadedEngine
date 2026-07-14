## ADDED Requirements

### Requirement: Batch 间非阻塞 Submit

`VulkanGraphExecutor::SubmitBatches()` SHALL NOT 在 batch 间调用 `device.waitForFences(UINT64_MAX)` 阻塞 CPU。Batch 间顺序由 Vulkan 队列内隐式顺序保证，跨队列依赖由 semaphore 保证。

#### Scenario: 多 Batch 并行提交

- **GIVEN** execution graph 有 3 个无依赖 batch
- **WHEN** `SubmitBatches()` 依次提交
- **THEN** CPU 不阻塞等待每个 batch 的 fence
- **AND** 仅最后一个 batch 的 fence 用于下一帧 `Aquire` 同步

#### Scenario: 跨队列 Batch 正确同步

- **GIVEN** batch N 使用 direct queue, batch N+1 使用 compute queue 且有 release/acquire 依赖
- **WHEN** `SubmitBatches()` 提交
- **THEN** direct queue 和 compute queue 间通过 semaphore 正确同步
- **AND** 无 blocking fence wait

### Requirement: 跨队列资源共享检测

`VulkanResourceState::isSharedBetweenQueues` SHALL 正确检测资源是否被多个 queue family 使用（direct + compute），而非恒返回 `false`。

#### Scenario: 资源共享检测

- **GIVEN** 资源被 raster pass (direct queue) 和 compute pass (compute queue) 同时使用
- **WHEN** `isSharedBetweenQueues` 被查询
- **THEN** 返回 `true`
- **AND** 对应的 barrier 正确处理 queue family ownership transfer

### Requirement: Split Barriers 用于 Gapped 资源

`PrepareBatchResourceBarriers()` SHALL 为 gapped 资源（batch N 和 batch N+2 间有间隔）生成分离的 release barrier（batch N）和 acquire barrier（batch N+2），使用匹配的 pipeline stage 和 access 掩码。

#### Scenario: Gapped 资源的分离 Barrier

- **GIVEN** 资源在 batch 0 和 batch 2 中被使用，batch 1 不使用
- **WHEN** `PrepareBatchResourceBarriers()` 被调用
- **THEN** batch 0 记录 release barrier，batch 2 记录 acquire barrier
- **AND** 两个 barrier 的 stage/access mask 匹配
