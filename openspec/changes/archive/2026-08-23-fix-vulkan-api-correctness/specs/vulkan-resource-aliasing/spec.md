## MODIFIED Requirements

### Requirement: Aliased Pool 使用与 vmaAllocateMemory 兼容的 VMA 内存类型

`VulkanResourceAliasing::AllocateAliasedPool()` SHALL 使用 `VMA_MEMORY_USAGE_UNKNOWN` + 显式 `requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` + `preferredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`（而非 `VMA_MEMORY_USAGE_AUTO` 或 deprecated `VMA_MEMORY_USAGE_CPU_TO_GPU`）作为 `VmaAllocationCreateInfo` 配置，确保与 `vmaAllocateMemory()` API 兼容且不依赖 deprecated 枚举。内存类型选择 SHALL 基于 `vkGetPhysicalDeviceMemoryProperties` 的查询结果，SHALL NOT 硬编码 memory type index（如 0/1）。

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

#### Scenario: 虚拟块内存类型来自查询结果

- **GIVEN** 设备内存类型数量 < 2，或类型 0/1 不满足 `HOST_VISIBLE`
- **WHEN** `CreateVirtualBlocks` 创建虚拟块
- **THEN** 使用 `vkGetPhysicalDeviceMemoryProperties` 查询出的合法内存类型，而非硬编码索引 0/1

### Requirement: VmaAllocation SHALL be converted to VkDeviceMemory for binding

`VulkanResourceAliasing::AliasedAllocation` SHALL 包含 `VkDeviceMemory deviceMemory` 字段。`vkBindBufferMemory` 和 `vkBindImageMemory` 调用 SHALL 使用该 `deviceMemory`，并 SHALL 使用 `VmaAllocationInfo.offset` 作为绑定偏移（`deviceMemory + offset`），SHALL NOT 丢弃 offset 一律绑定到 `deviceMemory + 0`。`ManagedGPUResource.aliasedOffset` 等消费方 SHALL 存储池内计划偏移（不含 VMA block offset；GPU 绑定在绑定处统一追加 `GetPoolBlockOffset()`，CPU 映射指针 `pMappedData` 已含 block offset），保证 CPU/GPU 视图一致。

#### Scenario: 别名池内资源绑定偏移正确

- **GIVEN** `AllocateAliasedPool` 返回的 `VmaAllocationInfo.offset` 非 0（池内非首个 allocation）
- **WHEN** 资源绑定 `vkBindBufferMemory`/`vkBindImageMemory`
- **THEN** 绑定偏移为 `deviceMemory + VmaAllocationInfo.offset`
- **AND** 资源实际可访问且数据互不覆盖

#### Scenario: offset 传播到消费方

- **GIVEN** `CommitVirtualAllocations` 提交虚拟分配
- **WHEN** `VulkanGraphLocalResourceManager` 执行绑定
- **THEN** 其使用的 offset 与 VMA 返回的 allocation offset 相等

## ADDED Requirements

### Requirement: 虚拟块销毁前必须释放全部虚拟分配

`DestroyVirtualBlocks()` SHALL 在调用 `vmaDestroyVirtualBlock` 前，对块内所有未释放的虚拟分配调用 `vmaVirtualFree`（或 `vmaClearVirtualBlock`），SHALL NOT 带活分配直接销毁虚拟块。`m_ActiveVirtualAllocs` 的清理 SHALL 发生在虚拟块销毁之前。

#### Scenario: 存在活分配时销毁虚拟块

- **GIVEN** `m_ActiveVirtualAllocs` 中仍有未释放的虚拟分配
- **WHEN** `DestroyVirtualBlocks()` 被调用
- **THEN** 每个活分配先被 `vmaVirtualFree`
- **AND** 之后才调用 `vmaDestroyVirtualBlock`
- **AND** 不触发 VMA 校验错误（validation / -8 类失败）

### Requirement: 别名池绑定越界必须被拒绝

`BindBufferToAliasedPool`（及等价 late 注册路径）SHALL 在计算出的 `alignedOffset + vkMemReqs.size > GetTotalAliasedSize()`（即资源超出池大小）时拒绝绑定：输出诊断日志并返回失败，SHALL NOT 调用 `vkBindBufferMemory` 绑定超出池大小的偏移。

#### Scenario: 池分配后注册新 buffer 且偏移越界

- **GIVEN** aliased pool 已按 `totalSize` 分配，之后 `AddBuffer` 注册新 buffer 且其对齐后偏移 ≥ 池大小
- **WHEN** 执行绑定
- **THEN** 打印 `CA_LOG_ERR` 诊断
- **AND** 不执行越界 `vkBindBufferMemory` 调用

### Requirement: VMA 分配失败必须被检查并传播

所有 VMA 分配调用（`vmaCreateBuffer`/`vmaCreateImage`/`vmaAllocateMemory` 等）的返回值 SHALL 被检查。失败时 SHALL 输出诊断并返回失败/空句柄给调用方，调用方 SHALL 在继续使用前验证分配结果非空。

#### Scenario: vmaCreateBuffer 失败

- **GIVEN** 显存不足或内存类型不满足导致 `vmaCreateBuffer` 返回非 `VK_SUCCESS`
- **WHEN** 分配路径返回
- **THEN** 资源条目不存储空分配
- **AND** 后续使用（bind/update）在无效分配上被跳过并记录错误
