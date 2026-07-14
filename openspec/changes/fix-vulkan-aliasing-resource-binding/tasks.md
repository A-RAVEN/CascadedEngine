## 1. 新增辅助函数（格式 & 内存基础设施）

- [ ] 1.1 在 `Interface/RenderInterface/header/Common.h` 中实现 `GetFormatBlockSize(ETextureFormat)` 函数——内联 constexpr 函数，对枚举中每种格式返回正确的 bytes-per-pixel/block（RGBA8=4, RGB32=16, D24S8=4 等），fallback 返回 4
- [ ] 1.2 在 `Interface/RenderInterface/header/Common.h` 中实现 `IsCompressedFormat(ETextureFormat)` 函数——constexpr 返回 `false`（当前 `ETextureFormat` 枚举不含 BCn 格式，保留框架以便后续扩展）
- [ ] 1.3 在 `VulkanMemoryManager` 中新增 `void GetAllocationInfo(VmaAllocation allocation, VmaAllocationInfo* outInfo) const` 方法——内部调用 `vmaGetAllocationInfo(m_Allocator, allocation, outInfo)`，文件 `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.h` 和 `.cpp`

## 2. VulkanResourceAliasing 扩展

- [ ] 2.1 在 `VulkanResourceAliasing::AliasedAllocation` 结构体中新增 `VkDeviceMemory deviceMemory = VK_NULL_HANDLE` 字段（`VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.h` 第 22-28 行）
- [ ] 2.2 修改 `VulkanResourceAliasing::AllocateAliasedPool()`——在 `vmaAllocateMemory` 成功后调用 `memoryManager.GetAllocationInfo(m_AliasedPoolAllocation, &allocInfo)` 提取 `VkDeviceMemory`，遍历 `m_AliasedAllocations` 将所有 entry 的 `deviceMemory` 设置为 `allocInfo.deviceMemory`，并将 aliased pool 的 `deviceMemory` 也存入新增的成员 `m_AliasedPoolDeviceMemory`（`VulkanResourceAliasing.cpp`）
- [ ] 2.3 在 `VulkanResourceAliasing` 中新增方法 `void ReplanWithRealAlignment(castl::unordered_map<uint64_t, VkMemoryRequirements> const& realMemReqs)`——用真实对齐和大小覆盖 `m_ResourceLifetimes` 中每个资源的 `alignment` 和 `size` 字段，重新运行贪心区间调度算法重新计算 `aliasedAlloc.offset` 和 `m_TotalAliasedSize`（`VulkanResourceAliasing.h` + `.cpp`）
- [ ] 2.4 在 `VulkanResourceAliasing` 中新增 `bool IsPoolAllocated() const` 方法——返回 `m_AliasedPoolAllocation != VK_NULL_HANDLE`（`VulkanResourceAliasing.h`）
- [ ] 2.5 在 `VulkanResourceAliasing` 中新增 `void* GetMappedPtr() const` 方法——返回 `m_AliasedPoolMappedPtr`（`VulkanResourceAliasing.h`）
- [ ] 2.6 在 `VulkanResourceAliasing` 中新增 `VkDeviceMemory GetPoolDeviceMemory() const` 方法——返回 `m_AliasedPoolDeviceMemory`（`VulkanResourceAliasing.h`）

## 3. ManagedGPUResource 结构调整

- [ ] 3.1 在 `ManagedGPUResource` 结构体中添加 `uint64_t aliasedOffset = 0` 字段（`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.h` 第 28-36 行之间）

## 4. 纹理格式、aspectMask、usage flags 动态推导辅助函数

- [ ] 4.1 在 `VulkanGraphLocalResourceManager.cpp` 匿名 namespace 中实现 `static vk::ImageAspectFlags GetImageAspectMask(ETextureFormat format)`——深度模板格式（`FormatHasStencil`）返回 `eDepth | eStencil`，深度格式（`FormatHasDepth`）返回 `eDepth`，其他返回 `eColor`，复用 `Common.h` 已有的 `FormatHasDepth`/`FormatHasStencil`
- [ ] 4.2 在 `VulkanGraphLocalResourceManager.cpp` 匿名 namespace 中实现 `static vk::ImageUsageFlags GetTextureImageUsage(ETextureAccessTypeFlags access)`——按位映射 `eSampled→eSampled, eRT→eColorAttachment, eDepthStencil→eDepthStencilAttachment, eTransferSrc→eTransferSrc, eTransferDst→eTransferDst, eUnorderedAccess→eStorage`，若 `access` 为空则默认 `eSampled | eColorAttachment`
- [ ] 4.3 在 `VulkanGraphLocalResourceManager.cpp` 匿名 namespace 中实现 `static vk::BufferUsageFlags GetBufferUsageFlags(EBufferUsageFlags usage)`——按位映射 `eVertexBuffer→eVertexBuffer, eIndexBuffer→eIndexBuffer, eStructuredBuffer→eStorageBuffer, eConstantBuffer→eUniformBuffer, eDataSrc→eTransferSrc, eDataDst→eTransferDst, eUnorderedAccess→eStorageBuffer`，始终包含 `eTransferDst | eTransferSrc`
- [ ] 4.4 在 `VulkanGraphLocalResourceManager.cpp` 匿名 namespace 中实现 `static uint64_t CalculateTextureSize(GPUTextureDescriptor const& desc)`——使用 `GetFormatBlockSize` 和 `IsCompressedFormat`，按 mip level 面积递减求和，最后乘 `desc.layers`

## 5. 纹理大小估算修正

- [ ] 5.1 修改 `RegisterTemporaryTexture()` 中的纹理大小估算（`VulkanGraphLocalResourceManager.cpp` 第 90 行）——将 `desc.width * desc.height * 4 * desc.mipLevels * desc.layers` 替换为 `CalculateTextureSize(desc)`

