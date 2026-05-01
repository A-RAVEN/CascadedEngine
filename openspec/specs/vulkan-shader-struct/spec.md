## Requirements

### Requirement: VulkanShaderStruct SHALL implement version control

VulkanShaderStruct SHALL实现版本控制机制，跟踪数据变更以支持增量更新。

#### Scenario: 初始版本为0
- **WHEN** VulkanShaderStruct刚被创建
- **THEN** m_Version为0

#### Scenario: 值更新递增版本
- **WHEN** 调用SetValueInternal设置值
- **THEN** m_Version递增

#### Scenario: 计算最大子版本
- **WHEN** 调用ComputeMaxChildrenVersion()
- **THEN** 返回自身版本和所有子结构体版本的最大值

---

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

---

### Requirement: VulkanShaderStruct SHALL implement SetValueInternal

VulkanShaderStruct SHALL实现SetValueInternal方法，支持设置uniform值。

#### Scenario: 设置基本类型值
- **WHEN** 调用SetValueInternal设置float/int等基本类型
- **THEN** 值被正确写入staging buffer

#### Scenario: 设置数组元素
- **WHEN** 调用SetValueInternal设置数组的第N个元素
- **THEN** 值被写入正确的数组元素偏移

#### Scenario: 设置不存在的成员
- **WHEN** 调用SetValueInternal设置不存在的成员名
- **THEN** 系统记录警告日志
- **AND** 不产生崩溃

---

### Requirement: VulkanShaderStruct SHALL implement SetImageInternal

VulkanShaderStruct SHALL实现SetImageInternal方法，支持绑定纹理资源。

#### Scenario: 绑定纹理
- **WHEN** 调用SetImageInternal(name, imageHandle, view, index)
- **THEN** imageHandle和view被存储到m_NameToImageHandles

#### Scenario: 绑定纹理数组元素
- **WHEN** 调用SetImageInternal设置纹理数组的第N个元素
- **THEN** 正确存储到数组对应位置

---

### Requirement: VulkanShaderStruct SHALL implement SetBufferInternal

VulkanShaderStruct SHALL实现SetBufferInternal方法，支持绑定buffer资源。

#### Scenario: 绑定buffer
- **WHEN** 调用SetBufferInternal(name, bufferHandle, index)
- **THEN** bufferHandle被存储到m_NameToBufferHandles

---

### Requirement: VulkanShaderStruct SHALL implement SetSamplerInternal

VulkanShaderStruct SHALL实现SetSamplerInternal方法，支持绑定采样器。

#### Scenario: 绑定采样器
- **WHEN** 调用SetSamplerInternal(name, samplerDesc, index)
- **THEN** samplerDesc被存储到m_NameToSamplerDescriptors

---

### Requirement: VulkanShaderStruct SHALL implement SetStructInternal

VulkanShaderStruct SHALL实现SetStructInternal方法，支持嵌套结构体。

#### Scenario: 绑定子结构体
- **WHEN** 调用SetStructInternal(name, subStruct, index)
- **THEN** subStruct被存储到m_NameToSubStructs

#### Scenario: 子结构体版本影响父版本
- **WHEN** 子结构体的版本更新
- **THEN** 父结构体的ComputeMaxChildrenVersion()返回更大的值

---

### Requirement: VulkanShaderStruct SHALL provide uniform buffer update

VulkanShaderStruct SHALL提供UpdateUniformBuffer方法，支持将staging数据写入GPU buffer。

#### Scenario: 更新uniform buffer
- **WHEN** 调用UpdateUniformBuffer(version, pBuffer, bufferSize, offset)
- **THEN** staging buffer数据被写入目标buffer
- **AND** 只写入version更新的部分

---

### Requirement: VulkanShaderStruct SHALL provide resource accessors

VulkanShaderStruct SHALL提供访问器方法，获取绑定的资源信息。

#### Scenario: 获取图像句柄
- **WHEN** 调用GetImageHandles()
- **THEN** 返回m_NameToImageHandles的const引用

#### Scenario: 获取buffer句柄
- **WHEN** 调用GetBufferHandles()
- **THEN** 返回m_NameToBufferHandles的const引用

#### Scenario: 获取采样器描述符
- **WHEN** 调用GetSamplerDescriptors()
- **THEN** 返回m_NameToSamplerDescriptors的const引用

#### Scenario: 获取子结构体
- **WHEN** 调用GetSubStructs()
- **THEN** 返回m_NameToSubStructs的const引用

---

### Requirement: VulkanShaderStruct SHALL支持通过CBuffer初始化barrier上传数据

VulkanShaderStruct SHALL配合VulkanGraphExecutor的CBuffer初始化流程，通过staging buffer将uniform数据上传到GPU。

#### Scenario: GraphExecutor通过UpdateUniformBuffer写入staging数据
- **WHEN** CBuffer初始化流程执行staging上传
- **THEN** GraphExecutor创建staging buffer
- **AND** 调用VulkanShaderStruct::ComputeMaxChildrenVersion()获取最新版本
- **AND** 调用VulkanShaderStruct::UpdateUniformBuffer(0, mappedPtr, bufferSize, 0)写入全量数据
- **AND** staging buffer通过vkCmdCopyBuffer拷贝到目标GPU buffer
