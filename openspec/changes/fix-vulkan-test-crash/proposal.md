## Why

`GPUBackendTester.exe --backend vulkan` 退出阶段崩溃。以下按"已确认事实"与"会话观察（证据已丢失）"分层记录：

**已确认事实：**
1. **PRESENT semaphore 复用 VUID 违规**：validation log（`test_output/TestSimpleTriangle_validation.log`）记录 2 次 `VUID-vkQueueSubmit-pSignalSemaphores-00067`——`pSubmits` 元素的 `pSignalSemaphores` 中每个 binary semaphore 在被 signal 时必须处于 unsignaled 状态。触发对象是 **PRESENT semaphore 被重复 signal**（VkSwapchainKHR 提示 image 已被 present 但未 re-acquire），非 acquire semaphore。参考本地文档 `docs/vulkan-api-docs/refpages/vkQueueSubmit.md`。
2. **mimalloc 假设已被证伪（不构成嫌疑）**：`GPUBackendTester.exe` 仅从 mimalloc-debug.dll import `mi_version`（`Main.cpp:814` 调用 `mi_version()`），无 `mi_malloc`/`mi_free`/`mi_zalloc`，无 operator new/delete 覆写；`CACore/private/MimallocImpl.cpp:1` 的 `mimalloc-new-delete.h` 覆写被注释；运行日志明示 `mimalloc-redirect ... standard malloc is _not_ redirected`。EXE 与 Vulkan DLL 均在系统（ucrtbase）堆上分配，**不存在"EXE 用 mimalloc 堆、DLL 用系统堆"的跨模块堆不配对**。

**会话观察（留存证据已被覆盖/丢失，需重新确证）：**
3. **退出清理路径崩溃**：2026-08-08 复现（交互 3578 帧后手动关窗、headless 200/4000 帧），`VulkanWindowHandle released` 日志打印后、`VulkanRenderBackend released` 打印前崩溃，bash 报告 SIGSEGV / exit 139。**现存日志无法复核**：`test_output/headless200_stdout.txt` 止于 `Submit Count:1444`（shader import 的累计 I/O 提交计数，`CACore/header/Serialization.h:22/202`，正常输出、非崩溃特征值）；`TestSimpleTriangle.log` 已被 d3d12 运行覆盖（现仅 29 字节）。
4. **D3D12 后端退出阶段崩溃**：headless 200 帧，shader import 后（`Submit Count:1452`）SEH 触发但栈空。崩溃相位与 vulkan（声称的退出清理路径）不同，**不构成"两个独立后端共享崩溃 → 框架级内存问题"的强证据**，作为独立待查现象并列记录。

历史背景：早期 baseline（`ea9dd11`）在 `Submit Count:1444` 后崩溃、渲染从未开始。`fix-vulkan-api-correctness` 修复后渲染成功启动，暴露出更晚的崩溃点。

## What Changes

- 重新确证退出清理路径崩溃：在 `RenderBackend_Vulkan::Release()` 各销毁步骤间插入临时日志 + headless 复现，定位崩溃步骤
- **确定性清理修复**：将 `m_GPUFrameManager.WaitIdle()` 前移到 window 清理循环之前（当前 window 销毁先于 WaitIdle，present 未完成即销毁 swapchain 属 UAF 候选）
- 修复 PRESENT semaphore 复用 VUID：按 swapchain image index 分槽复用 acquire/present semaphore，保证 re-signal 前已 unsignaled
- 回归验证：headless 200/4000 帧 + non-headless 手动关窗，确认退出无崩溃

## Capabilities

### New Capabilities
无

### Modified Capabilities
无

> 边界说明：本 change 是内部 bug 修复，无行为契约变化，无需新增/修改 spec。若涉及 CACore 链接调整，仅影响构建侧、经 PUBLIC 传播至全部 CACore 消费者（含 D3D12），列入 design Risks 全量回归范围。

## Impact

- `GPUBackendTester.exe` — 测试运行时行为
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` — 退出清理路径（WaitIdle 前移、window/资源销毁顺序）
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanFrameManager.cpp` — frame context / window sync semaphore 复用
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanWindowHandle.cpp` — acquire/present semaphore 生命周期
