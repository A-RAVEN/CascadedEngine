## 1. 修复 VMA 分配类型（核心崩溃修复）

- [x] 1.1 修改 `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.cpp` 中 `AllocateAliasedPool()` 函数：
  - 将 `allocInfo.usage = VMA_MEMORY_USAGE_AUTO` 改为 `allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN`
  - 新增 `allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT`
  - 新增 `allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`
  - 保留 `allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT`（确保 VMA 自动映射，后续通过 pMappedData 获取指针）
  - 移除显式 `MapMemory()` 调用（见 task 2.x，通过 `VmaAllocationInfo::pMappedData` 赋值 `m_AliasedPoolMappedPtr`）
  - 移除 `FreeAliasedPool()` 中对应的 `UnmapMemory()` 调用

## 2. 移除多余的 MapMemory 调用

- [x] 2.1 检查 `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.h` 中 `AllocateMemory()` 的返回值/输出方式，确认是否能获取 `VmaAllocationInfo`（特别是 `pMappedData` 字段）
- [x] 2.2 若 MemoryManager 已返回 `VmaAllocationInfo`：在 `VulkanResourceAliasing.cpp` 的 `AllocateAliasedPool()` 中，将 `memoryManager.MapMemory(m_AliasedPoolAllocation)` 替换为 `allocInfo.pMappedData`，并移除后续的 `UnmapMemory` 调用（第 161-165 行的 `FreeAliasedPool()` 中）
- [x] 2.3 ~~若 MemoryManager 不返回 `VmaAllocationInfo`~~ → MemoryManager 已有 `VmaAllocationInfo*` 参数，走 2.2 路径，无需新增重载

## 3. 修复 allocation 句柄时序问题

- [x] 3.1 在 `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.h` 的 `VulkanResourceAliasing` 类中，在 `private:` 区域声明新方法：
  ```cpp
  void UpdateAliasedAllocationsMap();
  ```
- [x] 3.2 在 `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.cpp` 中实现 `UpdateAliasedAllocationsMap()`：
  - 遍历 `m_AliasedAllocations` 的每个条目
  - 将条目的 `allocation` 字段更新为 `m_AliasedPoolAllocation`
  - 将条目的 `mappedPtr` 字段更新为 `m_AliasedPoolMappedPtr`
  - 添加日志输出更新结果（如 "Updated N aliased allocations with pool handle"）
- [x] 3.3 在 `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` 的 `AllocateAliasedResources()` 中，第 118 行 `AllocateAliasedPool()` 成功返回后、资源创建循环之前，插入：
  ```cpp
  m_AliasingManager.UpdateAliasedAllocationsMap();
  ```

## 4. 编译验证

- [x] 4.1 运行 `build.py`（或 `build.bat`），验证 BUILD SUCCESSFUL
- [x] 4.2 ~~若编译失败~~ → 首次编译遇到 `access private member` 错误（`UpdateAliasedAllocationsMap` 误放在 `private:`），已修复为 `public:`，重新编译通过

## 5. 运行时验证

- [x] 5.1 运行测试程序（`Test/GPUBackendTester/`），确认不再出现 `VK_ERROR_FEATURE_NOT_PRESENT (-8)` 错误
- [x] 5.2 验证日志中出现 "Allocated aliased pool of size X" 消息（确认池分配成功）
- [x] 5.3 若仍有错误，检查 Vulkan Validation Layer 输出 → Validation layer 报 `VUID-VkImageViewCreateInfo-image-01020`（Image 未绑定内存），这是 aliasing stub 的已知限制，将在 `fix-vulkan-aliasing-resource-binding` 中修复。VMA 分配 flags 组合正确、pool 分配和 mapped pointer 均正常
