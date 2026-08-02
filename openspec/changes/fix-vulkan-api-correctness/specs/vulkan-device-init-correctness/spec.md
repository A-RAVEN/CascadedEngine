## ADDED Requirements

### Requirement: 设备 feature 开启前必须查询验证

`RenderBackend_Vulkan` SHALL 在调用 `vkCreateDevice` 前通过 `vkGetPhysicalDeviceFeatures2`（或等价 vk.hpp 接口）查询 `graphicsPipelineLibrary` 与 `dynamicRendering` 等 feature 支持情况，SHALL NOT 无条件将 feature 置 `VK_TRUE` 传入 `VkDeviceCreateInfo`。物理设备选择 SHALL 基于 capability 检查（而非盲目取 `physicalDevices.front()`）。请求的 feature 不被支持时，SHALL 回退到不启用该 feature 的路径（如 GPL → monolithic 管线），或给出明确的初始化失败诊断，不得静默依赖设备碰巧支持。

#### Scenario: 设备不支持 graphicsPipelineLibrary

- **GIVEN** 物理设备未暴露 `VK_EXT_graphics_pipeline_library` 扩展或 feature
- **WHEN** 后端初始化创建 `vk::Device`
- **THEN** 不请求 `graphicsPipelineLibrary=VK_TRUE`
- **AND** `m_PipelineLibrarySupported=false`
- **AND** 管线创建走 monolithic 回退路径，初始化不失败

#### Scenario: 设备 feature 查询成功且支持

- **GIVEN** 物理设备经 `vkGetPhysicalDeviceFeatures2` 确认支持 `graphicsPipelineLibrary`
- **WHEN** 后端初始化创建 `vk::Device`
- **THEN** 开启 `graphicsPipelineLibrary` feature 并设置 `m_PipelineLibrarySupported=true`

### Requirement: 设备扩展启用与实际使用匹配

设备扩展列表 SHALL 仅包含代码实际使用（或为实际使用扩展所必需）的扩展。已被核心版本提升（promoted to core）且未使用其任何 feature/命令的扩展条目 SHALL 被移除。

#### Scenario: 移除冗余已提升扩展

- **GIVEN** 设备扩展列表包含 `VK_KHR_MAINTENANCE_4_EXTENSION_NAME`（Vulkan 1.3 已提升为核心），且代码未使用任何 maintenance4 命令或 feature
- **WHEN** 后端初始化组装 `GetDeviceExtensionNames()`
- **THEN** 该扩展名不再出现在设备扩展列表中

#### Scenario: 实际使用的扩展保留

- **GIVEN** 代码实际使用 `VK_EXT_graphics_pipeline_library` 与 `VK_EXT_debug_utils`
- **WHEN** 后端初始化组装设备扩展列表
- **THEN** 这些扩展名仍被请求
