## Context

`GPUBackendTester.exe --backend vulkan` 存在两个问题：

1. **ACCESS_VIOLATION crash**：在 `Submit Count:1444` 后崩溃，headless 和非 headless 模式均出现。baseline（`ea9dd11`）同样存在。
2. **VMA 内存分配失败**：`Failed to allocate raw Vulkan memory: -8`，仅非 headless 模式多帧运行时复现。伴随 `VirtualBlock commit failed` → fallback 到 single-pool 路径。

目前不确认两个问题是否有因果关系。排查顺序：先理解 crash 的触发条件（堆栈、调用链），再判断内存错误是否相关。

## Goals / Non-Goals

**Goals:**
- 分析 crash dump 定位 ACCESS_VIOLATION 的崩溃调用栈和根因
- 排查 VMA 内存分配失败 `-8` 错误在 aliasing pool 分配链路中的产生原因
- 修复确认的 crash 和内存分配问题
- headless 单帧/多帧 + non-headless 完整回归

**Non-Goals:**
- 不修改 GPU 资源生命周期管理架构（除非问题根因涉及）
- 不引入新的调试基础设施

## Decisions

### D1: 优先分析 crash dump

当前有 crash dump 文件（`crash_20260801_204049.dmp` 等），可使用 WinDbg 或 VS 调试器分析崩溃栈。Crash 地址 `0x00007FF7F7FAF4B5` 在 `GPUBackendTester.exe` 模块内，说明是代码逻辑崩溃而非系统 DLL 问题。

**排查步骤**：
1. 用 WinDbg 打开最新 `.dmp` 文件，执行 `!analyze -v` 获取完整调用栈
2. 定位崩溃指令地址，反查对应源码行
3. 分析崩溃时的寄存器和内存状态

### D2: 排查 VMA 内存分配失败

`-8` 可能是 VK_ERROR_FEATURE_NOT_PRESENT 或 VMA 内部错误码。`CommitVirtualAllocations` 中 `AllocateMemory` 调用是 VMA 的 `vmaAllocateMemory`，错误发生在 VirtualBlock commit 路径。

**排查步骤**：
1. 确认 `-8` 对应哪个 VkResult 或 VMA 错误码
2. 检查 `AllocateMemory` 的 `VmaAllocationCreateInfo` 参数是否合法
3. 检查 VirtualBlock 的 size 是否超过 GPU 可用内存
4. 检查多帧运行中是否存在内存泄漏（VirtualBlock 未释放）

### D3: 多帧 headless 对比验证

内存错误仅在非 headless 模式复现，可能是多帧运行导致。用 `--headless 100` 运行并对比内存行为。

## Risks / Trade-offs

- [Crash 根因未知] 当前只有 crash dump 没有符号文件，可能无法直接映射到源码行。需要通过地址偏移推算。
- [内存错误 非 headless only] 无法自动化复现，需要手动运行非 headless 模式确认修复效果。
