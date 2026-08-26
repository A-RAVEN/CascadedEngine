## ADDED Requirements

### Requirement: 设备资源在 vmaDestroyAllocator 前全部释放（L1）
Vulkan 后端 SHALL 在任何 VMA 分配器被销毁（`vmaDestroyAllocator`）前释放全部由该分配器创建的 buffer/image 等 VMA 分配，使 teardown 全程 allocator 无 outstanding allocation。用户通过 `CreateGPUBuffer`/`CreateGPUTexture` 创建的 `VulkanBuffer`/`VulkanTexture` SHALL 在最后一个 `shared_ptr` 引用释放时（对象销毁）释放其 `m_Allocation`（经 `Release()`），不得仅依赖外部显式调用。

#### Scenario: 全量 7 测试 teardown 无 VMA 断言（L1）
- **WHEN** `--backend vulkan --headless 1/200` 跑完全部 7 测试后进程 teardown（经 `~RenderBackend_Vulkan → Release()`）
- **THEN** `m_MemoryManager.Release()`（`vmaDestroyAllocator`）不触发 VMA `"Some allocations were not freed before destruction of this memory block!"` 断言，进程退出码 0、无新增 `crash_*.dmp`

### Requirement: teardown 登记 VMA allocation，扫残留必释放（L3a）
Vulkan 后端 SHALL 跟踪其通过 `CreateGPUBuffer`/`CreateGPUTexture` 以及 `AllocateMemory`（别名池物理块/虚拟块物理提交）创建的 VMA allocation（登记 allocation + vk 句柄 + owner；无 owner 的裸分配登记为 ownerless），并在后端 `Release()` 时、`vmaDestroyAllocator` 之前，扫描登记表中的残留 allocation 并强制释放——即使某资源未显式释放、被拖到了 teardown，也必被扫到并释放。allocation 一经 L1（对象计数归零 `Release()`）正常释放即从登记表摘除，避免与 L3a 重复释放。**（[AUDIT-R5-1] 补 `AllocateMemory` 分配路径。）**

#### Scenario: teardown 扫残留必释放（L3a）
- **WHEN** `CreateGPUBuffer`/`CreateGPUTexture` 创建的某资源未在 teardown 前释放（对象计数仍>0 或已死未释放）
- **THEN** 后端在 `m_MemoryManager.Release()`（`vmaDestroyAllocator`）之前的 sweep 中扫到该残留 allocation 并释放（带 owner 经 `owner->Release()`，ownerless 直接 vmaDestroy）；allocator 销毁时无 outstanding 存活分配

### Requirement: 窗口销毁单点、无窗口 UAF 且回调生命周期安全
窗口（`cawindow::IWindow`）SHALL 至多被销毁一次；`WindowImpl::Release`/自定义 deleter 不得对同一 GLFW 窗口重复 `glfwDestroyWindow`；且窗口销毁期间 GLFW 派发的回调（focus/size/close 等）不得调用到已释放的窗口系统/回调对象，避免 ACCESS_VIOLATION。

#### Scenario: d3d12 多测试 teardown 无窗口 UAF
- **WHEN** `--backend d3d12 --headless 200` 跑完全部 7 测试后进程 teardown（窗口销毁，GLFW 可能派发 window-focus 等事件）
- **THEN** 不触发 `0xC0000005`（含回调函数对象被释放后仍被调用），进程退出码 0

### Requirement: crash dump 在 assert/abort 下仍被捕获
崩溃诊断 SHALL 在 CRT assert/abort（含 `_CrtSetReportHook2` auto-dismiss 路径）下仍写出 `crash_*.dmp`，不得因报告 hook 丢失崩溃证据。

#### Scenario: assert/abort 触发写 dump
- **WHEN** 调试断言或 `abort()` 发生（即使被 auto-dismiss 报错 hook 处理）
- **THEN** 仍产生 `crash_*.dmp`（MiniDump 捕获不因报告 hook 失效）
