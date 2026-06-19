## Why

`VulkanWindowHandle::CreateSurface()` 将 `hwnd` 硬编码为 `nullptr`，导致 `vkCreateWin32SurfaceKHR()` 触发 Vulkan 验证层错误 `VUID-VkWin32SurfaceCreateInfoKHR-hwnd-01308`，随后抛出 `vk::SurfaceLostKHRError` 异常。窗口已经创建且 `IWindow` 接口提供了 `GetNativeWindowHandle()`，只是未调用。此外 `GetSizeSafe()` 也硬编码返回 `{800, 600}` 而非查询实际窗口尺寸。

## What Changes

- `CreateSurface()`: 通过 `m_Window->GetNativeWindowHandle()` 和 `m_Window->GetWindowSystem()->GetSystemNativeHandle()` 获取真实的 HWND/HINSTANCE，并添加创建失败的错误处理
- `GetSizeSafe()`: 通过 `m_Window->GetWindowSize()` 获取真实窗口尺寸

## Capabilities

### Modified Capabilities
- `vulkan-window-handle`: Vulkan 窗口 Surface/Swapchain 管理——修复 HWND 获取和窗口尺寸查询

## Impact

- `VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.cpp` (修改 `CreateSurface()` 和 `GetSizeSafe()`)

无 API 变更，无 breaking change。
