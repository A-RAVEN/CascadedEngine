# Tasks: 修复 Command Buffer 分配失败 + Crash Handler stdout 刷新

**Change ID**: fix-cmd-buffer-alloc-failure
**Created**: 2026-07-05

---

## 1. Crash Handler stdout 刷新

- [x] 1.1 修改 `Test/GPUBackendTester/private/MiniDump.cpp`：在 `TerminateProcess(GetCurrentProcess(), 1)` 之前增加 `fflush(stdout)` 和 `fflush(stderr)`

## 2. 编译并运行，获取诊断信息

- [x] 2.1 运行 `build.py`，编译成功
- [x] 2.2 运行测试，诊断输出揭示根因：VMA AllocateMemory 返回 VK_ERROR_FEATURE_NOT_PRESENT (-8)，2MB aliased pool 分配失败导致 AllocateAliasedResources 返回 false

## 3. 根因修复

- [x] 3.1 修复内容：
  - `VulkanGraphExecutor::AllocateAliasedResources()` 改为返回 bool，传播底层返回值
  - `CompileAndExecute` 检查返回值，失败时 abort frame 并 return
  - `VulkanFrameBoundResourceManager` 增加 `m_FenceSubmitted` 标记，防止未提交的 fence 在下一帧 Aquire 时导致 hang
  - Swapchain acquire 移到 AllocateAliasedResources 检查之后，避免 abort 帧占用 swapchain image
- [x] 3.2 编译验证：BUILD SUCCESSFUL
- [x] 3.3 测试：帧优雅中止（不再 crash 到 CA_ASSERT_BREAK），D3D12 不受影响。cleanup 阶段仍有 crash（VMA aliased pool 分配失败的深层根因，属独立问题）
