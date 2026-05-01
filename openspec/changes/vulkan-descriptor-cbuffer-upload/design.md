## Context

VulkanGraphExecutor 已完成 ShaderModule 创建、PipelineLayout 构建、Pipeline 创建的完整串联。当前渲染管线在 `RecordRenderPass` / `RecordComputePass` 中调用 `bindDescriptorSets`，但由于 `VulkanResourceBindingInstance` 的 `BuildResources()` 和 `BuildDescriptors()` 均未实现，`GetDescriptorSet(0)` 始终返回 nullptr，导致没有任何资源绑定到管线。

同时，CBuffer（Uniform Buffer）数据停留在 CPU staging buffer 中，从未上传到 GPU。`PrepareBatchResourceBarriers` 中有 `VulkanCBufferInitializeBarriers` 结构体和循环框架，但体为空。

### 当前数据流断裂点

```
VulkanShaderStruct (CPU staging)
  ├── m_StructLocalUniformStagingBuffer  ← 数据在此
  ├── GetImageHandles() / GetBufferHandles() / GetSamplerDescriptors()  ← 资源句柄在此
  │
  │  ❌ 断裂：无 GPU buffer 分配、无 DescriptorSet 分配、无数据上传
  │
  ▼
VulkanResourceBindingInstance
  ├── Init() ✅ — 已填充 CBufferBindingElement / ImageBindingElement / ...
  ├── BuildResources() ❌ — 空
  ├── BuildDescriptors() ❌ — 骨架
  ├── m_DescriptorSets {} ← 始终为空
  │
  │  ❌ 断裂：bindDescriptorSets 传入空 set
  │
  ▼
vk::cmdBindDescriptorSets → Pipeline 收到空绑定
```

### D3D12 参考实现

D3D12 的完整流程：
1. `GPUConstantBufferManager` 收集所有 CBuffer 请求，统一分配 `BufferHandle`
2. `GPUResourceBindingInstance::BuildResources()` 注册 image/buffer/CBuffer 到 LocalResourceManager
3. `GPUResourceBindingInstance::BuildDescriptors()` 从 GPU Descriptor Heap 分配 chunk，写入 SRV/CBV/UAV/Sampler
4. `CBufferInitializeBarriers::CBufferCopyInitialize()` 通过 staging + CopyBufferRegion 上传数据

## Goals / Non-Goals

**Goals:**
- 实现 DescriptorSet 分配与写入，使 `bindDescriptorSets` 能绑定真实资源
- 实现 CBuffer GPU buffer 分配与数据上传，使 shader 能读取 uniform 数据
- 串联 BuildResources → BuildDescriptors → CBuffer Upload 的完整调用链

**Non-Goals:**
- 不实现 GPUFrameManager / 多帧重叠（Phase 2）
- 不实现 LinearMemoryManager（staging buffer 暂时独立创建）
- 不实现跨队列同步（Queue Family ownership transfer）
- 不实现 SamplerManager（sampler 暂时在 DescriptorSet 中内联创建）
- 不重构 VulkanShaderStruct 中已声明但未使用的 descriptor 成员（m_DescriptorSetLayout 等），留待后续清理

## Decisions

### D1: CBuffer GPU buffer 由 VulkanGraphLocalResourceManager 分配，不由 VulkanShaderStruct 持有

**选择**: 在 `BuildResources` 阶段，为每个 `CBufferBindingElement` 调用 `m_LocalResourceManager.AddBuffer()` 分配 GPU buffer，返回的 `uint64_t resourceId` 存入 `CBufferBindingElement::gpuBufferResourceId`。`PrepareBatchResourceBarriers` 再从 `VulkanGraphExecutor::m_CBufferResourceIdMap` 查找该 CBuffer 对应的 resourceId，填充 `VulkanCBufferInitializeBarriers::cbufferData`。

**理由**: VulkanShaderStruct 是跨帧复用的逻辑对象，不应持有每帧不同的 GPU buffer。GPU buffer 生命周期与 GPUGraph 执行一致，由 LocalResourceManager 管理别名和回收。LocalResourceManager 内部以 `uint64_t resourceId` 标识临时资源，`BufferHandle` 没有临时资源构造函数，因此直接使用 `uint64_t`。

