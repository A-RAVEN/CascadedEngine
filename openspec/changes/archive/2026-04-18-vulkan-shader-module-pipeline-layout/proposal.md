## Why

VulkanRenderBackendNew 无法渲染任何帧。根本原因是 `GetOrCreateShaderModule()` 返回 `nullptr`，导致整条渲染管线断裂：没有 shader module → shader stages 为空 → pipeline 无法创建 → 无绘制命令可执行。同时，`GetOrCreatePipelineLayout()` 虽然已实现，但没有消费已有的 `VulkanShaderResourceBindingInfo` 来构建正确的 `vk::DescriptorSetLayout`，导致 pipeline layout 也是无效的。这两个模块是渲染管线的咽喉要道，不打通它们，后续所有功能（CBuffer 上传、Descriptor 绑定、BuildPipelineStates）都无法验证。

## What Changes

- 实现 `VulkanGraphExecutor::GetOrCreateShaderModule()`：从 ShaderLibrary 获取 SPIR-V 数据，创建并缓存 `vk::ShaderModule`
- 重构 `VulkanGraphExecutor::GetOrCreatePipelineLayout()`：消费已有的 `VulkanShaderResourceBindingInfo.setLayoutInfos` 创建 `vk::DescriptorSetLayout`，组装 `vk::PipelineLayout`
- 修复 `VulkanShaderStruct::Init()` 中的空 descriptor set layout 占位，改为从 BindingInfo 构建
- 填充 `BuildPipelineStates` 中的 shader stages 和 pipeline layout 引用，使管线创建流程完整串联

## Capabilities

### New Capabilities
- `vulkan-shader-module`: Vulkan ShaderModule 创建与缓存管理，从 SPIR-V 字节码创建 vk::ShaderModule 并按 hash 缓存
- `vulkan-pipeline-layout`: Vulkan PipelineLayout 构建，从 VulkanShaderResourceBindingInfo 消费 descriptor set layout 信息创建正确的 vk::PipelineLayout

### Modified Capabilities
- `vulkan-shader-struct`: descriptor set layout 从空占位改为从 BindingInfo 构建，pipeline layout 随之修正

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/.cpp` — GetOrCreateShaderModule、GetOrCreatePipelineLayout、BuildPipelineStates
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.h/.cpp` — Init 中的 descriptor set layout 创建
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h` — 提供 GetShaderCode 的 SPIR-V 访问接口（已存在）
- 依赖已有的 `vulkan-shader-binding` spec (VulkanShaderResourceBindingInfo) 和 `vulkan-shader-library` spec (SPIR-V 加载)
