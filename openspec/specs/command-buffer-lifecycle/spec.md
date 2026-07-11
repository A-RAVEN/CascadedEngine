# Command Buffer Lifecycle

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: Command buffer 分配成功

`VulkanCommandListManager::AllocateCommandBuffer` SHALL 在有效 `vk::CommandPool` 上成功分配命令缓冲区。分配的 `vk::CommandBuffer` SHALL 为有效句柄（非 null）。

#### Scenario: 首帧分配成功

- **WHEN** 首次调用 `GraphicsCommand()` 从刚通过 `resetCommandPool` 重置的 pool 中分配 command buffer
- **THEN** `allocateCommandBuffers` 返回包含 1 个有效 `vk::CommandBuffer` 的 vector

#### Scenario: 多帧复用

- **WHEN** 同一 frame context 在多帧中循环使用，每帧 `Aquire()` 中 `Reset()` 后重新分配 command buffer
- **THEN** 每帧的 command buffer 分配均成功
