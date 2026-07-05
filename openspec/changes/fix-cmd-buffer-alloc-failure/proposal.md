# Proposal: 修复 Command Buffer 分配失败 + Crash Handler stdout 刷新

**Change ID**: fix-cmd-buffer-alloc-failure
**Status**: Proposed
**Created**: 2026-07-05

---

## Why

`fix-vulkan-subobject-app-pointer` 修复后，测试推进到 Phase 7（Execute），但在 `VulkanCommandListManager::AllocateCommandBuffer` 中 `allocateCommandBuffers` 返回空 vector 触发 `CA_ASSERT_BREAK`。当前 crash handler 在 `TerminateProcess` 前未 flush stdout，导致 Vulkan validation layer 的报错和 `CA_LOG_ERR` 的输出丢失，无法定位根因。

## What Changes

- **Crash handler stdout flush**: `MiniDump.cpp` 中 `TerminateProcess` 前增加 `fflush(stdout)`，确保崩溃前的 buffered 日志落地
- **Command buffer 分配**: 调查并修复 `allocateCommandBuffers` 返回空的根因（具体修复方案取决于 visible 后的 validation 报错内容）

## Capabilities

### Modified Capabilities

- `crash-diagnostics`: crash handler 终止进程前 SHALL flush stdout，确保 CA_LOG 和 Vulkan validation layer 输出不丢失

### New Capabilities

- `command-buffer-lifecycle`: Vulkan 命令缓冲区的分配、重置、生命周期管理 SHALL 在所有帧（包括首帧）正常工作

## Impact

- **Test/GPUBackendTester/private/MiniDump.cpp** — `TerminateProcess` 前增加 `fflush(stdout)`
- **VulkanRenderBackendNew/private/** — 具体文件取决于 validation 报错揭示的根因

## Non-goals

- 不改变 pApp 传播机制（已在 `fix-vulkan-subobject-app-pointer` 中修复）
- 不改变 GPUGraph 执行流程
