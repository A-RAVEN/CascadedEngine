## ADDED Requirements

### Requirement: ShaderModule null 检查
`GetOrCreateShaderModule` 返回值 SHALL 在传入管线创建 API 前进行 null 检查。若返回 null，管线创建 SHALL 被跳过。

#### Scenario: Vertex shader module 为 null 时跳过 GPL 管线创建
- **WHEN** `BuildPipelineStates` 中 `GetOrCreateShaderModule` 返回 null 的 vertex shader module
- **THEN** 该 pass 的管线创建被跳过（continue），不传入 null module

#### Scenario: Compute shader module 为 null 时跳过计算管线创建
- **WHEN** `BuildDispatchIndirectArgs` 中 `GetOrCreateShaderModule` 返回 null 的 compute shader module
- **THEN** 该 dispatch 的管线创建被跳过

### Requirement: RenderPass null 检查
`GetOrCreateRenderPass` 返回值 SHALL 在 `BuildPipelineStates` 中 null 检查，与 `RecordRenderPass` 保持一致。

#### Scenario: RenderPass 为 null 时跳过
- **WHEN** `BuildPipelineStates` 中 `GetOrCreateRenderPass` 返回 null
- **THEN** 该 pass 的管线创建被跳过（continue）

### Requirement: PipelineLayout null 检查
`GetOrCreatePipelineLayout` 返回值 SHALL 在 `BuildPipelineStates` 中 null 检查，防止 VK_NULL_HANDLE 传入管线创建 API。

#### Scenario: PipelineLayout 为 null 时跳过 raster 管线
- **WHEN** `BuildPipelineStates` raster 路径中 `GetOrCreatePipelineLayout` 返回 null
- **THEN** 该 pass 的管线创建被跳过（continue）

#### Scenario: PipelineLayout 为 null 时跳过 compute 管线
- **WHEN** `BuildPipelineStates` compute 路径中 `GetOrCreatePipelineLayout` 返回 null
- **THEN** 该 dispatch 的管线创建被跳过（continue）

### Requirement: RecordRenderPass 空管线保护
`RecordRenderPass` SHALL 在 `batchData.pipeline` 为 null 时跳过整个 draw batch（包括描述符绑定和 draw call），与 `RecordComputePass` 行为一致。

#### Scenario: 空 pipeline 时跳过 draw
- **WHEN** `batchData.pipeline` 为 VK_NULL_HANDLE
- **THEN** 系统不调用 bindDescriptorSets、bindVertexBuffers、bindIndexBuffer、或任何 draw call

### Requirement: Staging buffer 分配检查（Buffer + Texture 双路径）
`VulkanBuffer::UploadData` 和 `VulkanTexture::UploadData` SHALL 检查 staging buffer/image 分配结果，分配失败时记录错误并返回。

#### Scenario: Buffer upload staging 失败
- **WHEN** `UploadData` 中 `AllocateBuffer` 返回 VK_NULL_HANDLE 的 staging buffer
- **THEN** 函数记录错误并返回，不继续 memcpy 或命令录制

#### Scenario: Texture upload staging 失败
- **WHEN** `VulkanTexture::UploadData` 中 `AllocateBuffer` 返回 VK_NULL_HANDLE 的 staging buffer
- **THEN** 函数记录错误并返回，不继续 memcpy 或命令录制

### Requirement: 管线库 shader module 验证
`VulkanPipelineLibrary` 的 `CreatePreRasterizationLibrary` 和 `CreateFragmentLibrary` SHALL 在创建管线前验证所有传入的 shader stage 具有非空 `.module`。

#### Scenario: 空 module 被拒绝
- **WHEN** `CreatePreRasterizationLibrary` 收到包含 null module 的 shader stage
- **THEN** 函数输出错误日志并返回 nullptr，不调用 device.createGraphicsPipeline
