## Why

VulkanGraphExecutor 已能创建 vk::Pipeline（通过 ShaderModule + PipelineLayout），但 Pipeline 无法接收任何资源数据——没有 DescriptorSet 分配和写入，没有 CBuffer 上传到 GPU。这导致所有 draw call 的 shader 只能操作空/未初始化的数据，渲染结果无意义。这是当前渲染管线的唯一断裂点，打通后即可产生第一帧有实际内容的渲染。

## What Changes

- 实现 `VulkanResourceBindingInstance::BuildResources()`：从 VulkanShaderStruct 收集 buffer/image/sampler 资源信息，为后续 Descriptor 写入准备数据
- 实现 `VulkanResourceBindingInstance::BuildDescriptors()`：从 DescriptorPool 分配 DescriptorSet，通过 `vk::UpdateDescriptorSets` 写入 uniform buffer、sampled image、sampler 等
- 实现 CBuffer 初始化流程：在 `PrepareBatchResourceBarriers()` 中，为每个 VulkanShaderStruct 创建 staging buffer、拷贝 uniform 数据到 GPU buffer、设置 buffer barrier 确保上传完成
- 创建全局 `vk::DescriptorPool`：在 VulkanGraphExecutor 初始化时创建，供所有 DescriptorSet 分配使用

## Capabilities

### New Capabilities
- `vulkan-descriptor-binding`: Vulkan DescriptorSet 分配与写入，从 VulkanShaderResourceBindingInfo 构建 DescriptorSetLayout，分配 DescriptorSet 并写入资源绑定

### Modified Capabilities
- `vulkan-shader-struct`: 新增 CBuffer 上传流程——通过 staging buffer 将 uniform 数据拷贝到 GPU buffer，供 DescriptorSet 引用

## Non-goals

- 不实现帧重叠/多帧资源管理（属于 Phase 2 GPUFrameManager）
- 不实现 LinearMemoryManager（staging buffer 暂时独立创建，后续优化）
- 不实现跨队列同步（Queue Family ownership transfer）
- 不实现 SamplerManager（sampler 暂时在 DescriptorSet 中内联创建）

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/.cpp` — BuildResources、BuildDescriptors 完整实现
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/.cpp` — CBuffer 初始化、DescriptorPool 创建、BuildResources/BuildDescriptors 调用串联
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.h/.cpp` — CBuffer GPU buffer 创建与 staging 数据上传
- 依赖已有 `vulkan-shader-binding` spec (VulkanShaderResourceBindingInfo)、`vulkan-shader-struct` spec (uniform staging)、`vulkan-pipeline-layout` spec (DescriptorSetLayout 缓存)
