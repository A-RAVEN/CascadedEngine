## Context

VulkanGraphExecutor 的 `Prepare` 阶段当前流程：

```
Prepare():
  1. InitArraySizes             (数组扩容)
  2. CollectResources           (Raster: 注册 attachment/buffer ✔, Compute: TODO ❌)
  3. CollectShaderBindings      (创建所有 pass 的 ResourceBindingInstance)
  4. RegisterCBufferUsageStates (Raster + Compute CBuffers ✔)
```

`CompileAndExecute` 后续流程：

```
CompileAndExecute():
  ...
  BuildResources()              (RegisterTemporaryTexture/Buffer fallback ← 资源生命周期 ✔)
  BuildDependencyFreeBatches    (需要 m_ComputePassRWStates ← ❌ 状态从未写入)
  BuildResourceUsageRanges      (需要 m_ComputePassRWStates ← ❌)
  PrepareBatchResourceBarriers  (需要 image/buffer 的 access/stage/layout 信息 ← ❌)
```

**核心问题**: `VulkanResourceBindingInstance::BuildResources()` 已处理 compute shader 的资源生命周期注册，但 **没有任何代码将 compute shader 资源的读/写状态写入 `m_ComputePassRWStates`**。`BuildDependencyFreeBatches` / `BuildResourceUsageRanges` / `PrepareBatchResourceBarriers` 都依赖这些 RW state 来生成正确的依赖图和 barrier。

**约束：**
- `RegisterComputeResources` 执行时（Prepare 第3步之后），binding instances 已创建，但 `usingStages` 字段尚未填充——该字段在 `BuildResources`（CompileAndExecute 阶段）中设置。因此本步骤必须**硬编码** `vk::PipelineStageFlagBits::eComputeShader`
- `accessType` 字段在 binding instance 创建时（`VulkanResourceBindingInstance::Init`）已填充，可以直接使用
- `RegisterCBufferUsageStates` 已处理 CBuffer 的状态注册，本步骤仅关注 image 和 buffer

**参考实现：** D3D12 `GPUGraphExecutor.cpp:480-505`

## Goals / Non-Goals

**Goals:**
- Compute Pass 的 image binding 状态写入 `m_ComputePassRWStates[passID].imageRWStates`
- Compute Pass 的 buffer binding 状态写入 `m_ComputePassRWStates[passID].bufferRWStates`
- 根据 SLANG `EShaderResourceAccess` 正确映射到 `vk::AccessFlags` 和 `vk::ImageLayout`
- 确保 batch 依赖分析和 barrier 生成能正确处理 compute 资源
- 对齐 D3D12 的 compute 资源同步行为

**Non-Goals:**
- 不改变 Prepare 阶段的整体执行顺序（仅在 `CollectShaderBindings` 之后插入）
- 不修改 Raster/Transfer Pass 的资源注册逻辑
- 不修改 `VulkanExecutorRWState` 类本身（现有接口已足够）
- 不修改 `BuildResources` 中的资源生命周期注册（保持已有 fallback 机制）
- 不涉及 async compute 的跨队列同步（属于 Phase 2）

## Decisions

### Decision 1: 新增 `RegisterComputeResources()` 方法

**选择**: 在 `CollectShaderBindings` 之后、`RegisterCBufferUsageStates` 之前新增独立方法 `RegisterComputeResources()`

`Prepare` 变为：
```
Prepare():
  1. InitArraySizes
  2. CollectResources            (Raster + Transfer)
  3. CollectShaderBindings       (创建 binding instances)
  4. RegisterComputeResources    ← 新增 (Compute image/buffer 状态)
  5. RegisterCBufferUsageStates  (所有 pass CBuffer 状态)
```

**替代方案**: 将 compute binding instance 创建移入 `CollectResources` 中（对齐 D3D12 模式）

**理由**: 保持最小变更原则。拆分步骤与现有架构一致（CollectShaderBindings 已独立），不改变 binding instance 生命周期的管理方式。`RegisterComputeResources` 职责单一：只负责 RW state 写入，不碰资源生命周期。

