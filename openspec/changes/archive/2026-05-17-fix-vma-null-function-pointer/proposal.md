## Why

VulkanMemoryAllocator (VMA) 在 `VMA_STATIC_VULKAN_FUNCTIONS=0` 且未提供 `pVulkanFunctions` 时，无法获取 Vulkan 函数指针，导致在 `vmaCreateAllocator` 内部通过空指针调用 `vkGetPhysicalDeviceProperties` 崩溃（0xC0000005 access violation at 0x0000000000000000）。项目使用 Vulkan-Hpp 动态加载器且未链接 `vulkan-1.lib`，VMA 无法自行解析 Vulkan 符号。

## What Changes

- 新增工具函数，从 `vk::defaultDispatchLoaderDynamic` 提取 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 填入 `VmaVulkanFunctions` 结构体
- 所有 `vmaCreateAllocator` 调用点均传入 `pVulkanFunctions`，使 VMA 能正确引导加载全部所需 Vulkan 函数指针
- `VulkanBuffer`、`VulkanTexture`、`VulkanResourceAliasing` 的临时 VMA allocator 迁移为统一使用 `VulkanMemoryManager` 持久 allocator，消除重复的 `vmaCreateAllocator`/`vmaDestroyAllocator` 乱用

## Capabilities

### New Capabilities
- `vma-vulkan-function-binding`: VMA 与 Vulkan-Hpp 动态调度之间的函数指针桥接——仅桥接 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 两个引导函数，其余所有 Vulkan 函数由 VMA 内部通过引导函数自行动态解析

### Modified Capabilities
- `vulkan-memory-management`: `VulkanMemoryManager` 新增 `AllocateMemory` 和 `FreeMemory` 裸内存分配接口，所有子对象（Buffer/Texture/ResourceAliasing）统一使用持久 allocator

## Impact

- `VulkanRenderBackendNew/private/Utils/VulkanVMAUtils.h` (新增)
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.h` (新增裸分配接口)
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.cpp`
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.cpp`
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanBuffer.cpp`
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp`

无 API 变更，无 breaking change。