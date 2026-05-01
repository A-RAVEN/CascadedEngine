## Requirements

### Requirement: VulkanGraphExecutor SHALL从VulkanShaderResourceBindingInfo构建DescriptorSetLayout

VulkanGraphExecutor SHALL实现GetOrCreatePipelineLayout方法，接受VulkanShaderResourceBindingInfo作为输入，消费其中setLayoutInfos创建vk::DescriptorSetLayout，并缓存以避免重复创建。

#### Scenario: 接口变更 — 接受VulkanShaderResourceBindingInfo
- **WHEN** BuildPipelineStates需要获取PipelineLayout
- **THEN** 从VulkanShaderFileInfo::shaderBindingInfo获取VulkanShaderResourceBindingInfo
- **AND** 调用GetOrCreatePipelineLayout(bindingInfo)而非GetOrCreatePipelineLayout(reflectionData)

#### Scenario: 创建DescriptorSetLayout
- **WHEN** GetOrCreatePipelineLayout处理一个shader组合的bindingInfo
- **THEN** 遍历setLayoutInfos中每个VulkanDescriptorSetLayoutInfo
- **AND** 为每个set调用VulkanDescriptorSetLayoutInfo::GetCreateInfo()获取vk::DescriptorSetLayoutCreateInfo
- **AND** 调用vk::createDescriptorSetLayout创建vk::DescriptorSetLayout
- **AND** 将DescriptorSetLayout缓存到m_DescriptorSetLayoutCache

#### Scenario: 空descriptor set创建空DescriptorSetLayout
- **WHEN** setLayoutInfos中某个set的bindings为空
- **THEN** 仍为该set创建空的DescriptorSetLayout（0 bindings）
- **AND** 该空layout占位在PipelineLayout中保持setIndex连续性
- **AND** 防止shader中layout(set=N)的绑定因跳过空set而错位

#### Scenario: DescriptorSetLayout缓存命中
- **WHEN** 同一bindingInfo已被处理过
- **THEN** 直接从缓存返回已有的DescriptorSetLayout集合

### Requirement: VulkanGraphExecutor SHALL从DescriptorSetLayout构建PipelineLayout

VulkanGraphExecutor SHALL使用已创建的DescriptorSetLayout集合组装vk::PipelineLayout，供图形和计算管线使用。

#### Scenario: 创建PipelineLayout
- **WHEN** 所有DescriptorSetLayout已创建完成
- **THEN** 使用vk::PipelineLayoutCreateInfo组装vk::PipelineLayout
- **AND** setLayouts包含所有DescriptorSetLayout（包括空set的空layout），按setIndex排序
- **AND** 将PipelineLayout缓存到m_PipelineLayoutCache

#### Scenario: PipelineLayout缓存命中
- **WHEN** 同一shader组合的PipelineLayout已被创建
- **THEN** 直接从缓存返回

#### Scenario: 光栅化pass使用PipelineLayout
- **WHEN** BuildPipelineStates处理光栅化pass
- **THEN** 从VulkanShaderFileInfo获取shaderBindingInfo
- **AND** 调用GetOrCreatePipelineLayout(bindingInfo)获取PipelineLayout
- **AND** 将PipelineLayout传递给VulkanPipelineLibrary的LinkPipeline/CreateMonolithicPipeline

#### Scenario: 计算pass使用PipelineLayout
- **WHEN** BuildPipelineStates处理计算pass
- **THEN** 从VulkanShaderFileInfo获取shaderBindingInfo
- **AND** 调用GetOrCreatePipelineLayout(bindingInfo)获取PipelineLayout
- **AND** 将PipelineLayout传递给vk::ComputePipelineCreateInfo

#### Scenario: 缓存清理
- **WHEN** 调用CleanupCaches
- **THEN** 销毁所有缓存的vk::PipelineLayout和vk::DescriptorSetLayout
- **AND** 清空m_PipelineLayoutCache和m_DescriptorSetLayoutCache

### Requirement: PipelineLayout构建SHALL支持缓存key生成

系统SHALL为每个唯一的shader binding组合生成缓存key，用于PipelineLayout和DescriptorSetLayout的缓存查找。

#### Scenario: 缓存key基于binding信息
- **WHEN** 为一组shader的binding info生成缓存key
- **THEN** key包含每个set的setIndex以及该set下所有binding的描述信息（descriptor type、binding number、descriptor count、stage flags）
- **AND** 相同binding组合产生相同key
- **AND** setIndex不同或set内容不同产生不同key

#### Scenario: 缓存key类型
- **WHEN** 使用缓存key查找m_PipelineLayoutCache或m_DescriptorSetLayoutCache
- **THEN** key类型应为基于binding信息计算的size_t hash值
- **AND** hash算法应覆盖所有setLayoutInfos的所有bindings的所有字段及setIndex
