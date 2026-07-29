## ADDED Requirements

### Requirement: 描述符写入指针稳定性
`VulkanResourceBindingInstance::BuildDescriptors` SHALL 在写入循环前对 `m_BufferInfos` 和 `m_ImageInfos` 显式调用 `reserve()`，防止 vector 重分配使 `WriteDescriptorSet::pBufferInfo`/`pImageInfo` 指针失效。

#### Scenario: 多次 SetUniformBuffer 不导致悬垂指针
- **WHEN** `BuildDescriptors` 处理多个 CBuffer 绑定点
- **THEN** `m_BufferInfos` 预分配足够容量，push_back 不会重分配

#### Scenario: 多次 SetSampledImage 不导致悬垂指针
- **WHEN** `BuildDescriptors` 处理多个纹理绑定点
- **THEN** `m_ImageInfos` 预分配足够容量，push_back 不会重分配

### Requirement: Sampler maxLod 默认为 VK_LOD_CLAMP_NONE
`MakeSamplerCreateInfo` SHALL 将 `vk::SamplerCreateInfo::maxLod` 设为 `VK_LOD_CLAMP_NONE`。

#### Scenario: 默认 sampler 可使用全 mip chain
- **WHEN** 用户未指定 maxLod
- **THEN** 创建的 VkSampler 具有 maxLod=VK_LOD_CLAMP_NONE

### Requirement: Sampler borderColor 映射
`MakeSamplerCreateInfo` SHALL 将 `TextureSamplerDescriptor::boarderColor` 映射到 `vk::SamplerCreateInfo::borderColor`。

#### Scenario: 不透明黑色 border
- **WHEN** `TextureSamplerDescriptor` 设置 boarderColor=eOpaqueBlack
- **THEN** 创建的 VkSampler 具有 borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK

### Requirement: GetOrCreateSampler 异常安全
`GetOrCreateSampler` SHALL 捕获 `createSampler` 异常，失败时返回 `VK_NULL_HANDLE`。

#### Scenario: Sampler 创建失败返回空
- **WHEN** `createSampler` 抛出异常
- **THEN** 系统捕获异常，记录错误，返回 VK_NULL_HANDLE

### Requirement: ShaderLibrary 指针稳定性
`ShaderLibrary` 中返回裸指针的 getter（`FindShaderEntry` 等）SHALL 确保返回的指针在调用者使用期间保持有效。

#### Scenario: 并发访问不使指针失效
- **WHEN** 调用者持有 `FindShaderEntry` 返回的指针，同时其他操作修改底层容器
- **THEN** 已返回的指针仍然有效（通过 immutable 缓存或 reference counting）

### Requirement: RecordComputePass 空管线保护
`RecordComputePass` SHALL 在 `dispatchData.pipeline` 为 null 时跳过整个 dispatch（包括描述符绑定和 dispatch call）。

#### Scenario: 空 pipeline 时跳过 dispatch
- **WHEN** `dispatchData.pipeline` 为 VK_NULL_HANDLE
- **THEN** 系统不调用 bindPipeline、bindDescriptorSets、或 dispatch
