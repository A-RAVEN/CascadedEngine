## Context

当前Vulkan后端在shader binding的descriptor流程中存在类型不一致问题：

- **Reflection阶段** (`ShaderImporter_Vulkan.cpp:164`): Texture资源正确映射为`vk::DescriptorType::eSampledImage`，Sampler资源映射为`vk::DescriptorType::eSampler`，DescriptorSetLayout声明也是分开的
- **Descriptor写入阶段** (`VulkanResourceBindingInstance.cpp:493`): `SetSampledImage`却使用`vk::DescriptorType::eCombinedImageSampler`写入，并要求传入`vk::Sampler`参数，与DescriptorSetLayout声明不匹配
- **DescriptorPool创建阶段** (`VulkanGraphExecutor.cpp:493`): `poolSizes`中sampled image类型使用了`eCombinedImageSampler`，与DescriptorSetLayout声明的`eSampledImage`不匹配

`eCombinedImageSampler`类型要求imageView和sampler在同一个descriptor binding中，而`eSampledImage`只需要imageView，sampler通过独立的`eSampler` binding提供。与D3D12对齐的设计应当使用后者（分开的image和sampler）。

**SPIR-V语义约束**: SLang/HLSL编译到SPIR-V时总是生成分离的`OpTypeImage` + `OpTypeSampler`声明，而非组合的`OpTypeSampledImage`。因此此代码库中**永远不应使用`eCombinedImageSampler`**——DescriptorSetLayout已正确反映此语义，运行时写入和DescriptorPool需要对齐。

## Goals / Non-Goals

**Goals:**
- 让`SetSampledImage`的descriptor写入类型从`eCombinedImageSampler`改为`eSampledImage`
- 移除`SetSampledImage`多余的sampler参数
- 移除`BuildDescriptors`中ImageBinding路径里临时创建默认sampler的逻辑
- 更新DescriptorPool创建时的类型计数

**Non-Goals:**
- 不改变reflection阶段的类型映射（已经正确）
- 不改变SamplerBinding的独立管理逻辑（已经正确）
- 不引入SamplerManager等新的缓存机制

## Decisions

### 决策: 使用eSampledImage代替eCombinedImageSampler

**理由**: DescriptorSetLayout已经声明为`eSampledImage`，运行时写入必须匹配。此外，image与sampler分离与D3D12模型对齐。SLang编译器始终生成分离的image/sampler声明，`eCombinedImageSampler`在此代码库中没有适用场景。

**替代方案**: 修改reflection让Texture映射到`eCombinedImageSampler`。这会导致与SLang的语义不匹配（SLang中texture和sampler总是分开的），且失去了单独更换sampler的灵活性。

### 涉及变更的函数

| 函数 | 变更 |
|------|------|
| `SetSampledImage` | 移除`vk::Sampler sampler`参数，descriptorType改为`eSampledImage`，`vk::DescriptorImageInfo`不再填充sampler字段 |
| `BuildDescriptors` (Image路径) | 移除`device.createSampler()`调用和对`SetSampledImage`的sampler传参。新增assertion检查对应的SamplerBinding是否已配置 |
| `VulkanGraphExecutor` (DescriptorPool) | `poolSizes`中`eCombinedImageSampler` → `eSampledImage`（行493），变量名`sampledImageCount`无需更改 |

## Risks / Trade-offs

- **当前无外部调用者**: `SetSampledImage`仅在`BuildDescriptors`内部调用，修改签名不影响外部API

- **Sampler未配置时的行为变化**: 修复前，`BuildDescriptors`会偷偷为每个Image创建一个默认`LinearRepeat` sampler并通过`eCombinedImageSampler`写入（尽管类型不匹配）。修复后，sampler descriptor slot由独立的SamplerBinding管理，**若app层未配置对应SamplerBinding，sampler descriptor slot将保持未绑定状态**，根据Vulkan规范读取未定义的descriptor是undefined behavior。缓解措施：在`BuildDescriptors`的Image路径（分支为非UAV时）添加assertion，检查`shaderBindingInfo.samplerInfos`中是否存在与当前set/binding空间相关的sampler binding，若缺少则发出warning提醒开发者配置sampler。

- **SamplerBinding仍独立工作**: 修复后image和sampler的descriptor写入各自独立，shader中`texture.sample(sampler, uv)`的正确绑定依赖app层在ShaderStruct中正确配置sampler descriptor。当前已有的`SamplerBindingElement`路径已正确处理。
