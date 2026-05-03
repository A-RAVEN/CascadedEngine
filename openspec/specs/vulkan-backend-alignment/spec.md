# Vulkan后端 D3D12 对齐路线图

**Version**: 1.0
**Created**: 2026-05-02
**Status**: Active
**参考文档**: `Documents/Vulkan后端与D3D12后端对齐分析.md`

---

## 概述

本规范追踪 VulkanRenderBackendNew 在接口功能上向 D3D12RenderBackend（参考实现）对齐的阶段计划。
每个 Phase 列出差距项、优先级、对应 Change。

---

## Phase 1: 核心功能补全 (使后端可用)

目标：Vulkan 后端能完整执行 GPUGraph 的所有 Pass 类型。

| # | 模块 | 差距 | 优先级 | Change | 状态 |
|---|------|------|--------|--------|------|
| 1 | Shader Module | ShaderModule 创建 | P0 | - | ✅ 已完成 |
| 2 | Pipeline Layout | PipelineLayout + DescriptorSetLayout 缓存 | P0 | - | ✅ 已完成 |
| 3 | Pipeline 创建 | Graphics + Compute Pipeline 完整流程 | P0 | - | ✅ 已完成 |
| 4 | Descriptor 流程 | DescriptorPool → Allocate → Write → Bind | P0 | - | ✅ 已完成 |
| 5 | CBuffer 流程 | ShaderStruct → staging upload → barrier | P0 | - | ✅ 已完成 |
| 6 | **Compute Pass 资源注册** | CollectResources 中 Compute image/buffer 注册 | P0 | `vulkan-compute-pass-resources` | ✅ 已完成 |
| 7 | **RenderPass 格式转换** | 消除硬编码 `eD32Sfloat` / `eR8G8B8A8Unorm` | P1 | `fix-vulkan-renderpass-format-hardcoding` | ✅ 已完成 |

---

## Phase 2: 性能与同步

目标：达到与 D3D12 同级别的帧性能和异步能力。

| # | 模块 | 差距 | 优先级 | 依赖 | 状态 |
|---|------|------|--------|------|------|
| 1 | **GPUFrameManager** | 无帧管理子系统，每次 ExecuteGraph 创建/销毁所有资源 | P0 | Phase 1 完成 | ✅ 已完成 |
| 2 | **异步 Submit** | `SubmitBatches` 中 `waitForFences(UINT64_MAX)` 同步等待 | P0 | GPUFrameManager | ✅ 已完成 |
| 3 | **LinearMemoryManager** | 每个 staging buffer 独立 VMA 分配，无复用 | P1 | - | ✅ 已完成 |
| 4 | **跨队列同步** | Barrier 始终 `VK_QUEUE_FAMILY_IGNORED` | P1 | GPUFrameManager | ❌ 未开始 |

### GPUFrameManager 细节

当前 Vulkan 是同步执行模式：`ExecuteGraph` 阻塞直到 GPU 完成。D3D12 有完整的多帧重叠架构：

| 维度 | D3D12 | Vulkan (当前) |
|------|-------|---------------|
| 帧管理 | `GPUFrameManager` + `FrameContext` | 无，单帧内完成 |
| 资源分配器 | 每帧独立 Descriptor/Heap/Staging/Aliased | 每次 re-create |
| 同步 | Fence per frame (Direct + Compute) | `waitForFences(..., UINT64_MAX)` |
| 命令列表 | 每帧独立 CommandListManager | 临时创建 |

---

## Phase 3: 架构完善

目标：补齐低优先级的架构差异。

| # | 模块 | 差距 | 优先级 | 状态 |
|---|------|------|--------|------|
| 1 | SamplerManager | 无独立 sampler 管理器，每帧重建 sampler | P2 | ❌ 未开始 |
| 2 | ApplyExternalResourceStates | 空函数，不更新外部资源状态 | P2 | ❌ 未开始 |
| 3 | RunTestCode | 测试入口未实现 | P3 | ❌ 未开始 |

---

## Vulkan 独有优势（非差距）

这些是 Vulkan 后端已具备、D3D12 没有或不具备的能力：

| 特性 | 说明 |
|------|------|
| Pipeline Library (GPL) | `VK_EXT_graphics_pipeline_library` 支持管线部件独立编译 |
| Vulkan HPP | C++ 封装的 `vk::*` 类型，现代 API 风格 |
| Resource Aliasing | 基于生命周期的别名算法，不依赖 Virtual Block |
| Explicit Descriptor Sets | 每个 set 独立的布局，比 flat binding 更具表现力 |

---

## 变更历史

| 日期 | 变更 | 描述 |
|------|------|------|
| 2026-05-01 | CBuffer + Descriptor 完成 | 关闭 Phase 1 项目 4-5 |
| 2026-05-01 | Image Sampler 分离 | 修复 combined image sampler 问题 |
| 2026-05-02 | Compute Pass 资源注册 | 关闭 Phase 1 项目 6 |
| 2026-05-02 | RenderPass 格式转换 | 关闭 Phase 1 项目 7 |
