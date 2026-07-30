# Proposal: Vulkan 资源别名完整实现（VmaVirtualBlock 二阶段分配）

**Change ID**: vulkan-resource-aliasing-full
**Status**: Proposed
**Created**: 2026-07-12
**Updated**: 2026-07-30（审计修订 v2 — VmaPool → VmaVirtualBlock 架构）

---

## Why

当前 Vulkan aliasing 实现虽经 `fix-vma-aliased-pool-allocation` 和 `fix-vulkan-aliasing-resource-binding` 修复后可分配 aliased pool 并绑定资源，但距离 D3D12 的 `AliasedMemoryAllocator` 完整实现仍有架构差距：

- D3D12 使用 `VirtualBlock`（512MB）做**真正的二阶段分配**：Phase 1 虚拟分配（CPU-only），Phase 2 物理提交（`CreatePlacedResource`）。非重叠资源在虚拟地址层面自动复用
- D3D12 多 block 自动扩展（`VirtualBlock` 满时创建新 block）
- D3D12 分离不同 `D3D12_HEAP_TYPE`（DEFAULT / UPLOAD）
- Vulkan 当前只有一个裸 VMA allocation + 手动 offset 计算，无二阶段分配、无空闲区间回收、无 buffer/image 分离

**审计新发现**（2026-07-30 对抗验证，3 HIGH + 3 MEDIUM）：
- **F1 (HIGH)**: `AddBuffer` 在 pool 已分配后静默绑定 offset=0，与其他资源内存冲突
- **F2 (HIGH)**: VmaVirtualBlock 在 VMA 中可用，完美映射 D3D12 VirtualBlock 语义
- **F3 (MEDIUM)**: 当前贪心算法 dead-group 累积浪费内存
- **F4 (MEDIUM)**: `ReplanWithRealAlignment` pool size 无上界检查
- **F5 (MEDIUM)**: Buffer/Image 分离缺失导致 NV GPU 上走 Route B 降级

## What Changes

### 核心架构升级：VmaVirtualBlock 二阶段分配

用 VMA `VmaVirtualBlock`（而非 `VmaPool`）实现 D3D12 同级的两阶段分配：

- **Phase 1 — 虚拟规划**（CPU-only，不碰 GPU）：`vmaVirtualAllocate` 在虚拟 block 内预留 offset。Per-batch alloc/free 循环中，非重叠资源自动复用相同的虚拟 offset → **真正的 aliasing**
- **Phase 2 — 物理提交**（一次性）：分配一块物理 `VmaAllocation`（大小 = 虚拟空间峰值），所有资源 `vkBindBufferMemory`/`vkBindImageMemory` 到同一物理内存的不同 offset

### 对比：VmaPool vs VmaVirtualBlock

| | VmaPool（原 proposal） | VmaVirtualBlock（修订） |
|---|---|---|
| Offset 可见性 | 黑盒（VMA 内部管理） | 白盒（应用完全掌控） |
| 确定性复用 | 无保证 | 显式 alloc/free，offset 可预测 |
| D3D12 等价性 | 部分（语义不同） | 完全等价 |
| Per-batch free | 需要遍历 VMA 分配 | 显式 `vmaVirtualFree` |
| 碎片化风险 | 依赖 VMA 内部实现 | 应用可控 |

### 前置修复（Section 0）

- **AddBuffer 内存冲突**：pool 已分配后 AddBuffer 不重新规划 offset → 绑定到 offset=0 冲突
- **Dead-group 累积**：贪心算法 `activeGroups` 只增不减 → 内存浪费
- **Pool size guard**：`ReplanWithRealAlignment` 无上限检查

## Capabilities

### New Capabilities
- `vulkan-resource-aliasing-full`: 基于 VmaVirtualBlock 的二阶段分配、per-batch 生命周期、多 block 扩展、跨帧复用、buffer/image 分离

### Modified Capabilities
- `vulkan-resource-aliasing`: 从"单 pool 手动 bind"升级为"VmaVirtualBlock 二阶段分配"

## Impact

- `VulkanResourceAliasing` — 重构为 VmaVirtualBlock 管理 + 多 block 列表
- `VulkanGraphLocalResourceManager` — AllocateAliasedResources 改为 per-batch 驱动
- D3D12 `AliasedMemoryAllocator` 作为参考架构

## Non-goals

- 不实现 virtual addressing（依赖 VMA Virtual Block API）
- 不修改 D3D12 端
- 不引入新的第三方依赖（VMA 已集成）
