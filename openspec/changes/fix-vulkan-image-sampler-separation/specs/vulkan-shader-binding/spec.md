## ADDED Requirements

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
