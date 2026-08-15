## Context

`GPUBackendTester.exe` 退出阶段崩溃。经插桩实证 + 对抗验证（round 1 workflow: 32 findings/27 confirmed），根因状态如下：

**已确认事实：**
1. **headless 退出崩溃 = TimerSystem teardown UAF（根因已确立，见 D5'）**：崩溃模块 `ThreadManager.DLL` RVA `0x3405C`，worker 线程，Stop 期间。
2. **VUID-00067 present semaphore 复用（D3）**：代码结构正确（对抗验证确认），实证验证待补。
3. **mimalloc 假设已证伪**。
4. **round 3（apply 实证）修正：TimerSystem 是 STATIC 库，gCPUTimer 每模块一份拷贝**——`~TimerSystem_Impl` 的 `==this` 置空对 worker 崩溃无效；有效修复是崩溃模块自身置空（详见 D5' 修复方向）。
5. **round 3（apply 实证）新发现：d3d12 第二独立崩溃 `0x87D`**——D3D12 debug layer（D3D12SDKLayers）在 `~RenderBackend_D3D12` 成员析构期抛异常，基线被 0x3405C 掩盖，首次暴露（详见 D7）。

## Goals / Non-Goals

**Goals:**
- **确立 ThreadManager teardown 期崩溃的真正根因**并修复
- 保留 D3（对抗验证确认正确）+ 补实证验证与 uniform-imageCount 防护
- D1 防御性保留；修 PresentWindows 空 finalize 越界
- 回归：headless 1/200/4000 + non-headless + D3D12 双后端

**Non-Goals:**
- 不改 ThreadManager 任务调度架构（除非根因涉及）
- 不引入新的调试基础设施（除非必要）

## Decisions

### D1: WaitIdle 前移（已实现，防御性保留）

实证修正：headless 路径 `Main.cpp:118` 在 windowHandle 析构前已显式 WaitIdle——headless 崩溃不是 present-in-flight UAF。D1 对 non-headless（手动关窗）仍正确：window 的 `CleanupSwapchain` 销毁 swapchain imageViews 前确保 GPU idle。

**已实现**：`m_GPUFrameManager.WaitIdle()` 从 Release() window 循环后前移到函数顶部（framebuffer destroy 之前）。**已定案保留**（tasks 仅确认，不再开放决策）。

### D2: mimalloc 排除（已复核确认）

dumpbin + 源码复核：EXE 仅 import `mi_version`，DLL 无 `mi_*` 引用，`MimallocImpl.cpp:1` 覆写被注释。两模块共用系统堆。**不做任何链接改动**。

### D3: PRESENT semaphore 按 (window × image) 分槽（已实现，对抗验证确认，实证待补）

**根因**：`VUID-vkQueueSubmit-pSignalSemaphores-00067` 由 PRESENT semaphore 被重复 signal 触发（validation layer 官方建议 per-image semaphore, index by acquired image）。

**实现**（拆数组）：`m_AcquireSemaphores`（每 window，逐帧串行化下安全）+ `m_PresentSemaphores`（每 window×image，索引 `[w*imageCount+i]`）；`EnsureWindowSync(windowCount, imageCount)` 重签名；executor 4 处索引更新；销毁点保持 `VulkanFrameContext::Release()`（在 WaitIdle 之后）。

**对抗验证结论（round 1）**：
- ✅ 本地文档引用真实：`docs/vulkan-api-docs/refpages/vkQueueSubmit.md:127-131` 含 VUID-00067 且语义一致；`docs/vulkan-api-docs/spec/validusage.json:2605-2607` 一致
- ✅ 索引公式与 `EnsureWindowSync` 覆盖安全（单窗口）；acquire per-window 在逐帧串行化（SubmitBatches waitForFences）下安全
- ✅ `vkDestroySemaphore` VUID-05149 满足（WaitIdle 在销毁前，`RenderBackend_Vulkan.cpp:440`）
- ⚠️ **实证短板**：最近 headless 运行 validation log 为 0 字节，"VUID 已消除"未经非空日志确认——需补一次 headless 运行（验证层启用、日志非空、0 个 VUID）复验
- ⚠️ **uniform-imageCount 未校验前提**：`m_PresentImageCount` 单一 stride，多窗口 imageCount 不同时 present semaphore 跨 window 碰撞（`GetPresentSync(0,1)` 可能别名到 `GetPresentSync(1,0)`）。防护：EnsureWindowSync 断言各 window imageCount 一致，或按 window 存真实 block 偏移
- ⚠️ **PresentWindows 空 finalize 越界**（`VulkanGraphExecutor.cpp:2678`）：`m_PresentBackBuffers[0]` 在 finalize pass 为空时越界——需复用 acquire 循环的 `isEmpty` 守卫（compute-only 图会崩）

