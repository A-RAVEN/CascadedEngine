# Tasks: Vulkan 资源别名完整实现（VmaVirtualBlock 二阶段分配）

**Change ID**: vulkan-resource-aliasing-full
**Updated**: 2026-07-30（审计修订 v2 — VmaPool → VmaVirtualBlock，新增 Section 0 前置修复）

---

## 0. 前置 Bug 修复（审计发现，阻断性）

- [ ] 0.1 **AddBuffer 内存冲突修复** `VulkanGraphLocalResourceManager.cpp` — `AddBuffer`（line 142-156）：当 pool 已分配后 `AddBuffer` 被调用时，`BindBufferToAliasedPool` 使用 `GetAliasedAllocation(id)` 获取 offset，但新资源从未经过 `AnalyzeAndPlanAliasing`，返回默认 `AliasedAllocation{offset=0}` → 静默绑定到 offset 0 与其他资源冲突。修复：在 `BindBufferToAliasedPool` 中为新增资源计算正确 offset（调用 `AnalyzeAndPlanAliasing` 增量更新或直接计算 aligned offset 追加到 pool 末尾）【F1, HIGH】

- [ ] 0.2 **贪心算法 dead-group 清理** `VulkanResourceAliasing.cpp` — `AnalyzeAndPlanAliasing`（line 56-101）：`activeGroups` 只增不减，即使所有资源生命周期已结束。修复：在每轮迭代后移除 `endBatch < 当前资源 startBatch` 的 group【F4, MEDIUM】

- [ ] 0.3 **Pool size 上界检查** `VulkanResourceAliasing.cpp` — `AllocateAliasedPool`（line 150-191）：`ReplanWithRealAlignment` 可能使 `m_TotalAliasedSize` 超过设备 `maxMemoryAllocationSize`。修复：分配前检查 `m_TotalAliasedSize` 是否在设备限制内，超限时返回错误【F5, MEDIUM】

---

## 1. VmaVirtualBlock 创建与管理（Phase 1 基础设施）

- [ ] 1.1 确认项目 VMA 版本 ≥ 3.0 且 Virtual Block API 可用。检查 `vk_mem_alloc.h` 中 `VmaVirtualBlock`、`vmaCreateVirtualBlock`、`vmaVirtualAllocate`、`vmaVirtualFree`、`vmaDestroyVirtualBlock`、`vmaClearVirtualBlock` 是否可用。版本不足时升级 VMA。

- [ ] 1.2 `VulkanResourceAliasing` 新增 VirtualBlock 管理结构：
  ```cpp
  struct VirtualBlockPool {
      VmaVirtualBlock virtualBlock;
      VmaAllocation physicalAllocation;  // Phase 2 物理内存（跨帧复用）
      uint64_t physicalSize;
      uint32_t memoryTypeIndex;
      bool isBufferPool;  // true=buffer, false=image
  };
  castl::unordered_map<uint32_t, VirtualBlockPool> m_BlockPools;  // key=memoryTypeIndex
  ```

- [ ] 1.3 实现 `CreateVirtualBlocks(memoryTypeBits, blockSize)` — 为每种 memory type 创建 VirtualBlock（默认 256MB，对标 D3D12 的 512MB 但保守起步）

- [ ] 1.4 实现 `DestroyVirtualBlocks()` — 释放所有 VirtualBlock + 物理 allocation

---

## 2. Phase 1 — 虚拟分配（Per-batch 生命周期）

- [ ] 2.1 实现 `AllocResource(batchIndex, resourceId, size, alignment, isBuffer)` → 选择正确的 VirtualBlock（按 memoryTypeIndex + isBuffer），调用 `vmaVirtualAllocate`，记录 `(offset, VmaVirtualAllocation, batchIndex)` 到 `m_ActiveVirtualAllocations`

- [ ] 2.2 实现 `FreeResourcesUpToBatch(batchIndex)` → 遍历 `m_ActiveVirtualAllocations`，对所有 `batchIndex <= endBatch` 的资源调用 `vmaVirtualFree`。释放的虚拟区间立即可供后续 `AllocResource` 复用 — 这是 aliasing 的核心

