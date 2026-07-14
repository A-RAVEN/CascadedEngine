## 1. VmaPool 创建与管理

- [ ] 1.1 `VulkanResourceAliasing` 新增 `VmaPool m_BufferPool` / `VmaPool m_ImagePool` 成员
- [ ] 1.2 实现 `CreatePools(memoryTypeIndex, blockSize)` —— 为 buffer 和 image 分别创建 VmaPool
- [ ] 1.3 实现 `DestroyPools()` —— 释放所有 pool

## 2. Per-batch 生命周期管理

- [ ] 2.1 实现 `AllocBuffer(batchIndex, desc, usage)` → 从 buffer pool 子分配，记录分配 batch
- [ ] 2.2 实现 `AllocImage(batchIndex, desc, access)` → 从 image pool 子分配
- [ ] 2.3 实现 `FreeResourcesUpToBatch(batchIndex)` → 释放生命周期在 batchIndex 结束的所有资源，回收 VMA 子分配空间
- [ ] 2.4 `AllocateAliasedResources` 改为 per-batch 驱动：遍历 batch 列表，每 batch 分配新资源 + 释放过期资源

## 3. 多 Pool 自动扩展

- [ ] 3.1 当 buffer/image pool 子分配失败时（VMA 返回空间不足），自动创建新 pool 追加到 pool 列表
- [ ] 3.2 维护 `castl::vector<VmaPool>` 的 pool 列表

## 4. 跨帧 Pool 复用

- [ ] 4.1 Pool 从 `VulkanGraphLocalResourceManager::Init/Release` 生命周期改为 `RenderBackend_Vulkan` 级别
- [ ] 4.2 帧末调用 `ResetPools()` —— 重置 VMA pool 子分配状态（`vmaResetPool` 或等价操作），保留物理内存
- [ ] 4.3 首帧或 resize 时动态调整 pool block size

## 5. D3D12 对齐验证

- [ ] 5.1 对照 `AliasedMemoryAllocator::AllocateGPUResource` / `FreeVirtualMemmories` / `CommitAllocations` 验证接口等价性
- [ ] 5.2 对照 `VirtualBlock::TryAllocateGPUResource` 验证子分配+空闲回收逻辑

## 6. 编译验证

- [ ] 6.1 运行 `build.py`，验证 BUILD SUCCESSFUL