### D5（已撤回）: scheduler-delete 竞态——对抗验证证伪

原假设：`NewScheduler()` deleter 的 `WaitAll()` 后 `delete` 与 worker 线程 race，scheduler 被删时 worker 仍在执行节点 → UAF。

**证伪证据链**（round 1 critical finding，已独立复核）：
1. `RenderBackend_Vulkan::ExecuteGraph(TaskScheduler* scheduler, ...)` **完全忽略 scheduler 参数**（函数体仅用 graph/frameManager/executor）；`VulkanGraphExecutor.cpp` 对 TaskScheduler/NewTask 零引用 → per-frame scheduler 恒空，从不入队节点
2. worker 任务全部来自 `IOManager_FS::SubmitAndWait`（每次 1 个 task + 显式 WaitAll）；1445 次 scheduler deleter = 1444 次 IOManager + 1 次 per-frame
3. 单任务下 `m_PendingTaskCount` 仅在该任务最后一次 `NotifyChildNodeFinish` 后才归零，delete 被计数 gate 串行化——D5 声称的窗口被覆盖
4. D5 窗口（Execute_Internal 返回与 ReleaseSelf 之间）不访问 scheduler（ReleaseSelf 用 ThreadManager 的 allocator）
5. **决定性**：崩溃发生在所有 scheduler 销毁之后（日志最后是 ~ThreadManager_Impl → Stop → CRASH）——无存活 scheduler，scheduler-delete UAF 物理上不可能

**结论**：scheduler-delete 竞态**不成立**，zombie-defer 方案**撤回**（基于错误前提）。

### D5'（新，已确立）: ThreadManager teardown 期 worker 崩溃 = TimerSystem 模块实例 teardown 顺序 UAF

**根因（workflow 调查 + 指令级确证，对抗验证 isReal=True）**：

**机制链**：
1. `GPUBackendTester` 按 `CA_ADD_MODULE` 注册序：**TimerSystem_Impl 先**（`Main.cpp:993`）、**ThreadManager 后**（`Main.cpp:994`）
2. `~CAModuleManager` 按注册序**前向释放**（`CaModuleManager.cpp:60-63`）→ 先 `delete` 堆上 TimerSystem_Impl → 全局指针 `gCPUTimer`（`Interface/TimerSystem/private/Timer.cpp:4-12`，裸指针、从不置空）变为**悬垂**
3. 再 `delete` ThreadManager_Impl → `~ThreadManager_Impl`（`ThreadManager_Impl.cpp:186-190`）→ `Stop()`（:313-328）→ `m_Running=false`、`worker.Stop()`（m_Stop=true+Notify）、join
4. worker（tid 140904）从 `cv.wait`（:868）唤醒、离开内层块 → **`CPUTimerScope("Idle")` 析构**（`Interface/TimerSystem/header/CATimer/Timer.h:31-34`，先于 :889 `WorkLoop exit` fprintf）调用 `GetGlobalTimerSystem()->EndEvent()` → 对已释放 TimerSystem_Impl 的虚表做虚调用 → ACCESS_VIOLATION

**指令级确证**：`ThreadManager.DLL RVA 0x3405C` = `~CPUTimerScope` 内 `mov rax,[rax+10h]`（读 vtable EndEvent 槽）；前一条 `mov rax,[rax]`（读 vptr）成功、`[rax+10h]` 失败 ⇒ `gCPUTimer` 是**非空悬垂指针**（指向已释放实例），非 null 场景。4 次运行（双后端）崩溃地址减基址后 RVA 全为 `0x3405C`。

