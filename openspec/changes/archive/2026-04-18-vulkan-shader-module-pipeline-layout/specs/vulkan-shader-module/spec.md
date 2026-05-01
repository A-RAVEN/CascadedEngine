## ADDED Requirements

### Requirement: VulkanGraphExecutor SHALL创建并缓存vk::ShaderModule

VulkanGraphExecutor SHALL实现GetOrCreateShaderModule方法，从ShaderLibrary获取SPIR-V字节码创建vk::ShaderModule，并按SHA-256 hash缓存以避免重复创建。

#### Scenario: 首次创建ShaderModule
- **WHEN** 调用GetOrCreateShaderModule(programHash)且缓存中不存在该hash
- **THEN** 从ShaderLibrary::GetShaderCode(programHash)获取VulkanShaderCode的spirvCode
- **AND** 使用vk::ShaderModuleCreateInfo创建vk::ShaderModule
- **AND** 将ShaderModule存入m_ShaderModuleCache（key为cahash::sha256_hash::result_type）
- **AND** 返回有效的vk::ShaderModule

#### Scenario: 缓存命中ShaderModule
- **WHEN** 调用GetOrCreateShaderModule(programHash)且缓存中已存在该hash
- **THEN** 直接返回缓存的vk::ShaderModule，不重复创建

#### Scenario: ShaderCode不存在
- **WHEN** 调用GetOrCreateShaderModule(programHash)但ShaderLibrary中无对应VulkanShaderCode
- **THEN** 记录错误日志
- **AND** 返回vk::ShaderModule(nullptr)

#### Scenario: ShaderModule创建失败
- **WHEN** vk::createShaderModule抛出异常或返回错误结果
- **THEN** 记录错误日志（包含SPIR-V大小信息）
- **AND** 返回vk::ShaderModule(nullptr)
- **AND** 不缓存nullptr（允许后续重试）

#### Scenario: 缓存清理
- **WHEN** 调用CleanupCaches
- **THEN** 销毁所有缓存的vk::ShaderModule
- **AND** 清空m_ShaderModuleCache

### Requirement: GetOrCreateShaderModule SHALL接受SHA-256 hash作为输入

GetOrCreateShaderModule的签名SHALL改为接受`cahash::sha256_hash::result_type`作为shader标识，而非ShaderInfo。调用方负责从pass数据中查询shader file info，提取programHash。

#### Scenario: 从pass数据获取shader hash
- **WHEN** BuildPipelineStates处理一个pass的shader
- **THEN** 通过ShaderInfo查询ShaderLibrary获取VulkanShaderFileInfo
- **AND** 从VulkanShaderFileInfo::entryPointToShaderProgram获取programHash
- **AND** 使用programHash调用GetOrCreateShaderModule

### Requirement: m_ShaderModuleCache SHALL使用SHA-256 hash作为key类型

m_ShaderModuleCache的key类型SHALL从size_t改为cahash::sha256_hash::result_type，避免256-bit hash截断为64-bit导致的碰撞风险。

#### Scenario: 缓存key类型
- **WHEN** 向m_ShaderModuleCache插入或查找条目
- **THEN** 使用cahash::sha256_hash::result_type作为key
- **AND** 需为result_type提供std::hash特化或使用等价hash方案

### Requirement: ShaderModule SHALL在BuildPipelineStates中被正确使用

BuildPipelineStates SHALL使用GetOrCreateShaderModule获取vk::ShaderModule，构建正确的vk::PipelineShaderStageCreateInfo。

#### Scenario: 顶点着色器stage创建
- **WHEN** BuildPipelineStates处理光栅化pass的顶点着色器
- **THEN** 从VulkanShaderFileInfo获取顶点着色器的programHash
- **AND** 调用GetOrCreateShaderModule(programHash)获取vk::ShaderModule
- **AND** 创建vk::PipelineShaderStageCreateInfo，stage为eVertex
- **AND** pName为着色器入口点名称

#### Scenario: 片段着色器stage创建
- **WHEN** BuildPipelineStates处理光栅化pass的片段着色器
- **THEN** 从VulkanShaderFileInfo获取片段着色器的programHash
- **AND** 调用GetOrCreateShaderModule(programHash)获取vk::ShaderModule
- **AND** 创建vk::PipelineShaderStageCreateInfo，stage为eFragment

#### Scenario: 计算着色器stage创建
- **WHEN** BuildPipelineStates处理计算pass
- **THEN** 从VulkanShaderFileInfo获取计算着色器的programHash
- **AND** 调用GetOrCreateShaderModule(programHash)获取vk::ShaderModule
- **AND** 创建vk::PipelineShaderStageCreateInfo，stage为eCompute
