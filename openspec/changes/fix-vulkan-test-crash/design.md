## Context

`GPUBackendTester.exe` 退出阶段崩溃。事实与观察分层：

**已确认事实（可复核）：**
1. **PRESENT semaphore 复用 VUID**：validation log 2 次 `VUID-vkQueueSubmit-pSignalSemaphores-00067`。语义：`pSubmits` 的 `pSignalSemaphores` 中每个 binary semaphore 被 signal 时必须已 unsignaled（[VkQueueSubmit](docs/vulkan-api-docs/refpages/vkQueueSubmit.md)）。触发的是 PRESENT semaphore 被重复 signal（swapchain image 已 present 未 re-acquire）。
2. **mimalloc 假设已证伪**：EXE 仅 import `mi_version`（Main.cpp:814），无 `mi_malloc`/`mi_free`/new-delete 覆写；`MimallocImpl.cpp:1` 覆写被注释；`standard malloc is _not_ redirected`。EXE 与 Vulkan DLL 均走系统堆。**无跨模块堆不配对**。
3. **VulkanRenderBackend.dll 链接线已含 mimalloc**：build.ninja:8015 的 DLL 链接命令含 `lib\mimalloc-debug.dll.lib`（经 CACore PUBLIC 传递），但 DLL 二进制无任何 `mi_*` 导入（符号被链接器丢弃）——"补链接 mimalloc"是 no-op，需区分"链接线含目标"与"二进制引用符号"两层。

**会话观察（留存证据已被 d3d12 运行覆盖，不可复核）：**
4. 2026-08-08：vulkan 交互 3578 帧后关窗、headless 200/4000 帧，均崩溃于 `VulkanWindowHandle released` 打印后、`VulkanRenderBackend released` 打印前（bash SIGSEGV/exit 139）。现存 `headless200_stdout.txt` 止于 `Submit Count:1444`；`TestSimpleTriangle.log` 被覆盖。**需重新确证**。
5. D3D12 headless 200 帧：shader import 后（`Submit Count:1452`）SEH 触发但栈空。崩溃相位与 vulkan 不同，并列待查，不做"框架级问题"强结论。

## Goals / Non-Goals

**Goals:**
- 重新确证退出清理路径崩溃（插临时日志 + headless 复现），定位崩溃步骤
- 修复确认的清理顺序问题（WaitIdle 前移）
- 修复 PRESENT semaphore 复用 VUID
- 回归：headless 200/4000 帧 + non-headless 手动关窗

**Non-Goals:**
- 不修改 GPU 资源生命周期管理架构（除非问题根因涉及）
- 不引入新的调试基础设施（除非现有日志不足以定位）

## Decisions

### D1: 退出清理路径崩溃定位 + 确定性顺序修复

**已排除的候选**（代码顺序已证实安全）：
- `m_GPUFrameManager.WaitIdle()`（:455）先于 `m_GPUFrameManager.Release()`（:456），`VulkanFrameContext::Release()` 的 fence/semaphore/descriptorPool 销毁有 GPU idle 保证——**不是首查对象**。

**确定性修复（直接写入，非待验证候选）**：
- 当前 `RenderBackend_Vulkan::Release()` 顺序为：framebuffer destroy（:435-442）→ window handle Release（:444-452）→ WaitIdle（:455）→ Release（:456）。
- window 的 `CleanupSwapchain` 销毁 swapchain imageViews，发生在 WaitIdle **之前**。若 GPU 仍有 in-flight present/acquire，销毁 swapchain 属 UAF。
- **修复**：将 `m_GPUFrameManager.WaitIdle()` 前移到 window 清理循环之前。此修复对 non-headless（手动关窗，GPU 可能有未完成 present）是必要修复；headless 场景测试器已在退出前 WaitIdle 兜底，非 headless 崩溃根因，但顺序修正无害且正确。