- [ ] 2.3 `VulkanGraphLocalResourceManager::AllocateAliasedResources` 改为 per-batch 驱动：
  1. 构建 `newResourcesOnBatch[batchID]` / `dyingAfterBatch[batchID]`（复用 `BuildResourceUsageRanges` 的现有数据）
  2. 遍历 batch 列表：
     - `AllocResource(batchID, ...)` 分配此 batch 新资源
     - `FreeResourcesUpToBatch(batchID)` 释放此 batch 结束的资源
  3. 记录每个资源的 virtual offset 到 `m_BatchAllocationMap`

- [ ] 2.4 在 Phase 1 完成后计算 peak virtual usage（`vmaGetVirtualBlockStatistics`），确定 Phase 2 物理 allocation 大小

---

## 3. Phase 2 — 物理提交

- [ ] 3.1 实现 `CommitVirtualAllocations()`：为每个 VirtualBlock 分配物理 `VmaAllocation`（大小 = peak virtual usage），存储到 `VirtualBlockPool::physicalAllocation`

- [ ] 3.2 实现 `BindResourcesToPhysicalMemory()`：遍历所有已规划资源，创建 `VkBuffer`/`VkImage`，用 `vkBindBufferMemory`/`vkBindImageMemory` 绑定到物理内存的对应 virtual offset

- [ ] 3.3 物理 allocation resize 逻辑：若当前帧 peak virtual usage > 已有 `physicalAllocation` 大小 → `vkDeviceWaitIdle` → 释放旧 allocation → 分配新 allocation → 重新 bind 所有资源

---

## 4. 多 VirtualBlock 自动扩展

- [ ] 4.1 当 VirtualBlock 内 `vmaVirtualAllocate` 失败（返回 `VK_ERROR_OUT_OF_DEVICE_MEMORY`）时，创建同 memoryType 的新 VirtualBlock（大小 = 当前 block × 1.5），追加到 pool 列表

- [ ] 4.2 维护 `castl::vector<VirtualBlockPool> m_BlockPoolList` per memoryType（替代 1.2 的简单 unordered_map，支持同 memoryType 多 block）

---

## 5. 跨帧复用

- [ ] 5.1 将 `VulkanResourceAliasing` 所有权从 `VulkanGraphLocalResourceManager`（per-frame `VulkanGraphExecutor` 成员）移到 `RenderBackend_Vulkan`（app lifetime member）

- [ ] 5.2 实现 `ResetVirtualBlocks()`（帧末调用）：
  - 释放所有 `VkBuffer`/`VkImage` handle（resources 本身是 per-frame 的）
  - 调用 `vmaClearVirtualBlock` 重置所有虚拟 block 到空状态
  - 保留物理 `VmaAllocation` — 跨帧复用

- [ ] 5.3 首帧动态确定 VirtualBlock 初始大小（基于资源估算 × 2 作为安全边际）。后续帧基于历史 peak 调整

---

## 6. Buffer/Image 分离

- [ ] 6.1 查询 `VkPhysicalDeviceMemoryProperties` 获取 buffer 和 image 的 `memoryTypeBits`。若不同 → 分别创建 VirtualBlock。相同的 memoryType 可共享 block

- [ ] 6.2 `AllocResource` 中根据 `isBuffer` 参数选择正确的 VirtualBlock pool

---

## 7. D3D12 对齐验证

- [ ] 7.1 对照 `AliasedMemoryAllocator::AllocateGPUResource` / `FreeVirtualMemmories` / `CommitAllocations`（`D3D12RenderBackend/private/ResourceManagment/MemoryManager.cpp:161-261`）验证接口等价性

- [ ] 7.2 对照 `VirtualBlock::TryAllocateGPUResource`（`MemoryManager.cpp:263-371`）验证子分配+空闲回收逻辑

---

## 8. 编译验证与测试

- [ ] 8.1 运行 `build.py`，确认 BUILD SUCCESSFUL
- [ ] 8.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30`，确认不崩溃且 aliasing 生效（验证 peak memory < total unaliased memory）
