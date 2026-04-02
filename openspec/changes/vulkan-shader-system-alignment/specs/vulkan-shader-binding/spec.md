## ADDED Requirements

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
