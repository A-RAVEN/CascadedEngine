## Why

`fix-vulkan-command-buffer-allocation` 的诊断代码在 `VulkanCommandListManager.cpp` 中引入了 4 个 fmt 编译错误（`error C3615: format_string_checker::on_error cannot result in a constant expression`），导致 Vulkan 后端 DLL 编译失败。这些错误是因为 `static_cast<void*>(VkCommandPool)` 指针类型在 fmt v11 的 `consteval` 编译期格式检查器中不被接受。

## What Changes

- 将 `CA_LOG_INFO`/`CA_LOG_ERR` 调用中的 `static_cast<void*>(static_cast<VkCommandPool>(...))` 参数替换为 `reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(...))`，使用整数类型绕过 fmt 编译期检查器对指针类型的限制
- 4 处修改：`Init()` 中 3 个 pool handle + 3 个 queue family（6 args），`AllocateCommandBuffer()` 的空 vector 诊断中 1 个 pool handle + 3 个 pool handle（4 args），异常诊断中 1 个 pool handle（1 arg）+ `e.what()`（1 arg）
- 修改后运行 `build.py` 确认 BUILD SUCCESSFUL

## Capabilities

### New Capabilities
- `build-compilation`: CA_LOG 宏 + VkCommandPool handle 参数 → fmt 编译期检查通过，无 `error C3615`

### Modified Capabilities
<!-- No requirement changes -->

## Impact

- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp`：修改 `Init()` 和 `AllocateCommandBuffer()` 中 4 处日志调用的参数类型
- 不影响运行时行为，仅修复编译
