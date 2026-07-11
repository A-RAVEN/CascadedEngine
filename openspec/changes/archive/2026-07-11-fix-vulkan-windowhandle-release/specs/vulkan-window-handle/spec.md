## ADDED Requirements

### Requirement: VulkanWindowHandle 析构时自动释放 Vulkan 资源

`VulkanWindowHandle` 析构函数 SHALL 自动调用 `Release()` 释放所有 Vulkan 资源（swapchain、image views、surface），实现与 D3D12 `ComPtr` 一致的 RAII 语义。`Release()` SHALL 支持重复调用（幂等），在首次调用后标记 `m_Released = true`，后续调用为 no-op。

#### Scenario: shared_ptr 析构触发 Vulkan 资源清理

- **GIVEN** `VulkanWindowHandle` 已初始化（swapchain、image views、surface 已创建）
- **WHEN** 最后一个 `shared_ptr<WindowHandle>` 析构
- **THEN** `~VulkanWindowHandle()` 调用 `Release()`
- **AND** `CleanupSwapchain()` 销毁所有 image views 和 swapchain
- **AND** `vkDestroySurfaceKHR` 销毁 surface
- **AND** 所有 Vulkan 句柄设为 `nullptr`

#### Scenario: Release() 重复调用安全

- **GIVEN** `VulkanWindowHandle::Release()` 已被调用过一次（资源已释放，`m_Released == true`）
- **WHEN** 析构函数或调用方再次调用 `Release()`
- **THEN** 第二次调用为 no-op（`m_Released` flag 提前返回）
- **AND** 不访问已释放的 Vulkan 资源

#### Scenario: RenderBackend_Vulkan::Release() 先于 WindowHandle 析构时主动释放

- **GIVEN** 测试已完成，`RenderBackend_Vulkan::Release()` 即将销毁 device/instance
- **WHEN** `RenderBackend_Vulkan::Release()` 遍历 `m_WindowHandles` 中所有存活的 WindowHandle 并调用其 `Release()`
- **THEN** 所有 WindowHandle 的 Vulkan 资源在 device/instance 销毁前被释放
- **AND** 后续 `VulkanWindowHandle` 析构函数中的 `Release()` 调用因 `m_Released == true` 变为 no-op
- **AND** 不会对已销毁的 device/instance 调用 `vkDestroy*`

### Requirement: VulkanWindowHandle 禁止移动语义

`VulkanWindowHandle` SHALL 显式删除移动构造函数和移动赋值运算符，防止移动后两个对象持有相同 Vulkan 句柄导致的 double-destroy。

#### Scenario: 编译期阻止移动

- **GIVEN** `VulkanWindowHandle` 声明了 `= delete` 的移动构造函数
- **WHEN** 代码尝试 `std::move` 或按值传递 `VulkanWindowHandle`
- **THEN** 编译失败，产生明确的错误信息
- **AND** 只能通过 `shared_ptr<VulkanWindowHandle>` 传递和管理生命周期
