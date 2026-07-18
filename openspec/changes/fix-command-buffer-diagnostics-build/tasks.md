# Tasks: 修复 fmt 编译错误

**Change ID**: fix-command-buffer-diagnostics-build
**Prerequisite**: 修复后 `fix-vulkan-command-buffer-allocation` 的 `VulkanCommandListManager.cpp` 改动可正常编译

---

## 1. 修改日志参数类型

- [x] 1.1 在 `VulkanCommandListManager::Init()` 的 `CA_LOG_INFO` 中，将 3 个 `static_cast<void*>(static_cast<VkCommandPool>(...))` 替换为 `reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(...))`。

- [x] 1.2 在 `VulkanCommandListManager::AllocateCommandBuffer()` 的空 vector `CA_LOG_ERR`（2 条）和异常 `CA_LOG_ERR`（1 条）中，同样替换所有 `static_cast<void*>(static_cast<VkCommandPool>(...))` 为 `reinterpret_cast<uintptr_t>(static_cast<VkCommandPool>(...))`。

## 2. 编译验证

- [x] 2.1 运行 `build.py`，确认 BUILD SUCCESSFUL。**必须在标 complete 前验证编译输出。**
