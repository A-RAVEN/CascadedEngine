## 1. TimerSystem teardown UAF + m_Instances 断言修复（根因已确立，round 2 修订）

> 根因：`~CAModuleManager` 按注册序前向释放，TimerSystem_Impl（注册先）先于 ThreadManager 被 delete → 全局 `gCPUTimer` 悬垂 → worker 唤醒后 `CPUTimerScope("Idle")` 析构对已释放虚表调 EndEvent → 崩溃（RVA 0x3405C，指令级确证）。**round 2 追加：`~CAModuleManager:72` 的 m_Instances 断言恒触发为第二独立崩溃源**（RemoveInstance 零调用点，容器从不被清空）。**round 3 实证修正：TimerSystem 是 STATIC 库（`Interface/TimerSystem/CMakeLists.txt:6`），gCPUTimer/Set/GetGlobalTimerSystem 每模块一份拷贝——`~TimerSystem_Impl` 的 `==this` 置空只清 TimerSystem_Impl.DLL 自己的拷贝（无效）；有效修复是崩溃模块自身（`~ThreadManager_Impl`）置空（详见 design.md D5'）**。

- [x] 1.1 纯判别实验（round 3 实证完成）：**仅置空+守卫、不改注册序**。结果：**机制确证 + 修复落点修正**——round 2 方案（`~TimerSystem_Impl` 的 `==this` 置空）**无效**（per-DLL 拷贝，见上），改为 **`~ThreadManager_Impl` 入口置空自身拷贝** → **vulkan headless1 退出码 0、无崩溃、无新 dump**；d3d12 的 0x3405C worker 崩溃同样消失（d3d12 随后暴露独立 D7 崩溃，见 Section 6）
- [x] 1.2 修复实施：置空 gCPUTimer + 全守卫——**有效修复**：`~ThreadManager_Impl` 入口 `catimer::SetGlobalTimerSystem(nullptr)`（清崩溃模块自身拷贝，round 3 实证）；**防御**：`~TimerSystem_Impl` 加 `==this` 置空；Timer.h 三处解引用加空守卫；TIMER_NEWFRAME 宏改 do-while 守卫；VulkanRendererBackendTester/Main.cpp:85 SetThreadName 加守卫（⚠️ 该 tester 不在默认构建范围，CMakeLists.txt:76 被注释，此改动未编译验证）
- [x] 1.3 **修 m_Instances 断言**（round 2 CRITICAL）：`~CAModuleManager` 实例释放循环后清空 m_Instances，断言不再恒触发 `__debugbreak`
- [x] 1.4 移除 ThreadManager_Impl.cpp 临时 `[TM]` 插桩（**9 处** + `<cstdio>` include，std::cout 走 pch 不受影响）
- [ ] 1.5 验收：headless 退出**进程退出码 0 + 无新增 crash_*.dmp**（非 JSON exit_code——`Main.cpp:1175` 在 ctx 析构前写入，检测不到 teardown 崩溃）

## 2. Vulkan 后端收尾（D3/D1 + 防御性修复）

- [x] 2.1 D3 PRESENT semaphore 按 (window × image) 分槽——已实现，对抗验证确认代码正确
- [x] 2.2 mimalloc 排除复核——已确认
- [ ] 2.3 **D3 实证复验**：跑一次非空 validation log 的 headless 200（确认验证层启用、日志非空、VUID-00067 为 0）
- [x] 2.4 **D3 uniform-imageCount 防护**：EnsureWindowSync 加 `CA_ASSERT`（log-only）"各 window imageCount 一致"断言——跨 window 碰撞触发 VUID 时先在日志暴露，不崩进程
- [x] 2.5 **PresentWindows 空 finalize 守卫**：`PresentWindows(*graph)` 调用复用 acquire 循环的 `if (!graph->GetFinalizePass().isEmpty())` 守卫（修 :2678 空 vector 取 [0] 越界）
- [x] 2.6 D1 确认保留（已定案，仅确认），不移除
- [x] 2.7 移除 RenderBackend_Vulkan.cpp 全部 11 处 `[Release]` 临时 stderr 插桩；**`#include <cstdio>` 保留**——Release() 的 pipeline cache 序列化用到 `fopen`/`fwrite`/`fclose`，验证日志用 `fprintf`
- [ ] 2.8 MiniDump 崩溃模块解析：保留 + 在回归中验证其输出（补入 Section 3 或此处明确验证步骤）；已增强为打印模块 base + RVA

## 6. [AUDIT] d3d12 teardown 0x87D（round 3 新发现，D7）

