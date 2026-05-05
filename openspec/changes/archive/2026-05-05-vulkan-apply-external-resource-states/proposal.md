## Why

Vulkan 后端的 `ApplyExternalResourceStates()` 当前为空函数，导致外部资源（External Image/Buffer、Backbuffer）在帧末的状态变更无法写回资源对象。D3D12 后端已完整实现此功能：遍历 render graph 收集的资源生命周期数据，将最终状态应用到外部资源对象上。补齐此功能是 Vulkan-D3D12 后端对齐的必要步骤。

## What Changes

- 实现 `VulkanGraphExecutor::ApplyExternalResourceStates()`：遍历 `m_ImageLifetimes` 和 `m_BufferLifetimes`，对 External/Backbuffer 类型资源取出最终 ResourceState 并写回对应的 `VulkanTexture` / `VulkanBuffer` 对象
- 为 `VulkanTexture` 添加 `SetResourceState()` / `GetResourceState()` 方法（对齐 D3D12 `D3DImageObject` 接口）
- 为 `VulkanBuffer` 添加 `SetResourceState()` / `GetResourceState()` 方法（对齐 D3D12 `D3DBufferObject` 接口）

## Capabilities

### New Capabilities

- `vulkan-external-resource-state`: 帧末将 render graph 计算的资源最终状态写回外部 Vulkan 资源对象

### Modified Capabilities

_(无已有 capability 被修改)_


## Impact

- `VulkanGraphExecutor.cpp` — 实现 `ApplyExternalResourceStates()` 函数体
- `VulkanTexture.h/.cpp` — 新增 `ResourceState` 存储和访问接口
- `VulkanBuffer.h/.cpp` — 新增 `ResourceState` 存储和访问接口
- `VulkanWindowHandle.h/.cpp` — 新增 per-swapchain-image Backbuffer 状态存储和 `ApplyCurrentBackBufferResourceState()` 方法
- `VulkanGraphExecutor.h` — 抽取 `VulkanResourceState` 至独立头文件 `GPUGraph/VulkanResourceState.h`