**这解释了全部现象**：双后端同偏移（公共 ThreadManager WorkLoop）、worker 线程、Stop 期间、`WorkLoop exit`=0（崩在 exit fprintf 之前）、确定性。

**排除项**：H2（m_OwningManager 悬垂，join-before-free 保证）、H3（queue_locks 每次 clear 不跨迭代）、H4（543 exec==543 release 平衡、池持锁分析排除越界）、H5（模块单加载）、H6（队列 Stop 时为空）。

**修复方向（round 3 apply 实证修订，顺序无关，覆盖所有消费者）**：

1. **置空 gCPUTimer——必须命中崩溃模块自身的拷贝**（round 3 实证修正）：
   - **round 2 方案的缺陷**：`~TimerSystem_Impl` 加 `==this` 守卫置空。实测（vulkan headless1）**无效**——TimerSystem 是 **STATIC 库**（`Interface/TimerSystem/CMakeLists.txt:6` `add_library(... STATIC ...)`），`gCPUTimer`/`SetGlobalTimerSystem`/`GetGlobalTimerSystem` 被**复制进每个链接它的模块**（ThreadManager.DLL、TimerSystem_Impl.DLL、GPUBackendTester.exe、VulkanRenderBackend.DLL、D3D12RenderBackend.DLL 各自一份 `.data`）。`~TimerSystem_Impl` 只清 **TimerSystem_Impl.DLL 自己的拷贝**（GPUBackendTester 场景下该拷贝从未被 Set → 恒 nullptr → `==this` 不命中）→ 碰不到 **ThreadManager.DLL 的悬垂拷贝**（worker 崩溃实际读的那份，由 Init/TryLink 指向堆实例）。
   - **有效修复**：崩溃模块**自身**在 Stop 前置空——`~ThreadManager_Impl` 入口 `catimer::SetGlobalTimerSystem(nullptr)`（清 ThreadManager.DLL 拷贝；worker 从 cv.wait 唤醒后 `CPUTimerScope("Idle")` 析构见 null → 守卫跳过）。**实证**：置空后 vulkan headless1 **退出码 0、无崩溃、无新 dump**；d3d12 的 0x3405C worker 崩溃同样消失。
   - **保留 `~TimerSystem_Impl` 的 `==this` 置空**作为防御（无害；处理"TimerSystem_Impl.DLL 拷贝曾指向自身堆实例"场景）。
   - 双实例认知仍成立（静态 `g_TimerSystem_Impl` + 工厂堆实例），但 `==this` 守卫的作用域是"**该模块自身拷贝**指向的那个实例"。
2. **全守卫**：所有 `GetGlobalTimerSystem()` 解引用点加 `if(auto* t = ...)`——CPUTimerScope 构造/析构（`Timer.h`）**且 `TIMER_NEWFRAME()`（`Timer.h:42`）与任何 `->SetThreadName` 调用**。**注意：guard 只对 null 有效；悬垂非空拷贝必须靠"源头置空"解决**——所以"全守卫"是兜底、不是主修复。
3. **修 `~CAModuleManager:72` 的 m_Instances 断言**（已实现）：实例释放循环后 `m_Instances.clear()`，断言不再恒触发 `__debugbreak`。**实证**：退出不再因此中断。
4. **验收标准**：headless 退出后**进程退出码 0 + 无新增 crash_*.dmp**（弃用 JSON exit_code——`Main.cpp:1175` closeJsonArray 先于 ctx 析构，检测不到 teardown 崩溃）。**注意：d3d12 需先修 D7 的 0x87D 才能满足此标准**。

**明确不做 swap 注册序**：置空+全守卫是顺序无关的，不依赖"注册序==销毁序"这一未强制的不变式，也不留下悬垂指针。框架级 LIFO 逆序释放（`CaModuleManager.cpp:60-63`）更稳健但翻转隐式契约、需审计各模块析构，列为可选后续项。