**定位步骤**：
1. 在 `RenderBackend_Vulkan::Release()` 每个销毁步骤间插入临时日志（window 清理后 → WaitIdle 前移后 → pipeline cache serialize → renderPass/shaderModule/pipelineLayout/setLayout destroy → commandList/sampler/memoryManager release → device destroy）
2. headless 200 帧复现，确认崩溃步骤
3. **插桩重点前移**：pipeline cache serialize/getPipelineCacheData（:458-481）、renderPass/shaderModule/pipelineLayout 等 cache destroy（:490-512）、`m_MemoryManager.Release()`（:521）
4. 若插桩确认崩溃与顺序无关，才考虑 ASan/Application Verifier 捕获首次越界写（SEH 不触发表明栈损坏可能来自更早的运行期越界）

### D2: mimalloc 假设——已证伪，作为排除项

对抗验证（dumpbin + 二进制 grep + 源码）证实：

- `GPUBackendTester.exe`：仅 import `mi_version`（无 `mi_malloc`/`mi_free`/`mi_zalloc`，无 operator new/delete 覆写）→ new/delete/malloc/free 全走系统堆
- `VulkanRenderBackend.dll`：无任何 `mi_*` 引用 → 系统堆
- `MimallocImpl.cpp:1` 的 `mimalloc-new-delete.h` 覆写被注释
- 运行日志：`standard malloc is _not_ redirected`

**结论**：两模块共用同一 CRT（ucrtbase）堆，"跨 EXE↔DLL 堆不配对"前提不成立，**排除 mimalloc 为根因**。不做任何链接改动（DLL 链接线已含 mimalloc-debug.dll.lib，补链接是 no-op）。

**遗留说明**：`mi_version()` 导入 + `mimalloc-redirect.dll` 随 mimalloc-debug.dll 传递加载是日志警告来源，但未导致实际堆分裂，与崩溃无关。

### D3: PRESENT semaphore 复用 VUID 修复

**根因修正**：`VUID-vkQueueSubmit-pSignalSemaphores-00067` 由 **PRESENT semaphore** 在 `vkQueueSubmit` 的 `pSignalSemaphores` 中被重复 signal 触发（validation log 原文：swapchain image 已 present 但未 re-acquire）。不是 acquire semaphore 连续 signal。

**现状**（对抗验证核对 `VulkanFrameManager.cpp`）：
- `EnsureWindowSync`（:238-249）按窗口位置为每个 slot 创建 acquire+present 一对 semaphore
- `VulkanFrameContext::Release()`（:180-187）销毁这些 semaphore
- 索引按"窗口位置"，未按 swapchain image index——多个 swapchain image 复用同一对 semaphore，present 后未 re-acquire 即 re-signal

**修复方案**：
1. semaphore 归属保持在 `VulkanFrameContext`（与现 `m_WindowSyncs` 生命周期一致），**不**移到 `VulkanWindowHandle`（其 Release 在 RenderBackend_Vulkan.cpp:449 早于 WaitIdle :455，会破坏"先 WaitIdle 再销毁"顺序）
2. 索引从"window 位置"改为"window × swapchain image 槽位"（如 `m_WindowSyncs[windowIdx * imageCount + imageIndex]`），每个 image 独立 acquire/present semaphore
3. 销毁点保持 `VulkanFrameContext::Release()`（在 D1 前移的 WaitIdle 之后），符合 VUID-vkDestroySemaphore 前提

### D4: `-8` 内存分配失败（历史遗留，降级观察项）

原 proposal（提交 `a90472a` 版本）记录 non-headless 多帧 `Failed to allocate raw Vulkan memory: -8`。当前测试（headless 200/4000 帧）未复现、无 `-8` 日志留存。**保留为低优先级观察项**，仅在上文 D1/D3 修复后仍出现时处理。

## Risks / Trade-offs

- [证据已丢失] 退出崩溃的确定性复现依赖重新插桩确认；现存日志被覆盖，无法复用旧观测
- [栈损坏] SEH 不触发表明可能来自运行期越界写，若顺序修复无效需 ASan/AppVerifier
- [链接改动] 本 change **不做** CACore 链接改动（mimalloc 排除），规避全量回归风险；若未来需要统一分配器另立 change