**新增 API**: `VulkanGraphLocalResourceManager` 需要新增方法 `uint64_t AddBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex)`，内部调用 `RegisterTemporaryBuffer` 获取 `resourceId` 并直接返回，无需额外 `RegisterBufferHandle`（CBuffer 不通过 `BufferHandle` 引用）。

**替代方案**: 让 VulkanShaderStruct 自己持有 GPU buffer（类似 D3D12 的 `GPUConstantBufferManager`）—— 但这引入了跨帧资源管理复杂度，且与当前 Vulkan 后端的 LocalResourceManager 别名模型不一致。当前决策为**不引入**独立的 `GPUConstantBufferManager`，由 `GraphLocalResourceManager` 统一分配。

### D2: DescriptorPool 在 CompileAndExecute 开头按需创建，Reset 时销毁

**选择**: 在 `CompileAndExecute` 开头（`Prepare` 阶段后）统计所有 BindingInstance 的 descriptor 需求总量，一次性创建 `vk::DescriptorPool`，存入 `m_DescriptorPool`。创建新 pool 前，若 `m_DescriptorPool` 非空则先销毁旧 pool（避免每帧泄漏）。帧结束时的 `Reset()` 同样确保 pool 被销毁。

**理由**: 当前是同步单帧执行模式，每帧 CompileAndExecute 时创建/销毁 Pool 最简单。Vulkan 的 DescriptorPool 可配置 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` 支持逐个释放，但当前帧结束时整体销毁更高效。

**替代方案**: 使用 `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT` 扩展实现延迟更新 —— 增加复杂度但当前无性能瓶颈，不必要。

### D3: BuildResources 和 BuildDescriptors 的调用位置

**选择**: 在 `PrepareBatchResourceBarriers` 之前调用 `BuildResources`，在 `BuildPipelineStates` 之后、`Execute` 之前调用 `BuildDescriptors`。`SetCBufferUsageState` 在 `CollectShaderBindings` 完成后、`BuildDependencyFreeBatches` 之前执行。

**理由**: 
- `BuildResources` 分配 CBuffer GPU buffer 并填充 `CBufferBindingElement::gpuBufferResourceId`。`PrepareBatchResourceBarriers` 需要这些 resourceId 来填充 `VulkanCBufferInitializeBarriers::cbufferData`，因此 `BuildResources` 必须在 `PrepareBatchResourceBarriers` **之前**。
- `BuildDescriptors` 需要已缓存的 DescriptorSetLayout（由 `GetOrCreatePipelineLayout` 创建），所以必须在 `BuildPipelineStates` **之后**。
- `SetCBufferUsageState` 必须在 `BuildDependencyFreeBatches` 之前，因为 `BuildDependencyFreeBatches` 和 `BuildResourceUsageRanges` 依赖 `m_CBufferLifetimes`。

```
调用顺序:
  Prepare → CollectResources → CollectShaderBindings
  → RegisterCBufferUsageStates ← NEW: 遍历 m_ShaderResourceInstances 注册 CBuffer 生命周期
  → BuildDependencyFreeBatches → BuildResourceUsageRanges
  → AllocateAliasedResources
  → BuildResources ← NEW: 分配 CBuffer GPU buffer, 注册 image/buffer 资源
  → PrepareBatchResourceBarriers (此时 gpuBufferResourceId 已就绪)
  → BuildPipelineStates (创建 DescriptorSetLayout 缓存)
  → BuildDescriptors ← NEW: 分配 DescriptorSet, 写入资源绑定
  → Execute (CBuffer staging 拷贝 + bindDescriptorSets)
