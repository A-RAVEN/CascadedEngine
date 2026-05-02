## Why

当前 Vulkan 后端的 CBuffer 管理存在两个问题：1) 当多个不同的 `VulkanShaderResourceSet` 引用同一个 `VulkanShaderStruct` 时，`BuildResources` 会为每个 Set 独立调用 `AddBuffer` 分配独立的 `vk::Buffer`，但 `m_CBufferResourceIdMap`（`map<ShaderStruct*, uint64_t>`）只能保存一个 resourceId，导致只有一个 buffer 被 CBuffer 上传路径处理，其他 buffer 保持未初始化状态；2) CBuffer 在 `AllocateAliasedResources()` 之后才分配，不参与 aliasing 内存复用，当存在几十个生命周期不重叠的 CBuffer 时浪费显存。

D3D12 参考实现 `GPUConstantBufferManager` 通过 `shared_dic<ShaderStruct*, BufferHandle>` 的 `get_or_create` 模式天然保证唯一映射，且 CBuffer 在 aliasing 阶段统一分配。本变更将此模式移植到 Vulkan。

### 问题场景示例

```
Pass 0: shader "Mesh.hlsl" + TransformCB (同一个 struct 对象 &t)
Pass 1: shader "Particle.hlsl" + TransformCB (同一个 struct 对象 &t)

当前行为:
  BindingInstance_A → AddBuffer() → buffer_42, DescriptorSet_A 指向 buffer_42
  BindingInstance_B → AddBuffer() → buffer_43, DescriptorSet_B 指向 buffer_43
  m_CBufferResourceIdMap[&t] = 42  // 先写入
  m_CBufferResourceIdMap[&t] = 43  // 被覆盖!
  CBuffer 上传仅写 buffer_43 → buffer_42 永远未初始化 ⚠️
```

## What Changes

- 新增 `VulkanConstantBufferManager` 类，提供 `ShaderStruct* → uint64_t resourceId` 的唯一映射
- 将 CBuffer 的 `RegisterTemporaryBuffer` 调用移到 `AllocateAliasedResources()` 之前，使 CBuffer 参与 aliasing
- `VulkanResourceBindingInstance::BuildResources` 不再调用 `AddBuffer`，改为从 `VulkanConstantBufferManager` 获取 resourceId
- `VulkanGraphExecutor` 新增 `m_ConstantBufferManager` 成员，替代现有的 `m_CBufferResourceIdMap`

## Capabilities

### New Capabilities
- `vulkan-cbuffer-manager`: Vulkan CBuffer 统一管理，提供 ShaderStruct→Buffer 唯一映射，支撑 CBuffer 参与 aliasing 内存复用

### Modified Capabilities
<!-- No existing specs are modified; this is new capability only. -->

## Impact

- **新增文件**: `VulkanRenderBackendNew/private/GPUGraph/VulkanConstantBufferManager.h/.cpp`
- **修改文件**:
  - `VulkanGraphExecutor.h/.cpp` — 新增 `m_ConstantBufferManager` 成员，调整 `CompileAndExecute` 阶段顺序，修复 `RegisterCBufferForAliasing` 排序
  - `VulkanResourceBindingInstance.h/.cpp` — `BuildResources` 签名变更，不再直接调用 `AddBuffer`，CBuffer 仅 lookup
  - `VulkanGraphLocalResourceManager.h/.cpp` — 可能需要暴露 `RegisterTemporaryBuffer` 用于 CBuffer 预注册
- **不影响**: 应用层 API、ShaderStruct 接口、DescriptorSet 创建流程
- **D3D12 对齐度**: 此前 D3D12 `GPUConstantBufferManager` 被标记为"未实现"，本次变更后对齐

### 审查发现项 (2026-05-01)

代码审查发现 `RegisterCBufferForAliasing` 存在排序 bug——在 `m_CBufferLifetimes` 被 `BuildResourceUsageRanges` 填充之前执行，导致 `MarkResourceUse` 从未被调用。此外：
- `IterateResources` 方法无调用点，为死代码
- CBuffer `GPUBufferDescriptor` 创建逻辑在 `RegisterCBufferForAliasing` 和 `BuildResources` 中重复

修复任务见 tasks.md 第 7 节。

## Non-Goals

- 不实现 LinearMemoryManager（仍使用 per-CBuffer staging buffer）
- 不实现 SamplerManager
- 不实现 GPUFrameManager / 多帧重叠
- 不修改 VulkanShaderStruct 的接口
