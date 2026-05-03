## MODIFIED Requirements

### Requirement: Phase 2 性能与同步进度
Vulkan 后端对齐路线图 Phase 2 SHALL 包含以下已完成项：
- GPUFrameManager: 已完成
- 异步 Submit: 已完成
- LinearMemoryManager: 已完成

Phase 2 路线图表更新为：

| # | 模块 | 差距 | 优先级 | 状态 |
|---|------|------|--------|------|
| 1 | **GPUFrameManager** | 无帧管理子系统，每次 ExecuteGraph 创建/销毁所有资源 | P0 | ✅ 已完成 |
| 2 | **异步 Submit** | `SubmitBatches` 中 `waitForFences(UINT64_MAX)` 同步等待 | P0 | ✅ 已完成 |
| 3 | **LinearMemoryManager** | 每个 staging buffer 独立 VMA 分配，无复用 | P1 | ✅ 已完成 |
| 4 | **跨队列同步** | Barrier 始终 `VK_QUEUE_FAMILY_IGNORED` | P1 | ❌ 未开始 |

#### Scenario: 路线图进度更新
- **WHEN** 本 Change 实现完成
- **THEN** `vulkan-backend-alignment` spec 中 Phase 2 项目 1-3 标记为已完成，项目 4（跨队列同步）保持未开始
