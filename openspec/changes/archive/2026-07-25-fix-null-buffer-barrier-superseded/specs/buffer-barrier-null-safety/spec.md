## ADDED Requirements

### Requirement: 屏障系统跳过无法解析的 buffer
当 `m_LocalResourceManager.GetBuffer(BufferHandle)` 返回 `nullptr`（VK_NULL_HANDLE）时，资源屏障系统 SHALL 跳过该 buffer 的 barrier 创建，不得将 null handle 传入 `vkCmdPipelineBarrier`。

#### Scenario: QFOT barrier 跳过 null buffer
- **WHEN** qfot 路径中 `GetBuffer(buffer)` 返回 null
- **THEN** 跳过该 buffer 的 `AddBufferBarrier` 调用，不产生 validation error

#### Scenario: 常规 barrier 跳过 null buffer
- **WHEN** 常规 barrier 路径中 `GetBuffer(buffer)` 返回 null
- **THEN** 跳过该 buffer 的 `AddBufferBarrier` 调用，不产生 validation error
