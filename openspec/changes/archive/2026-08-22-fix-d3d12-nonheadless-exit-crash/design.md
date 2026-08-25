## Context

d3d12 后端 non-headless（手动关窗）退出崩溃 `0x87D`（D3D12 debug layer RaiseException，确定性复现）。插桩实证定位：

- 崩溃发生在 **TestSimpleTriangle 局部变量析构**（`[TEST] loop exited` 打印、`[RTD] after testFunc` 未打印、`[MM] dtor` 未到）——不是后端 teardown
- 机制：**测试循环最后一帧的 GPU 工作仍在 in-flight 时，测试的 D3D12 资源（vbuffer/GPUGraph）被析构** → debug layer 检测"资源销毁时仍有 GPU 工作引用"→ 0x87D
- 关键缺陷：`RenderBackend_D3D12::WaitIdle()` 是 **stub 空实现**（`"no headless GPU sync requirement"`）；non-headless 循环后**不调用** WaitIdle；headless 路径虽有调用但 stub 无效
- 复现：用 `CloseWindow()`（= `glfwSetWindowShouldClose`，与点 X 完全一致）在最后一帧渲染**之前**置 shouldClose 可确定性复现；**之后**置则通过——时序差异决定 GPU 是否已排空（**round-2 对抗验证注（2026-08-17）**："之前置"的确切语义 = 循环体内、该帧 ExecuteGraph 之前置位，使恰好一帧 in-flight；"之后置则通过"的实证结论需在 apply 时复核——若 after-render 置亦崩，需修正此句；tasks 4.2 已统一为"循环体首条语句插入"消除歧义）
- headless 通过纯属侥幸（帧数多、GPU 有时间排空），非保证

关联：fix-vulkan-test-crash 已在 `GPUFrameManager::WaitIdle()` 实现队列 fence 冲刷（signal direct+compute 队列 + 等待），并已确认 `Present(1,0)` 的 vsync 节流是 67s 回归耗时的主因（已全局改 `Present(0,0)`，本次保留）。

**对抗验证（2026-08-16，24 agents / 15 confirmed / 4 refuted）**：
- D1/D2 机制经官方文档 + 代码核实**成立**：present 在 swapchain 创建时提供的 direct queue 上执行（`GetPresentQueue()==GetDirectQueue()`），`GPUFrameManager::WaitIdle()` 的 fresh-signal（`++m_FrameIndex` 后 signal 新值）在 CPU 序上晚于最后一次 Present 入队，等待即保证最后一帧渲染与 present 前序工作全部完成；与 e8595ac 修复 headless 的实证一致。
- 涉及 API（`ID3D12CommandQueue::Signal` / `ID3D12Fence::GetCompletedValue` / `SetEventOnCompletion` / `IDXGISwapChain::Present(0,0)` / `DXGI_SWAP_EFFECT_FLIP_DISCARD`）全部核对通过，审查者引用的 9 个文档 URL 全部真实存在。
- 发现并修正的 scope/工件问题：① 根因对**全部 7 个测试函数**结构相同，原 D2 只修 TestSimpleTriangle 是 scope gap；② 3.x"移除诊断插桩"已被 e8595ac 完成，属过时任务；③ 3.1 删自动关窗模拟 与 4.2 回归依赖该模拟 自相矛盾。

## Goals / Non-Goals

**Goals:**
- 实现真正的 `RenderBackend_D3D12::WaitIdle()`，保证调用返回后 GPU 全部工作完成（含最后一帧 present 前序渲染）
- **全部 7 个测试函数**的 non-headless 循环后调用 WaitIdle，使任一测试手动关窗退出时局部 D3D12 资源析构前 GPU 已 idle（对抗验证：只修 TestSimpleTriangle 是 scope gap，其余 6 个照崩）
- 复用 `GPUFrameManager::WaitIdle()` 作为正式入口，消除 Release() 重复逻辑
- 确认诊断插桩已由 e8595ac 移除、不残留、不重新引入
- 回归：non-headless 自动关窗（临时 CloseWindow 插桩）+ headless 双后端退出码 0

**Non-Goals:**
- 不改 present sync（保持全局 `Present(0,0)`，上一决策"先保持这样"）
- 不重构测试基础设施（不新增 `--auto-close-frames` 类永久 CLI 参数；non-headless 回归用临时 CloseWindow 插桩，验证后移除）
- 不处理 D3D12 后端的其它资源泄漏观察项（GPUResource 无析构释放等，列为观察）
- Vulkan 后端 non-headless 的 WaitIdle 覆盖随 task 2.1 双后端自动获得（`WaitIdle` 为 `CRenderBackend` 虚接口），不单独处理

## Decisions

### D1: `RenderBackend_D3D12::WaitIdle()` 实现为 `m_GPUFrameManager.WaitIdle()`

`GPUFrameManager::WaitIdle()`（FrameBoundResourceManager.cpp:49-68）已正确实现：`++m_FrameIndex` → direct 队列 + compute 队列 signal 到新值 → 等待两个 fence 完成。它覆盖队列上所有已提交工作（含最后一帧的渲染）。

