## Context

`VulkanGraphExecutor::CompileAndExecute()` 每帧为 graph 临时资源（CBuffer、中间纹理等）执行 aliasing 分配。当前实现：

```
AllocateAliasedResources()
  ├─ AnalyzeAndPlanAliasing()   → 贪心区间调度，计算 aliased 总大小
  ├─ AllocateAliasedPool(size)  → VMA 分配大块内存池 ← 💥 崩溃点
  └─ 遍历资源创建 buffer/image  → device.createBuffer/Image（独立分配，未绑定池）
```

`AllocateAliasedPool()` 使用 `VMA_MEMORY_USAGE_AUTO` + `vmaAllocateMemory()`，此组合被 VMA 3.1.0 明确禁止（`vk_mem_alloc.h:3925-3929`）：

```cpp
case VMA_MEMORY_USAGE_AUTO:
    if(bufImgUsage == VmaBufferImageUsage::UNKNOWN) {
        // vmaAllocateMemory 不传资源类型 → 拒绝
        return false;  // → VK_ERROR_FEATURE_NOT_PRESENT
    }
```

对抗验证还发现三个额外问题：

1. **时序问题**: `AnalyzeAndPlanAliasing()`（第 115 行）在 `AllocateAliasedPool()`（第 118 行）之前执行，但 `AnalyzeAndPlanAliasing()` 第 93 行 `alloc.allocation = m_AliasedPoolAllocation` 将当时仍为 `VK_NULL_HANDLE` 的值写入 `m_AliasedAllocations`。即使 `AllocateAliasedPool()` 成功，这些条目的 allocation 仍为 `VK_NULL_HANDLE`。

2. **deprecated API**: `VMA_MEMORY_USAGE_CPU_TO_GPU` 在 VMA 3.1.0 中已标记 deprecated。应使用 `VMA_MEMORY_USAGE_UNKNOWN` + 显式 `requiredFlags`。

3. **多余的 MapMemory 调用**: `VMA_ALLOCATION_CREATE_MAPPED_BIT` 已使 VMA 自动通过 `VmaAllocationInfo::pMappedData` 返回映射指针（`AllocateMemory` 内部调用 `vmaMapMemory()` + `vkMapMemory()`），当前第 140 行的显式 `MapMemory()` 调用多余。

注意：aliased pool 当前为 stub 实现——池分配后，后续资源通过独立的 `device.createBuffer()/createImage()` 创建，未实际绑定到池。但修复分配失败是使其正常工作（即使是 stub）的前提。

## Goals / Non-Goals

**Goals:**
- 修复 VMA 分配失败：`VMA_MEMORY_USAGE_AUTO` → `VMA_MEMORY_USAGE_UNKNOWN` + 显式 `requiredFlags=HOST_VISIBLE` + `preferredFlags=DEVICE_LOCAL`
- 修复 allocation 句柄时序：`AnalyzeAndPlanAliasing()` 存储 `VK_NULL_HANDLE` 后，在 `AllocateAliasedPool()` 成功时遍历更新
- 移除多余的 `MapMemory()` 调用，改用 `VmaAllocationInfo::pMappedData`
- 确保内存类型选择在所有 GPU（集成/独立）上正常工作
- 保持 persistent mapping 能力（用于后续 CPU 写入 staging 数据）
- 使用非 deprecated VMA API，避免未来版本升级风险

**Non-Goals:**
- 不重构 aliasing 为完整实现（资源绑定到 VMA custom pool）
- 不跳过 aliasing 流程（即使当前为 stub，保留架构为后续完整实现做准备）
- 不改为 `vmaCreateBuffer` 路径（涉及调用链重构，风险大于收益）
- 不在此阶段将内存类型改为纯 DEVICE_LOCAL（aliasing stub 阶段 CPU_TO_GPU 语义差异可接受）

## Decisions

### Decision 1: 使用 `VMA_MEMORY_USAGE_UNKNOWN` + 显式 flags 替代 deprecated `VMA_MEMORY_USAGE_CPU_TO_GPU`

