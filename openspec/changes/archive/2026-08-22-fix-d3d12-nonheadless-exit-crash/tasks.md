> **归档状态（2026-08-22）**：D3D12 核心修复（WaitIdle + 7 测试 non-headless 调用）+ vulkan ① 守卫（PresentWindows 空 backbuffer 越界）全部落地并验证。vulkan headless ② 窗口 teardown UAF 为 baseline 既有、属 vulkan 后端问题，**移交专项 change `fix-vulkan-headless-window-teardown`**；vulkan 4.3 腿不随本 change 交付。归档时仅 6.3（vulkan ② 验证）未闭环，系移交项。

## 1. WaitIdle 实现（D1）

- [x] 1.1 `RenderBackend_D3D12::WaitIdle()`（RenderBackend_D3D12.cpp:248 的 stub）实现为 `m_GPUFrameManager.WaitIdle()`——signal direct+compute 队列 fence 并等待，返回后 GPU 全部工作完成（含最后一帧 present 前序渲染；对抗验证已确认 fresh-signal 覆盖 present）
- [x] 1.2 Release() 顶部已有的 `m_GPUFrameManager.WaitIdle()`（RenderBackend_D3D12.cpp:224）改为调用 `WaitIdle()`（统一入口，语义不变）
- [x] 1.3 （加固，可选）对两个 `ID3D12CommandQueue::Signal` 的 HRESULT 做 FAILED 检查（抛错/返回失败），并对 `WaitForSingleObject` 返回值判 `WAIT_FAILED`；真 GPU 挂死的无限等待已在 Risks 接受
- [x] 1.4 （注释软化）RenderBackend_D3D12.cpp:222 与新增注释中硬编码的 `0x87D` 改为机制描述（"资源在 GPU 工作引用时被析构 → debug layer 报错"）——该异常码官方文档不可追溯（design Risks）

## 2. non-headless 调用 WaitIdle（D2，范围 = 全部 7 个测试）

> 对抗验证（2026-08-16，24 agents）确认：根因对全部 7 个测试函数结构相同，只修 TestSimpleTriangle 会留下另外 6 个同样的 non-headless 0x87D。**不能**下沉到 runTestWithDiagnostics 统一加——`testFunc(ctx)` 返回时局部资源已析构，WaitIdle 来不及。必须加在每个测试函数内、循环后、函数返回前，与各自 headless 分支（均有 WaitIdle）对称。`WaitIdle()` 是 `CRenderBackend` 虚接口，vulkan/d3d12 双后端自动生效。

- [x] 2.1 在下列 7 个测试函数的 non-headless 分支（`while (!WindowShouldClose())` 循环后）各加 `ctx.pGPUBackend->WaitIdle();`：
  - TestSimpleTriangle（Main.cpp:122）
  - TestTriangleWithConstantColor（Main.cpp:212）
  - TestTriangleWithStructuredBufferColor（Main.cpp:293）
  - TestTriangleWithImageBuffer（Main.cpp:411）
  - TestDoublePass（Main.cpp:571）
  - TestComputeBuffer（Main.cpp:681）
  - TestIMGUI（Main.cpp:736）

## 3. 诊断插桩状态确认（D3，已由 e8595ac 完成）

> 对抗验证确认：`[TEST]`/`[RTD]`/`[MAIN]`/`[MM]`/`[D3D12REL]`、自动关窗模拟（frameCount/CloseWindow）、`<cstdio>` include 已在 commit e8595ac（fix-vulkan-test-crash 实施）移除，全仓 grep 零残留。本 change 无移除动作，改为确认不残留 + 不重新引入。

- [x] 3.1 TestSimpleTriangle 已恢复 `while (!WindowShouldClose()) { UpdateSystem; NewScheduler; ExecuteGraph; }` 原始结构（无自动关窗模拟）——e8595ac 完成
- [x] 3.2 runTestWithDiagnostics / main 无 `[RTD]`/`[MAIN]` 标记——e8595ac 完成
- [x] 3.3 CaModuleManager 无 `[MM]` 工厂标记、无 `<cstdio>` include——e8595ac 完成
- [x] 3.4 RenderBackend_D3D12 无 `[D3D12REL]` 标记——e8595ac 完成
- [x] 3.5 apply 前全仓 grep 复核（Grep 工具，glob `*.cpp`/`*.h`/`*.hpp`）：转义字面标记 `\[TEST\]|\[RTD\]|\[MAIN\]|\[MM\]|\[D3D12REL\]`（注意转义——原字符类写法 `[TEST]` 会命中几乎每行源码）与临时插桩形态 `newWindow.lock()->CloseWindow` 均零命中

## 4. 回归验证

> D4（保留全局 `Present(0,0)`）为维持现状决策，本次无改动；4.2/4.3 回归同时隐式验证其生效。