**决策依据**：根因已确立（指令级证据）+ round 2 对抗验证修订了修复方案。

### D6（修正）: 节点池地址复用 vs "Finalize 重跑"（状态机缺执行态是真 bug）

round 1 修正：marker 中"同一节点地址被反复 Execute_Internal"**更可能是池地址复用**（`TThreadSafePointerPool::Alloc` 复用空闲槽并 `Initialize_Internal`），而非 m_SubTasks 重入。但确认 **TaskNode 状态机缺陷是真 bug**：`m_Running` 只有 `ePrepare`/`eInvalid` 两个态、执行期间从不置执行态，`WaitingToRun` 对已执行未释放节点仍返回 true。列为独立观察项，本 change 不处理（避免基于错误解读追加无效修复）。

### D7（新，round 3 apply 实证）: d3d12 第二独立崩溃 0x87D（D3D12 debug layer 成员析构期抛异常）

**根因状态**：已定位到模块/阶段，具体触发对象待审。

**现象**：修好 ThreadManager UAF（D5'）后，d3d12 headless1 退出码 1。`UNKNOWN_EXCEPTION (0x0000087D)`，异常地址 KERNELBASE.dll RVA `0xC1B6A`（RaiseException），调用栈 8 帧深在 **D3D12SDKLayers.dll**（debug layer）。确定性复现。

**定位**（插桩实证，`[MM]`/`[D3D12REL]` 标记）：
- `~CAModuleManager` 释放 factory 2（D3D12）期间，`RenderBackend_D3D12::Release()` **完整跑完**（step=0..7 全打印，含 `m_Device = nullptr`）
- InfoQueue QueryInterface 成功（hr=0），但 **stored messages = 0**（无 ERROR/CORRUPTION 消息）
- `SetBreakOnSeverity(ERROR/CORRUPTION, FALSE)` **无效**（0x87D 仍抛）→ **不是 break-on-error 机制**
- 崩溃发生在 `[MM] release factory 2 done` **之前** → `Release()` 返回后、`~RenderBackend_D3D12` **成员析构期间**（某成员持有 D3D12 引用，设备最终引用归零时 debug layer 抛 0x87D）

**排除项**：非 break-on-error；非 ThreadManager（0x3405C 已修）；与 vulkan 修复无关（改动不含 D3D12 行为变更，测试行为与基线一致 `Submit Count:1452`）。

**根因假设**：某 D3D12 对象在 `Release()` 未被释放，其成员析构/设备最终析构时触发 debug layer 抛 0x87D——疑似 **D3D12 资源泄漏或设备析构时存活对象**。具体对象待定位。

**基线关系**：d3d12 基线（`test_output/d3d12_headless1_stdout.txt`）同样崩在 0x3405C，**从未到达 D3D12 teardown** → 0x87D 为基线掩盖（baseline 中同样存在），本 change 首次暴露。按 CLAUDE.md 规则需 git stash 对比验证。

**修复方向（待确认）**：定位 `~RenderBackend_D3D12` 成员析构中触发 debug layer 的对象（各 manager 的残留引用、D3D12MA、command list/allocator/descriptor heap 等），修复泄漏/释放顺序，使设备析构无存活对象。

## Risks / Trade-offs

- [D7 d3d12] d3d12 第二独立崩溃 0x87D（D3D12 debug layer 成员析构期抛异常）——根因对象待定位，修复前 d3d12 headless 无法满足验收（1.5/3.5）
- [框架级] ThreadManager 是公共模块（Vulkan+D3D12 共用），修复影响面大，需双后端回归 + 对抗审查
- [D3 实证] "VUID 已消除"待非空 validation log 复验
- [已完成改动] D3/D1/MiniDump 已在工作树，审查需覆盖其正确性
- [行为变化] 若修复涉及 Stop/teardown 顺序，影响所有模块退出路径
- [构建范围] VulkanRendererBackendTester 不在默认构建（CMakeLists.txt:76 被注释），其 SetThreadName 守卫改动未编译验证
