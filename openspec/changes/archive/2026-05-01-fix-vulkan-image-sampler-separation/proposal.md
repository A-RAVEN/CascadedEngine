## Why

Vulkan后端的`SetSampledImage`在写入descriptor时使用了`eCombinedImageSampler`类型，但DescriptorSetLayout在reflection阶段声明的是`eSampledImage`类型，DescriptorPool创建时也使用了`eCombinedImageSampler`，三处不一致会导致Vulkan validation layer报错。此外，HLSL/Slang编译到SPIR-V时始终生成分离的`OpTypeImage`+`OpTypeSampler`（而非`OpTypeSampledImage`），当前实现在ImageBinding路径中错误地将image和临时sampler合并写入，违背了分开管理的设计原则。

## What Changes

- **修复`SetSampledImage`**: 移除多余的`vk::Sampler`参数，descriptor写入改用`vk::DescriptorType::eSampledImage`，与DescriptorSetLayout声明保持一致
- **修复`BuildDescriptors`**: 移除ImageBinding路径中临时创建默认sampler的逻辑，sampler应由独立的SamplerBinding管理
- **修复DescriptorPool**: 将image descriptor的池化计数从`eCombinedImageSampler`改为`eSampledImage`

## Capabilities

### Modified Capabilities
- `vulkan-shader-binding`: 修正Texture资源类型到Vulkan descriptor类型的运行时映射 — 写入侧从`eCombinedImageSampler`改为`eSampledImage`，确保与DescriptorSetLayout声明一致

## Impact

- `VulkanResourceBindingInstance` — `SetSampledImage`签名变更（**BREAKING**: 移除sampler参数）
- `VulkanResourceBindingInstance::BuildDescriptors` — 移除image binding内的临时sampler创建
- `VulkanGraphExecutor` — DescriptorPool size计数从`eCombinedImageSampler`改为`eSampledImage`
- 调用`SetSampledImage`的代码需要更新（当前仅在`BuildDescriptors`内部调用）
