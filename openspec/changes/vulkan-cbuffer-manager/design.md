## Context

当前 Vulkan 后端的 CBuffer 生命周期管理分散在两个组件中：
- `VulkanResourceBindingInstance::BuildResources` — 为每个 binding element 调用 `AddBuffer` 分配独立 `vk::Buffer`
- `VulkanGraphExecutor::m_CBufferResourceIdMap` — `map<ShaderStruct*, uint64_t>` 映射，但多实例时会被覆盖

D3D12 参考实现 (`GPUConstantBufferManager`) 使用 `shared_dic<ShaderStruct*, BufferHandle>` 集中管理，`get_or_create` 保证唯一性，且 CBuffer 在 aliasing 阶段统一分配。

### 当前数据流

```
CompileAndExecute
  RegisterCBufferUsageStates  → m_CBufferLifetimes[pStruct] = { batches, queueFlags }
  AllocateAliasedResources    → 分配 image/buffer (不含 CBuffer)
  BuildResources              → AddBuffer → 独立 vk::Buffer (不参与 aliasing)
    → 填充 m_CBufferResourceIdMap (多实例同名 key 会被覆盖)
  PrepareBatchResourceBarriers → 依据 m_CBufferResourceIdMap 创建 upload barriers
  BuildDescriptors             → 使用 gpuBufferResourceId 绑到 DescriptorSet
  RecordBatchCommands          → staging upload → only last-mapped buffer gets data
```

### 目标数据流

```
CompileAndExecute
  RegisterCBufferUsageStates  → m_CBufferLifetimes
  RegisterCBufferResources    → NEW: VulkanConstantBufferManager 注册所有 CBuffer
    → RegisterTemporaryBuffer (不分配, 只注册元数据到 AliasingManager)
  AllocateAliasedResources    → 统一分配 (含 CBuffer)
  BuildResources              → 从 ConstantBufferManager 获取 resourceId (不分配)
  PrepareBatchResourceBarriers → 通过 ConstantBufferManager 查找
  BuildDescriptors             → 通过 ConstantBufferManager 获取 buffer
  RecordBatchCommands          → staging upload 覆盖所有唯一 CBuffer
```

## Goals / Non-Goals

**Goals:**
- 新增 `VulkanConstantBufferManager` 类，提供 `ShaderStruct* → resourceId` 唯一映射
- CBuffer 在 `AllocateAliasedResources` 之前注册，参与 aliasing 内存复用
- 删除 `m_CBufferResourceIdMap`，所有 CBuffer 查询统一走 `VulkanConstantBufferManager`
- `VulkanResourceBindingInstance::BuildResources` 不再分配 GPU buffer

**Non-Goals:**
- 不修改 `VulkanShaderStruct` 接口
- 不修改 `VulkanGraphLocalResourceManager::AddBuffer` 内部实现
- 不修改 DescriptorSet 创建和写入逻辑
- 不修改 CBuffer staging upload 流程

## Decisions

### D1: VulkanConstantBufferManager 使用 shared_dic<ShaderStruct*, uint64_t>

**选择**: `castl::shared_dic<VulkanShaderStruct const*, uint64_t> m_CBufferResources`

**备选**: 继续使用 `castl::unordered_map` + 手动去重

**理由**: 与 D3D12 `GPUConstantBufferManager` 的使用模式一致。`shared_dic::get_or_create` 天然保证同一 `ShaderStruct*` 只分配一次 resourceId，无需手动检查。

### D2: CBuffer 预注册方法 — RegisterCBufferForAliasing

**选择**: 在 `VulkanConstantBufferManager` 中新增 `RegisterCBufferForAliasing(VulkanGraphLocalResourceManager&, CBufferBindingElement const&)` 方法，内部调用 `RegisterTemporaryBuffer`（不是 `AddBuffer`）

**理由**: `RegisterTemporaryBuffer` 只记录元数据到 `m_LocalResources` 和 `m_AliasingManager`，不创建实际 GPU 资源。实际的 `vk::Buffer` 在 `AllocateAliasedResources` 中统一创建。CBuffer 此前使用 `AddBuffer` 的原因是它在 aliasing 之后才调用，无法走 aliasing 路径。

### D3: CompileAndExecute 阶段调整

**选择**: 在 `RegisterCBufferUsageStates` 之后、`AllocateAliasedResources` 之前插入新的 `RegisterCBufferForAliasing` 步骤

**顺序变更**:
```
Before:                         After:
  RegisterCBufferUsageStates      RegisterCBufferUsageStates
  BuildDependencyFreeBatches      RegisterCBufferForAliasing  ← NEW
  BuildResourceUsageRanges        BuildDependencyFreeBatches
  AllocateAliasedResources        BuildResourceUsageRanges
  BuildResources                  AllocateAliasedResources
                                  BuildResources  ← 不再 Alloc, 仅 lookup
```

**理由**: `AllocateAliasedResources` 遍历 `m_LocalResources` 为所有已注册资源分配 GPU 内存。CBuffer 必须在此步骤之前完成注册。

### D4: BuildResources 签名和行为变更

**选择**: `BuildResources` 不再接收 `resourceManager`，改为接收 `VulkanConstantBufferManager&`。CBuffer binding elements 的 `gpuBufferResourceId` 通过 `cbufferManager.GetOrCreateResource(...)` 获取（如果是新 struct 则注册，已存在则返回已有 ID）。

**理由**: 清除 `BuildResources` 的分配职责，使其专注于 usingStages 设置和 Image/Buffer fallback 注册。CBuffer 的 resourceId 解析走 ConstantBufferManager 的唯一映射。

### D5: 移除 m_CBufferResourceIdMap

**选择**: 完全删除 `VulkanGraphExecutor::m_CBufferResourceIdMap`，所有 CBuffer resourceId 查询走 `m_ConstantBufferManager`

**涉及替换的位置**:
1. `CompileAndExecute` 中填充 map 的循环 → 删除（已在 RegisterCBufferForAliasing 阶段处理）
2. `PrepareBatchResourceBarriers` 中 `m_CBufferResourceIdMap.find(pStruct)` → `m_ConstantBufferManager.GetResourceId(pStruct)`
3. `Reset()` 中 `m_CBufferResourceIdMap.clear()` → `m_ConstantBufferManager.Clear()`

## Risks / Trade-offs

- **[Risk] CBuffer 参与 aliasing 后，如果 aliasing 分析认为两个 CBuffer 生命周期重叠，会分配到不同的 aliasing slot** → 正确行为，无风险
- **[Risk] `shared_dic` 以 `ShaderStruct*` 为 key，如果应用层删除 struct 对象后重新创建（新指针），会导致重新分配 buffer** → 与 D3D12 行为一致，每帧 Reset 会 Clear，非问题
- **[Trade-off] CBuffer 在 aliasing 前注册意味着需要提前遍历所有 BindingInstance** → 增加了约 O(N) 的额外遍历，N 为 BindingInstance 数量，通常很小（几个到十几个），可忽略
