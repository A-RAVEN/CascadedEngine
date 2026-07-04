# Vulkan Pipeline States Completion

**Version**: 1.1
**Created**: 2026-06-27
**Updated**: 2026-06-28（多轮审查后修订：非 GPL 路径由独立的 else 分支修复处理；GPL+VK_NULL_HANDLE 经 spec 验证正确）

---

## ADDED Requirements

### Requirement: FragmentOutputStates 结构体完整性

`FragmentOutputStateCache` SHALL 包含 Vulkan Graphics Pipeline Library 的 Fragment Output Interface 所需字段：颜色附件格式列表、深度模板格式、MSAA 采样数。

#### Scenario: 包含颜色格式

- **WHEN** 创建 `FragmentOutputStateCache` 实例
- **THEN** 结构体包含 `colorFormats` 字段（`castl::vector<vk::Format>`）表示所有颜色附件的格式

#### Scenario: 包含深度模板格式

- **WHEN** 创建 `FragmentOutputStateCache` 实例
- **THEN** 结构体包含 `depthStencilFormat` 字段（`vk::Format`）表示深度模板附件格式

#### Scenario: 包含 MSAA 采样数

- **WHEN** 创建 `FragmentOutputStateCache` 实例
- **THEN** 结构体包含 `sampleCount` 字段（`vk::SampleCountFlagBits`）表示 MSAA 采样数

### Requirement: FragmentShaderStates 结构体创建

`FragmentShaderStates.h` 当前是空命名空间（`namespace graphics_backend { }`），SHALL 改写为声明 `FragmentShaderStateCache` 结构体，包含 fragment shader module 引用字段。改写前 SHALL 验证无外部代码依赖该命名空间（grep 已确认安全）。

#### Scenario: 包含 shader module

- **WHEN** 创建 `FragmentShaderStateCache` 实例
- **THEN** 结构体包含 `fragmentShaderCache` 字段（`TypedVKHashVal<ShaderModuleCache>`）

### Requirement: GeometryShaderStates 结构体完整性

`GeometryShaderStates` SHALL 包含 tessellation control、tessellation evaluation 和 geometry shader module 引用字段，而非仅 vertex shader。

#### Scenario: 包含 Tessellation Control Shader

- **WHEN** 创建 `GeometryShaderStates` 实例
- **THEN** 结构体包含 `tessControlShaderCache` 字段

#### Scenario: 包含 Tessellation Evaluation Shader

- **WHEN** 创建 `GeometryShaderStates` 实例
- **THEN** 结构体包含 `tessEvalShaderCache` 字段

#### Scenario: 包含 Geometry Shader

- **WHEN** 创建 `GeometryShaderStates` 实例
- **THEN** 结构体包含 `geometryShaderCache` 字段

### Requirement: FragmentOutputStateManager 修正

`FragmentOutputStateManager.h` SHALL 声明外部类 `FragmentOutputStateManager`（而非 `VertexInputStateManager`），方法名 SHALL 使用 `EnsureFragmentOutputState` / `GetFragmentOutputState`，类型 SHALL 使用 `FragmentOutputStateCache`。

#### Scenario: 类名正确

- **WHEN** 其他代码引用 `FragmentOutputStateManager`
- **THEN** 该类存在且不是 `VertexInputStateManager` 的复制粘贴

### Requirement: GeometryShaderStateManager 修正

`GeometryShaderStateManager.h` SHALL 修正两类名称错误：外部类名 → `GeometryShaderStateManager`（而非 `VertexInputStateManager`），内部类名 → `GeometryShaderState`（而非 `FragmentOutputState`），内部类成员类型 → `GeometryShaderStates`（而非 `FragmentOutputStateCache`），方法名 → `EnsureGeometryShaderState` / `GetGeometryShaderState`。

#### Scenario: 外部类名正确

- **WHEN** 其他代码引用 `GeometryShaderStateManager`
- **THEN** 该类存在且不是 `VertexInputStateManager` 的复制粘贴

#### Scenario: 内部类名和类型正确

- **WHEN** 查看 `GeometryShaderStateManager.h` 内部
- **THEN** 内部类名为 `GeometryShaderState`（非 `FragmentOutputState`），其成员类型为 `GeometryShaderStates`（非 `FragmentOutputStateCache`）

### Requirement: CreateMonolithicPipeline 硬编码值动态化

`VulkanGraphExecutor::CreateMonolithicPipeline` SHALL 将以下 7 个硬编码值从 DrawCallBatch 数据动态读取。`viewportCount=1` / `scissorCount=1` 为 dynamic viewport 标准 Vulkan 用法，无需修改。

| 硬编码项 | 当前值 | 数据来源 |
|---------|--------|---------|
| `inputAssemblyState.topology` | `eTriangleList` | `batch.pipelineStateDesc.m_InputAssemblyStates.Get().topology` |
| `rasterizationState.polygonMode` | `eFill` | `batch.pipelineStateDesc.m_PipelineStates.Get().rasterizationStates.polygonMode` |
| `rasterizationState.cullMode` | `eBack` | 同上 |
| `rasterizationState.frontFace` | `eClockwise` | 同上 |
| `multisampleState.rasterizationSamples` | `e1` | `m_PipelineStates.Get().msCount`（enum 转 vk::SampleCountFlagBits） |
| `colorBlendAttachment.colorWriteMask` | RGBA 全通道 | `m_PipelineStates.Get().colorAttachments.attachmentBlendStates[0].channelMask` |
| `colorBlendState.attachmentCount` | `1` | `rasterPass.GetColorAttachmentCount()`（⚠️ `attachmentBlendStates.size()` 是定长数组永远返回 8，不可用） |

#### Scenario: 所有值动态读取

- **WHEN** 创建 raster pass pipeline
- **THEN** 以上 7 个 pipeline state 值均从实际的 DrawCallBatch 数据读取，无硬编码
