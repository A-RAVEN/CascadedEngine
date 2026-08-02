## Why

`GPUBackendTester.exe --backend vulkan` 在运行时以下问题：

1. **ACCESS_VIOLATION crash**：headless 和 non-headless 模式均在 `Submit Count:1444` 后崩溃。baseline（`ea9dd11`）同样存在此问题，`fix-vulkan-polish` 的改动未引入或恶化此 crash，但根因不明。
2. **VMA 内存分配失败**：non-headless 模式多帧运行时出现 `Failed to allocate raw Vulkan memory: -8`（`VulkanMemoryManager::AllocateMemory`），伴随 `VulkanResourceAliasing::CommitVirtualAllocations` 失败后 fallback 到 single-pool 路径。headless 模式（单帧/多帧）下均未复现此错误。

非 headless 模式下 crash 和内存错误同时出现，但因果关系未确认。需要排查并修复。

## What Changes

- 排查 ACCESS_VIOLATION crash 根因：分析 crash dump，定位崩溃调用栈和触发条件
- 排查 VMA 内存分配失败根因：确认 `-8` 错误码含义（VkResult 或 VMA 错误），分析 aliasing pool 分配链路中是否存在资源泄漏或碎片化
- 修复确认的 crash 和内存分配问题
- 修复后对 headless（单帧/多帧）和 non-headless 模式做完整回归测试

## Capabilities

### New Capabilities
无

### Modified Capabilities
无

## Impact

- `GPUBackendTester.exe` — 测试运行时行为
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.cpp` — 内存分配错误发生点
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanResourceAliasing.cpp` — aliasing pool commit 失败路径
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` — fallback 逻辑
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` — submit/frame 执行路径
