## Requirements

### Requirement: System SHALL construct VulkanShaderResourceBindingInfo from reflection data

系统SHALL提供从ShaderReflectionData构建VulkanShaderResourceBindingInfo的能力，用于创建descriptor set布局。

#### Scenario: 构建基本binding info
- **WHEN** 调用ConstructShaderDescriptorInfo(reflectionData)
- **THEN** 系统返回VulkanShaderResourceBindingInfo
- **AND** 包含所有cbuffer/image/buffer/sampler的绑定信息
- **AND** 包含正确的descriptor set和binding索引

#### Scenario: 处理嵌套结构体
- **WHEN** shader包含嵌套的结构体绑定
- **THEN** 系统正确解析结构体层次
- **AND** 为每个层次生成正确的StructBindingInfo

---

### Requirement: VulkanShaderResourceBindingInfo SHALL track descriptor counts

VulkanShaderResourceBindingInfo SHALL跟踪所需的descriptor总数，用于分配descriptor pool。

#### Scenario: 获取descriptor数量
- **WHEN** 查询VulkanShaderResourceBindingInfo的descriptorCount
- **THEN** 返回所有类型的descriptor总数
- **AND** 分别提供samplerDescriptorCount

---

### Requirement: VulkanShaderResourceBindingInfo SHALL provide descriptor set layout bindings

系统SHALL为每个descriptor set提供vk::DescriptorSetLayoutBinding数组，用于创建descriptor set layout。

#### Scenario: 获取指定set的layout bindings
- **WHEN** 调用GetDescriptorSetLayoutBindings(setIndex)
- **THEN** 返回该set的所有vk::DescriptorSetLayoutBinding
- **AND** binding顺序正确

#### Scenario: 空descriptor set处理
- **WHEN** 查询没有绑定的descriptor set
- **THEN** 返回空数组

---

### Requirement: System SHALL map ShaderCompilerSlang resource types to Vulkan descriptor types

系统SHALL正确映射ShaderCompilerSlang::EShaderResourceType到vk::DescriptorType。

#### Scenario: Texture映射
- **WHEN** 资源类型为eTexture
- **THEN** 映射到vk::DescriptorType::eSampledImage

#### Scenario: RWTexture映射
- **WHEN** 资源类型为eRWTexture
- **THEN** 映射到vk::DescriptorType::eStorageImage

#### Scenario: StructuredBuffer映射
- **WHEN** 资源类型为eStructuredBuffer
- **THEN** 映射到vk::DescriptorType::eStorageBuffer

#### Scenario: Sampler映射
- **WHEN** 资源类型为eSampler
- **THEN** 映射到vk::DescriptorType::eSampler

#### Scenario: CBuffer映射
- **WHEN** 资源类型为eCBuffer
- **THEN** 映射到vk::DescriptorType::eUniformBuffer

---

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

---

### Requirement: Compute shader bindings SHALL expose access type for state registration

`VulkanResourceBindingInstance`的Image和Buffer binding elements SHALL通过`bindingInfo.accessType`字段暴露SLANG shader reflection中的资源访问类型，供`RegisterComputeResources`使用。

#### Scenario: Image binding暴露accessType
- **WHEN** `VulkanResourceBindingInstance::Init`收集compute shader的image binding
- **THEN** `ImageBindingElement::bindingInfo.accessType`包含SLANG reflection的`EShaderResourceAccess`（`eReadOnly` / `eWriteOnly` / `eReadWrite`）

#### Scenario: Buffer binding暴露accessType
- **WHEN** `VulkanResourceBindingInstance::Init`收集compute shader的buffer binding
- **THEN** `BufferBindingElement::bindingInfo.accessType`包含SLANG reflection的`EShaderResourceAccess`

### Requirement: Compute shader resource binding SHALL be staged with correct pipeline stage

`VulkanResourceBindingInstance::BuildDescriptors` SHALL为compute shader的storage image和storage buffer使用正确的`vk::ImageLayout`（`eGeneral` for UAV, `eShaderReadOnlyOptimal` for SRV），与`RegisterComputeResources`中注册的layout一致。

#### Scenario: Compute storage image descriptor layout
- **WHEN** `BuildDescriptors`处理compute shader的storage image binding
- **THEN** 使用`vk::ImageLayout::eGeneral`，与`RegisterComputeResources`注册的状态一致

#### Scenario: Compute sampled image descriptor layout
- **WHEN** `BuildDescriptors`处理compute shader的sampled image binding
- **THEN** 使用`vk::ImageLayout::eShaderReadOnlyOptimal`，与`RegisterComputeResources`注册的状态一致

---

### Requirement: Vulkan shader binding instance dedup key SHALL include per-pass shader-struct identity

`VulkanResourceBindingInstance` 的去重身份（`VulkanShaderResourceSet`）SHALL 包含 per-pass 的 `VulkanShaderStruct*` 指针（连同 shader path 与各绑定名），使相同 shader path + 相同绑定名但不同 per-pass 常量（struct）的 pass 各自得到独立绑定实例及其常量绑定，而非被合并到首个实例。

#### Scenario: 同 shader 同绑定名、不同 per-pass struct 的 raster pass 各自绑定
- **WHEN** 两个 raster pass 使用相同 shader path 与相同绑定名（如 "imguiCommon"/"imguiTextureBinding"），但各自携带不同的 per-pass `VulkanShaderStruct*`（其常量内容不同）
- **THEN** 两个 pass 得到不同的 `VulkanShaderResourceSet` 去重键（hash/等值不同）
- **AND** `createOrGetBindingInstance` 为每个 pass 返回独立的 `VulkanResourceBindingInstance`
- **AND** 每个实例各自绑定对应 per-pass 的常量（uniform buffer），而非绑定首个 pass 的常量

#### Scenario: IMGUI 多窗口副窗不再偏移
- **WHEN** IMGUI 多窗口（主窗 + 副窗）以不同 `IMGUIScale_Pos`（不同 viewport pos）渲染同一 "Shaders/Imgui"
- **THEN** 每个 viewport 的 pass 绑定各自的 imguiCommon 常量（`.zw` = 各自 viewport pos）
- **AND** 副窗回读 PNG 内容不再偏移 `(副窗pos − 主窗pos)`，与 D3D12 一致

#### Scenario: compute dispatch 同 shader 同绑定名、不同 per-dispatch struct 各自绑定
- **WHEN** 两个 compute dispatch 使用相同 shader path 与绑定名，但各自携带不同的 per-dispatch `VulkanShaderStruct*`
- **THEN** 两个 dispatch 各自绑定各自的 cbuffer/UAV
- **AND** 首个 dispatch 的常量不泄漏到第二个 dispatch

#### Scenario: 同 shader 同绑定名、不同 per-pass struct 的 pass 不合并
- **WHEN** 两个 pass 使用相同 shader path 与相同绑定名，但各自携带不同的 per-pass `VulkanShaderStruct*`
- **THEN** 两个 pass 去重后得到**不同**的 `VulkanResourceBindingInstance`（各自绑定各自常量），而非合并到首个
- **AND** 该判定依赖"每 pass struct 指针"参与去重身份（hash/等值），且两个不同 struct 的 pass 绝不因"同 path 同绑定名"被合并