**选择**: `VMA_MEMORY_USAGE_UNKNOWN` + `requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` + `preferredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`

**备选方案**:

| 方案 | 优点 | 缺点 |
|------|------|------|
| A. `VMA_MEMORY_USAGE_UNKNOWN` + 显式 flags ✅ | 非 deprecated API；底层控制力强；与 `vmaAllocateMemory` 兼容 | 需手动指定 flags，失去 VMA 智能选型 |
| B. `VMA_MEMORY_USAGE_CPU_TO_GPU` | 与 `vmaAllocateMemory` 兼容；自动 `requiredFlags \|= HOST_VISIBLE` | **VMA 3.1.0 已标记 deprecated**，未来版本可能移除；语义不匹配（aliased pool 用于 GPU 内部资源，非 CPU→GPU staging） |
| C. 改为 `vmaCreateBuffer` 路径 | VMA 完整管理 buffer 生命周期 | 重构范围大；aliased pool 需维持独立内存块语义 |
| D. 跳过 aliasing 整个流程 | 最简单 | 失去 aliasing 架构占位；后续实现时需重新对接 |

**理由**:
- `VMA_MEMORY_USAGE_CPU_TO_GPU` 在 VMA 3.1.0 中已被标记为 deprecated（源码注释 "Deprecated. Use VMA_MEMORY_USAGE_AUTO." 但 AUTO 在此场景不可用）。选择 UNKNOWN + 显式 flags 是最保守、最兼容的方案
- `requiredFlags=HOST_VISIBLE` 确保内存可 mapping（`VMA_ALLOCATION_CREATE_MAPPED_BIT` 需要 HOST_VISIBLE 才能生效）
- `preferredFlags=DEVICE_LOCAL` 在独立 GPU 上优先选 BAR 内存（如 Resizable BAR 可用），提升 GPU 端访问性能；若不可用则回退到系统内存
- 集成 GPU 已有 HOST_VISIBLE 默认就是统一内存，`preferredFlags` 无影响
- 不依赖 `bufImgUsage`（不需要知道 buffer/image 类型），与 `vmaAllocateMemory` 兼容

**语义差异说明**:
- Aliased pool 的理想内存类型为纯 `DEVICE_LOCAL`（存放 GPU 内部资源）
- 但当前 stub 阶段 pool 实际未被绑定到资源，此修复的目标仅是消除崩溃
- 完整 aliasing 实现时（资源通过 VMA custom pool 创建），可切换为纯 DEVICE_LOCAL 方案，并配合 staging buffer 处理 CPU 写入
- 当前 HOST_VISIBLE 作为必要妥协（满足 `MAPPED_BIT` 前提），性能差异在 stub 阶段可忽略

### Decision 2: 使用 `VmaAllocationInfo::pMappedData` 替代显式 `MapMemory()` 调用

**选择**: 从 `VulkanMemoryManager::AllocateMemory()` 直接获取 `VmaAllocationInfo`，读取 `pMappedData` 字段

**理由**:
- `VMA_ALLOCATION_CREATE_MAPPED_BIT` 标志已指示 VMA 在分配时自动调用 `vkMapMemory()`
- VMA 通过 `VmaAllocationInfo::pMappedData` 暴露映射指针
- 当前代码在 `AllocateMemory()` 返回后额外调用 `MapMemory()` → `vmaMapMemory()`，而 VMA 内部发现已映射则直接返回 `allocation->GetMappedData()`，该调用完全多余
- 移除冗余调用减少函数调用开销，简化代码

**实现方式**:
```cpp
// 需要扩展 VulkanMemoryManager::AllocateMemory() 返回 VmaAllocationInfo
// 或新增 AllocateMemory(info) 重载，在 VulkanResourceAliasing 中直接读取 pMappedData
```

### Decision 3: 新增 `UpdateAliasedAllocationsMap()` 解决 allocation 句柄时序

**选择**: 在 `AllocateAliasedPool()` 成功后新增 `UpdateAliasedAllocationsMap()` 方法，遍历 `m_AliasedAllocations` 更新每条目的 `allocation` 和 `mappedPtr`

