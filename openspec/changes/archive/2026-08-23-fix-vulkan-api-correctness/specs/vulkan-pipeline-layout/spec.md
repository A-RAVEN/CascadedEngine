## ADDED Requirements

### Requirement: GPL 路径受运行时支持检查门控

使用 `VK_EXT_graphics_pipeline_library` 创建管线库的代码路径 SHALL 同时受运行时设备支持检查（`GetApp()->IsPipelineLibrarySupported()` / `m_PipelineLibrarySupported`）门控，SHALL NOT 仅依赖编译期常量（如 `VULKAN_SUPPORT_PIPELINE_LIBRARY`）。设备不支持 GPL 时 SHALL 走 monolithic 管线路径。

#### Scenario: 设备不支持 GPL 扩展

- **GIVEN** 设备未暴露 `VK_EXT_graphics_pipeline_library`，`IsPipelineLibrarySupported()==false`，但编译期常量仍为 true
- **WHEN** `VertexInputStateManager` 等构建管线状态
- **THEN** 不使用 `VkGraphicsPipelineLibraryCreateInfoEXT`，走 monolithic 路径
- **AND** 不因 GPL 结构被使用而触发验证错误
