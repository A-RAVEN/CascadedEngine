## ADDED Requirements

### Requirement: VulkanWindowHandle 从 IWindow 获取原生窗口句柄

`VulkanWindowHandle::CreateSurface()` SHALL 通过 `m_Window->GetNativeWindowHandle()` 和 `m_Window->GetWindowSystem()->GetSystemNativeHandle()` 获取真实的 Win32 HWND 和 HINSTANCE，而非使用硬编码的 `nullptr` 或 `GetModuleHandle(nullptr)`。

#### Scenario: 创建 Win32 Surface

- **GIVEN** `VulkanWindowHandle::Init()` 已接收有效的 `castl::shared_ptr<cawindow::IWindow>`
- **WHEN** `CreateSurface()` 被调用
- **THEN** `vkCreateWin32SurfaceKHR` 使用从 `m_Window` 获取的真实 `HWND` 和 `HINSTANCE`
- **AND** Surface 创建成功

### Requirement: VulkanWindowHandle 获取真实窗口尺寸

`VulkanWindowHandle::GetSizeSafe()` SHALL 通过 `m_Window->GetWindowSize()` 获取实际窗口尺寸，而非返回硬编码的 `{800, 600}`。若 `m_Window` 为空则返回 fallback 值 `{800, 600}`。

#### Scenario: 查询窗口尺寸

- **GIVEN** `VulkanWindowHandle::Init()` 已接收有效的窗口
- **WHEN** `GetSizeSafe()` 被调用
- **THEN** 返回 `m_Window->GetWindowSize()` 获取的实际尺寸

#### Scenario: 窗口为空时的兜底

- **GIVEN** `VulkanWindowHandle` 的 `m_Window` 为空
- **WHEN** `GetSizeSafe()` 被调用
- **THEN** 返回 `{800, 600}` 防止除零等边界问题

### Requirement: Surface 创建失败时产生可诊断错误

`VulkanWindowHandle::CreateSurface()` SHALL 在 `vkCreateWin32SurfaceKHR` 返回后检查 `m_Surface` 有效性，若为空则输出错误日志。

#### Scenario: Surface 创建失败

- **GIVEN** `CreateSurface()` 被调用但 `m_Surface` 为空
- **WHEN** Surface 创建完成后
- **THEN** 输出 `CA_LOG_ERR` 错误日志
