## 1. Access 映射 Helpers

- [x] 1.1 在 `VulkanGraphExecutor.cpp` 中实现 `ComputeAccessToVulkanAccess(EShaderResourceAccess)` — 映射 `eReadOnly→eShaderRead`, `eWriteOnly→eShaderWrite`, `eReadWrite→eShaderRead|eShaderWrite`
- [x] 1.2 在 `VulkanGraphExecutor.cpp` 中实现 `ComputeAccessToImageLayout(EShaderResourceAccess)` — 映射 `eReadOnly→eShaderReadOnlyOptimal`, `eWriteOnly→eGeneral`, `eReadWrite→eGeneral`

## 2. VulkanGraphExecutor 新增 RegisterComputeResources 方法

- [x] 2.1 在 `VulkanGraphExecutor.h` 声明 `RegisterComputeResources(GPUGraph const& graph)` 私有方法
- [x] 2.2 在 `VulkanGraphExecutor.cpp` 实现 `RegisterComputeResources` 方法骨架：遍历 compute passes → dispatches → binding instances，获取 queue type（`asyncCompute ? eCompute : eDirect`）

## 3. Image 状态注册

- [x] 3.1 遍历 `GetImageBindings()`，对每个 ImageBindingElement 中的每个 image handle，调用 `passRWState.SetImageRWState()`，参数：
  - stage: `vk::PipelineStageFlagBits::eComputeShader`（硬编码，usingStages 此时未填充）
  - access: `ComputeAccessToVulkanAccess(bindingInfo.accessType)`
  - layout: `ComputeAccessToImageLayout(bindingInfo.accessType)`
  - queueType: 根据 `asyncCompute` 判断
- [x] 3.2 注意：本步骤**不调用** `RegisterTemporaryTexture`，资源生命周期由 `BuildResources` 的 fallback 机制处理

## 4. Buffer 状态注册

- [x] 4.1 遍历 `GetBufferBindings()`，对每个 BufferBindingElement 中的每个 buffer handle，调用 `passRWState.SetBufferRWState()`，参数：
  - stage: `vk::PipelineStageFlagBits::eComputeShader`（硬编码）
  - access: `ComputeAccessToVulkanAccess(bindingInfo.accessType)`
  - queueType: 根据 `asyncCompute` 判断
- [x] 4.2 注意：本步骤**不调用** `RegisterTemporaryBuffer`

## 5. Prepare 阶段集成

- [x] 5.1 在 `Prepare()` 方法中插入 `RegisterComputeResources(graph)` 调用（位于 `CollectShaderBindings` 之后、`RegisterCBufferUsageStates` 之前）
- [x] 5.2 移除 `CollectResources()` 中 compute pass 部分的 `// TODO` 空壳代码（行 737-745）

## 6. 验证与对比

- [x] 6.1 对照 D3D12 `GPUGraphExecutor.cpp:480-505` 确认 compute 资源的 access/stage/layout 映射覆盖所有 binding 类型
- [x] 6.2 确认 `RegisterCBufferUsageStates` 中已有的 compute CBuffer 注册不与新增代码重复
- [x] 6.3 确认 `BuildResources` 的 fallback 注册与本步骤的状态设置没有冲突（两个独立维度：生命周期 vs 同步状态）
