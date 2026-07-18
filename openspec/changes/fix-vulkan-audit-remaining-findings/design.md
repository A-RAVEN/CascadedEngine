## Context

审计发现的 28 个 medium/low 缺陷，不阻塞渲染链路但影响质量。分为四类：barrier 效率、错误路径、内存清理、API 正确性。修复简单直接，每个缺陷 ~3-5 行改动。

## Goals / Non-Goals

**Goals:**
- Barrier 使用最紧 stage mask，减少 GPU 流水线停顿
- 错误路径不产生 UB、静默失败、无 SDK 的生产崩溃
- 清理已知泄漏和死代码
- 修复 API 字段语义错误

**Non-Goals:**
- 不重构 barrier 系统架构
- 不添加完整的 device lost 恢复机制（仅添加超时检测）
- 不重新设计 VMA 资源管理

## Decisions

### D1: ExecuteBarriers 合并 + 紧 stage mask

- **选择**：收集 barrier 时记录各 barrier 的 stageFlags，取按位或作为最紧 src/dst mask；将 image+buffer barrier 合并为一次 `vkCmdPipelineBarrier` 调用
- **理由**：当前硬编码 `eAllCommands` 在 tile-based GPU 上造成不必要的全管线停顿

### D2: Validation layer 条件编译

- **选择**：`#ifndef NDEBUG` 包裹 validation layer 启用代码，并在 `createInstance` 外层 try-catch 防止 layer 缺失崩溃
- **理由**：允许同一二进制在开发机（有 SDK）和生产机（无 SDK）上运行

### D3: InitializedState 拆分

- **选择**：`InitializedImageState()` 返回 `{true, layout, access, stage, queue}`；`InitializedBufferState()` 返回 `{false, eUndefined, ...}`
- **理由**：消除 `isImage=true` 在 buffer 上下文中的语义混淆，防止未来代码误判

### D4: GPL library 即时清理

- **选择**：LinkPipeline 失败时，遍历本次创建的 4 个 library part 调用 `device.destroyPipeline` 并从 `m_CreatedPipelines` 移除
- **理由**：当前 library part 一直存活到 session 结束，每帧泄漏累积 VRAM
