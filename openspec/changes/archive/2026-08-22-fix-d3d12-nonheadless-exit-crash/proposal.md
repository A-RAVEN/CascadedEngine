## Why

d3d12 后端 **non-headless（手动关窗）退出阶段崩溃** `0x87D`（D3D12 debug layer 抛异常，确定性复现）。根因（插桩实证 + 2026-08-16 对抗验证）：**任一测试函数循环最后一帧的 GPU 工作仍在 in-flight 时，测试函数局部的 D3D12 资源（vbuffer/graph，部分测试还有 CreateGPUBuffer/CreateGPUTexture 堆资源）被析构** → debug layer 检测到"资源销毁时仍有 GPU 工作引用"→ RaiseException 0x87D。该根因对 Main.cpp 全部 7 个测试函数结构相同。关键缺陷：`RenderBackend_D3D12::WaitIdle()` 是 **stub 空实现**（无 GPU 同步），且所有 non-headless 循环后根本不调用 WaitIdle。headless 通过只是因为帧数多给了 GPU 排空时间，属侥幸。

## What Changes

- **实现真正的 `RenderBackend_D3D12::WaitIdle()`**（替换 stub）：signal 直连队列 + compute 队列 fence 并等待，确保调用返回后 GPU 全部工作完成（含最后一帧 present 前序渲染）
- **全部 7 个测试函数的 non-headless 循环后调用 WaitIdle**（headless 分支已有调用；`WaitIdle` 为 `CRenderBackend` 虚接口，vulkan/d3d12 双后端生效）：TestSimpleTriangle / TestTriangleWithConstantColor / TestTriangleWithStructuredBufferColor / TestTriangleWithImageBuffer / TestDoublePass / TestComputeBuffer / TestIMGUI，保证任一测试手动关窗退出时局部 D3D12 资源析构前 GPU 已 idle
- 复用 `GPUFrameManager::WaitIdle()`（fix-vulkan-test-crash 中已实现）作为正式入口，消除 Release() 里的重复逻辑
- 临时诊断插桩（`[TEST]`/`[RTD]`/`[MAIN]`/`[MM]`/`[D3D12REL]`、自动关窗模拟、`<cstdio>`）已由 e8595ac 移除，本次确认不残留、不重新引入
- 回归：non-headless 自动关窗（临时 CloseWindow 插桩，验证后移除）+ headless 双后端退出码 0

## Capabilities

### New Capabilities
无

### Modified Capabilities
无

> 边界说明：本 change 是内部 bug 修复，无行为契约变化，无需新增/修改 spec。D3D12 后端的 WaitIdle 语义由 stub 变为真实实现，属实现细节修复。

## Impact

- `D3D12RenderBackend/private/RenderBackend_D3D12.{h,cpp}` — WaitIdle 实现（替换 stub）；Release() 入口统一为 `WaitIdle()`
- `Test/GPUBackendTester/Main.cpp` — 7 个测试 non-headless 循环后调 WaitIdle（诊断插桩已由 e8595ac 移除，仅确认不残留）
- 相关：fix-vulkan-test-crash 已实现的 `GPUFrameManager::WaitIdle()` 被复用为正式入口
