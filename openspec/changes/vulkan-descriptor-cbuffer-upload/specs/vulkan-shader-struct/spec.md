## MODIFIED Requirements

### Requirement: VulkanShaderStruct SHALL maintain uniform buffer staging

VulkanShaderStruct SHALL维护uniform buffer staging区域，支持批量上传到GPU。VulkanShaderStruct不再自己持有GPU buffer或descriptor对象——GPU buffer由VulkanGraphLocalResourceManager分配，descriptor由VulkanResourceBindingInstance管理。VulkanShaderStruct仅提供CPU端staging数据和访问接口，供GraphExecutor的上传流程消费。

#### Scenario: Staging buffer大小正确
- **WHEN** VulkanShaderStruct初始化完成
- **THEN** m_StructLocalUniformStagingBuffer大小等于struct的totalSize

#### Scenario: SetValueInternal写入staging buffer
- **WHEN** 调用SetValueInternal(name, value, size, index)
- **THEN** 值被写入staging buffer的正确偏移位置

#### Scenario: VulkanShaderStruct不持有GPU buffer
- **WHEN** VulkanShaderStruct被用于渲染
- **THEN** VulkanShaderStruct不分配或持有GPU buffer
- **AND** GPU buffer由VulkanGraphLocalResourceManager在BuildResources阶段分配
- **AND** uint64_t resourceId存储在VulkanResourceBindingInstance的CBufferBindingElement::gpuBufferResourceId中

#### Scenario: VulkanShaderStruct不持有descriptor对象
- **WHEN** VulkanShaderStruct被用于渲染
- **THEN** m_DescriptorSetLayout、m_PipelineLayout、m_DescriptorPool、m_DescriptorSet保持VK_NULL_HANDLE
- **AND** Descriptor对象由VulkanResourceBindingInstance通过BuildDescriptors管理

#### Scenario: 提供CBufferSize接口
- **WHEN** 调用GetCBufferSize()
- **THEN** 返回`p_StructData->m_StructUniforms.m_MemorySize`向上对齐到256字节后的值
- **AND** 该大小用于GPU buffer分配和DescriptorBufferInfo的range字段

## ADDED Requirements

### Requirement: VulkanShaderStruct SHALL支持通过CBuffer初始化barrier上传数据

VulkanShaderStruct SHALL配合VulkanGraphExecutor的CBuffer初始化流程，通过staging buffer将uniform数据上传到GPU。

#### Scenario: GraphExecutor通过UpdateUniformBuffer写入staging数据
- **WHEN** CBuffer初始化流程执行staging上传
- **THEN** GraphExecutor创建staging buffer
- **AND** 调用VulkanShaderStruct::ComputeMaxChildrenVersion()获取最新版本
- **AND** 调用VulkanShaderStruct::UpdateUniformBuffer(0, mappedPtr, bufferSize, 0)写入全量数据
- **AND** staging buffer通过vkCmdCopyBuffer拷贝到目标GPU buffer
