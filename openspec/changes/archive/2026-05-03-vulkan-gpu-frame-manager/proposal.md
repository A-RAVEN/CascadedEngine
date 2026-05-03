## Why

当前 Vulkan 后端采用同步执行模式：`CompileAndExecute` 在单帧内完成所有资源分配、命令录制、提交和等待，`waitForFences(UINT64_MAX)` 阻塞 CPU 直到 GPU 完成才返回。这导致 CPU 和 GPU 完全串行工作，无法利用多帧重叠来掩盖 CPU 编译/录制延迟。D3D12 后端通过 `GPUFrameManager` 实现了 N 帧并行流水线，Vulkan 后端必须补齐帧管理子系统才能达到同级别的性能上限。

## What Changes

- 新增 `VulkanGPUFrameManager`：管理 N 个 `VulkanFrameContext` 的 round-robin 调度，支持可配置的 `maxFrameCount`
- 新增 `VulkanFrameContext`：每帧独立的资源集封装，包含 fence/semaphore 同步原语 + `VulkanFrameBoundResourceManager`
- 新增 `VulkanFrameBoundResourceManager`：每帧独立的描述符池、命令列表管理器、线性上传内存、别名内存分配器、Fence 对
- 新增 `VulkanLinearMemoryManager`：从预分配大块 staging buffer 中线性分配，替代当前独立 VMA 分配/释放模式
- 改造 `VulkanGraphExecutor`：从 "一次性执行器" 变为 "每帧执行器"，运行在 FrameContext 的资源上下文下
- **BREAKING**: `CompileAndExecute` 接口语义变更 — 不再阻塞等待 GPU，改为提交后立即返回，帧完成由下一轮 `AquireFrameContext` 保证
- 改造 `SubmitBatches`：去掉 `waitForFences(UINT64_MAX)`，改为记录 fence value 并在下一帧 Aquire 时等待

## Capabilities

### New Capabilities
- `vulkan-frame-manager`: Vulkan 帧管理子系统，实现多帧重叠渲染架构，管理每帧独立的资源分配器和同步原语

### Modified Capabilities
- `vulkan-backend-alignment`: 更新 Phase 2 中 GPUFrameManager、异步 Submit、LinearMemoryManager 三项的状态（从"未开始"到"已完成"）

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/.cpp`：重构执行流程，从 FrameContext 获取资源
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.h/.cpp`：持有 `VulkanGPUFrameManager` 实例，改造 `CompileAndExecute` 入口
- `VulkanRenderBackendNew/private/ResourceManagement/`：新建 `VulkanFrameManager.h/.cpp`、`VulkanLinearMemoryManager.h/.cpp`
- `VulkanRenderBackendNew/private/GPUGraph/VulkanConstantBufferManager.h/.cpp`：staging buffer 分配改用 `VulkanLinearMemoryManager`
- `openspec/specs/vulkan-backend-alignment/spec.md`：更新 Phase 2 进度

## Non-goals

- 跨队列同步（Phase 2 第 4 项）不在本 Change 范围内，后续单独处理
- SamplerManager 不在本 Change 范围内（Phase 3）
- ApplyExternalResourceStates 不在本 Change 范围内（Phase 3）
