## Context

当前 VulkanRenderBackendNew 模块使用 Vulkan-Hpp 动态调度（`VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`），通过 `vk::DynamicLoader` 在运行时从 `vulkan-1.dll` 加载所有 Vulkan 函数指针。VMA 配置为 `VMA_STATIC_VULKAN_FUNCTIONS=0` 且 `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`，期望用户通过 `VmaAllocatorCreateInfo::pVulkanFunctions` 传入至少 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 两个引导函数指针。

由于 `pVulkanFunctions` 在所有调用点均为 `nullptr`，VMA 的三个函数导入路径全部失败：
1. `ImportVulkanFunctions_Static` — 因 `VMA_STATIC_VULKAN_FUNCTIONS=0` 被跳过
2. `ImportVulkanFunctions_Custom` — 因 `pVulkanFunctions == nullptr` 被跳过
3. `ImportVulkanFunctions_Dynamic` — 需要 `m_VulkanFunctions.vkGetInstanceProcAddr` 非空才能引导加载其他函数，但前两条路径均未填充此指针

最终 `m_VulkanFunctions.vkGetPhysicalDeviceProperties` 保持为 null（由 `memset` 初始化为零），在 `vk_mem_alloc.h:12826` 通过空指针调用导致 crash。

此外，`VulkanBuffer`、`VulkanTexture`、`VulkanResourceAliasing` 均各自创建临时 VMA allocator 来分配/释放资源。这不仅有大量重复代码，而且在 `AllocateAliasedPool()/FreeAliasedPool()` 中存在悬垂 allocation 问题：allocator 在 allocation 被 free 之前先被销毁了。

## Goals / Non-Goals

**Goals:**
- 使所有 `vmaCreateAllocator` 调用点正确传入 VMA 所需的 Vulkan 函数指针，修复崩溃
- 提供可复用的工具函数，避免重复代码
- 将 Buffer/Texture/ResourceAliasing 的临时 allocator 迁移为统一使用 `VulkanMemoryManager` 持久 allocator

**Non-Goals:**
- 不改变 VMA 的配置模式（保持 `VMA_STATIC_VULKAN_FUNCTIONS=0`）
- 不引入 volk 或其他 Vulkan 加载器

## Decisions

### 方案：从 `vk::defaultDispatchLoaderDynamic` 提取函数指针

通过 `vk::defaultDispatchLoaderDynamic` 的 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 成员填充 `VmaVulkanFunctions`，传入 `VmaAllocatorCreateInfo::pVulkanFunctions`。

**为什么选此方案？**
- VMA 只需要 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 两个引导函数，其余所有 Vulkan 函数指针 VMA 内部通过这两个函数自动获取
- `vk::defaultDispatchLoaderDynamic` 在 `RenderBackend_Vulkan::Init()` 中已经完成初始化（instance 和 device 级别函数均已加载），函数指针可用
- 不引入新的依赖，不需要链接 `vulkan-1.lib`
- 代码量极小

**替代方案考虑：**
- **链接 vulkan-1.lib + `VMA_STATIC_VULKAN_FUNCTIONS=1`**：尽管可行，但会引入对 Vulkan SDK 导入库的链接依赖，且项目已明确使用动态加载路线
- **引入 volk**：过度工程化，项目已从 volk 迁移到 Vulkan-Hpp 原生动态加载
- **提供全局 `vkGetInstanceProcAddr` 桥接函数**：在 `VMAImpl.cpp` 中定义 `extern "C"` 的 `vkGetInstanceProcAddr`，委托到 `vk::defaultDispatchLoaderDynamic`，使 VMA 的 `ImportVulkanFunctions_Static` 路径生效。不采用此方案，因为：存在与 `vulkan-1.lib` 的 ODR 冲突风险；且不传 `pVulkanFunctions` 依赖隐式行为，不如显式传参清晰

## Risks / Trade-offs

- [VMA 与 Vulkan-Hpp 分别维护独立的函数指针表] → 设计中已有意保持这种隔离；VMA 只用于 GPU 内存管理，不需要与 Vulkan-Hpp 共享 dispatch table
- [未来 VMA 升级可能需要额外函数] → VMA 通过 `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` 动态解析，无需修改用户代码
- [初始化顺序依赖] → `FillVmaVulkanFunctions` 必须在 `vk::defaultDispatchLoaderDynamic` 完成 device 级初始化之后调用。当前调用路径（`RenderBackend_Vulkan::Init()` → 创建 instance/device → `initDispatchLoaderDynamic(device)` → 子对象 `Init()`）保证 device 级函数指针已就绪。若未来重构初始化顺序需注意此前提