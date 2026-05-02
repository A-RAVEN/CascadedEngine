## Requirements

### Requirement: System SHALL construct VulkanShaderResourceBindingInfo from reflection data

系统SHALL提供从ShaderReflectionData构建VulkanShaderResourceBindingInfo的能力，用于创建descriptor set布局。

#### Scenario: 构建基本binding info
- **WHEN** 调用ConstructShaderDescriptorInfo(reflectionData)
- **THEN** 系统返回VulkanShaderResourceBindingInfo
- **AND** 包含所有cbuffer/image/buffer/sampler的绑定信息
- **AND** 包含正确的descriptor set和binding索引

#### Scenario: 处理嵌套结构体
- **WHEN** shader包含嵌套的结构体绑定
- **THEN** 系统正确解析结构体层次
- **AND** 为每个层次生成正确的StructBindingInfo

---

### Requirement: VulkanShaderResourceBindingInfo SHALL track descriptor counts

VulkanShaderResourceBindingInfo SHALL跟踪所需的descriptor总数，用于分配descriptor pool。

#### Scenario: 获取descriptor数量
- **WHEN** 查询VulkanShaderResourceBindingInfo的descriptorCount
- **THEN** 返回所有类型的descriptor总数
- **AND** 分别提供samplerDescriptorCount

---

### Requirement: VulkanShaderResourceBindingInfo SHALL provide descriptor set layout bindings

系统SHALL为每个descriptor set提供vk::DescriptorSetLayoutBinding数组，用于创建descriptor set layout。

#### Scenario: 获取指定set的layout bindings
- **WHEN** 调用GetDescriptorSetLayoutBindings(setIndex)
- **THEN** 返回该set的所有vk::DescriptorSetLayoutBinding
- **AND** binding顺序正确

#### Scenario: 空descriptor set处理
- **WHEN** 查询没有绑定的descriptor set
- **THEN** 返回空数组

---

### Requirement: System SHALL map ShaderCompilerSlang resource types to Vulkan descriptor types

系统SHALL正确映射ShaderCompilerSlang::EShaderResourceType到vk::DescriptorType。

#### Scenario: Texture映射
- **WHEN** 资源类型为eTexture
- **THEN** 映射到vk::DescriptorType::eSampledImage

#### Scenario: RWTexture映射
- **WHEN** 资源类型为eRWTexture
- **THEN** 映射到vk::DescriptorType::eStorageImage

#### Scenario: StructuredBuffer映射
- **WHEN** 资源类型为eStructuredBuffer
- **THEN** 映射到vk::DescriptorType::eStorageBuffer

#### Scenario: Sampler映射
- **WHEN** 资源类型为eSampler
- **THEN** 映射到vk::DescriptorType::eSampler

#### Scenario: CBuffer映射
- **WHEN** 资源类型为eCBuffer
- **THEN** 映射到vk::DescriptorType::eUniformBuffer

---

### Requirement: Descriptor写入类型SHALL与DescriptorSetLayout声明一致

系统在运行时写入descriptor时SHALL使用与DescriptorSetLayout声明一致的vk::DescriptorType。

#### Scenario: SampledImage写入
- **WHEN** ImageBinding的bindingInfo指示为sampled image（非UAV）
- **THEN** descriptor写入使用vk::DescriptorType::eSampledImage
- **AND** vk::DescriptorImageInfo仅需填充imageView和imageLayout字段
- **AND** 不需要填充sampler字段

#### Scenario: Sampler独立写入
- **WHEN** SamplerBinding需要写入sampler descriptor
- **THEN** descriptor写入使用vk::DescriptorType::eSampler
- **AND** sampler由独立的SamplerBinding路径管理

#### Scenario: Sampler未配置时产生警告
- **WHEN** ImageBinding为sampled image（非UAV）但未找到对应的SamplerBinding
- **THEN** 系统通过CA_LOG_WARN发出警告，提示开发者需要配置sampler
- **AND** image descriptor仍然正常写入（sampler slot保持未绑定状态）

#### Scenario: DescriptorPool计数一致
- **WHEN** 创建VkDescriptorPool
- **THEN** SampledImage descriptor的pool size使用VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
- **AND** 不包含VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER

---

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
