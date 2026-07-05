# Design: 修复 Command Buffer 分配失败 + Crash Handler stdout 刷新

**Change ID**: fix-cmd-buffer-alloc-failure
**Created**: 2026-07-05

---

## Context

`fix-vulkan-subobject-app-pointer` 修复后，`GPUBackendTester` 的 Vulkan 测试从 Phase 4 推进到 Phase 7（Execute），但在 `VulkanCommandListManager::AllocateCommandBuffer` 中触发 `CA_ASSERT_BREAK`。

汇编分析确认：`allocateCommandBuffers` 调用返回了空的 `std::vector<CommandBuffer>`（非抛异常），编译器优化掉了 `VK_RESULT_CHECK` 的条件分支，直接进入 `CA_LOG_ERR` + `__debugbreak()`。

Command pool 状态已确认：pool handle 有效（非 null），`commandBufferCount = 1`，pool 在 `Aquire()` 中经 `resetCommandPool` 重置后第一次分配。`GetDevice()` 返回的 device 与创建 pool 时的 device 一致。

**核心障碍**：Vulkan validation layer 的报错和 `CA_LOG_ERR` 的输出因 `TerminateProcess` 前 stdout 未 flush 而丢失。

## Goals / Non-Goals

**Goals:**
- Crash handler 在 `TerminateProcess` 前 flush stdout + stderr，确保诊断信息可见
- 定位并修复 `allocateCommandBuffers` 返回空的根因

**Non-Goals:**
- 不改变 pApp 传播机制
- 不改变 GPUGraph 执行流程

## Decisions

### D1: Crash handler flush stdout/stderr

在 `MiniDump.cpp::ApplicationCrashHandler` 中，`TerminateProcess` 之前增加：

```cpp
fflush(stdout);
fflush(stderr);
```

**原理**: 没有额外风险，`fflush` 在 SEH 上下文中安全。确保 `CA_LOG_ERR`、`fmt::print`、Vulkan validation callback 的所有输出落盘。

### D2: 根因定位策略

流程：
1. 添加 flush → 重新编译运行
2. 查看 `test_output/TestSimpleTriangle.log` 中的 Vulkan validation 报错
3. 根据具体报错确定修复方案

**备选方案**: 预判为 pool 标志位问题或 queue family 不匹配 → 拒绝，等看到实际报错再决定。

## Risks / Trade-offs

- **实际根因可能复杂**: 如果 Vulkan validation 没有报错（纯 driver bug），则需进一步手段（如 GPU 调试工具）。
  → 缓解：至少 crash handler flush 后能看到 `CA_LOG_ERR("Vulkan Result Check Failed!")` 的上下文信息。

## Open Questions

- `allocateCommandBuffers` 返回空的根因（待 validation 报错揭示后确定）
