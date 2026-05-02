## ADDED Requirements

### Requirement: Compute shader bindings SHALL expose access type for state registration

`VulkanResourceBindingInstance`的Image和Buffer binding elements SHALL通过`bindingInfo.accessType`字段暴露SLANG shader reflection中的资源访问类型，供`RegisterComputeResources`使用。

#### Scenario: Image binding暴露accessType
- **WHEN** `VulkanResourceBindingInstance::Init`收集compute shader的image binding
- **THEN** `ImageBindingElement::bindingInfo.accessType`包含SLANG reflection的`EShaderResourceAccess`（`eReadOnly` / `eWriteOnly` / `eReadWrite`）

#### Scenario: Buffer binding暴露accessType
- **WHEN** `VulkanResourceBindingInstance::Init`收集compute shader的buffer binding
- **THEN** `BufferBindingElement::bindingInfo.accessType`包含SLANG reflection的`EShaderResourceAccess`

### Requirement: Compute shader resource binding SHALL be staged with correct pipeline stage

`VulkanResourceBindingInstance::BuildDescriptors` SHALL为compute shader的storage image和storage buffer使用正确的`vk::ImageLayout`（`eGeneral` for UAV, `eShaderReadOnlyOptimal` for SRV），与`RegisterComputeResources`中注册的layout一致。

#### Scenario: Compute storage image descriptor layout
- **WHEN** `BuildDescriptors`处理compute shader的storage image binding
- **THEN** 使用`vk::ImageLayout::eGeneral`，与`RegisterComputeResources`注册的状态一致

#### Scenario: Compute sampled image descriptor layout
- **WHEN** `BuildDescriptors`处理compute shader的sampled image binding
- **THEN** 使用`vk::ImageLayout::eShaderReadOnlyOptimal`，与`RegisterComputeResources`注册的状态一致
