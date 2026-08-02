## ADDED Requirements

### Requirement: 描述符池/命令池重置前必须保证 GPU 工作完成

`VulkanFrameContext::Aquire` 中调用 `vkResetDescriptorPool` 与 `resetCommandPool`/`freeCommandBuffers` 前 SHALL 保证上一帧所有 GPU 工作（含跨队列 semaphore 消费）已完成。当前实现依赖 `SubmitBatches` 的逐 batch fence 等待+重置实现串行化，SHALL 将该前提以注释/断言形式固化，SHALL NOT 移除该串行化或在其存在前改为帧级 fence 等待而引入死锁。

#### Scenario: 多帧连续执行

- **GIVEN** 测试连续执行 100 帧
- **WHEN** 每帧 `Aquire` 重置 descriptor pool 与命令池
- **THEN** 重置时上一帧 GPU 工作已完成（fence 已等待）
- **AND** 无验证层错误、无崩溃

### Requirement: LinearMemoryManager 重置前提明确

`VulkanLinearMemoryManager::Reset` 回退 page offset 复用 staging 区域 SHALL 仅在无 in-flight GPU 工作引用该区域时发生。该前提 SHALL 与帧级串行化绑定并在注释中明确，未来引入 async frames-in-flight 时 SHALL 增加 per-page fence 追踪。超大分配（超过单页默认大小）SHALL NOT 产生永久占用的空页浪费；分配失败时 SHALL 返回失败而非静默返回空分配。

#### Scenario: 超大 staging 分配

- **GIVEN** 请求 size 超过单页默认大小（如 64MB）
- **WHEN** `AllocUploadStagingBuffer` 被调用
- **THEN** 返回专用分配（不保留无用空页），或明确失败并诊断
- **AND** 调用方检查失败

#### Scenario: 帧级串行化下 Reset 复用 staging

- **GIVEN** 单帧 in-flight（逐 batch fence 等待），staging 区域无活 GPU 引用
- **WHEN** `Reset` 回退 offset
- **THEN** 复用安全