## 6. 两阶段资源绑定 — Phase A（获取真实对齐）

- [ ] 6.1 在 `VulkanGraphLocalResourceManager` 中新增私有方法 `PhaseA_CreateTempResourcesAndGetReqs(std::unordered_map<uint64_t, VkMemoryRequirements>& outMemReqs)`——遍历 `m_LocalResources`，对每个 buffer 调用 `vkCreateBuffer` (VkBufferCreateInfo 不绑定内存) + `vkGetBufferMemoryRequirements`，对每个 image 调用 `vkCreateImage` (VkImageCreateInfo 不绑定内存) + `vkGetImageMemoryRequirements`，将结果存入 `outMemReqs`，立即 destroy 临时 handle
- [ ] 6.2 在 `AllocateAliasedResources()` 中调用 `PhaseA_CreateTempResourcesAndGetReqs()`，将结果传入 `m_AliasingManager.ReplanWithRealAlignment()` 重新规划偏移
- [ ] 6.3 Phase A 中的 `VkBufferCreateInfo` 和 `VkImageCreateInfo` 填充逻辑提炼为独立辅助函数，供 Phase A 和 Phase B 共用（避免重复代码）——新增加 `FillBufferCreateInfo(GraphLocalResource const&, VkBufferCreateInfo&)` 和 `FillImageCreateInfo(GraphLocalResource const&, VkImageCreateInfo&)` 两个静态函数

## 7. 两阶段资源绑定 — Phase B（绑定到 aliased pool）

- [ ] 7.1 在 `AllocateAliasedResources()` 中——Phase A 规划完成后，调用 `m_AliasingManager.AllocateAliasedPool(m_AliasingManager.GetTotalAliasedSize())` 分配 pool
- [ ] 7.2 从 `m_AliasingManager` 获取 `VkDeviceMemory deviceMemory = m_AliasingManager.GetPoolDeviceMemory()` 和 `void* poolMappedPtr = m_AliasingManager.GetMappedPtr()`
- [ ] 7.3 遍历 `m_LocalResources`，对 buffer 类型：用共用函数 `FillBufferCreateInfo` 创建 `VkBufferCreateInfo` → `vkCreateBuffer`（不绑定内存）→ 获取 `aliasedAlloc = m_AliasingManager.GetAliasedAllocation(id)` → 计算 `alignedOffset = (aliasedAlloc.offset + memReqs.alignment - 1) & ~(memReqs.alignment - 1)` → `vkBindBufferMemory(rawBuffer, deviceMemory, alignedOffset)` → 包装为 `vk::Buffer` → 存入 `managed`
- [ ] 7.4 遍历 `m_LocalResources`，对 texture 类型：用共用函数 `FillImageCreateInfo` 创建 `VkImageCreateInfo`（使用 `VulkanTexture::ConvertFormat` 获取 format，使用 `GetTextureImageUsage` 推导 usage flags）→ `vkCreateImage`（不绑定内存）→ 计算 alignedOffset → `vkBindImageMemory(rawImage, deviceMemory, alignedOffset)` → 包装为 `vk::Image` → 存入 `managed`
- [ ] 7.5 对每个 texture 创建 ImageView：使用 `GetImageAspectMask` 动态设置 `viewInfo.subresourceRange.aspectMask`，format 与 image format 一致
- [ ] 7.6 对每个 `ManagedGPUResource` 设置 `managed.aliasedOffset = alignedOffset` 和 `managed.mappedPtr = static_cast<uint8_t*>(poolMappedPtr) + alignedOffset`

## 8. AddBuffer() 纳入 aliasing 系统

- [ ] 8.1 修改 `VulkanGraphLocalResourceManager::AddBuffer()`（`VulkanGraphLocalResourceManager.cpp` 第 44-76 行）——调用 `RegisterTemporaryBuffer()` 后检查 `m_AliasingManager.IsPoolAllocated()`
  - 若已分配：执行 Phase A（单资源）获取真实对齐 → 调用 `ReplanWithRealAlignment` 更新偏移 → 将 buffer 绑定到 aliased pool（复用 Phase B 的 buffer 创建和绑定逻辑，提取为公共私有方法 `BindBufferToAliasedPool(uint64_t id)`）
  - 若未分配：仅注册，由后续 `AllocateAliasedResources()` 统一处理
- [ ] 8.2 确保 `AddBuffer()` 中对 `m_TotalMemoryUsed` 的追踪仅统计 aliased pool 使用量，而非重复计算独立分配大小

## 9. 资源销毁修正

- [ ] 9.1 修改 `ReleaseAllResources()`（`VulkanGraphLocalResourceManager.cpp` 第 236-263 行）——确认 `device.destroyBuffer(resource.buffer)` 和 `device.destroyImage(resource.image)` 保持不变（handle destroy 正确）；确认没有间接调用 `vmaDestroyBuffer`/`vmaDestroyImage`/`vmaFreeMemory`；aliased pool 由 `m_AliasingManager.FreeAliasedPool()` 统一释放（第 260 行，已存在）

## 10. Buffer usage flags 动态推导

- [ ] 10.1 修改 Phase A 和 Phase B 中 buffer 创建的 `VkBufferCreateInfo::usage` 字段——从硬编码的 `eTransferDst | eVertexBuffer` 改为调用 `GetBufferUsageFlags(localResource.bufferUsage)` 动态推导（使用任务 4.3 添加的辅助函数）

## 11. 编译验证与修复

- [ ] 11.1 运行 `build.py` 编译项目，若失败则分析编译错误并修复，直到 BUILD SUCCESSFUL
- [ ] 11.2 验证 Vulkan Validation Layer 无 aspectMask 相关 VUID 错误（特别关注深度纹理场景）
