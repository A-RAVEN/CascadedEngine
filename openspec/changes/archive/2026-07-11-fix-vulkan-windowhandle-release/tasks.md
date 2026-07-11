## 1. VulkanWindowHandle 头文件修改

- [x] 1.1 将析构函数声明从 `= default` 改为自定义声明（仅声明，实现在 .cpp）
- [x] 1.2 添加 `bool m_Released = false;` 成员变量用于幂等保护
- [x] 1.3 将移动构造函数和移动赋值运算符改为 `= delete`

## 2. VulkanWindowHandle 实现修改

- [x] 2.1 添加自定义析构函数实现：调用 `Release()`
- [x] 2.2 修改 `Release()` 添加 `m_Released` 幂等保护：首次调用执行清理并设 `m_Released = true`，后续调用 no-op
- [x] 2.3 `RecreateSwapchain()` 成功重建后重置 `m_Released = false`（防御性措施，防止未来代码路径中 Release() 后重建导致析构时跳过清理）

## 3. RenderBackend_Vulkan 修改

- [x] 3.1 在 `RenderBackend_Vulkan::Release()` 中，`m_WindowHandles.clear()` 之前，遍历所有存活的 WindowHandle 并调用 `Release()`

## 4. 编译验证

- [x] 4.1 运行 build.py 编译项目，确认 BUILD SUCCESSFUL
- [ ] 4.2 运行 GPUBackendTester Vulkan headless 测试验证不再崩溃
- [ ] 4.3 检查 validation layer 输出无 object leak 警告
