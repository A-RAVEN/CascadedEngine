## 1. 工具函数

- [x] 1.1 新建 `VulkanRenderBackendNew/private/Utils/VulkanVMAUtils.h`，实现 `FillVmaVulkanFunctions(VmaVulkanFunctions& out)` 函数：先用 `memset` 清零整个结构体，再从 `vk::defaultDispatchLoaderDynamic` 提取 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr` 填入（需要 `#include <vk_mem_alloc.h>` 和 `#include <vulkan/vulkan.hpp>`）

## 2. 调用点修复 & 统一使用 MemoryManager

- [x] 2.1 [P] `VulkanMemoryManager.h/cpp`：`Init()` 中填充 `VmaVulkanFunctions` 并设置 `allocatorInfo.pVulkanFunctions`；新增 `AllocateMemory()` / `FreeMemory()` 裸分配接口
- [x] 2.2 [P] `VulkanBuffer.cpp`：`Init()`、`Release()`、`Map()`、`Unmap()` 四处删除临时 VMA allocator，改为调用 `GetApp()->GetMemoryManager()` 对应方法
- [x] 2.3 [P] `VulkanTexture.cpp`：`Init()`、`Release()` 两处删除临时 VMA allocator，改为调用 `GetApp()->GetMemoryManager()` 对应方法
- [x] 2.4 [P] `VulkanResourceAliasing.cpp`：`AllocateAliasedPool()`、`FreeAliasedPool()` 两处删除临时 VMA allocator，改为通过 `GetApp()->GetMemoryManager()` 的持久 allocator 分配/释放

## 3. 验证

- [x] 3.1 运行 `python build.py` 编译项目，确保 BUILD SUCCESSFUL
- [x] 3.2 运行 `D3D12RendererBackendTester.exe`，验证不再在 VMA 初始化时崩溃