### Decision 2: 状态注册方式

**选择**: 遍历每个 compute dispatch 的 `VulkanResourceBindingInstance`，直接迭代其 binding elements

```
for each compute dispatch:
  pBindingInstance = dispatchData.pResourceBindingInstance
  for each ImageBinding:
    queueType = computePass.asyncCompute ? eCompute : eDirect
    passRWState.SetImageRWState(imageHandle,
      vk::PipelineStageFlagBits::eComputeShader,  // 硬编码，usingStages 此时不可用
      ComputeAccessToVulkanAccess(bindingInfo.accessType),
      ComputeAccessToImageLayout(bindingInfo.accessType),
      queueType)
  for each BufferBinding:
    passRWState.SetBufferRWState(bufferHandle,
      vk::PipelineStageFlagBits::eComputeShader,
      ComputeAccessToVulkanAccess(bindingInfo.accessType),
      queueType)
```

**理由**: `VulkanResourceBindingInstance` 已暴露 `GetImageBindings()` / `GetBufferBindings()` 等 getter。这与 `RegisterCBufferUsageStates` 中访问 `GetCBufferBindings()` 的模式一致。

**去重**: 如果多个 dispatch 共享同一个 `VulkanShaderResourceSet`（hash 相同），`createOrGetBindingInstance` 返回同一实例，会重复迭代。但由于 `Combine()` 对 access/stage flags 做 OR 操作为幂等，不会产生错误结果，只是轻微的重复遍历。

### Decision 3: usingStages 硬编码

**选择**: 在 `RegisterComputeResources` 中统一使用 `vk::PipelineStageFlagBits::eComputeShader`

**理由**: `ImageBindingElement::usingStages` 和 `BufferBindingElement::usingStages` 在 `BuildResources()` 中才被设置（`VulkanResourceBindingInstance.cpp:256,276`），而 `RegisterComputeResources` 在 Prepare 阶段执行，此时这些字段为空。对于 compute shader 的资源，统一使用 `eComputeShader` 是正确的（唯一 stage 就是 compute）。

### Decision 4: accessType → Vulkan flags 映射

**选择**: 新增两个 helper 函数

```cpp
// ComputeAccessToVulkanAccess: EShaderResourceAccess → vk::AccessFlags
// eReadOnly  → eShaderRead
// eWriteOnly → eShaderWrite
// eReadWrite → eShaderRead | eShaderWrite
static vk::AccessFlags ComputeAccessToVulkanAccess(
    ShaderCompilerSlang::EShaderResourceAccess access);

// ComputeAccessToImageLayout: EShaderResourceAccess → vk::ImageLayout
// eReadOnly  → eShaderReadOnlyOptimal
// eWriteOnly → eGeneral
// eReadWrite → eGeneral
static vk::ImageLayout ComputeAccessToImageLayout(
    ShaderCompilerSlang::EShaderResourceAccess access);
```

**理由**: 与 D3D12 `addPassShaderInstancesResourcesRWStates` 中的 usingStages 映射一致。将映射抽成独立 helper 便于复用且易于测试。

## Risks / Trade-offs

- **风险**: 共享 binding instance 导致重复迭代 → **缓解**: `Combine()` 幂等，结果正确，重复遍历开销极小（每个 dispatch 通常只有个位数 binding）
- **风险**: usingStages 硬编码 `eComputeShader` 过于简化 → **评估**: compute shader 资源只有 compute stage 使用，硬编码是正确的。未来如果有 ray tracing 共用同一个 BindingInstance，需重新设计
- **折衷**: 资源生命周期注册（RegisterTemporaryTexture/Buffer）在本步骤不处理，完全依赖 `BuildResources` 的 fallback。这意味着如果某个 compute 资源在 `BuildResources` 之前通过其他 path 已经注册，不会冲突

## Open Questions

- Compute pass 的 dispatch 目前是无 attachment 的，是否需要像 raster pass 那样额外处理 depth/stencil attachment？ → **无需**，compute pass 没有 framebuffer
- 是否需要支持 async compute 的独立 queue family？ → **No**（Phase 2 范围）
