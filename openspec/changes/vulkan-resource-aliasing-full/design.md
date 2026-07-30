# Design: Vulkan 资源别名完整实现（VmaVirtualBlock 二阶段分配）

**Change ID**: vulkan-resource-aliasing-full
**Updated**: 2026-07-30（审计修订 v2 — VmaPool → VmaVirtualBlock 架构）

---

## Context

D3D12 `AliasedMemoryAllocator` 使用 D3D12MA `VirtualBlock`（512MB）做虚拟地址子分配 + per-batch alloc/free + `CommitBlock`（`CreatePlacedResource` 物理映射）。Vulkan 等价技术：VMA `VmaVirtualBlock`（`vmaVirtualAllocate`/`vmaVirtualFree`）+ 手动 `vkBindBufferMemory`/`vkBindImageMemory`。

**2026-07-30 基线审查**（35 agent 对抗验证，0 REFUTED）：0/15 任务已开始。发现 3 个前置 bug + VmaPool vs VmaVirtualBlock 语义差异。原 proposal 选 VmaPool 无法实现真正的二阶段 aliasing — 修订为 VmaVirtualBlock。

---

## Goals / Non-Goals

**Goals:**
- VmaVirtualBlock 二阶段分配（Phase 1 虚拟规划 → Phase 2 物理提交）
- Per-batch alloc/free 生命周期（非重叠资源确定性复用 offset）
- 多 VirtualBlock 自动扩展
- Buffer/Image 分离 VirtualBlock（应对 NV GPU independent memory type bits）
- 跨帧 VirtualBlock 复用（保留物理内存，帧末仅重置虚拟空间）

**Non-Goals:**
- 不实现自定义 virtual addressing（依赖 VMA Virtual Block API）
- 不实现 heap type 多态（Vulkan memory type 已覆盖）
- 不实现 defragmentation（首版用 first-fit，后续可加）

---

## Decisions

### D1: VmaVirtualBlock 二阶段分配（替代原 VmaPool 方案）

**选择**：`vmaVirtualAllocate`/`vmaVirtualFree` 管理虚拟 offset，然后一次性分配物理 `VmaAllocation` + 手动 `vkBind*Memory`

```
            D3D12                              Vulkan (本方案)
            ──────                             ────────────────
Phase 1     VirtualBlock::Allocate()           vmaVirtualAllocate()
(CPU only)  → 预留虚拟地址区间                   → 预留虚拟 offset
            VirtualBlock::Free()               vmaVirtualFree()
            → 归还区间，后续资源可复用             → 归还区间，后续资源可复用

Phase 2     CreateHeap → CreatePlaced           vmaAllocateMemory()
(物理提交)   Resource at offset                  → vkBindBufferMemory at offset
```

**备选（已拒绝）**：VmaPool + `vmaCreateBuffer(pool)`。理由：
- VmaPool 是黑盒：VMA 内部管理 free list，无法保证"free A 后新建 B 一定复用 A 的空间"
- 无 offset 可见性：`vmaCreateBuffer` 自动选 offset，应用无法控制
- 与 D3D12 语义不匹配：D3D12 VirtualBlock 的 alloc/free 是确定性的，VmaVirtualBlock 才是等价物

**VMA 版本要求**：Virtual Block API 从 VMA 3.0 开始支持。需确认项目 VMA 版本 ≥ 3.0 且编译了 virtual allocator 支持。若版本不足，升级 VMA 到最新 stable。

### D2: Per-batch 生命周期模式

遵循 D3D12 `GPUResourceStates.cpp:252-283` 的 alloc/free 循环：

```cpp
// 每 batch 执行：
// 1. Allocate new resources for this batch
for (auto& resource : newResourcesOnBatch[batchID])
    AllocResource(resource);  // vmaVirtualAllocate → offset

// 2. Free resources whose lifetime ends at this batch
for (auto& resource : dyingAfterBatch[batchID])
    FreeResource(resource);   // vmaVirtualFree → offset 可复用
```

**关键保证**：Phase 1 的 alloc/free 是纯 CPU 操作，不创建任何 Vulkan 对象。这意味着可以快速反复规划，找到最小物理内存需求后再一次性提交。

