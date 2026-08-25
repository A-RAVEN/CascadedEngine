## Why

`GPUBackendTester.exe --backend vulkan` 退出阶段崩溃。经插桩实证 + 对抗验证（workflow, 32 findings / 27 confirmed），根因状态分层如下：

**已确认事实：**

1. **headless 退出崩溃在 ThreadManager.DLL 偏移 0x405C，worker 线程，发生在其自身 teardown（Stop）期间——真正根因未确立**
   - 崩溃模块 = `ThreadManager.DLL`（非 Vulkan backend、非 CRT 堆、非 vulkan 驱动）；Vulkan 与 D3D12 后端同偏移崩溃 → 与 GPU 后端无关
   - 崩溃线程 = worker（tid 140904）；Stop 在主线程（tid 79184）；`WorkLoop exit` 标记 = 0 → 无 worker 干净退出
   - 崩溃发生在**所有 1445 个 scheduler 全部销毁之后**（日志：…worker released → 3×scheduler deleter → ~ThreadManager_Impl → Stop → CRASH）
   - **已证伪**：scheduler-delete 竞态（原 D5）——`RenderBackend_Vulkan::ExecuteGraph` 完全忽略 scheduler 参数、per-frame scheduler 为空、崩溃时无存活 scheduler，scheduler-delete UAF 物理上不可能
2. **PRESENT semaphore 复用 VUID-00067（D3 代码正确，实证待补）**
   - `docs/vulkan-api-docs/refpages/vkQueueSubmit.md:127-131` 含 VUID-00067"signal 时须 unsignaled"，引用真实无失真
   - D3 按 `window × swapchain image` 分槽的代码结构经对抗验证确认正确（索引安全、`vkDestroySemaphore` VUID-05149 满足、acquire 逐帧串行化安全）
   - **实证短板**：最近一次 headless 运行的 validation log 为 **0 字节**，无法确认"VUID 已消除"——需一次非空 validation log 的 headless 运行复验
   - **未校验前提**：uniform-imageCount 假设（多窗口 imageCount 不同时 present semaphore 会跨 window 碰撞）
3. **mimalloc 假设已证伪（复核确认）**：EXE 仅 import `mi_version`；DLL 无 `mi_*` 引用；覆写被注释；两模块共用系统堆。不做链接改动

**对抗验证结论（round 1，2026-08-15）**：32 findings，27 确认。最重一条 **critical**：原 D5 根因被证伪。其余确认项：D3 代码正确但有实证短板、PresentWindows 空 finalize 越界、artifacts 多处措辞/遗漏问题。5 条反驳（含"Stop 边界未定义""OUT_OF_DATE 残留 semaphore"等，均被证伪）。

## What Changes

- **ThreadManager teardown 期崩溃根因（已确立并修复，round 3 实证）**：TimerSystem teardown UAF——`~ThreadManager_Impl` 置空自身 gCPUTimer 拷贝（TimerSystem 是 STATIC 库、全局每模块一份拷贝，round 2 的 `~TimerSystem_Impl` 置空无效已修正）；vulkan headless1 退出码 0
- **d3d12 第二独立崩溃 0x87D（round 3 新发现，[AUDIT]）**：D3D12 debug layer 在 `~RenderBackend_D3D12` 成员析构期抛异常，基线被 0x3405C 掩盖——定位并修复（Section 6）
- **D3**：PRESENT semaphore 按 (window × image) 分槽——代码已实现且经对抗验证正确；补非空 validation log 的实证复验
- **D3 补强**：uniform-imageCount 假设加运行时断言（已实现）；修 `PresentWindows` 空 finalize 越界（已实现）
- **D1**：`WaitIdle` 前移——已实现，防御性保留（non-headless 正确，headless 无效）
- **m_Instances 断言修复**（round 2 CRITICAL，已实现）：实例释放后清空容器，退出无 `__debugbreak`
- 移除临时诊断插桩（`[Release]` / 9 处 `[TM]` / D3D12 `[D3D12REL]`/`[MM]`/InfoQueue 均已移除，e8595ac 完成），保留 MiniDump 崩溃模块解析（已增强 base+RVA）
- 回归验证：headless 1/200/4000 + non-headless + D3D12 双后端 + 非空 validation log

## Capabilities

### New Capabilities
无

### Modified Capabilities
无

> 边界说明：本 change 是内部 bug 修复，无行为契约变化，无需新增/修改 spec。ThreadManager 是框架公共模块（Vulkan+D3D12 共用），改动影响面广，列入 Risks 全量回归范围。

## Impact

- `ThreadManager/private/ThreadManager_Impl.cpp` — teardown 期崩溃根因修复（定位后实施）
- `ThreadManager/private/ThreadManager_Impl.h` — 若修复需新增成员（潜在）
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` — D1 WaitIdle 前移（已实现）+ 移除 [Release] 插桩
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanFrameManager.{h,cpp}` — D3 分槽（已实现）+ uniform 假设防护
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` — D3 索引（已实现）+ PresentWindows 空 finalize 守卫
- `Test/GPUBackendTester/private/MiniDump.cpp` — 崩溃模块解析（保留）