> 根因：`~RenderBackend_D3D12` 成员析构期间某 D3D12 对象存活/泄漏触发 debug layer（D3D12SDKLayers）抛 `0x87D`（RaiseException，确定性复现）。Release() 完整跑完、InfoQueue 0 消息、SetBreakOnSeverity 无效 → 非 break-on-error。基线被 0x3405C 掩盖，首次暴露（详见 design.md D7）。

- [ ] 6.1 **git stash 对比验证**：stash 本次全部改动 → 重建 → d3d12 headless1 → 确认基线崩在 0x3405C（0x87D 不可达）→ 证 0x87D 为基线掩盖
- [ ] 6.2 定位触发对象：`~RenderBackend_D3D12` 成员析构中触发 debug layer 的对象（各 manager 残留引用 / D3D12MA / command list/allocator/descriptor heap / fence 等），用成员级插桩或 ReportLiveDeviceObjects 输出
- [ ] 6.3 修复泄漏/释放顺序，使设备析构无存活对象
- [ ] 6.4 移除 D3D12 临时诊断插桩（InfoQueue + `[D3D12REL]`/`[MM]` 标记 + 新增 include `<cstdio>`/`directx/d3d12sdklayers.h>` 若无其它用途）
- [ ] 6.5 d3d12 headless1 退出码 0 + 无新 dump

## 3. 回归验证

- [ ] 3.1 headless 1 帧退出无崩溃
- [ ] 3.2 headless 200 帧退出无崩溃（含 2.3 的非空 validation log 检查）
- [ ] 3.3 headless 4000 帧退出无崩溃
- [ ] 3.4 non-headless 手动关窗退出无崩溃
- [ ] 3.5 D3D12 headless 退出无崩溃（双后端回归）
- [ ] 3.6 validation log 无 `VUID-vkQueueSubmit-pSignalSemaphores-00067` 且日志非空
- [ ] 3.7 **验收标准**（round 2）：每次回归记录**进程退出码（须 0）+ 检查无新增 crash_*.dmp**——JSON exit_code 在 ctx 析构前写入，不能作为 teardown 干净的证据

## 4. 编译验证

- [ ] 4.1 运行 `python build.py --config Debug`，验证 BUILD SUCCESSFUL
- [ ] 4.2 若有编译错误或警告，分析并修复后重新验证

## 5. Review & Adversarial Verify

- [ ] 5.1 对全部修改做对抗验证审查（外部 API 正确性按 CLAUDE.md 规则引用本地文档 `docs/vulkan-api-docs/`）
- [ ] 5.2 对抗者独立复查审查者引用的每个本地文档路径/URL 真实性，并在对抗结果中注明复查过的引用及真实性结论
- [ ] 5.3 每轮审查先做回归检查：核对上一轮已修复的问题是否回归（如 D5 证伪后是否仍有遗留引用；D3 代码是否被后续改动覆盖），在 Review Log 记录
- [ ] 5.4 新增 [AUDIT] task 前与 Review Log 去重（按根因；D5 已证伪，不得以相同根因重开 zombie-defer 类任务）
- [ ] 5.5 审查发现的新问题作为 [AUDIT] task 追加，循环直到无新问题或 3 轮

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| 1 | 2026-08-15 | 37 (review workflow) | 32（27 确认/5 反驳） | 原 D5 根因撤回（证伪）；artifacts 按 findings 修正 | 真正根因未确立（H1-H6 调查中）；D3 实证待补；uniform-imageCount/空 finalize 待修 |
| 2 | 2026-08-15 | 29 (fix-design review workflow) | 25（22 确认/3 反驳） | 修复方案修订：弃 swap → 置空+全守卫（顺序无关）+ ==this 守卫 + NewFrame 守卫；新增 m_Instances 断言修复（CRITICAL，第二独立崩溃源）；验收改"进程退出码 0 + 无新 dump" | 修复未实现；D3 实证待补；uniform-imageCount/空 finalize 待修 |
| 3 | 2026-08-15 | apply 实证（无 workflow） | 3（① round 2 置空方案落点错误：TimerSystem 是 STATIC 库、gCPUTimer 每模块一份拷贝，`~TimerSystem_Impl` 置空无效；② d3d12 第二独立崩溃 0x87D：D3D12 debug layer 成员析构期抛异常，基线被 0x3405C 掩盖；③ VulkanRendererBackendTester 不在构建范围，其 SetThreadName 守卫未编译验证） | 置空落点修正为 `~ThreadManager_Impl`（崩溃模块自身拷贝）；vulkan headless1 退出码 0 + 无新 dump；d3d12 0x3405C worker 崩溃消失；m_Instances 断言修复实证通过；D3 uniform-imageCount/空 finalize 防护实现 | d3d12 0x87D 根因对象待定位（Section 6）；D3 非空 validation log 实证待补；VulkanRendererBackendTester 守卫未编译验证 |
