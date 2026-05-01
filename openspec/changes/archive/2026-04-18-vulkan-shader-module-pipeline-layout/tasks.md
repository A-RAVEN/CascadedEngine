## 1. ShaderModule 创建与缓存

- [x] 1.1 将m_ShaderModuleCache的key类型从size_t改为cahash::sha256_hash::result_type，为result_type提供std::hash特化或等价hash方案
- [x] 1.2 修改GetOrCreateShaderModule签名为`vk::ShaderModule GetOrCreateShaderModule(cahash::sha256_hash::result_type const& programHash)`，移除原来的ShaderInfo参数
- [x] 1.3 实现GetOrCreateShaderModule：用programHash查询m_ShaderModuleCache，命中则返回；未命中则调用ShaderLibrary::GetShaderCode(programHash)获取VulkanShaderCode，使用spirvCode通过vk::ShaderModuleCreateInfo创建vk::ShaderModule，缓存并返回
- [x] 1.4 添加错误处理：GetShaderCode返回nullptr时记录错误日志并返回vk::ShaderModule(nullptr)；vk::createShaderModule失败时不缓存nullptr，允许重试
- [x] 1.5 扩展CleanupCaches：销毁所有缓存的vk::ShaderModule并清空m_ShaderModuleCache

## 2. DescriptorSetLayout 构建

- [x] 2.1 修改GetOrCreatePipelineLayout签名为`vk::PipelineLayout GetOrCreatePipelineLayout(VulkanShaderResourceBindingInfo const& bindingInfo)`，移除原来的ShaderReflectionData参数
- [x] 2.2 实现缓存key生成：基于VulkanShaderResourceBindingInfo的setLayoutInfos生成唯一hash，hash输入包含每个set的setIndex及所有binding的descriptorType、binding、descriptorCount、stageFlags，用于DescriptorSetLayout和PipelineLayout缓存查找
- [x] 2.3 实现GetOrCreatePipelineLayout中的DescriptorSetLayout创建：遍历setLayoutInfos，对每个set调用VulkanDescriptorSetLayoutInfo::GetCreateInfo()获取vk::DescriptorSetLayoutCreateInfo，再调用vk::createDescriptorSetLayout，缓存到m_DescriptorSetLayoutCache
- [x] 2.4 处理空set：setLayoutInfos中bindings为空的set仍创建空DescriptorSetLayout（0 bindings），以保持PipelineLayout中setIndex连续性，防止shader中layout(set=N)绑定错位

## 3. PipelineLayout 构建

- [x] 3.1 实现PipelineLayout创建：使用已创建的DescriptorSetLayout集合（按setIndex排序，包含空set的空layout），通过vk::PipelineLayoutCreateInfo组装vk::PipelineLayout，缓存到m_PipelineLayoutCache
- [x] 3.2 扩展CleanupCaches：销毁所有缓存的vk::PipelineLayout和vk::DescriptorSetLayout，清空m_PipelineLayoutCache和m_DescriptorSetLayoutCache

## 4. BuildPipelineStates 串联

- [x] 4.1 在BuildPipelineStates中，为每个pass的shader通过ShaderInfo查询ShaderLibrary获取VulkanShaderFileInfo，从中提取entryPointToShaderProgram的programHash和shaderBindingInfo
- [x] 4.2 光栅化pass：使用programHash调用GetOrCreateShaderModule获取顶点/片段ShaderModule，构建vk::PipelineShaderStageCreateInfo（包含正确的entryPointName）
- [x] 4.3 光栅化pass：使用shaderBindingInfo调用GetOrCreatePipelineLayout获取PipelineLayout，传递给VulkanPipelineLibrary::LinkPipeline或CreateMonolithicPipeline
- [x] 4.4 计算pass：使用programHash调用GetOrCreateShaderModule获取计算ShaderModule，构建shader stage
- [x] 4.5 计算pass：使用shaderBindingInfo调用GetOrCreatePipelineLayout获取PipelineLayout，传递给vk::ComputePipelineCreateInfo

## 5. VulkanShaderStruct Init 修正

- [x] 5.1 移除VulkanShaderStruct::Init中空descriptor set layout / pipeline layout / descriptor pool / descriptor set的创建代码（注意：descriptor set和descriptor pool也在移除范围内，不只是layout）
- [x] 5.2 确认m_DescriptorSetLayout、m_PipelineLayout、m_DescriptorPool、m_DescriptorSet初始为VK_NULL_HANDLE
- [x] 5.3 验证VulkanShaderStruct的所有调用点不依赖Init中创建的这些空对象
- [x] 5.4 在代码中添加注释说明descriptor对象的创建延迟到BuildResources阶段，属于下一个change
