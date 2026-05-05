## ADDED Requirements

### Requirement: VulkanTexture 存储资源最终状态
VulkanTexture SHALL 提供 `SetResourceState(VulkanResourceState)` 和 `GetResourceState()` 方法，以支持帧末状态写回。`m_LastResourceState` 初始化为 `{ eNone, eTopOfPipe, eUndefined, EGPUQueueType::eDirect, true }`，与现有 `m_CurrentLayout = eUndefined` 保持一致。

#### Scenario: 帧末状态写回
- **WHEN** `ApplyExternalResourceStates()` 遍历 External 类型 image
- **THEN** 对于每个 External image，断言 `m_ImageLifetimes[handle].states` 非空，从 `states.back()` 取出最后一帧的 `VulkanResourceState` 并调用 `texture->SetResourceState(state)`

### Requirement: VulkanBuffer 存储资源最终状态
VulkanBuffer SHALL 提供 `SetResourceState(VulkanResourceState)` 和 `GetResourceState()` 方法，以支持帧末状态写回。`m_LastResourceState` 初始化为 `{ eNone, eTopOfPipe, eUndefined, EGPUQueueType::eDirect, false }`，与现有 `m_PipelineStageFlags = eTopOfPipe`、`m_AccessFlags = eNone` 保持一致。对于 Buffer，`imageLayout` 字段无意义（`isImage=false`），设为 `eUndefined` 作为哨兵值。

#### Scenario: External Buffer 状态写回
- **WHEN** `ApplyExternalResourceStates()` 遍历 External 类型 buffer
- **THEN** 对于每个 External buffer，断言 `m_BufferLifetimes[handle].states` 非空，从 `states.back()` 取出最后一帧的 `VulkanResourceState` 并调用 `buffer->SetResourceState(state)`

### Requirement: VulkanWindowHandle 存储 Backbuffer 资源最终状态
VulkanWindowHandle SHALL 提供 `ApplyCurrentBackBufferResourceState(VulkanResourceState)` 方法，以支持帧末 Backbuffer 状态写回。状态存储为 `castl::vector<VulkanResourceState> m_BackBufferResourceStates`，按 `m_CurrentImageIndex` 索引，每个 swapchain image 独立存储其状态。

#### Scenario: Backbuffer 状态写回
- **WHEN** `ApplyExternalResourceStates()` 遍历 Backbuffer 类型 image
- **THEN** 对于每个 Backbuffer image，断言 `m_ImageLifetimes[handle].states` 非空，从 `states.back()` 取出最后一帧的 `VulkanResourceState` 并调用 `windowHandle->ApplyCurrentBackBufferResourceState(state)`，该方法将状态写入 `m_BackBufferResourceStates[m_CurrentImageIndex]`

#### Scenario: Backbuffer 状态数组初始化
- **WHEN** VulkanWindowHandle 创建/重建 Swapchain
- **THEN** `m_BackBufferResourceStates` resize 为 swapchain image 数量，每个元素初始化为 `VulkanResourceState::InitializedState()`

### Requirement: ApplyExternalResourceStates 跳过内部资源
`ApplyExternalResourceStates()` SHALL 跳过 Internal 类型资源，仅对 External 和 Backbuffer 类型资源写回状态。

#### Scenario: Internal 资源被跳过
- **WHEN** `ApplyExternalResourceStates()` 遍历到 Internal 类型 image 或 buffer
- **THEN** 该资源不进行任何状态写回操作，直接跳过
