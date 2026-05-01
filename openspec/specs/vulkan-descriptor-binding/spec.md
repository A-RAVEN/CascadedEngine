## Requirements

### Requirement: VulkanResourceBindingInstance SHALL实现BuildResources

VulkanResourceBindingInstance SHALL实现BuildResources方法，将绑定的image/buffer/CBuffer资源注册到VulkanGraphLocalResourceManager，并为CBuffer分配GPU buffer。

#### Scenario: BuildResources接收GPUGraph
- **WHEN** 调用BuildResources
- **THEN** 方法签名为`void BuildResources(VulkanGraphLocalResourceManager&, GPUGraph const&)`
- **AND** GPUGraph用于获取ImageHandle/BufferHandle的descriptor（区分External/Internal/Backbuffer）

#### Scenario: 注册Image资源
- **WHEN** 调用BuildResources且ImageBindingElement包含有效ImageHandle
- **THEN** 若LocalResourceManager尚未识别该ImageHandle（GetTextureView返回空），则通过GetDescriptor(graph, imageHandle)获取descriptor并调用RegisterTemporaryTexture+RegisterTextureHandle注册

#### Scenario: 注册Buffer资源
- **WHEN** 调用BuildResources且BufferBindingElement包含有效BufferHandle
- **THEN** 若LocalResourceManager尚未识别该BufferHandle（GetBuffer返回空），则通过GetDescriptor(graph, bufferHandle)获取descriptor并调用RegisterTemporaryBuffer+RegisterBufferHandle注册

#### Scenario: 为CBuffer分配GPU buffer
- **WHEN** 调用BuildResources且CBufferBindingElement包含有效pCBufferStruct
- **THEN** 从pCBufferStruct获取CBufferSize，对齐到256字节
- **AND** 调用LocalResourceManager.AddBuffer(desc, usage, batchIndex)分配GPU buffer（AddBuffer为本次新增方法，内部调用RegisterTemporaryBuffer并返回uint64_t resourceId）
- **AND** 将返回的uint64_t resourceId存入CBufferBindingElement::gpuBufferResourceId
- **AND** 将该pair插入VulkanGraphExecutor::m_CBufferResourceIdMap

#### Scenario: 填充usingStages
- **WHEN** 调用BuildResources处理每个binding element
- **THEN** 从pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask)获取stage flags
- **AND** 设置binding element的usingStages字段

### Requirement: VulkanResourceBindingInstance SHALL实现BuildDescriptors

VulkanResourceBindingInstance SHALL实现BuildDescriptors方法，从DescriptorPool分配DescriptorSet并写入资源绑定信息。

#### Scenario: BuildDescriptors接收VulkanGraphExecutor
- **WHEN** 调用BuildDescriptors
- **THEN** 方法签名为`void BuildDescriptors(VulkanGraphExecutor&, vk::DescriptorPool)`
- **AND** executor用于访问DescriptorSetLayout缓存、LocalResourceManager（获取vk::Buffer/vk::ImageView）

#### Scenario: 分配DescriptorSet
- **WHEN** 调用BuildDescriptors且DescriptorPool有效
- **THEN** 遍历pShaderFileInfo->shaderBindingInfo.setLayoutInfos，按setIndex排序
- **AND** 对每个set通过executor的cache查找已缓存的DescriptorSetLayout
- **AND** 调用AllocateDescriptorSets从pool分配所有set的DescriptorSet
- **AND** AllocateDescriptorSets内部按shader的setIndex（而非数组索引）作为key存入m_DescriptorSets

#### Scenario: 写入CBuffer描述符
- **WHEN** CBufferBindingElement有有效的gpuBufferResourceId
- **THEN** 通过executor.GetLocalResourceManager().GetBuffer(resourceId)获取vk::Buffer
- **AND** 创建vk::DescriptorBufferInfo（offset=0, range=GetCBufferSize()）
- **AND** 调用SetUniformBuffer写入vk::WriteDescriptorSet

#### Scenario: 写入Image描述符
- **WHEN** ImageBindingElement有有效的ImageHandle
- **THEN** 通过executor.GetLocalResourceManager().GetTextureView(imageHandle)获取vk::ImageView
- **AND** 调用SetSampledImage或SetStorageImage写入vk::WriteDescriptorSet

#### Scenario: 写入Buffer描述符
- **WHEN** BufferBindingElement有有效的BufferHandle
- **THEN** 通过executor.GetLocalResourceManager().GetBuffer(bufferHandle)获取vk::Buffer
- **AND** 调用SetStorageBuffer写入vk::WriteDescriptorSet

