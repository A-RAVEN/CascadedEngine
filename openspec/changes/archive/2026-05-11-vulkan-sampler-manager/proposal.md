## Why

Vulkan 后端的 sampler 对象当前在 `BuildDescriptors()` 中每帧创建并在 `Release()` 中每帧销毁，没有全局缓存机制。D3D12 后端有独立的 `SamplerManager` 组件（全局缓存 + `GetCPUHandle(desc)` 接口），Vulkan 缺少对应组件。这是对齐文档中最后一个未完成的架构级差异项。

## What Changes

- 新建 `VulkanSamplerManager` 类，提供全局 sampler 缓存（基于 `shared_dic<TextureSamplerDescriptor, vk::Sampler>`）
- 在 `RenderBackend_Vulkan` 中集成 `VulkanSamplerManager` 成员，生命周期与 backend 一致
- 修改 `VulkanResourceBindingInstance::BuildDescriptors()` 从 Manager 获取 sampler 而非每帧 `createSampler`
- 移除 `VulkanResourceBindingInstance` 中的 `m_CreatedSamplers` vector 及其双路径销毁逻辑（`BuildDescriptors` 开头 + `Release`）
- 更新对齐文档，将 SamplerManager 状态从 ❌ 改为 ✅

## Capabilities

### New Capabilities
- `vulkan-sampler-manager`: Vulkan 后端的全局 sampler 缓存管理器，提供 `GetOrCreateSampler(TextureSamplerDescriptor) → vk::Sampler` 接口，sampler 对象全局创建一次、跨帧复用、backend 销毁时统一回收。

### Modified Capabilities
- `vulkan-backend-alignment`: SamplerManager 对齐状态从 ❌ 未实现 更新为 ✅ 已对齐。

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanSamplerManager.h/.cpp` — 新建
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.h/.cpp` — 添加成员、初始化、Release 集成
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/.cpp` — 修改 `BuildDescriptors` 和 `Release`，移除 `m_CreatedSamplers`
- `Documents/Vulkan后端与D3D12后端对齐分析.md` — 状态更新