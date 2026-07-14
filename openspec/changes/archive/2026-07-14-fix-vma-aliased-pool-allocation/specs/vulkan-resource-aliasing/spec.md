## ADDED Requirements

### Requirement: Aliased Pool 使用与 vmaAllocateMemory 兼容的 VMA 内存类型

`VulkanResourceAliasing::AllocateAliasedPool()` SHALL 使用 `VMA_MEMORY_USAGE_UNKNOWN` + 显式 `requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` + `preferredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`（而非 `VMA_MEMORY_USAGE_AUTO` 或 deprecated `VMA_MEMORY_USAGE_CPU_TO_GPU`）作为 `VmaAllocationCreateInfo` 配置，确保与 `vmaAllocateMemory()` API 兼容且不依赖 deprecated 枚举。

#### Scenario: 独立 GPU 上分配 aliased pool 成功

- **GIVEN** 系统有独立 GPU（如 NVIDIA/AMD 独显）
- **WHEN** `AllocateAliasedPool(totalSize)` 被调用
- **THEN** VMA 选择 `HOST_VISIBLE` 内存类型（优先 `DEVICE_LOCAL | HOST_VISIBLE` BAR 内存，回退到纯 `HOST_VISIBLE` 系统内存）
- **AND** 分配成功返回 `VK_SUCCESS`
- **AND** `m_AliasedPoolMappedPtr` 为非空指针

#### Scenario: 集成 GPU 上分配 aliased pool 成功

- **GIVEN** 系统有集成 GPU（如 Intel UHD/Arc 集成显卡）
- **WHEN** `AllocateAliasedPool(totalSize)` 被调用
- **THEN** VMA 选择统一内存（`DEVICE_LOCAL | HOST_VISIBLE`）
- **AND** 分配成功返回 `VK_SUCCESS`

#### Scenario: 内存不足时正确传播错误

- **GIVEN** HOST_VISIBLE 内存不足以满足 `totalSize`
- **WHEN** `AllocateAliasedPool(totalSize)` 被调用
- **THEN** 返回 `VK_ERROR_OUT_OF_DEVICE_MEMORY`（而非 `VK_ERROR_FEATURE_NOT_PRESENT`）
- **AND** 调用方通过 `AllocateAliasedResources() → return false` 正确处理错误

### Requirement: Aliased Pool 支持 Persistent Mapping（无显式 MapMemory 调用）

`AllocateAliasedPool()` SHALL 使用 `VMA_ALLOCATION_CREATE_MAPPED_BIT` flag 创建 persistently mapped 分配，并通过 `VmaAllocationInfo::pMappedData` 直接获取映射指针，不调用显式的 `vmaMapMemory()`。

#### Scenario: 分配后可通过 VmaAllocationInfo 直接获取 mapped 指针

- **GIVEN** `AllocateAliasedPool()` 成功返回
- **WHEN** 调用方访问 `m_AliasedPoolMappedPtr`
- **THEN** 指针为非空，可安全读写（无需手动 map/unmap）
- **AND** 代码中不存在对 `MapMemory()` 的调用

### Requirement: Aliased Pool 分配后 allocation 句柄正确传播到已分析条目

`AllocateAliasedPool()` 成功后，`m_AliasedAllocations` 中所有条目 SHALL 持有有效的 `VmaAllocation` 句柄（非 `VK_NULL_HANDLE`）和有效的 `mappedPtr`（非 `nullptr`）。

#### Scenario: AnalyzeAndPlanAliasing 先于 AllocateAliasedPool 执行时 allocation 被正确更新

- **GIVEN** `AnalyzeAndPlanAliasing()` 已在 `AllocateAliasedPool()` 之前执行，并在 `m_AliasedAllocations` 中写入了条目的 `allocation = VK_NULL_HANDLE`
- **WHEN** `AllocateAliasedPool()` 成功后调用 `UpdateAliasedAllocationsMap()`
- **THEN** `m_AliasedAllocations` 中所有条目的 `allocation` 字段为非 `VK_NULL_HANDLE`
- **AND** 所有条目的 `mappedPtr` 字段为非 `nullptr`
- **AND** 调用 `GetAliasedAllocation(resourceId)` 时返回的 `AliasedAllocation` 含有有效的 `allocation` 和 `mappedPtr`