- [x] 4.1 `python build.py --config Debug` 全量构建成功
- [x] 4.2 **复现用例（non-headless d3d12 自动关窗）**：因自动关窗模拟已由 e8595ac 移除，回归时**临时**在每个被测测试的 non-headless 循环体首条语句（紧接左花括号后、UpdateSystem 前）插入 `newWindow.lock()->CloseWindow();`（= `glfwSetWindowShouldClose`，与点 X 一致）——循环恰好执行一帧、shouldClose 在该帧 ExecuteGraph 之前置位、退出循环时该帧 GPU 工作 in-flight → **修复前必现 0x87D、修复后必过，回归有判别力**。**不得插在 `while` 关键字之前**（否则 0 帧运行无法复现；对无 pre-loop submit 的 TestSimpleTriangle/TestTriangleWithConstantColor/TestTriangleWithStructuredBufferColor 会假通过）。覆盖 **TestSimpleTriangle + 至少 2 个其余测试**（建议 TestTriangleWithImageBuffer、TestComputeBuffer——有 pre-loop submit 且持 CreateGPUBuffer/CreateGPUTexture 堆资源，触发面更直接）。其余 4 个测试的 non-headless 路径由结构对称性 + 4.3 headless 全量覆盖兜底。退出码 0 + 无崩溃。验证后移除（见 4.5）——**实证：全部 7 个测试插桩（覆盖面更广）`--backend d3d12` 跑出退出码 0 + 无新增 crash_*.dmp（69→69）**
- [x] 4.3 headless 双后端（vulkan+d3d12）1/200/4000 退出码 0 + 无新 crash_*.dmp——**d3d12 腿：1/200/4000 全 exit 0 + 无新 dump ✓；vulkan 腿：暴露 baseline 连环崩溃（① PresentWindows 空 backbuffer 越界，已在本 change §6 修复；② 窗口 teardown UAF，cdb 0xDDDD 实证），vulkan 腿整体移交专项 change（见 §6.3）**
- [x] 4.4 验收：进程退出码 0 + 无新增 crash_*.dmp（每次回归记录；需 4.5 移除插桩 + 3.5 grep 零命中后才成立）——**d3d12 验收成立：4.2 全 7 测试 exit 0 + 无新 dump、4.3 d3d12 1/200/4000 exit 0 + 无新 dump、插桩已移除 + grep 零命中；vulkan 验收随 4.3 vulkan 腿移交专项 change**
- [x] 4.5 **移除临时 CloseWindow 插桩**：4.2 回归完成后，移除所有测试循环体内的 `CloseWindow()` 临时插桩，恢复原始循环结构；配合 3.5 的 grep 复核（含 `newWindow.lock()->CloseWindow` 关键词）确认零命中——**已完成：7 处插桩全部移除，grep 零命中，WaitIdle 14 处保留**

## 5. Review & Adversarial Verify

- [x] 5.1 对全部修改做对抗验证审查（外部 API 正确性：`ID3D12CommandQueue::Signal`/`ID3D12Fence::SetEventOnCompletion`/`GetCompletedValue` 语义，按 CLAUDE.md 引用本地文档或官方链接）——round 0/1 已做（见 Review Log）
- [x] 5.2 对抗者独立复查审查者引用的每个文档路径/URL 真实性——round 0/1 已做
- [x] 5.3 每轮审查先做回归检查（如 WaitIdle 修复是否真消除 0x87D、诊断插桩是否清干净），记录 Review Log——round 0/1 已做；apply 期新增 vulkan 发现记入 round 2
- [x] 5.4 新增 [AUDIT] task 前与 Review Log 去重——§6 已按根因记录（与 fix-vulkan-test-crash 2.5 守卫同根因，非重复开 task，改为在 change 内补守卫）
- [x] 5.5 审查发现的新问题作为 [AUDIT] task 追加，循环直到无新问题或 3 轮——apply 期 vulkan ① 已作为 §6 [AUDIT] 修复；② 移交 vulkan 专项 change（见 Review Log round 2）

## 6. [AUDIT] vulkan PresentWindows 空 m_PresentBackBuffers 越界（4.3 回归发现，baseline 既存）

> **发现**：4.3 vulkan headless 回归时 TestTriangleWithImageBuffer 确定性崩溃（RelWithDebInfo AV / Debug 断言对话框）。`Tools/read_dump.py` + **cdb 交叉验证**（cdb 铁证：`GetWindowPtr` 内联中 `mov rax,[rcx+90h]`，**rcx=0** = 空 vector 的 `[0]` 越界读 → null this → AV）。
> **根因**：`GPUGraph.h:473` `isEmpty()` = `m_ImageUsages.empty() && m_PresentBackBuffers.empty()`——**只要 `m_ImageUsages` 非空（`Finalize(texture)` 只有 image usage 无 present backbuffer），`isEmpty()` 即 false** → `CompileAndExecute:595` 守卫通过 → `PresentWindows:2682` 对空 `m_PresentBackBuffers[0]` 越界读。fix-vulkan-test-crash task 2.5 的守卫**用错条件**（该查 `m_PresentBackBuffers.empty()`）。
> **修复**：调用点守卫改查 backbuffer 非空；PresentWindows 内部 :2682 加非空防御（`pFirstWindow` 可为 null，:2683 已有 null 处理）。

