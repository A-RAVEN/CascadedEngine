## Why

VulkanGraphExecutor 的 `CollectResources` 阶段中，Compute Pass 的 RW 状态注册是空实现（仅有 `// TODO: Register compute shader resources`）。虽然 `VulkanResourceBindingInstance::BuildResources()` 已经在 `CompileAndExecute` 中为 compute shader 执行了 `RegisterTemporaryTexture/Buffer` 的 fallback 注册，但 **compute pass 的 image/buffer 读写状态从未被写入 `m_ComputePassRWStates`**。

这导致：
- `BuildDependencyFreeBatches` 的依赖分析看不到 compute 资源的读写冲突
- `PrepareBatchResourceBarriers` 无法为 compute 资源生成正确的 image layout transition 和 memory barrier
- Compute 路径的 barrier/同步基本不可用

参照 D3D12 后端已完整实现的 `addPassShaderInstancesResourcesRWStates` 流程，补全 compute shader 资源的 **状态追踪**（VulkanExecutorRWState）是让 Vulkan 后端正确支持 Compute Shader 的前提。

## What Changes

- **VulkanGraphExecutor::Prepare**: 在 `CollectShaderBindings` 之后新增 `RegisterComputeResources` 步骤，为 Compute Pass 完整实现资源状态注册：
  - 遍历每个 dispatch 的 `VulkanResourceBindingInstance`，获取其 image/buffer binding elements
  - 对每个 image binding 调用 `m_ComputePassRWStates[passID].SetImageRWState()`
  - 对每个 buffer binding 调用 `m_ComputePassRWStates[passID].SetBufferRWState()`
  - 根据 `bindingInfo.accessType` 映射到正确的 `vk::AccessFlags` 和 `vk::ImageLayout`
  - 资源生命周期注册（RegisterTemporaryTexture/Buffer）由已有的 `BuildResources` 处理，本步骤只负责状态
- **新增两个 access 映射 helper**：`ComputeAccessToVulkanAccess(EShaderResourceAccess)` 和 `ComputeAccessToImageLayout(EShaderResourceAccess)`
- **移除** `CollectResources` 中 compute pass 的 `// TODO` 空壳代码（行 737-745）

## Capabilities

### New Capabilities
- `vulkan-compute-resource-registration`: Compute Shader Pass 的资源状态追踪，包括访问类型映射、VulkanExecutorRWState 写入

### Modified Capabilities
- `vulkan-shader-binding`: 补充 Compute Shader 在 Prepare 阶段对 binding 状态收集的需求

## Impact

- **VulkanGraphExecutor.cpp** (~80行新增): `Prepare()` 中新增 `RegisterComputeResources` 步骤；两个 access 映射 helper
- **VulkanGraphExecutor.h**: 新增 `RegisterComputeResources(GPUGraph const& graph)` 私有方法声明
- **对比参考**: `D3D12RenderBackend/private/GPUGraph/GPUGraphExecutor.cpp:480-505`
