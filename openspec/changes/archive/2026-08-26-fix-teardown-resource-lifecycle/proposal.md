## Why

`fix-reverse-module-teardown-order`（已归档）把模块实例销毁改为逆序，消除了"顺序性"的 device UAF（0xDD）。但**全量 7 测试（`--backend <vulkan|d3d12> --headless 1/200`）在同一进程跑完后的进程 teardown 仍然崩溃**，暴露三个独立、非顺序性的 teardown 缺陷（每个测试本身都 pass，崩溃发生在全部测试跑完后的退出阶段）：

1. **vulkan VMA 泄漏 → 销毁分配器断言 abort**：`~RenderBackend_Vulkan → Release() → m_MemoryManager.Release()（=vmaDestroyAllocator）` 时仍有 VMA allocation（buffer/image）未释放 → VMA 调试断言 `"Some allocations were not freed before destruction of this memory block!"`（`vk_mem_alloc.h:10492`）→ `abort`。实测 vulkan 1/200 退出码 3。**根因已钉死（2026-08-23 spike）**：泄漏 allocation 由 `VulkanBuffer::Init`/`VulkanTexture::Init` 创建（`CreateGPUBuffer`/`CreateGPUTexture` 用户对象）。根因 = `VulkanBuffer`/`VulkanTexture`/`GPUBuffer`/`GPUTexture`/`VulkanSubobjectBase` 均无析构函数调 `Release()` 且后端无调用者，`shared_ptr` 归零时 `m_Allocation` 从未 `vmaDestroyBuffer`/`vmaDestroyImage`。**注意（对抗验证更正）**：凡经 `CreateGPUBuffer`/`CreateGPUTexture` 创建且未显式释放的对象（**含 `IMGUIContext::m_Fontimage`**，IMGUIContext.cpp:491 创建、:631 仅 `m_Fontimage={}` 丢弃 shared_ptr、`~VulkanTexture` trivial）均按同一机制泄漏；实测触发 abort 的单测为 ImageBuffer/DoublePass/ComputeBuffer（exit 3）。`VulkanLinearMemoryManager`（staging 页）已确认在 allocator 销毁前释放、非泄漏源。

2. **d3d12 窗口销毁 UAF → ACCESS_VIOLATION**：实测 d3d12 all-7 `--headless 200` 退出码 1（d3d12 `--headless 1` 干净）。**2026-08-23 dump 修正根因**：栈并非旧假设的 `~shared_ptr<IWindow> → deleter → glfwDestroyWindow`，而是 `std::_Func_class<IWindow*,bool>::operator()`(functional:930) → `WindowSystem_ImplGlfw_WindowFocusCallback`(WindowSystem_Impl.cpp:67) → `_glfwInputWindowFocus`/`maximizeWindowManually`(win32_window.c:516)，`Rax=0xDDDDDDDDDDDDDDDD` 说明**回调函数对象已释放**——即"仍存活的孤立窗口上的一次非销毁窗口操作（`SetWindowPos/SetWindowSize → WM_ACTIVATE → maximizeWindowManually → _glfwInputWindowFocus`）在逆序 teardown 中 `s_WindowSystem` 已释放时派发 window-focus 事件打到已释放回调对象"（注意：**不是** `glfwDestroyWindow` 期间派发——vendored GLFW 3.4 在 destroy 前先 `memset(&window->callbacks,0,...)`，销毁路径无应用回调可达），与逆序 teardown 的释放顺序相关，属回调对象（非 GLFW 窗口句柄）生命周期的 UAF。**load-bearing 修法**：`~WindowSystem()` 置空 `s_WindowSystem` + 全部 GLFW 回调空判（`ws && ws->m_Xxx`）。

3. **crash-dump 捕获被 auto-dismiss hook 弱化**：为抑制调试断言弹窗加的 `_CrtSetReportHook2`（`MessageBoxTimeout` auto-dismiss）改变了 abort 路径——此后 vulkan teardown abort（退出码 3）**不再写 MiniDump**（实测 `dumps=0`；此前 BREAKPOINT 路径会写）。调试时丢掉了崩溃证据。

**边界（诚实）**：这三项都是"顺序性"之外的独立缺陷（资源泄漏 / GLFW 窗口生命周期 / 崩溃捕获），非逆序 change 的目标；是全量运行把它暴露出来的。窗口可见性已按 `fix-reverse-module-teardown-order` 评估复原（headless 不再隐藏窗口，可见并正常关闭）。

## What Changes