- [x] 6.1 调用点守卫（VulkanGraphExecutor.cpp:595）：`!graph->GetFinalizePass().isEmpty()` → `!graph->GetFinalizePass().m_PresentBackBuffers.empty()`（同时修 593-594 注释语义）——**已实现，实证：vulkan 越过 PresentWindows 不再崩在 :2682**
- [x] 6.2 PresentWindows 内部（VulkanGraphExecutor.cpp:2682）：`m_PresentBackBuffers[0]` 前加 `!empty()` 检查，空则 `pFirstWindow = nullptr`（:2683 已有 null → presentFamily=-1 分支）——**已实现（防御）**
- [ ] 6.3 重建 Debug + **4.3 vulkan headless 1/200/4000 退出码 0 + 无新 dump**；d3d12 headless 回归不受影响——**d3d12 无回归 ✓；vulkan 1/200/4000 仍崩（exit 1），暴露出第 ② 层 baseline bug：`~VulkanWindowHandle`→`CleanupSwapchain`→`destroyImageView` 销毁已释放 image view（cdb 实证 rcx=0xDDDDDDDD 已释放内存）→ validation layer AV。② 根因（窗口生命周期/引用计数）需专项 change `fix-vulkan-headless-window-teardown` 追，本 change 归档时移交**

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| 0（pre-apply 对抗验证） | 2026-08-16 | 24（4 审查视角 + 19 逐条对抗 + 9 URL 复查） | 15 确认：HIGH×4（scope gap：只修 TestSimpleTriangle，其余 6 个同模式照崩）、MEDIUM×3（3.1-3.4 过时、3.1 vs 4.2 自动关窗矛盾×2）、LOW×2（Vulkan 边界未记录、Signal HRESULT/返回值未检查）、INFO×6（API 语义确认、present 覆盖确认、binary_semaphore UB、fix-vulkan-test-crash 6.4 记账、0x87D 码不可追溯、design 机制论断核实） | 已并入本 tasks/design/proposal 修订（scope→7 测试、3.x→已完成、4.2 用临时插桩、加固可选） | 无阻断项；D1/D2 机制成立可实施；4 条 finding 被对抗推翻不处理 |
| 1（apply 前第二轮对抗验证） | 2026-08-17 | 19（4 视角 + 15 逐条对抗） | 12 确认：HIGH×1（4.2 插桩位置歧义——"循环首帧前"对无 pre-loop submit 的 3 测试假通过）、MEDIUM×3（4.2 与 design:8 复现语义不一致需 apply 时实测复核、"验证后移除"无独立任务 + 3.5 grep 不含 CloseWindow）、LOW×2（4.2 只覆盖 3/7、3.5 grep 是正则字符类）、INFO×6（0x87D 注释软化无任务承接、D4 无 tasks 登记、引用核对通过、D1/D2/D3/D4 成立） | 已并入本 tasks/design 修订（1.4 注释软化、3.5 正则转义+CloseWindow 关键词、§4 D4 登记、4.2 插桩位置明确化 + "不得插在 while 前"警告、4.5 移除任务、design:8/D3 语义对齐） | round-0 15 条全闭合、4 条 refuted 零泄漏；3 条本轮 finding 被对抗推翻；D1/D2/D3/D4 成立，可进入 apply |
| 2（apply 回归期） | 2026-08-18~22 | 实证（Tools/read_dump.py + cdb 交叉验证，非 workflow） | 2：① **vulkan PresentWindows 空 m_PresentBackBuffers[0] 越界**（baseline；cdb 铁证 `mov rax,[rcx+90h]` 且 **rcx=0** = 空 vector `[0]` OOB → null this；根因 `FinalizePass::isEmpty()` 用错条件——`m_ImageUsages` 非空即 false，未保护 backbuffer 空）；② **vulkan 窗口 teardown UAF**（`~VulkanWindowHandle`→`CleanupSwapchain`→`destroyImageView` 销毁已释放 image view，cdb 实证 **rcx=0xDDDDDDDD** 调试堆已释放填充 → validation layer AV） | ① 已在本 change §6 修复（调用点守卫改查 `m_PresentBackBuffers.empty()` + PresentWindows 内部防御），实证 vulkan 越过 PresentWindows；**d3d12 核心修复全绿**（4.2 non-headless 全 7 测试 exit 0、4.3 d3d12 1/200/4000 exit 0、无新 dump） | ② 窗口 UAF 根因（窗口生命周期/引用计数，swapchain recreate 或 graph 持有过期引用）待 vulkan 专项 change 追；**vulkan headless 4.3 腿随 ② 移交** `fix-vulkan-headless-window-teardown`。另记：fix-vulkan-test-crash 曾声称"vulkan headless1 退出码 0"与 baseline 实测不符，需复核 |
