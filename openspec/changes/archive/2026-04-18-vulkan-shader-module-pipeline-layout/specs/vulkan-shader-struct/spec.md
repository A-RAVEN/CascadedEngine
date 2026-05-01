## MODIFIED Requirements

### Requirement: VulkanShaderStruct SHALL maintain uniform buffer staging

VulkanShaderStruct SHALL维护uniform buffer staging区域，支持批量上传到GPU。Init方法不再创建任何Vulkan descriptor/pipeline对象，这些对象延迟到BuildResources阶段从GraphExecutor获取。

#### Scenario: Staging buffer大小正确
- **WHEN** VulkanShaderStruct初始化完成
- **THEN** m_StructLocalUniformStagingBuffer大小等于struct的totalSize

#### Scenario: SetValueInternal写入staging buffer
- **WHEN** 调用SetValueInternal(name, value, size, index)
- **THEN** 值被写入staging buffer的正确偏移位置

#### Scenario: Init不再创建任何Vulkan descriptor/pipeline对象
- **WHEN** 调用VulkanShaderStruct::Init
- **THEN** 不创建vk::DescriptorSetLayout、vk::PipelineLayout、vk::DescriptorPool、vk::DescriptorSet
- **AND** m_DescriptorSetLayout为VK_NULL_HANDLE
- **AND** m_PipelineLayout为VK_NULL_HANDLE
- **AND** m_DescriptorPool为VK_NULL_HANDLE
- **AND** m_DescriptorSet为VK_NULL_HANDLE

#### Scenario: VulkanShaderStruct的descriptor功能在本change后不可用
- **WHEN** 本change完成后VulkanShaderStruct被用于渲染
- **THEN** VulkanShaderStruct不持有任何descriptor set或descriptor pool
- **AND** descriptor set的创建和资源绑定将在下一个change（BuildResources/BuildDescriptors）中实现
- **AND** 在BuildResources实现之前，VulkanShaderStruct的descriptor相关成员保持VK_NULL_HANDLE
