# Proposal: 修复 Vulkan 命令缓冲区分配失败

**Change ID**: fix-vulkan-command-buffer-allocation
**Status**: Proposed
**Created**: 2026-07-15

---

## Why

`VulkanGraphExecutor::RecordBatchCommands()` 调用 `GraphicsCommand()` 时，FrameContext 的 `VulkanCommandListManager` 从 `m_GraphicsPool` 分配命令缓冲区失败（`vkAllocateCommandBuffers` 返回空 vector），触发 `VK_RESULT_CHECK` 断言崩溃。此 bug 导致 Vulkan 后端**无法运行任何测试**（包括 `TestSimpleTriangle`），阻塞了所有后续的端到端验证工作。

注意：全局 `VulkanCommandListManager`（在 `RenderBackend_Vulkan` 中）初始化正常，但 FrameContext 内嵌的 `VulkanCommandListManager` 在 pool 重置后首次分配即失败。

## What Changes

- **诊断增强**：在 `AllocateCommandBuffer` 中捕获并记录 `vkAllocateCommandBuffers` 失败的实际错误信息。当前代码在 throwing 模式下返回 `std::vector<vk::CommandBuffer>`（非 `ResultValue`），失败时可能抛异常或返回空 vector。需要 try-catch 包裹调用以捕获 `vk::SystemError` 并输出诊断信息，为精准定位根因提供信息
- **Init 流程审查**：审查 `VulkanFrameContext::Aquire()` → `Reset()` → `GraphicsCommand()` 调用链中 pool 生命周期是否正确。经过对抗审查确认：两条初始化路径（全局 `RenderBackend_Vulkan` 和 FrameContext）等价，均使用零参 `InitSubObj`（仅 `SetApp`）+ 显式 `Init()`，不存在"双重初始化"问题
- **首次帧 Reset 跳过**：在 `VulkanFrameContext::Aquire()` 中，首次帧（`m_FirstFrame == true`）时跳过 `resourceManager.Reset()`。首次帧所有状态（`m_GraphicsCommand`、fence、semaphore）都是 Init 后的默认值，对从未分配过的 pool 调用 `resetCommandPool` 不必要，且某些驱动可能对此场景行为异常——恰好导致"创建成功 → reset → 分配失败"的症状（对抗审查发现）
- **修复方案**：根据诊断结果实施具体修复。可能的方向包括：
  - 修复 pool 的创建参数（queue family index 或 flag）
  - 修复 pool 重置逻辑（若 `resetCommandPool` 后 pool 状态异常）
- **Release() 关闭顺序修复**：`RenderBackend_Vulkan::Release()` 在调用 `m_Device.waitIdle()` 之前就销毁了 FrameContext 的 pool/fence/semaphore，违反 Vulkan 规范。需要在 `m_GPUFrameManager.Release()` 前调用 `m_GPUFrameManager.WaitIdle()`

## Capabilities

### New Capabilities
- `vulkan-command-buffer-allocation`: Vulkan 命令缓冲区从 FrameContext pool 的创建和分配的正确性——涵盖 pool 初始化、设备一致性、reset 后复用、错误码诊断

### Modified Capabilities
- （无）本变更不修改已有 spec 的需求

## Impact

- **VulkanCommandListManager.cpp** `AllocateCommandBuffer()`: 添加 VkResult 日志输出
- **VulkanCommandListManager.h/.cpp**: 审查并修复 `Init()` / `Reset()` / `GraphicsCommand()` 流程
- **VulkanFrameBoundResourceManager.cpp** `Init()`: 审查 `InitSubObj` + 显式 `Init()` 的双重调用路径
- **VulkanFrameContext.cpp** `Aquire()`: 审查 pool reset 时机

## Non-goals

- 不修改全局 `VulkanCommandListManager`（`RenderBackend_Vulkan::m_CommandListManager`，该实例工作正常）
- 不修改命令缓冲区的录制内容（`RecordRenderPass` 等）
- 不修改 `VulkanFrameManager` 的帧调度逻辑
- 不引入新的第三方依赖