- **vulkan VMA 泄漏**：**根因已钉死（2026-08-23 spike）**——`CreateGPUBuffer`/`CreateGPUTexture` 创建的 `VulkanBuffer`/`VulkanTexture` 在 `shared_ptr` 归零时被销毁却不调用 `Release()`（均无析构函数调 `Release()` 且后端无调用者），其 `m_Allocation` 从未 `vmaDestroyBuffer`/`vmaDestroyImage`，在 `m_MemoryManager.Release()`（`vmaDestroyAllocator`）时仍 outstanding → VMA 断言 abort。**修法（design D5，用户拍板两点）**：(1) **L1（方案 B）** `CreateGPUBuffer`/`CreateGPUTexture` 返回的 `shared_ptr` 装自定义 deleter（**须放弃 make_shared 改 `new`+二参 deleter**，[AUDIT-7]），refcount 归零时调 `Release()`——**对象死即释放**；(2) **L3a（登记 VMA allocation，扫残留必释放）** `VulkanMemoryManager` 登记 `{VmaAllocation, vk 句柄, owner}`，`Free*` 幂等摘除，`Release()` 在 `vmaDestroyAllocator` 前扫残留——**即使资源未释放拖到 teardown 也必被扫到释放**（用户要求②）。两者互补：L1 管"死在 teardown 前"，L3a 管"活到 teardown 时"（若某资源活过 backend——**[AUDIT-R5-2]**：`m_Fontimage` 经核查属 **L1**，逆序下 IMGUIContext 早于 backend 销毁、在 backend 前析构走 L1 deleter，非此例）。使全量 7 测试 vulkan teardown 退出码 0、无 VMA 断言。**D3D12 超范围声明**：D3D12 后端存在同构 allocation 泄漏（`D3D12MA::Allocation*` 裸指针、`D3D12MemAlloc.cpp:8219/8271` 也有断言），但因 d3d12 测试先因窗口 UAF exit 1、且本 change 聚焦 vulkan teardown 生命周期，**D3D12 allocation 泄漏不在本 change 范围**（另案或在后续 change 处理）。
- **d3d12 窗口销毁 UAF**：**根因修正（2026-08-23 dump 栈）**非"`~shared_ptr`→`glfwDestroyWindow` 双销毁"，而是逆序 teardown 中窗口销毁与 GLFW 回调对象（`s_WindowSystem`/`m_WindowFocusCallback`）生命周期错位——销毁窗口时 GLFW 派发 window-focus 事件打到已释放回调对象（`Rax=0xDDD…`）。修复：保证窗口销毁在回调对象仍有效时，或窗口销毁前解除 GLFW 回调。使 d3d12 all-7 `--headless 200` 退出码 0。
- **crash-dump 捕获恢复**：auto-dismiss hook 保留（抑制阻塞弹窗），但恢复 MiniDump 捕获——任何 crash/abort 仍写 `crash_*.dmp`（不因 report hook 丢失）。
- 回归：vulkan + d3d12 全量 7 测试 `--headless 1/200`（4000 帧耗时过长，按需），退出码 0 + 无新 dump；`Tools/read_dump.py` 快读任何新 dump。

## Capabilities

### New Capabilities
无。

### Modified Capabilities
- `resource-handle-registration` / `vulkan-window-handle`／`command-buffer-lifecycle`（泄漏点已钉死为 `VulkanBuffer`/`VulkanTexture`：设备资源须在 `vmaDestroyAllocator` 前释放——通过**析构调 `Release()`（L1）+ backend teardown 登记扫描（L3a）**达成）。
- `crash-diagnostics`（`crash_*.dmp` 捕获须在 assert/abort 路径下仍生效）。

> 边界说明：本 change 修的是"非顺序性"的 teardown 资源生命周期/窗口销毁/崩溃捕获；模块逆序 teardown 已在 `fix-reverse-module-teardown-order`（已归档）完成。窗口"是否隐藏"经评估维持可见（headless 可见并正常回收）。

## Impact

- `VulkanRenderBackendNew/private/VulkanObjects/VulkanBuffer.{h,cpp}` / `VulkanTexture.{h,cpp}`（L1：`RenderBackend_Vulkan.cpp` 的 `CreateGPUBuffer`/`CreateGPUTexture` 返回装自定义 deleter 的 `shared_ptr`（`new`+二参 deleter），refcount 归零调 `Release()`；以及 `Release()` 判 `m_Allocation` 幂等）
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanMemoryManager.{h,cpp}`（L3a：新增 `m_Allocations: unordered_map<VmaAllocation, AllocRecord>` 登记 Allocation（`AllocateBuffer`/`AllocateImage`/**`AllocateMemory`**），`FreeBuffer`/`FreeImage`/**`FreeMemory`** 幂等（find-or-return→vmaDestroy→erase），`Release()` 在 `vmaDestroyAllocator` 前扫残留（含 kind=Memory 别名池/虚拟块物理分配；[AUDIT-R5-1]））
- `WindowSystem/private/WindowSystem_Impl.cpp` / `Window_Impl.cpp`（窗口销毁时回调对象生命周期：`s_WindowSystem`/`m_WindowFocusCallback` 与窗口销毁顺序）
- `Test/GPUBackendTester/private/MiniDump.cpp` / `Main.cpp`（auto-dismiss hook 下恢复 dump 捕获）
- 回归：vulkan + d3d12 全量 7 测试（`TestSimpleTriangle`/`TestTriangleWithConstantColor`/`TestTriangleWithStructuredBufferColor`/`TestTriangleWithImageBuffer`/`TestDoublePass`/`TestComputeBuffer`/`TestIMGUI`）`--headless 1/200`。
