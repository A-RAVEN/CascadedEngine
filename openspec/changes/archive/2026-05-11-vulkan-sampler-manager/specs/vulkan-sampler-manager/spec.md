# Vulkan Sampler Manager

**Version**: 1.0
**Created**: 2026-05-10
**Status**: Active

---

## 概述

本规范定义 Vulkan 后端的全局 sampler 缓存管理器，与 D3D12 `SamplerManager` 功能对齐。

---

## ADDED Requirements

### Requirement: Sampler 全局缓存

系统 SHALL 提供 `VulkanSamplerManager` 类，对每个唯一的 `TextureSamplerDescriptor` 全局只创建一次 `vk::Sampler`，跨帧复用。

#### Scenario: 首次获取 sampler

- **WHEN** `GetOrCreateSampler(desc)` 被调用且 `desc` 对应的 sampler 未在缓存中
- **THEN** 系统 SHALL 通过 `MakeSamplerCreateInfo(desc)` 创建 `vk::SamplerCreateInfo`，调用 `device.createSampler(info)` 创建新 sampler，存入缓存，并返回该 `vk::Sampler`

#### Scenario: 重复获取相同 descriptor

- **WHEN** `GetOrCreateSampler(desc)` 被调用且 `desc` 对应的 sampler 已在缓存中
- **THEN** 系统 SHALL 直接返回缓存的 `vk::Sampler`，不调用 `createSampler`

### Requirement: Sampler 生命周期管理

`VulkanSamplerManager` SHALL 由 `RenderBackend_Vulkan` 持有，生命周期与 backend 一致。

#### Scenario: Release 时销毁所有 sampler

- **WHEN** `Release()` 被调用
- **THEN** 系统 SHALL 遍历缓存中所有 `vk::Sampler`，调用 `device.destroySampler()` 逐一销毁，然后清空缓存

#### Scenario: Manager 置于 RenderBackend_Vulkan

- **WHEN** `RenderBackend_Vulkan` 被初始化
- **THEN** `VulkanSamplerManager` SHALL 作为其成员被构造，并通过 `GetSamplerManager()` 访问器暴露

### Requirement: BuildDescriptors 集成

`VulkanResourceBindingInstance::BuildDescriptors()` SHALL 通过 `SamplerManager` 获取 sampler，而非直接调用 `device.createSampler()`。

#### Scenario: 从 Manager 获取 sampler 绑定

- **WHEN** `BuildDescriptors()` 遍历 `m_SamplerBindings`
- **THEN** 系统 SHALL 调用 `GetApp()->GetSamplerManager().GetOrCreateSampler(samplerDesc)` 获取 `vk::Sampler`，传入 `SetSampler()` 写入 DescriptorSet

### Requirement: 移除 m_CreatedSamplers

`VulkanResourceBindingInstance` SHALL 不再维护 `m_CreatedSamplers` 成员变量及其相关销毁逻辑。

#### Scenario: BuildDescriptors 不再销毁旧 sampler

- **WHEN** `BuildDescriptors()` 被调用
- **THEN** 系统 SHALL NOT 遍历 `m_CreatedSamplers` 调用 `destroySampler`（此逻辑已移除）

#### Scenario: Release 不再销毁 sampler

- **WHEN** `VulkanResourceBindingInstance::Release()` 被调用
- **THEN** 系统 SHALL NOT 遍历 `m_CreatedSamplers` 调用 `destroySampler`（此逻辑已移除，sampler 由 Manager 管理）