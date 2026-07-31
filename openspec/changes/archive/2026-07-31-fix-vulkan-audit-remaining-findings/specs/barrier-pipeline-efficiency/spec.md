## ADDED Requirements

### Requirement: ExecuteBarriers 使用最紧 stage mask 并合并调用
`ExecuteBarriers` SHALL 从各 barrier 的 `VulkanResourceState::stageFlags` 收集最紧的 src/dst stage mask（按位或），并将所有 image barrier 和 buffer barrier 合并为单次 `vkCmdPipelineBarrier` 调用。

#### Scenario: 单次调用合并 image 和 buffer barrier
- **WHEN** `ExecuteBarriers` 同时有 image barrier 和 buffer barrier
- **THEN** 只调用一次 `vkCmdPipelineBarrier`，同时传入两个 barrier 数组

#### Scenario: 使用最紧 stage mask
- **WHEN** 所有 barrier 的 stageFlags 按位或为 `eFragmentShader | eColorAttachmentOutput`
- **THEN** `vkCmdPipelineBarrier` 使用该值而非 `eAllCommands`

### Requirement: 计算 pass 条件 barrier
`RecordComputePass` SHALL 仅在实际写入 UAV 的 dispatch 后注入 memory barrier，而非在每个 compute dispatch 后无条件注入。

#### Scenario: 无 UAV 写入的 compute pass 不注入 barrier
- **WHEN** compute pass 仅执行 read-only 资源访问
- **THEN** 不注入 memory barrier

### Requirement: stateHaveGap 移除同队列冗余 release
当 `stateHaveGap` 为 true 且 barrier 不含 QFOT（srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED）时，`PrepareBatchResourceBarriers` SHALL 仅创建 acquire barrier，不创建冗余 release barrier。

#### Scenario: 同队列跳跃 batch 时仅发射 acquire
- **WHEN** 资源在 intermediate batch 未被使用，且为同队列访问
- **THEN** 仅在目标 batch 创建 acquire barrier，跳过 release barrier

### Requirement: Compute queue family 有效性检查
`CompileAndExecute` SHALL 在任何 batch 使用 asyncCompute 前验证 compute queue family 非 -1。若为 -1，SHALL 输出警告并将所有 asyncCompute 的 batch 退化为同步 direct queue。

#### Scenario: 无 compute queue 时退化
- **WHEN** 硬件无独立 compute queue family（m_ComputeQueueFamily == -1），但 graph 标记了 asyncCompute
- **THEN** asyncCompute 被禁用，batch 在 direct queue 上同步执行
