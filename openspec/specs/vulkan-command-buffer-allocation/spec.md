# Spec: Vulkan 命令缓冲区分配

**Capability**: vulkan-command-buffer-allocation
**Change ID**: fix-vulkan-command-buffer-allocation

---

## ADDED Requirements

### Requirement: FrameContext 命令池可成功分配命令缓冲区

FrameContext 内嵌的 `VulkanCommandListManager` 的 `m_GraphicsPool`、`m_ComputePool`、`m_TransferPool` 在 pool 重置后，SHALL 能通过 `vkAllocateCommandBuffers` 成功分配至少一个 Primary 级别的 `VkCommandBuffer`。

#### Scenario: 首次帧 GraphicsCommand 分配成功
- **WHEN** 调用 `VulkanCommandListManager::GraphicsCommand()` 且 `m_GraphicsCommand` 为空
- **THEN** `AllocateCommandBuffer(m_GraphicsPool)` 返回有效的 `vk::CommandBuffer`

#### Scenario: Pool 重置后可再次分配
- **WHEN** 调用 `VulkanCommandListManager::Reset()` 重置所有 command pool，然后调用 `GraphicsCommand()`
- **THEN** 可以从已重置的 pool 中成功分配新的 `vk::CommandBuffer`

### Requirement: 命令缓冲区分配失败时输出诊断信息

`VulkanCommandListManager::AllocateCommandBuffer` 在 `vkAllocateCommandBuffers` 返回非 VK_SUCCESS 的 VkResult 时，SHALL 通过 `CA_LOG_ERR` 输出实际的 Vulkan 错误码。

#### Scenario: 分配失败时输出 VkResult 错误码
- **WHEN** `vkAllocateCommandBuffers` 返回的 `ResultValue` 中 `result != VK_SUCCESS`
- **THEN** 日志包含实际的 VkResult 值（如 "VK_ERROR_OUT_OF_DEVICE_MEMORY"），而非仅报告 "size == 0"

### Requirement: Command pool 必须由正确的 Device 创建和使用

FrameContext 的 `VulkanCommandListManager` 中创建 pool 的 `vk::Device` 与分配命令缓冲区时使用的 `vk::Device` SHALL 为同一实例。

#### Scenario: Pool 与分配调用使用同一 Device
- **WHEN** 在 `VulkanCommandListManager::Init()` 中通过 `GetDevice()` 创建 command pool
- **THEN** 在 `AllocateCommandBuffer` 中通过 `GetDevice()` 调用 `allocateCommandBuffers` 时返回的 Device 句柄与 pool 创建时的 Device 相同
