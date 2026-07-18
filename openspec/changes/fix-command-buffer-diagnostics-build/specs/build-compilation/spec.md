## ADDED Requirements

### Requirement: Vulkan 后端使用 CA_LOG 宏时可通过 fmt 编译期检查
当 `VulkanCommandListManager` 的 `CA_LOG_INFO`/`CA_LOG_ERR` 调用传递 Vulkan handle 作为参数时，fmt v11 的 `consteval` 编译期格式检查器 SHALL 通过，不应导致 `error C3615` 编译错误。

#### Scenario: Init() 日志编译通过
- **WHEN** 编译 `VulkanCommandListManager.cpp`
- **THEN** `Init()` 中的 `CA_LOG_INFO`（包含 3 个 pool handle + 3 个 queue family 参数）SHALL 无编译错误

#### Scenario: AllocateCommandBuffer 诊断日志编译通过
- **WHEN** 编译 `VulkanCommandListManager.cpp`
- **THEN** `AllocateCommandBuffer()` 中的 `CA_LOG_ERR`（空 vector 诊断 + 异常诊断）SHALL 无编译错误
