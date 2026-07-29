## ADDED Requirements

### Requirement: 纹理创建失败时清理已分配的 VkImage
`VulkanTexture::Init` SHALL 在 `createImageView` 抛出异常时，清理已分配的 VkImage 和 VmaAllocation。

#### Scenario: ImageView 创建失败时清理
- **WHEN** `VulkanTexture::Init` 已成功分配 VkImage，但后续 `createImageView` 抛出异常
- **THEN** 系统销毁 VkImage 并释放 VmaAllocation，无资源泄漏

### Requirement: Aliased 资源失败时按 Route A/B 分支清理
`AllocateAliasedResources` Phase B SHALL 在 buffer 创建或 imageView 创建失败时，按分配路径（Route A: bindImageMemory / Route B: VMA）选择正确的清理 API。

#### Scenario: Route A imageView 失败时 cleanup
- **WHEN** Phase B Route A 中 VkImage 已绑定到 aliased pool（managed.allocation 为空），但 createImageView 失败
- **THEN** 系统调用 `device.destroyImage(managed.image)` 清理

#### Scenario: Route B imageView 失败时 cleanup
- **WHEN** Phase B Route B 中 VkImage 通过 VMA 分配（managed.allocation 非空），但 createImageView 失败
- **THEN** 系统调用 `FreeImage(managed.image, managed.allocation)` 清理

#### Scenario: Buffer 创建失败时 cleanup
- **WHEN** Phase B 中 buffer 已创建但分配或绑定失败
- **THEN** 系统按 managed.allocation 分支：Route A 用 device.destroyBuffer，Route B 用 VMA FreeBuffer

### Requirement: UploadData fence 失败时清理 staging 资源
`VulkanBuffer::UploadData` 和 `VulkanTexture::UploadData` SHALL 在 fence 创建失败时，清理已分配的 staging buffer 和 command buffer。

#### Scenario: fence 创建失败时清理
- **WHEN** staging buffer 和 command buffer 已成功分配并录制，但 `createFence` 抛出异常
- **THEN** 系统释放 staging buffer（VMA free）和 command buffer 后重新抛出异常

### Requirement: Aliased pool 释放时清除分配记录
`FreeAliasedPool` SHALL 在释放 pool 后清空 `m_AliasedAllocations`。

#### Scenario: Pool 释放后查询返回空
- **WHEN** `FreeAliasedPool` 已调用
- **THEN** 后续 `GetAliasedAllocation` 返回空或无效句柄

### Requirement: 命令池部分失败时清理
`VulkanCommandListManager::Init` SHALL 在任一命令池创建失败时，销毁已成功创建的池。

#### Scenario: 第 2 个池创建失败时清理第 1 个池
- **WHEN** Graphics 命令池创建成功，但 Compute 命令池创建失败
- **THEN** 系统销毁 Graphics 命令池后传播异常

### Requirement: RenderBackend Init 步进清理
`RenderBackend_Vulkan::Init` SHALL 在任一子对象初始化失败时，就地销毁已成功初始化的组件（逆序），而非调用全量 `Release()`。

#### Scenario: MemoryManager Init 失败时步进清理
- **WHEN** Instance/Device/DebugMessenger 已成功创建，但 MemoryManager::Init 失败
- **THEN** 系统逆序销毁已创建对象（DebugMessenger → Device → Instance），不使用 Release()

### Requirement: LinearMemoryManager 分配安全
`VulkanLinearMemoryManager::AllocatePage` SHALL 验证 vmaCreateBuffer 成功且 stagingAlloc 非空；`AllocUploadStagingBuffer` SHALL 对超出页面大小的分配请求返回错误；`Reset` SHALL 实现页面裁剪防止帧间无界内存增长。

#### Scenario: vmaCreateBuffer 失败时返回错误
- **WHEN** `AllocatePage` 中 `vmaCreateBuffer` 失败（返回非 VK_SUCCESS）
- **THEN** 函数返回错误，不记录无效页面

#### Scenario: 超大分配请求返回错误
- **WHEN** `AllocUploadStagingBuffer` 收到超过页面大小的分配请求
- **THEN** 函数返回空 StagingAllocation 并记录错误，而非静默溢出

#### Scenario: Reset 裁剪旧页面
- **WHEN** `Reset` 被调用且积累的页面数超过阈值
- **THEN** 系统释放最旧的页面，防止内存无界增长
