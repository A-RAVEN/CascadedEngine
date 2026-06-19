# vma-vulkan-function-binding Specification

## Purpose
TBD - created by archiving change fix-vma-null-function-pointer. Update Purpose after archive.
## Requirements
### Requirement: VMA 初始化时获得 Vulkan 函数指针

系统 SHALL 在所有 `vmaCreateAllocator` 调用点通过 `VmaAllocatorCreateInfo::pVulkanFunctions` 传入至少包含 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 的 `VmaVulkanFunctions` 结构体，使得 VMA 能够通过动态加载获取所有所需的 Vulkan 函数指针。

#### Scenario: VMA allocator 创建不再崩溃

- **WHEN** Vulkan 后端初始化，调用 `VulkanMemoryManager::Init()` 创建 VMA allocator
- **THEN** `vmaCreateAllocator` 返回 `VK_SUCCESS` 而非触发 null 函数指针访问违例

### Requirement: 可复用的 VMA 函数指针填充工具

系统 SHALL 提供 `FillVmaVulkanFunctions` 工具函数，先将 `VmaVulkanFunctions` 结构体清零，再从已初始化的 `vk::defaultDispatchLoaderDynamic` 中提取 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 指针填充。

#### Scenario: FillVmaVulkanFunctions 填充引导函数

- **WHEN** 调用 `FillVmaVulkanFunctions(functions)`，其中 `vk::defaultDispatchLoaderDynamic` 已完成初始化
- **THEN** `functions.vkGetInstanceProcAddr` 和 `functions.vkGetDeviceProcAddr` 均为非空指针，其余成员均为 null（由 VMA 自行动态解析）

### Requirement: 统一使用 VulkanMemoryManager 持久 allocator

系统 SHALL 使所有 VMA 内存分配（Buffer、Image、裸内存）统一通过 `VulkanMemoryManager` 的持久 `VmaAllocator` 进行，不再在各子对象中创建临时 allocator。

#### Scenario: VulkanBuffer 通过 MemoryManager 分配

- **WHEN** `VulkanBuffer::Init()` 需要分配 buffer
- **THEN** 通过 `GetApp()->GetMemoryManager().AllocateBuffer()` 分配，不再创建临时 VmaAllocator

#### Scenario: VulkanTexture 通过 MemoryManager 分配

- **WHEN** `VulkanTexture::Init()` 需要分配 image
- **THEN** 通过 `GetApp()->GetMemoryManager().AllocateImage()` 分配，不再创建临时 VmaAllocator

#### Scenario: VulkanResourceAliasing 通过 MemoryManager 分配裸内存

- **WHEN** `VulkanResourceAliasing::AllocateAliasedPool()` 需要分配 aliased pool
- **THEN** 通过 `GetApp()->GetMemoryManager().AllocateMemory()` 分配，不再创建临时 VmaAllocator

