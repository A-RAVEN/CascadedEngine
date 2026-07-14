## Why

Vulkan 后端每帧执行 `VulkanGraphExecutor::CompileAndExecute()` 时，`VulkanResourceAliasing::AllocateAliasedPool()` 调用 `vmaAllocateMemory()` 分配 aliased 资源池，因 `VMA_MEMORY_USAGE_AUTO` 与 `vmaAllocateMemory()` 不兼容而返回 `VK_ERROR_FEATURE_NOT_PRESENT (-8)`，导致所有帧 abort。此前 `fix-vulkan-windowhandle-release` 的测试验证任务（4.2/4.3）因此无法通过。

对抗验证发现原始方案（使用 `VMA_MEMORY_USAGE_CPU_TO_GPU`）存在额外问题：该枚举在 VMA 3.1.0 中已标记 deprecated，且语义上 aliased pool 存放 GPU 内部资源而非 CPU→GPU staging 数据。此外 `AnalyzeAndPlanAliasing()` 在 `AllocateAliasedPool()` 之前运行，导致 `m_AliasedAllocations` 中存储的 allocation 为 `VK_NULL_HANDLE`，且多余的 `MapMemory` 调用可被 `VMA_ALLOCATION_CREATE_MAPPED_BIT` 自动处理。

## What Changes

- **修复 VMA aliased pool 分配失败**: 将 `VulkanResourceAliasing::AllocateAliasedPool()` 中的 `VMA_MEMORY_USAGE_AUTO` 改为 `VMA_MEMORY_USAGE_UNKNOWN` + 显式 `requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` + `preferredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`，避免 deprecated API 的同时与 `vmaAllocateMemory()` 兼容
- **修复 allocation 句柄时序问题**: 新增 `UpdateAliasedAllocationsMap()` 方法，在 `AllocateAliasedPool()` 成功后遍历 `m_AliasedAllocations` 更新已存储的 `VK_NULL_HANDLE` 为实际的 `VmaAllocation` 和 `mappedPtr`
- **移除多余的 MapMemory 调用**: `VMA_ALLOCATION_CREATE_MAPPED_BIT` 已使 VMA 自动通过 `VmaAllocationInfo::pMappedData` 返回映射指针，无需显式 `vmaMapMemory()`
- **确认 aliased pool 当前为 stub 实现**: 资源实际通过独立的 `device.createBuffer()/createImage()` 分配，未绑定到 aliased pool。Aliasing 池仅分配但未使用，后续需完整实现资源绑定

## Capabilities

### New Capabilities
- `vulkan-resource-aliasing`: Vulkan 图形帧临时资源的 aliasing 内存分配——分配策略、内存类型选择、与 VMA 的兼容性约束

### Modified Capabilities
- （无）

## Impact

- `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.cpp` — `AllocateAliasedPool()` 中修改 `VmaAllocationCreateInfo::usage` 和 `flags`，移除 `MapMemory` 调用，使用 `VmaAllocationInfo::pMappedData` 替代
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.h` — 新增 `UpdateAliasedAllocationsMap()` 声明
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` — `AllocateAliasedResources()` 中在 `AllocateAliasedPool()` 成功后调用 `UpdateAliasedAllocationsMap()`
- 修复后帧不再因 VMA 分配失败而 abort，测试可正常完成执行流程
- 不影响 D3D12 后端（独立实现）
- 使用 `VMA_MEMORY_USAGE_UNKNOWN` 替代 deprecated 的 `VMA_MEMORY_USAGE_CPU_TO_GPU`，避免未来 VMA 版本升级时的兼容性风险
