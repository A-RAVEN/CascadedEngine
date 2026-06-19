## Context

`VulkanWindowHandle` 是新 Vulkan 后端的窗口 Surface/Swapchain 管理器。`Init()` 接收 `castl::shared_ptr<cawindow::IWindow>`，但 `CreateSurface()` 中未调用 `IWindow` 的方法获取原生窗口句柄。

旧后端 (`VulkanRenderBackend/private/WindowContext.cpp:45-52`) 已有正确的实现模式：
```cpp
HWND winHandle = *static_cast<HWND*>(m_OwningWindow->GetNativeWindowHandle());
HINSTANCE winInstHandle = *static_cast<HINSTANCE*>(m_OwningWindow->GetWindowSystem()->GetSystemNativeHandle());
m_Surface = GetInstance().createWin32SurfaceKHR(vk::Win32SurfaceCreateInfoKHR({}, winInstHandle, winHandle));
```

`IWindow` 接口已定义 `GetNativeWindowHandle()` 返回 `void*`（实际为 `HWND*`）和 `GetWindowSystem()->GetSystemNativeHandle()`（返回 `HINSTANCE`）。

## Goals / Non-Goals

**Goals:**
- 修复 `CreateSurface()` 中 `hwnd = nullptr` 导致 Surface 创建失败
- `CreateSurface()` 添加创建失败的诊断日志，避免静默失败
- 修复 `GetSizeSafe()` 返回真实窗口尺寸而非硬编码值

**Non-Goals:**
- 不改动 `IWindow` 接口
- 不添加新的平台抽象（XCB/Wayland 仍为 TODO）
- 不处理 Surface 失效重建：Surface 生命周期绑定窗口，窗口销毁前 `Release()` 先销毁 Surface，不存在 Surface 失效场景。旧后端 (`WindowContext`) 同样不复建 Surface，行为一致

## Decisions

### 方案：照搬旧后端模式

`CreateSurface()` 中通过 `m_Window` 获取 `HWND` 和 `HINSTANCE`，与旧后端 `WindowContext::InitializeWindowHandle()` 一致。

```cpp
surfaceInfo.hinstance = *static_cast<HINSTANCE*>(m_Window->GetWindowSystem()->GetSystemNativeHandle());
surfaceInfo.hwnd = *static_cast<HWND*>(m_Window->GetNativeWindowHandle());
```

`GetSizeSafe()` 中调用 `m_Window->GetWindowSize()`：
```cpp
int width = 0, height = 0;
m_Window->GetWindowSize(width, height);
return uint2{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
```

**为什么选此方案？**
- 与旧后端完全一致的成熟模式
- `IWindow` 接口已稳定，无额外依赖
- 改动极小，低风险

### 方案：CreateSurface 添加错误处理

`CreateSurface()` 返回后检查 `m_Surface` 有效性，若创建失败记录错误日志，便于诊断。不抛异常——保持与旧后端一致的"调用方处理"策略。

**为什么选此方案？**
- 当前 bug 正是 Surface 创建静默失败导致下游 `SurfaceLostKHRError`，错误处理能让问题更早暴露
- 最小侵入：只加检查 + 日志，不改变调用方协议

## Risks / Trade-offs

- 无已知风险。`Init()` 保证 `m_Window` 非空后才调用 `CreateSurface()`，调用链已受保护
- `GetNativeWindowHandle()` 返回 `&WindowImpl::m_Win32Window`，该成员变量随 `WindowImpl` 生命周期（由 `shared_ptr<IWindow>` 持有），在 `CreateSurface()` 调用时有效
