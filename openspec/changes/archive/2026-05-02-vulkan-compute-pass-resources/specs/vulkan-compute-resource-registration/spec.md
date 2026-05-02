## ADDED Requirements

### Requirement: System SHALL register compute shader image RW states

系统SHALL在Prepare阶段将Compute Shader Pass使用的所有image类型shader资源的读写状态写入`VulkanExecutorRWState`，确保依赖分析和barrier生成能正确处理compute资源。

#### Scenario: 注册sampled image状态
- **WHEN** Compute Shader包含sampled image绑定（如`Texture2D`，`accessType = eReadOnly`）
- **THEN** 系统调用`VulkanExecutorRWState::SetImageRWState()`，参数为：
  - stage: `vk::PipelineStageFlagBits::eComputeShader`
  - access: `vk::AccessFlagBits::eShaderRead`
  - layout: `vk::ImageLayout::eShaderReadOnlyOptimal`
  - queueType: `asyncCompute ? eCompute : eDirect`
- **AND** 本步骤**不调用** `RegisterTemporaryTexture`（资源生命周期由`BuildResources`处理）

#### Scenario: 注册storage image状态（读写）
- **WHEN** Compute Shader包含storage image绑定（如`RWTexture2D`，`accessType = eReadWrite`）
- **THEN** 系统调用`SetImageRWState()`，access为`eShaderRead | eShaderWrite`，layout为`eGeneral`

#### Scenario: 注册storage image状态（只写）
- **WHEN** Compute Shader包含只写storage image绑定（`accessType = eWriteOnly`）
- **THEN** 系统调用`SetImageRWState()`，access为`eShaderWrite`，layout为`eGeneral`

#### Scenario: 处理无绑定资源的compute pass
- **WHEN** Compute Pass不包含任何image shader资源
- **THEN** 系统正常执行，对应`m_ComputePassRWStates[passID].imageRWStates`为空
- **AND** 不产生错误或警告

---

### Requirement: System SHALL register compute shader buffer RW states

系统SHALL在Prepare阶段将Compute Shader Pass使用的所有buffer类型shader资源的读写状态写入`VulkanExecutorRWState`。

#### Scenario: 注册storage buffer状态（只读）
- **WHEN** Compute Shader包含只读structured buffer（`accessType = eReadOnly`）
- **THEN** 系统调用`VulkanExecutorRWState::SetBufferRWState()`，参数为：
  - stage: `vk::PipelineStageFlagBits::eComputeShader`
  - access: `vk::AccessFlagBits::eShaderRead`
  - queueType: `asyncCompute ? eCompute : eDirect`

#### Scenario: 注册storage buffer状态（读写）
- **WHEN** Compute Shader包含读写storage buffer（如`RWStructuredBuffer`，`accessType = eReadWrite`）
- **THEN** 系统调用`SetBufferRWState()`，access为`eShaderRead | eShaderWrite`

#### Scenario: uniform buffer不重复处理
- **WHEN** Compute Shader包含uniform buffer绑定
- **THEN** buffer的CBuffer状态由`RegisterCBufferUsageStates`处理
- **AND** `RegisterComputeResources`不重复处理CBuffer资源

---

### Requirement: System SHALL provide access type to Vulkan flags mapping helpers

系统SHALL提供`EShaderResourceAccess`到Vulkan access flags和image layout的映射helper函数。

#### Scenario: access flags映射
- **WHEN** 调用`ComputeAccessToVulkanAccess(accessType)`
- **THEN** `eReadOnly`返回`eShaderRead`
- **AND** `eWriteOnly`返回`eShaderWrite`
- **AND** `eReadWrite`返回`eShaderRead | eShaderWrite`

#### Scenario: image layout映射
- **WHEN** 调用`ComputeAccessToImageLayout(accessType)`
- **THEN** `eReadOnly`返回`eShaderReadOnlyOptimal`
- **AND** `eWriteOnly`返回`eGeneral`
- **AND** `eReadWrite`返回`eGeneral`

---

### Requirement: RegisterComputeResources SHALL integrate into Prepare phase

系统SHALL在`Prepare`阶段中、`CollectShaderBindings`之后新增`RegisterComputeResources`步骤。

#### Scenario: Prepare执行顺序
- **WHEN** `VulkanGraphExecutor::Prepare`被调用
- **THEN** 按以下顺序执行：InitArraySizes → CollectResources → CollectShaderBindings → **RegisterComputeResources** → RegisterCBufferUsageStates

#### Scenario: binding instances可用性
- **WHEN** `RegisterComputeResources`被执行
- **THEN** 所有Compute Pass的`VulkanResourceBindingInstance`已由`CollectShaderBindings`创建完毕
- **AND** 可通过`dispatchData.pResourceBindingInstance`安全访问
- **AND** `bindingInfo.accessType`已填充，可直接使用

#### Scenario: usingStages不可用
- **WHEN** `RegisterComputeResources`被执行
- **THEN** `ImageBindingElement::usingStages`和`BufferBindingElement::usingStages`尚未填充（在后续`BuildResources`中设置）
- **AND** 本步骤硬编码使用`vk::PipelineStageFlagBits::eComputeShader`

---

### Requirement: Compute resource state registration SHALL align with D3D12 backend behavior

系统SHALL确保Vulkan后端的compute资源状态注册行为与D3D12后端等效。

#### Scenario: 资源覆盖对齐
- **WHEN** 相同的GPUGraph同时用于D3D12和Vulkan后端
- **THEN** Compute Pass中被标记为读写（`eReadWrite`）或只写（`eWriteOnly`）的image和buffer资源在Vulkan中产生等效的barrier依赖

#### Scenario: queue type处理
- **WHEN** Compute Pass标记为async compute（`asyncCompute = true`）
- **THEN** 资源状态注册到`EGPUQueueType::eCompute`队列
- **WHEN** Compute Pass未标记async compute
- **THEN** 资源状态注册到`EGPUQueueType::eDirect`队列
