## ADDED Requirements

### Requirement: Null attachment 时跳过整个 render pass
`RecordRenderPass` SHALL 在任一 attachment 的 `GetTextureView` 返回 null 时跳过整个 render pass，而非构建不完整的 attachment 列表。

#### Scenario: 部分 attachment 为 null 时跳过
- **WHEN** render pass 有 3 个 attachment，但其中一个的 ImageView 为 null
- **THEN** 系统返回（不调用 beginRenderPass），避免 framebuffer attachment 数量与 render pass 不匹配

### Requirement: AllocateCommandBuffer 空向量时明确返回
`AllocateCommandBuffer` SHALL 在 `allocateCommandBuffers` 返回空向量时，执行 `return vk::CommandBuffer{}` 而非执行到函数尾部导致未定义行为。

#### Scenario: 分配失败时返回空句柄
- **WHEN** `allocateCommandBuffers` 返回空向量
- **THEN** 函数返回 `vk::CommandBuffer{}`（VK_NULL_HANDLE），调用者可检测

### Requirement: Validation layer 仅在 debug 构建启用
Validation layer SHALL 仅在 `#ifndef NDEBUG` 条件下启用。即使启用，`createInstance` 的 layer 请求 SHALL 被 try-catch 保护，layer 缺失时回退到无 validation 模式。

#### Scenario: 生产环境无 SDK 时正常启动
- **WHEN** 非 debug 构建在生产机上运行（无 VK_LAYER_KHRONOS_validation）
- **THEN** createInstance 不尝试启用 validation layer，启动成功

### Requirement: VMA 分配失败时 out 参数置 null
`AllocateBuffer`、`AllocateImage`、`AllocateMemory` SHALL 在分配失败时将 out 参数（outBuffer、outImage、outAllocation）显式置为 `VK_NULL_HANDLE`。

#### Scenario: 分配失败后 out 参数为 null
- **WHEN** `AllocateBuffer` 中 VMA 分配失败
- **THEN** `outBuffer` 被设为 `VK_NULL_HANDLE`，调用者可通过 null 检查检测失败

### Requirement: Staging 分配失败时记录错误
`AllocUploadStagingBuffer` 失败时 SHALL 记录错误日志，而非通过 `continue` 静默跳过。

#### Scenario: Staging 失败时输出日志
- **WHEN** `AllocUploadStagingBuffer` 返回空 `StagingAllocation`
- **THEN** 系统输出 `CA_LOG_ERR` 日志明确指示上传失败

### Requirement: Pipeline cache 双重异常保护
Pipeline cache 回退创建（使用空数据）SHALL 被额外的 try-catch 保护，防止嵌套异常导致 `Init` 崩溃。

#### Scenario: 空数据创建也失败时不崩溃
- **WHEN** 外层 `createPipelineCache`（带磁盘数据）失败进入 catch 块，回退的 `createPipelineCache`（空数据）也失败
- **THEN** 内层异常被捕获，系统记录错误并以 nullptr cache 继续

### Requirement: Fence wait 使用有限超时
所有 `waitForFences` 调用 SHALL 使用有限超时（如 5 秒）替代 `UINT64_MAX`，超时时检测 VK_TIMEOUT 和 VK_ERROR_DEVICE_LOST。

#### Scenario: GPU 挂起时超时退出
- **WHEN** GPU 挂起导致 fence 永不 signal
- **THEN** `waitForFences` 在 5 秒后返回 VK_TIMEOUT，系统记录错误并返回失败（而非永久挂起）
