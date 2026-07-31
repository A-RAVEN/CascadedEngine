## ADDED Requirements

### Requirement: ReplanWithRealAlignment 更新总大小
`ReplanWithRealAlignment` SHALL 在更新各资源 lifetime.size 后，重新计算 `m_TotalUnaliasedSize` 为所有更新后 lifetime 大小的总和。

#### Scenario: 对齐计划后效率指标准确
- **WHEN** `ReplanWithRealAlignment` 更新了资源的实际大小（可能因对齐调整增大）
- **THEN** `m_TotalUnaliasedSize` 反映更新后的总大小，`GetAliasingEfficiency()` 返回正确比例

### Requirement: GPL LinkPipeline 失败时清理 library parts
`BuildPipelineStates` 中 GPL 路径 SHALL 在 `LinkPipeline` 失败时，立即销毁本次创建的 4 个 library pipeline part（preRasterization、fragmentShader、fragmentOutput、fullGraphics），并清理其追踪记录。

#### Scenario: Link 失败不累积库泄漏
- **WHEN** `LinkPipeline` 调用失败（如不兼容的 library part 组合）
- **THEN** 本次创建的 4 个 VkPipeline 立即被销毁，不保留在 `m_CreatedPipelines` 中

### Requirement: Swapchain 重建时清除依赖缓存
窗口 resize 触发 swapchain 重建时 SHALL 清空 `m_FramebufferCache` 及其他引用旧 swapchain imageView 的缓存。

#### Scenario: 多次 resize 不累积缓存
- **WHEN** 用户 resize 窗口 N 次
- **THEN** 缓存的 framebuffer 数量不随 resize 次数线性增长

### Requirement: 移除 m_AllocatedCommandBuffers 死代码
`VulkanCommandListManager` 中未使用的 `m_AllocatedCommandBuffers` 成员 SHALL 被移除。

#### Scenario: 死代码清理
- **WHEN** 开发者查看 VulkanCommandListManager 声明
- **THEN** 不存在误导性的 `m_AllocatedCommandBuffers` 字段

### Requirement: AllocatePage push_back 异常安全
`VulkanLinearMemoryManager::AllocatePage` SHALL 在 `m_Pages.push_back(page)` 可能抛出异常时，清理已分配的 VkBuffer 和 VmaAllocation。

#### Scenario: vector 重分配失败不泄漏 VMA
- **WHEN** `vmaCreateBuffer` 成功但 `m_Pages.push_back` 抛出 std::bad_alloc
- **THEN** 系统销毁已创建的 VkBuffer 和 VmaAllocation 后重新抛出异常