**理由**:
- `AnalyzeAndPlanAliasing()` 在 `AllocateAliasedPool()` 之前运行（`VulkanGraphLocalResourceManager::AllocateAliasedResources()` 第 115-118 行），前者在第 93-97 行将当前 `m_AliasedPoolAllocation`（此时为 `VK_NULL_HANDLE`）写入 `m_AliasedAllocations`
- 即使后续 `AllocateAliasedPool()` 成功分配，`m_AliasedAllocations` 中的条目仍持有 `VK_NULL_HANDLE`
- 有两种修复方案：
  - **方案 A**: 调整调用顺序——先分配池再分析 → 改动 `AllocateAliasedResources()` 结构，不利于后续 pooling 复用逻辑
  - **方案 B**: 新增 `UpdateAliasedAllocationsMap()` ✅ → 最小侵入；保留分析→分配→更新的天然流程；便于后续池复用（分析仅需大小，分配可以复用已有池）

**实现位置**:
- 在 `VulkanResourceAliasing.h` 中声明 `void UpdateAliasedAllocationsMap()`
- 在 `VulkanResourceAliasing.cpp` 中实现，遍历 `m_AliasedAllocations` 更新 `allocation` 和 `mappedPtr`
- 在 `VulkanGraphLocalResourceManager::AllocateAliasedResources()` 中 `AllocateAliasedPool()` 成功后调用

### Decision 4: 保留 aliasing stub 架构

虽然当前 aliased pool 分配后资源未绑定，但保留完整调用链有意义：
- `AnalyzeAndPlanAliasing()` + `AllocateAliasedPool()` + `FreeAliasedPool()` 构成了完整的生命周期框架
- 后续实现真正的 aliasing 绑定时，只需替换资源创建部分（`createBuffer` → VMA custom pool sub-allocation）
- 跳过整个流程会导致调用链断裂，增加后续对接成本

## Risks / Trade-offs

- **[Risk] 独立 GPU 上 HOST_VISIBLE 内存有限，2MB 分配可能仍失败** → 此时 VMA 返回 `VK_ERROR_OUT_OF_DEVICE_MEMORY`（而非 FEATURE_NOT_PRESENT），这是正确的错误传播。后续实现真正 aliasing 时可降级到非 mapped 池 + staging buffer 方案。
- **[Risk] Aliasing stub 每帧分配+释放 2MB，有性能开销** → 当前为正确性修复，性能优化（如跨帧复用池）留待 aliasing 完整实现时处理。
- **[Trade-off] 不跳过 aliasing 流程意味着仍需分配无用内存** → 接受的代价，换取架构完整性。
- **[Risk] VMA_MEMORY_USAGE_UNKNOWN 下需手动确保 HOST_VISIBLE** → 若忘记设置 `requiredFlags=HOST_VISIBLE`，`VMA_ALLOCATION_CREATE_MAPPED_BIT` 将在无 HOST_VISIBLE 的内存类型上失败。需在代码审查中验证 flags 正确性。
- **[Risk] VulkanMemoryManager::AllocateMemory 可能不暴露 VmaAllocationInfo** → 若当前封装不返回 `VmaAllocationInfo`，需扩展接口或新增重载以获取 `pMappedData`。若无法修改 MemoryManager 接口，可回退为保留 `MapMemory` 调用（虽冗余但无副作用）。
- **[Trade-off] 理想内存类型应为纯 DEVICE_LOCAL，当前使用 HOST_VISIBLE** → stub 阶段可接受：pool 未被实际绑定到资源，内存特性不影响渲染正确性。完整 aliasing 实现时需重新评估是否需要 HOST_VISIBLE，或切换为纯 DEVICE_LOCAL + staging buffer 方案。
- **[Note] CPU_TO_GPU deprecated 但 VMA 3.1.0 仍支持** → 使用 UNKNOWN 方案是为了面对未来版本升级时消除 deprecated 风险，而非当前版本无法使用。
