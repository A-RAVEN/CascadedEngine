## MODIFIED Requirements

### Requirement: VulkanResourceBindingInstance SHALL实现BuildDescriptors

VulkanResourceBindingInstance SHALL实现BuildDescriptors方法，从DescriptorPool分配DescriptorSet并写入资源绑定信息。对绑定到**已正确登记并绑定**的图内（Internal）资源的 Image/Buffer element，SHALL 写入描述符、不得因 Internal 类型未解析而 `continue` 跳过；所引用资源须携带正确 usage 位（buffer: `STORAGE_BUFFER`；图像: `eSampled`）。

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

#### Scenario: 写入Image描述符（内建不跳过）

- **WHEN** ImageBindingElement引用一个已正确登记并绑定的图内 `AllocImage` 或外部图像，`GetTextureView(imageHandle)` 返回有效 ImageView
- **THEN** 调用SetSampledImage或SetStorageImage写入vk::WriteDescriptorSet，**不因 Internal 类型而跳过**（不触发 `if(!imageView) continue;`）
- **AND** 被采样图像的 `VkImage` usage 含 `eSampled`（否则采样绑定报 `08114`）

#### Scenario: 写入Buffer描述符（内建不跳过，含 usage）

- **WHEN** BufferBindingElement引用一个已正确登记并绑定的图内 `AllocBuffer`，`GetBuffer(bufferHandle)` 返回有效 VkBuffer
- **THEN** 调用SetStorageBuffer写入vk::WriteDescriptorSet，**不因 Internal 类型而跳过**（不触发 `if(!vkBuffer) continue;`）
- **AND** 该 `VkBuffer` usage 含 `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`（否则 `eStorageBuffer` 写报 `00331`）

#### Scenario: 写入Sampler描述符

- **WHEN** SamplerBindingElement有有效的samplerDescriptor
- **THEN** 创建vk::Sampler（从TextureSamplerDescriptor转换）
- **AND** 调用SetSampler写入vk::WriteDescriptorSet

#### Scenario: 提交Descriptor写入

- **WHEN** 所有binding element处理完毕
- **THEN** 调用UpdateDescriptorSets提交所有m_PendingWrites
- **AND** m_DescriptorSets按set index填充完毕
