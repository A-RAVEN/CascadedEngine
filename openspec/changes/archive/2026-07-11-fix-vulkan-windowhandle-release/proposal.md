## Why

`VulkanWindowHandle` 析构时不会释放 Vulkan 资源（swapchain、image views、surface），因为其析构函数为 `= default` 且未调用 `Release()`。测试结束后 `shared_ptr<WindowHandle>` 析构 → 资源泄漏 → 模块关闭时 `vkDestroyDevice`/`vkDestroyInstance` 隐式清理引发驱动级崩溃。D3D12 后端无此问题，因为 `WindowContext` 使用 `ComPtr`（RAII 自动释放）。

## What Changes

- **BREAKING**: 将 `VulkanWindowHandle::Release()` 调用从显式手动调用改为析构函数自动触发，与 D3D12 的 RAII 行为对齐
- `VulkanWindowHandle` 添加自定义析构函数，内部调用 `Release()` 执行 swapchain/image views/surface 清理
- `Release()` 添加幂等保护（重复调用安全），因为析构函数和 `RenderBackend_Vulkan::Release()` 可能先后调用

## Capabilities

### New Capabilities
<!-- 无新增能力，纯 bug 修复 -->

### Modified Capabilities
- `vulkan-window-handle`: 新增 RAII 资源清理要求 — `VulkanWindowHandle` 析构时 SHALL 自动释放所有 Vulkan 资源（swapchain、image views、surface）

## Impact

- `VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.h`: 析构函数声明从 `= default` 改为自定义
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.cpp`: 新增析构函数实现，`Release()` 添加重复调用保护