#### Scenario: 写入Sampler描述符
- **WHEN** SamplerBindingElement有有效的samplerDescriptor
- **THEN** 创建vk::Sampler（从TextureSamplerDescriptor转换）
- **AND** 调用SetSampler写入vk::WriteDescriptorSet

#### Scenario: 提交Descriptor写入
- **WHEN** 所有binding element处理完毕
- **THEN** 调用UpdateDescriptorSets提交所有m_PendingWrites
- **AND** m_DescriptorSets按set index填充完毕

### Requirement: VulkanGraphExecutor SHALL创建DescriptorPool

VulkanGraphExecutor SHALL在每帧Prepare阶段创建vk::DescriptorPool，容量覆盖所有BindingInstance的descriptor需求。

#### Scenario: 统计descriptor需求并创建Pool
- **WHEN** CompileAndExecute进入Prepare阶段
- **THEN** 遍历所有VulkanResourceBindingInstance统计各类descriptor的数量总和
- **AND** 使用统计结果创建vk::DescriptorPool（包含VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT）
- **AND** 存入m_DescriptorPool

#### Scenario: 无BindingInstance时跳过Pool创建
- **WHEN** 无任何VulkanResourceBindingInstance存在
- **THEN** m_DescriptorPool保持nullptr
- **AND** BuildDescriptors不执行

### Requirement: VulkanGraphExecutor SHALL串联BuildResources和BuildDescriptors调用

VulkanGraphExecutor SHALL在正确的阶段调用BuildResources和BuildDescriptors，确保资源注册、Pipeline创建、Descriptor绑定的正确执行顺序。

#### Scenario: BuildResources在PrepareBatchResourceBarriers之前调用
- **WHEN** AllocateAliasedResources完成且PrepareBatchResourceBarriers尚未执行
- **THEN** 遍历所有m_ShaderResourceInstances调用BuildResources
- **AND** 传入VulkanGraphLocalResourceManager引用和GPUGraph引用

#### Scenario: BuildDescriptors在BuildPipelineStates之后调用
- **WHEN** BuildPipelineStates完成（DescriptorSetLayout已缓存）
- **THEN** 遍历所有m_ShaderResourceInstances调用BuildDescriptors
- **AND** 传入VulkanGraphExecutor自身引用和m_DescriptorPool

### Requirement: VulkanGraphExecutor SHALL实现CBuffer初始化与上传

VulkanGraphExecutor SHALL在PrepareBatchResourceBarriers中为每个CBuffer生成初始化数据，并在RecordBatchCommands中执行staging上传。

#### Scenario: 填充VulkanCBufferInitializeBarriers
- **WHEN** PrepareBatchResourceBarriers处理m_CBufferLifetimes
- **THEN** 通过m_CBufferResourceIdMap查找每个VulkanShaderStruct对应的uint64_t resourceId
- **AND** 将pair<uint64_t, VulkanShaderStruct const*>添加到对应batch的cbufferBarriers

#### Scenario: 执行CBuffer staging上传
- **WHEN** RecordBatchCommands处理有cbufferBarriers的batch
- **THEN** 为每个CBuffer创建staging buffer
- **AND** Map staging buffer，调用VulkanShaderStruct::UpdateUniformBuffer写入数据，Unmap
- **AND** 执行vkCmdCopyBuffer从staging拷贝到GPU buffer
- **AND** 执行pipeline barrier：transfer-write → uniform-read
- **AND** 将staging buffer添加到m_PendingStagingBuffers追踪

#### Scenario: 在CollectResources中注册CBuffer使用状态
- **WHEN** CollectResources遍历raster/compute pass的shader binding
- **THEN** 对每个VulkanShaderStruct调用SetCBufferUsageState
- **AND** 传入对应的pipeline stage flags和queue type

### Requirement: VulkanGraphExecutor SHALL绑定所有DescriptorSet

VulkanGraphExecutor SHALL在RecordRenderPass和RecordComputePass中绑定所有已分配的DescriptorSet，而非仅绑定set 0。

#### Scenario: 光栅化pass绑定所有DescriptorSet
- **WHEN** RecordRenderPass处理有pResourceBindingInstance的draw call batch
- **THEN** 遍历m_DescriptorSets按set index排序，将连续的set聚合成批次
- **AND** 对每个连续区间调用cmdBuf.bindDescriptorSets（firstSet为该区间最小setIndex，sets为对应DescriptorSet数组）

#### Scenario: 计算pass绑定所有DescriptorSet
- **WHEN** RecordComputePass处理有pResourceBindingInstance的dispatch
- **THEN** 遍历m_DescriptorSets按set index排序，将连续的set聚合成批次
- **AND** 对每个连续区间调用cmdBuf.bindDescriptorSets（firstSet为该区间最小setIndex，sets为对应DescriptorSet数组）