实现 `RenderBackend_D3D12::WaitIdle()`：
```cpp
void RenderBackend_D3D12::WaitIdle()
{
    m_GPUFrameManager.WaitIdle();
}
```
Release() 里已有的 `m_GPUFrameManager.WaitIdle()` 前移调用保留（语义一致，可改为调用自身 `WaitIdle()` 统一入口）。

**验证外部 API 正确性**：`ID3D12CommandQueue::Signal` / `ID3D12Fence::SetEventOnCompletion` / `GetCompletedValue` 语义（signal 后等待完成值 ≥ signal 值 = 队列前序工作全部完成）。本地已用（fix-vulkan-test-crash），行为经 headless 回归验证。

### D2: 全部 7 个测试的 non-headless 循环后调用 `ctx.pGPUBackend->WaitIdle()`

Main.cpp 7 个测试函数（TestSimpleTriangle / TestTriangleWithConstantColor / TestTriangleWithStructuredBufferColor / TestTriangleWithImageBuffer / TestDoublePass / TestComputeBuffer / TestIMGUI）的 non-headless 分支都是同一模式 `while (!WindowShouldClose()) { UpdateSystem; NewScheduler; ExecuteGraph; }`，循环后均无 WaitIdle，函数返回时局部 D3D12 资源（vbuffer/newGraph/windowHandle/windowBackBuffer，部分测试还有 CreateGPUBuffer/CreateGPUTexture 堆资源）被析构——**根因对全部 7 个测试一致**。

在每个 non-headless 循环后、函数返回前加 `ctx.pGPUBackend->WaitIdle();`，与各自 headless 分支（Main.cpp:118/208/289/407/567/677/732）对称。这样任一测试手动关窗退出时，局部资源析构前 GPU 已 idle。`WaitIdle` 是 `CRenderBackend` 虚接口（vulkan/d3d12 双后端），vulkan 后端已真实现为 `m_Device.waitIdle()`，本调用双后端自动生效。

**不能**下沉到 `runTestWithDiagnostics` 在 `testFunc(ctx)` 返回后统一加——函数返回时局部 RAII 资源已析构，WaitIdle 来不及（对抗验证确认此方案无效）。必须加在每个测试函数内、循环后、函数返回前。

### D3: 诊断插桩已由 e8595ac 移除，本 change 只确认不残留

`[TEST]`/`[RTD]`/`[MAIN]`/`[MM]`/`[D3D12REL]` 标记、自动关窗模拟（`frameCount`/`CloseWindow()`）、`<cstdio>` include 已在 fix-vulkan-test-crash 实施提交 **e8595ac** 中移除（全仓 grep 零残留，对抗验证确认）。本 change 的 3.x 任务改为"确认不残留 + 不重新引入"，无移除动作。

**non-headless 回归（4.2）的自动关窗方法**：因自动关窗模拟已移除，回归时**临时**在每个被测测试的 non-headless 循环体首条语句（紧接左花括号后、UpdateSystem 前）插入 `newWindow.lock()->CloseWindow()`（= `glfwSetWindowShouldClose`，与点 X 一致）——循环恰好执行一帧、shouldClose 在该帧 ExecuteGraph 之前置位、退出循环时该帧 GPU 工作 in-flight → 修复前必现 0x87D、修复后必过。**不得插在 `while` 关键字之前**（0 帧运行无法复现）。验证后移除（tasks 4.5）。备选方案（不采纳，超出 Non-Goals）：新增永久 `--auto-close-frames N` CLI 参数。

### D4: 保留全局 `Present(0,0)`

上一决策（"先保持这样"）：d3d12 全部 present 用 sync interval 0（不等 vsync）。本次不涉及。

## Risks / Trade-offs

- **WaitIdle 阻塞**：若队列 work 永不完成（GPU 挂死），WaitIdle 无限等待。对抗验证：设备移除时 `GetCompletedValue()` 返回 `UINT64_MAX`，预检跳过、**不会**挂死；真 GPU 挂死仍会无限阻塞——已接受（现有 headless 回归覆盖正常排空路径）。加固项（可选）：检查两个 `Signal` 的 HRESULT 与 `WaitForSingleObject` 返回值（task 1.3）
- **0x87D 码不可追溯**：该确切异常码在微软官方文档无对应条目（0x0000087D 属成功区段，应为 debug layer 内部 RaiseException 码）。机制本身（资源在 GPU 引用时被销毁）有官方出处（DirectX-Specs CPUEfficiency）。代码注释建议软化为"资源在 GPU 工作引用时被析构而报错"，不硬编码 0x87D
- **binary_semaphore UB（观察）**：`WindowContext::Release()`（WindowContext.h:29-32）对从未 acquire 的 `m_Semaphore(1)` 调 `release()`，计数超 max 属标准库 UB（[thread.sema.cnt]）。预存在、MSVC 实现下实际无害（无等待方）。列入观察，不在本 change 处理
- **测试遗漏**：WaitIdle 已覆盖全部 7 个测试的 non-headless 分支；若未来新增测试函数忘记在 non-headless 循环后调 WaitIdle，同类崩溃仍可能发生——维持"每个 non-headless 循环后必须 WaitIdle"的约定
- **Present(0,0) 副作用**：non-headless 真实渲染丢 vsync（可能撕裂）——已在上一次决策接受，本次不处理
