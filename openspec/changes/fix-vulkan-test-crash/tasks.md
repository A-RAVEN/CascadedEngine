## 1. 退出清理路径崩溃定位

- [ ] 1.1 确定性修复：将 `m_GPUFrameManager.WaitIdle()` 前移到 window 清理循环之前（`RenderBackend_Vulkan::Release()`，当前 window 销毁 :444-452 先于 WaitIdle :455）
- [ ] 1.2 在 `RenderBackend_Vulkan::Release()` 各销毁步骤间插入临时日志（window 清理后 → WaitIdle → pipeline cache serialize → renderPass/shaderModule/pipelineLayout/setLayout destroy → commandList/sampler/memoryManager release → device destroy）
- [ ] 1.3 headless 200 帧复现，确认崩溃在哪一步（headless 确定性复现；现存 headless200_stdout.txt 止于 Submit Count:1444 无法复核，需本次重新确认）
- [ ] 1.4 插桩重点：pipeline cache serialize/getPipelineCacheData（:458-481）、renderPass/shaderModule/pipelineLayout cache destroy（:490-512）、m_MemoryManager.Release（:521）。`VulkanFrameContext::Release()` 销毁顺序已被代码排除（WaitIdle :455 先于 Release :456），不做首查
- [ ] 1.5 若插桩确认崩溃与清理顺序无关，改用 ASan / Application Verifier 捕获首次越界写（SEH 不触发表明可能来自运行期越界）

## 2. mimalloc 假设排除（已证伪，复核性确认）

- [ ] 2.1 复核：`GPUBackendTester.exe` 仅 import `mi_version`（无 mi_malloc/mi_free/new-delete 覆写），`VulkanRenderBackend.dll` 无 mi_* 引用（dumpbin 复核）
- [ ] 2.2 复核：`CACore/private/MimallocImpl.cpp:1` 的 mimalloc-new-delete.h 覆写被注释；运行日志 `standard malloc is not redirected`
- [ ] 2.3 结论：两模块共用系统堆，确认排除 mimalloc 为根因。**不做任何链接改动**（VulkanRenderBackend 链接线已含 mimalloc-debug.dll.lib，补链接是 no-op）

## 3. PRESENT semaphore 复用 VUID 修复

- [ ] 3.1 确认当前 semaphore 复用方式：`EnsureWindowSync`（VulkanFrameManager.cpp:238-249）按窗口位置为每 slot 建 acquire+present 一对，未按 swapchain image index 分槽
- [ ] 3.2 修复：索引从"window 位置"改为"window × swapchain image 槽位"，每个 image 独立 acquire/present semaphore
- [ ] 3.3 生命周期：semaphore 归属保持在 `VulkanFrameContext`（不移到 VulkanWindowHandle，其 Release 早于 WaitIdle 会破坏销毁顺序）；销毁点 `VulkanFrameContext::Release()` 在 D1 前移的 WaitIdle 之后
- [ ] 3.4 验证：validation log 中 `VUID-vkQueueSubmit-pSignalSemaphores-00067` 消失

## 4. 回归验证

- [ ] 4.1 headless 200 帧：确认退出无 crash、无 VUID 错误
- [ ] 4.2 headless 4000 帧：确认退出无 crash
- [ ] 4.3 non-headless 手动关窗：确认退出无 crash（对照之前的 3578 帧崩溃观察）
- [ ] 4.4 历史遗留 `-8` 内存分配失败（原 proposal 提交 a90472a 版本）：仅在 D1/D3 修复后仍出现时处理

## 5. 编译验证

- [ ] 5.1 运行 `python build.py --config Debug`，验证 BUILD SUCCESSFUL
- [ ] 5.2 若有编译错误或警告，分析并修复后重新验证

## 6. Review & Adversarial Verify

- [ ] 6.1 对全部修改做对抗验证审查，验证修复正确性（外部 API 正确性按 CLAUDE.md 规则引用本地文档 `docs/vulkan-api-docs/`）
- [ ] 6.2 对抗者须独立复查审查者引用的每个本地文档路径或 URL 的真实性——路径/URL 是否真实存在、所引 API 是否确实在该文档中、语义描述是否一致，并在对抗结果中注明复查过的引用及真实性结论
- [ ] 6.3 每轮审查先做回归检查：核对上一轮已修复的问题是否在本轮回归（修复是否真正生效、是否被后续改动覆盖），在 Review Log 中记录回归检查结果
- [ ] 6.4 新增 [AUDIT] task 前先与 Review Log 已处理问题去重（按根因而非表象/行号判断同一问题）；同一问题不得重复开 task 反复修改——若确认是上一轮已处理问题的回归，审查重点转为"为何修复未生效/被覆盖"并补上缺失的验证，而不是推翻重改
- [ ] 6.5 审查发现的新问题作为 [AUDIT] task 追加，追加新的审查 task，循环直到无新问题或 3 轮

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| - | - | - | - | - | - |