### D3: Offset 管理 — 手动跟踪（保持当前 Phase B 模式）

**选择**：手动 `vkCreateBuffer` + `vkBindBufferMemory` 到 VirtualBlock 物理内存的指定 offset，而非让 VMA 自动管理。

**理由**：
- 与当前 Phase A/B 架构一致（已用 `vkBindBufferMemory`）
- FreeResourcesUpToBatch 需要知道哪个 offset 范围属于哪个 batch → 手动跟踪
- VMA `vmaCreateBuffer` with pool 会失去 offset 控制权

**实现**：
```cpp
// Phase 1: 虚拟分配（仅记录 offset，不创建 VkBuffer）
VmaVirtualAllocation va;
uint64_t offset;
vmaVirtualAllocate(virtualBlock, &allocInfo, &offset, &va);
m_BatchAllocations[batchID].push_back({resourceId, offset, va});

// Phase 2: 物理创建（一次性，所有 batch 规划完成后）
VmaAllocation physicalMem;
vmaAllocateMemory(allocator, memReqs, allocInfo, &physicalMem, nullptr);
VkDeviceMemory dm = /* extract from physicalMem */;

for (auto& [resourceId, offset] : m_AllCommittedResources) {
    vkCreateBuffer(device, &bufInfo, nullptr, &buffer);
    vkBindBufferMemory(device, buffer, dm, offset);
}
```

### D4: VirtualBlock 算法选择 — Buddy（VMA 默认）

**选择**：不设 `VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT`，使用默认 buddy algorithm。

**理由**：
- Buddy 支持任意顺序的 `vmaVirtualFree` → 兼容 `FreeResourcesUpToBatch` 的 per-batch free
- Linear algorithm 仅支持全量 reset（`vmaClearVirtualBlock`），无法逐个释放
- Buddy 的碎片化风险：每帧末清理所有虚拟分配（`FreeResourcesUpToBatch` 处理所有剩余资源），然后 `vmaClearVirtualBlock` 重置整个虚拟空间 → 碎片化不会跨帧累积

### D5: Buffer/Image 分离 VirtualBlock

**选择**：`castl::unordered_map<uint32_t, VirtualBlockPool> m_BlockPools`，按 `memoryTypeIndex` 分池。

**理由**：
- 查询 `VkPhysicalDeviceMemoryProperties` 的 `memoryTypeBits`，若 buffer 和 image 返回不同的 memory type bitmask → 使用不同 VirtualBlock
- 集成 GPU（Intel/AMD APU）通常 buffer/image 共享 memory type → 单 pool 足够
- 离散 GPU（NV/AMD）独立 VRAM + system RAM → 需要分离
- D3D12 `AliasedMemoryAllocator` 按 `D3D12_HEAP_TYPE` 分池，此方案等价

### D6: 跨帧复用

**选择**：VirtualBlock 和物理内存从 `VulkanGraphLocalResourceManager`（per-frame）移到 `RenderBackend_Vulkan`（app lifetime）。

- 帧末：`FreeResourcesUpToBatch(LAST_BATCH)` 清理所有资源 handle + `vmaClearVirtualBlock` 重置虚拟空间
- 物理 `VmaAllocation` **保留**不释放 — 复用跨帧
- 若下帧 peak virtual usage 超过当前物理 allocation 大小 → 释放旧 physical allocation，分配新的更大块

---

## Risks / Trade-offs

- **[VMA 版本兼容]**：Virtual Block API 需要 VMA ≥ 3.0。需在 design 阶段确认。若版本不足 → 升级 VMA
- **[Buddy 碎片化]**：帧内 alloc/free 大量小资源可能碎片化 → 帧末 `vmaClearVirtualBlock` 消除跨帧碎片化
- **[物理内存 resize]**：物理 allocation resize 时需要 `vkDeviceWaitIdle` → 仅在 GPU idle 时安全
- **[AddBuffer 延迟绑定]**：`AddBuffer` 在规划完成后被调用时需重新计算 alias → prerequisite task 0.1 修复
- **[Dead-group 累积]**：`AnalyzeAndPlanAliasing` 的 `activeGroups` 永不清理 → prerequisite task 0.2 修复