```

### D4: CBuffer 上传使用 vkCmdCopyBuffer + pipeline barrier

**选择**: 在 `RecordBatchCommands` 中，对每个 batch 的 `cbufferBarriers` 执行：创建 staging buffer → Map/Copy/Unmap → `vkCmdCopyBuffer` → pipeline barrier (transfer-write → uniform-read)。

**理由**: 与 D3D12 的 `CBufferCopyInitialize` 模式一致。Staging buffer 是每帧临时分配，通过 `m_PendingStagingBuffers` 追踪并在帧结束后回收。

**替代方案**: 使用 `vkMapMemory` 直接写入 HOST_VISIBLE | HOST_COHERENT buffer —— 这需要额外的 buffer 类型分支逻辑，且当前所有 CBuffer 已通过 staging buffer 模型实现，保持一致性更优。

### D5: CBuffer resourceId 与 VulkanResourceBindingInstance 的关联

**选择**: BuildResources 阶段分配 CBuffer GPU buffer 后，将 `uint64_t resourceId` 写入 `CBufferBindingElement` 新增的 `gpuBufferResourceId` 字段。BuildDescriptors 从该字段读取 resourceId，通过 `LocalResourceManager::GetBuffer(resourceId)` 获取 `vk::Buffer` 用于写入 DescriptorSet。同时 `VulkanGraphExecutor` 维护 `m_CBufferResourceIdMap`（`VulkanShaderStruct* → uint64_t`），供 `PrepareBatchResourceBarriers` 查找。

**理由**: CBufferBindingElement 目前缺少 GPU buffer 标识（只有 `pCBufferStruct` 指针和 `bindingInfo`），需要补充。LocalResourceManager 以 `uint64_t resourceId` 管理临时资源，直接使用 resourceId 最自然。`m_CBufferResourceIdMap` 解决 `PrepareBatchResourceBarriers` 中只有 `VulkanShaderStruct*` 而无法定位 resourceId 的问题。

### D6: RecordRenderPass/RecordComputePass 绑定所有 DescriptorSet

**选择**: 修改 `bindDescriptorSets` 调用，从仅绑定 set 0 改为按连续 set index 区间分组绑定所有已分配的 DescriptorSet。

**理由**: 当前仅绑定 set 0，但 shader 可能有多个 set（如 set 0 = per-frame, set 1 = per-material）。`m_DescriptorSets` 以 `setIndex` 为 key 存储。若 set 不连续（如只有 set 0 和 set 2），不能简单地把所有 set 塞进一个数组一次绑定（`firstSet=0` 会导致 set 2 被错误绑定到 set 1 槽位）。需要按连续区间多次调用 `bindDescriptorSets`。

### D7: VulkanGraphExecutor 维护 CBuffer resourceId 映射表

**选择**: `VulkanGraphExecutor` 新增成员 `castl::unordered_map<VulkanShaderStruct const*, uint64_t> m_CBufferResourceIdMap`。在 `BuildResources` 遍历每个 `BindingInstance` 的 `CBufferBindings` 时，将 `pair(pCBufferStruct, gpuBufferResourceId)` 插入映射表。`PrepareBatchResourceBarriers` 从该表查找 resourceId。

**理由**: `PrepareBatchResourceBarriers` 遍历 `m_CBufferLifetimes` 时只有 `VulkanShaderStruct*` key，而 resourceId 分散在各 `BindingInstance` 中。引入映射表避免在 barrier 阶段遍历所有 `BindingInstance` 做反向查找。

### D8: BuildResources 和 BuildDescriptors 的 API 签名变更

**选择**: 
- `BuildResources` 签名为 `void BuildResources(VulkanGraphLocalResourceManager& resourceManager, GPUGraph const& graph)`。需要 `GPUGraph` 来获取 ImageHandle/BufferHandle 的 descriptor（区分 External/Internal/Backbuffer），以便在需要时调用 `RegisterTemporaryTexture`/`RegisterTemporaryBuffer`。
- `BuildDescriptors` 签名为 `void BuildDescriptors(VulkanGraphExecutor& executor, vk::DescriptorPool pool)`。需要 `executor` 来访问 `m_DescriptorSetLayoutCache`（获取已缓存的 DescriptorSetLayout）和 `m_LocalResourceManager`（获取 `vk::Buffer`/`vk::ImageView`）。

**理由**: 现有签名 `BuildResources(LocalResourceManager&)` 和 `BuildDescriptors(vk::DescriptorPool)` 缺少获取 vk 对象和 layout cache 的途径，无法完成实现。

**替代方案**: 把所需对象作为独立参数传入（如 `LocalResourceManager&`、`DescriptorSetLayoutCache&`、`GPUGraph const&`）—— 但参数过多，直接传入 `executor` 更简洁。

### D9: DescriptorSet 分配使用 shader setIndex 而非数组索引

**选择**: 修改 `AllocateDescriptorSets` 的实现：接收按 `setIndex` 排序的 `(uint32_t setIndex, vk::DescriptorSetLayout layout)` pair 数组，分配后按 `setIndex` 存入 `m_DescriptorSets`（即 `m_DescriptorSets[setIndex] = sets[i]`），而非按数组索引 `i` 存储。

**理由**: 现有骨架代码用数组索引 `i` 作为 `m_DescriptorSets` 的 key。若 shader 的 setLayoutInfos 包含 `setIndex=0` 和 `setIndex=2`，分配后会变成 `m_DescriptorSets[0]=set0, m_DescriptorSets[1]=set2`。后续 `bindDescriptorSets(firstSet=0, sets={set0, set2})` 会把 set 2 绑定到 Vulkan pipeline 的 set 1 槽位，导致 shader 访问错误。

## Risks / Trade-offs

**[每帧创建/销毁 DescriptorPool]** → 当前同步执行模式下无性能问题。异步帧重叠模式下需改为池复用，届时再重构。需在创建新 pool 前显式销毁旧 pool，避免泄漏。

**[Staging buffer 每次独立创建]** → 与现有 TransferPass staging 策略一致。LinearMemoryManager 实现后可统一优化。

**[usingStages 未填充]** → `VulkanResourceBindingInstance::Init()` 中未调用 `GetShaderStageUsage` 设置 `usingStages`。当前不影响 Descriptor 写入，但影响 barrier 的 stage flag。在 BuildResources 中补充设置。

**[SetCBufferUsageState 未调用]** → `CollectResources` 执行时 `m_ShaderResourceInstances` 尚未创建（`CollectShaderBindings` 在其后），因此无法在 `CollectResources` 中遍历 binding 注册 CBuffer。需在 `CollectShaderBindings` 完成后、`BuildDependencyFreeBatches` 之前，新增一步遍历 `m_ShaderResourceInstances` 调用 `SetCBufferUsageState`，将 CBuffer 生命周期注册到 `m_CBufferLifetimes`。

**[BuildResources 的 image/buffer 注册可能冗余]** → `CollectResources` 已通过 `Foreach` 注册了所有 internal graph image/buffer，且 external resource 可直接通过 `BufferHandle`/`ImageHandle` 解析。`BuildResources` 中的 image/buffer 注册主要针对那些尚未建立 handle→resourceId 映射的资源，实际触发频率可能很低。如后续发现完全冗余，可移除该步骤。

**[BufferHandle 与临时资源类型不匹配]** → LocalResourceManager 的临时资源以 `uint64_t resourceId` 标识。`BufferHandle` 没有临时资源构造函数，因此 CBuffer 直接使用 `uint64_t`。若未来需要统一用 `BufferHandle` 引用所有资源，需先扩展 `BufferHandle` 的类型枚举或增加临时资源构造函数。

## Post-Implementation Review (2026-04-30)

Review 确认核心数据流已贯通（BuildResources → BuildDescriptors → CBuffer Upload → bindDescriptorSets），但发现以下遗留问题：

| Issue | Severity | Description |
|-------|----------|-------------|
| Image fallback 缺少 RegisterTextureHandle | **High** | BuildResources 的 image fallback 路径只调用了 `RegisterTemporaryTexture` 但丢弃返回值，未建立 `ImageHandle → resourceId` 映射。若 fallback 路径被触发，后续 `GetTextureView(ImageHandle)` 将返回 null |
| SetUniformBuffer 的 operator[] 隐患 | **Medium** | `m_DescriptorSets[set]` 在 key 不存在的静默插入 null，传给 vkUpdateDescriptorSets 会导致未定义行为。应改用 `find()` + `CA_ASSERT_BREAK` |
| CombinedImageSampler 零初始化 | **Low** | 当 ImageBindingElement 为 SampledImage 类型时创建了全字段零值的 vk::SamplerCreateInfo，可能触发 Validation Layer 警告 |
| TextureSamplerDescriptor 未映射 | **Low** | Sampler 绑定的 descriptor 参数未映射到 vk::SamplerCreateInfo，filter/anisotropy/addressMode 全为默认零值 |
| 死代码 | **Trivial** | `RegisterCBufferUsageStates` 中 `pShaderFileInfo` 变量被赋值但未使用 |
