## Context

`fix-reverse-module-teardown-order`（已归档）让模块实例按逆序销毁，消除了"顺序性"device UAF。但全量 7 测试**同一进程**跑完后的 teardown 仍崩 3 处（测试本身全 pass，崩溃在退出阶段）：

```
① vulkan  VMA 泄漏     → VulkanMemoryManager::Release() 调 vmaDestroyAllocator 时仍有 allocation
                          → VMA 断言 "Some allocations were not freed..." (vk_mem_alloc.h:10492) → abort（exit 3）
② d3d12   窗口销毁 UAF  → 窗口销毁时 GLFW window-focus 回调打到已释放回调函数对象
                          → ACCESS_VIOLATION 0xC0000005（exit 1，仅 headless 200，1 正常）
③ dump    捕获弱化      → _CrtSetReportHook2(MessageBoxTimeout auto-dismiss) 改变 abort 路径 → 不再写 crash_*.dmp（dumps=0）
```

**诊断已完成（2026-08-23 spike，非 task）**：
- **① VMA 泄漏根因已钉死**：泄漏 allocation 为 `VulkanBuffer::Init` / `VulkanTexture::Init`（`CreateGPUBuffer`/`CreateGPUTexture` 用户对象）。根因 = `VulkanBuffer`/`VulkanTexture`/`GPUBuffer`/`GPUTexture`/`VulkanSubobjectBase` 均无析构函数调 `Release()` 且后端无任何调用者，`shared_ptr` 归零时 `m_Allocation` 从未释放。**注意（对抗验证[AUDIT-5]更正）**：泄漏对象不限于"恰好 3 个测试"——凡经 `CreateGPUBuffer`/`CreateGPUTexture` 创建且未显式释放的对象（含 `IMGUIContext::m_Fontimage`）均按同一机制泄漏。实测触发 `vmaDestroyAllocator` abort 的单测为 TestTriangleWithImageBuffer/TestDoublePass/TestComputeBuffer（exit 3）；TestIMGUI 单独跑抛 unknown exception（未走完 `CreateGPUTexture`，独立问题）。
- **② d3d12 窗口 UAF 根因修正**：dump 栈为 `std::_Func_class<IWindow*,bool>::operator()`(functional:930) → `WindowSystem_ImplGlfw_WindowFocusCallback`(WindowSystem_Impl.cpp:67) → `_glfwInputWindowFocus`/`maximizeWindowManually`(win32_window.c:516)，`Rax=0xDDD…` = **回调函数对象已释放**，非旧假设的"`~shared_ptr→glfwDestroyWindow` 双销毁"。是逆序 teardown 中窗口销毁与 `s_WindowSystem`/`m_WindowFocusCallback` 生命周期错位。

**已排除的候选（不是泄漏源）**：
- `VulkanLinearMemoryManager`（staging 页）：`VulkanFrameManager::Release → m_StagingMemoryManager.Release()` 在 `m_MemoryManager.Release` 前释放（`VulkanFrameManager.cpp:53`）。
- ~~IMGUIContext `m_Fontimage`~~（**更正，不应排除**）：`IMGUIContext::Release()`（:629-631）`m_Fontimage = {}` 只丢弃 shared_ptr，`~VulkanTexture` trivial 不调 `Release()` → 其 `m_Allocation` 同样不释放，按同一机制泄漏。
- graph-local `/VulkanGraphExecutor`/`VulkanGraphLocalResourceManager` 资源：由 `ReleaseAllResources()` 释放，spike 中未出现 `GraphLocal::*` 泄漏。
- 以上共用同一 allocator（`GetMemoryManager().GetAllocator()`）。

## Goals / Non-Goals

**Goals:**
- 使 vulkan 全量 7 测试 teardown 在 `vmaDestroyAllocator` 时 allocator 无 outstanding allocation → 退出码 0、无 VMA 断言。
- 使 d3d12 全量 7 测试 `--headless 200` teardown 不崩（窗口销毁期间 GLFW 回调不命中已释放回调对象 → 顺序 UAF 消除）→ 退出码 0。
- 恢复 crash-dump 捕获：保留 auto-dismiss 弹窗抑制，但任何 crash/abort 仍写 `crash_*.dmp`。

**Non-Goals:**
- 不改模块逆序 teardown（已完成）。
- 不改窗口"可见性"（headless 维持可见并正常回收；评估否决了隐藏窗口的方案——那掩盖问题而非修复）。
- 不处理其余 validation 告警（IMGUI draw-bind VUID-04007/07312 另案）。

## Decisions

### D1: 修复 VMA 泄漏（诊断已完成 2026-08-23 spike）
**诊断结论（spike，非任务）**：泄漏对象 = `VulkanBuffer::Init` / `VulkanTexture::Init` 创建的 VMA allocation。用临时 live-allocation registry 在 `VulkanMemoryManager::Release()`（`vmaDestroyAllocator` 前）dump，实测所有 outstanding allocation 的 creator label 均为二者；单测隔离验证：恰好 3 个用 `CreateGPUBuffer`/`CreateGPUTexture` 的测试（ImageBuffer/DoublePass/ComputeBuffer）泄漏，4 个只用 graph-local `.AllocBuffer/.AllocImage` 的测试干净。
**根因**：`VulkanBuffer`/`VulkanTexture`/`GPUBuffer`/`GPUTexture`/`VulkanSubobjectBase` 均**无析构函数调用 `Release()`**，且 `VulkanBackendNew` 内无任何调用者调 `VulkanBuffer::Release()`/`VulkanTexture::Release()`。`CreateGPUBuffer`/`CreateGPUTexture` 返回的 `shared_ptr` 引用计数归零时对象被销毁，但 `m_Allocation` 从未 `vmaDestroyBuffer`/`vmaDestroyImage`。graph-local 资源（`.AllocBuffer/.AllocImage`）由 `VulkanGraphLocalResourceManager::ReleaseAllResources()` 释放，故不泄漏。
**核心修法 = 双层（L1 + L3a，见 D5 详述，2026-08-23 最终定稿）**：
- **L1（对象死时自释放）**：`CreateGPUBuffer`/`CreateGPUTexture` 返回的 `shared_ptr` 装自定义 deleter（方案 B），refcount 归零时调 `Release()`。让"对象死"与"底层句柄释放"同步。修"count 在 teardown 前归零"的对象（各测试局部 `testVertexBuffer`/`testTexture` 等）。
- **L3a（登记 VMA allocation，teardown 扫残留必释放）**：`CreateGPUBuffer`/`CreateGPUTexture` 成功分配时把 `{VmaAllocation, vk 句柄, owner}` 登记到 `VulkanMemoryManager::m_Allocations`；`FreeBuffer`/`FreeImage`/`FreeMemory` 正常释放后摘除；`VulkanMemoryManager::Release()` 在 `vmaDestroyAllocator` 之前扫残留——带 owner 的调 `owner->Release()`（清空对象 `m_Allocation` 防其日后析构再释放），无 owner 的按 kind 直接 vmaDestroy。修"count 在 teardown 时仍>0 的对象"（若某资源活过 backend——**[AUDIT-R5-2] 更正**：当前逆序 teardown 下 IMGUIContext(index7) 先于 backend(index2) 销毁，`m_Fontimage` 在 backend 前析构、属 **L1** 而非"活到 teardown"；L3a 为前瞻兜底，无当前实证案例，但用户②要求保留）。
**边界**：`VulkanMemoryManager::Release()` 的 `vmaDestroyAllocator` 必须在**残留清空后**调用；**禁止**改它去"容忍泄漏/禁断言"（这是掩盖）。所有分配释放后断言自然消失。

### D2: 修复 d3d12 窗口销毁 UAF
**2026-08-23 复现修正，Round-6 源码对抗验证再精确化（2026-08-26）**：d3d12 all-7 `--headless 200` 稳定 exit 1；dump 栈为 `std::_Func_class<IWindow*,bool>::operator()`(functional:930) → `WindowSystem_ImplGlfw_WindowFocusCallback`(WindowSystem_Impl.cpp:67) → `_glfwInputWindowFocus`/`maximizeWindowManually`(win32_window.c:516)，`Rax=0xDDDDDDDDDDDDDDDD` 说明**回调函数对象已释放**。→ 与旧假设（"`~shared_ptr<IWindow>` deleter → `glfwDestroyWindow` 对重复/失效 `m_Window` AV"）**不一致**。真实根因（Round-6 verified）：**不是在 `glfwDestroyWindow` 期间派发**——vendored GLFW 3.4 在 `glfwDestroyWindow` 内先 `memset(&window->callbacks,0,...)` 再 `_glfwDestroyWindowWin32`（src/window.c），销毁路径无应用的 focus/close 回调可达。崩溃事件由一个**仍存活的孤立窗口上的非销毁窗口操作**（`SetWindowPos/SetWindowSize → WM_ACTIVATE → maximizeWindowManually → _glfwInputWindowFocus`）在逆序 teardown 中`s_WindowSystem` 已释放时同步派发：窗口的 `shared_ptr` 被 tester / render-backend WindowHandle 另行持有（`m_Windows` 归属引用不必然代表唯一持有者），当 `WindowSystem`（模块 index 4，逆序先于 render backends / 窗口释放）析构后 `s_WindowSystem` 仍是**已释放但非空**的悬垂指针，回调 deref 到已释放 `m_WindowFocusCallback` → UAF。**修法（load-bearing）**：① `WindowSystem::~WindowSystem()` 置 `s_WindowSystem=nullptr`；② 全部 GLFW 静态回调 `WindowSystem* ws=s_WindowSystem; if(ws && ws->m_Xxx)` 空判。`WindowImpl::Release()` 幂等（`if(m_Window) glfwDestroyWindow(m_Window)`）仅作防御兜底（幂等不防"回调打到已释放接收者"）；此前"销毁前解除 GLFW 回调"对 3.4 是冗余（GLFW 自身 memset）且非主修复，实现时已从 Release 移除。
- 旧修法（幂等）保留为防御：`WindowImpl::Release()` 改幂等（`if (m_Window) { glfwDestroyWindow(m_Window); m_Window = nullptr; }`）。
- 主修复：核对窗口销毁与 callbacks（`s_WindowSystem`/`m_WindowFocusCallback`）的释放顺序，确保逆序 teardown 中窗口在 GM 回调对象仍有效时销毁，或窗口销毁前解除/清空 GLFW 回调引用。

### D3: 恢复 MiniDump 捕获（auto-dismiss hook 下）
`_CrtSetReportHook2`（MessageBoxTimeout）改变 abort 路径 → 不再走 MiniDump 的 SEH handler。**修法**：MiniDump handler 从仅处理 SEH 扩展到处理 `abort`/`SIGABRT`（`_set_abort_behavior`/`signal(SIGABRT)` 路径），或让 hook 在 abort 前主动 `MiniDump::WriteDump`，确保 `crash_*.dmp` 仍被写出。auto-dismiss 弹窗保留（抑制阻塞、问题可见）。

### D4: 回归（1/200，4000 过快不再用）
均为已验证的单/多测试路径，无需运行时改动。回归范围：vulkan + d3d12 全量 7 测试 `--headless 1/200` 退出码 0 + 无新 dump；`TestIMGUI`/`TestSimpleTriangle` 为崩溃关键路径。

### D5: L1（对象死时自释放）与 L3a（登记 VMA allocation，teardown 扫残留必释放）——两者都做，分工互补（2026-08-23 用户拍板 × 合并方案定稿）
**问题**：能否给 GPU 后端加个"teardown 主动释放所有存活资源"的机制，替代逐对象析构？
**用户拍板两点要求**：
① **L1**：GPU 资源（`shared_ptr<GPUBuffer>`/`<GPUTexture>`）的 shared_ptr 计数归零时，对象**必须自行正确释放其 VMA allocation**（含 `IMGUIContext::m_Fontimage`，IMGUIContext.cpp:631 只 `m_Fontimage={}`，VulkanTexture 无析构调 Release → 泄漏）。
② **L3a 是真正兜底**：即使某资源没被释放、被拖到了 teardown，backend 收尾时**必须能扫到并释放它**（不能扫不到）。

**结论**：**L1 管"死在 teardown 前"的对象（count 先归零 → L1 deleter 释放）；L3a 管"活到 teardown 时"的对象（count 仍是正数 → 必须由 sweep 强制释放）**。两者覆盖互斥的对象状态、必须共存。此前"L3a 0 命中/前瞻性防御/weak 扫不到已死对象"的定位**全部作废**——weak_ptr 方案扫不到死对象、也扫不到"活到 teardown 的对象"（因为它登记的是对象不是 allocation），是 bug。

| 类型 | 对象状态 | 谁释放 | 关键 | 实例 |
|------|---------|--------|------|------|
| **死前释放（L1）** | count 在 teardown 前归零 | L1 deleter | 对象死即释放，backend 仍存活时 `GetDevice()` 有效 | 各测试局部 buffer |
| **活到 teardown（L3a）** | count 在 teardown 时仍>0 | L3a sweep | 对象在 backend Release 时仍存活，必须在 `vmaDestroyAllocator` 前由 sweep 强制释放，且清空对象句柄防其日后析构再释放 | **[AUDIT-R5-2]**：无当前实证案例（L3a 前瞻兜底）。`m_Fontimage` 经核查**实为 L1**——逆序下 IMGUIContext(index7) 先于 backend(index2) 销毁，其在 backend 前析构走 L1 deleter，非"活到 teardown" |

**真正能做到②的机制 = 登记 VMA allocation 而非 weak_ptr<对象>**：
```
CreateGPUBuffer/CreateGPUTexture 成功分配 → 登记 {VmaAllocation, vk::Buffer/vk::Image, owner}
L1 deleter（对象计数归零）→ FreeBuffer/FreeImage → 释放 + 摘除登记
teardown sweep（VulkanMemoryManager::Release 的 vmaDestroyAllocator 之前）
  → 遍历 m_Allocations 残留：
      带 owner（对象仍存活）→ 调 owner->Release()（虚调用，清空对象 m_Allocation + 经 Free* 摘除）
      无 owner（别名池/raw/GraphLocal 裸资源）→ 按 kind 直接 vmaDestroyBuffer/Image/FreeMemory
  → 表空 → vmaDestroyAllocator
```
**为什么 weak 方案是 bug**：weak_ptr 登记"对象"，锁定对象是否还活着。而"已死未释放"（count=0，weak expired）它扫不到 → 而后者恰恰是泄漏（`m_Fontimage` 这类）。登记 **allocation** 则不受对象死活影响——只要分配未释放就在表里，sweep 必扫到，满足用户要求②。

**时序安全（sweep 顺序，代码确认）**：`RenderBackend_Vulkan::Release()` 于 `:485 WaitIdle`（GPU 空闲）→ `:510 m_GPUFrameManager.Release` → `:575 m_MemoryManager.Release`（本次 sweep 所在）→ `:581 device.destroy` → `:598 instance.destroy`。故 sweep 全程：GPU 已空闲、`m_Allocator` 与 `device` 仍有效。sweep 调 `owner->Release()` 会清空对象 `m_Allocation` → 之后该对象（如活过 teardown 的 `m_Fontimage`）若再析构，deleter 的 `Release()` 见 `m_Allocation==null` → 直接 return；**既不 double-free，也不在 pApp/device 已销毁（:581/:598）后 deref 悬垂指针**。sweep 必须在 `vmaDestroyAllocator` 之前清空残留（不可反过来）。

**double-free 消除（统一规则：把"是否已释放"的真值存进登记表）**：
```
登记表 m_Allocations: unordered_map<VmaAllocation, AllocRecord>
  struct AllocRecord { enum Kind{Buffer,Image,Memory} kind; vk::Buffer buffer; vk::Image image; VulkanSubobjectBase* owner; }
FreeBuffer/FreeImage/FreeMemory 改为幂等: auto it=m_Allocations.find(alloc); if(it==end) return; [vmaDestroyBuffer/Image/vmaFreeMemory]; m_Allocations.erase(it);
  → "是否已释放"的真值 = 是否仍在表里，而非对象成员。所有释放路径都必经这个幂等 Free*。
```
逐场景收敛（都无 double-free）：
1. **死在 teardown 前**：L1 deleter → `VulkanX::Release` → 幂等 `Free*`（find 到→vmaDestroy→erase）→ sweep 时该 allocation 已出表 → 扫不到 → 无第二释放。
2. **活到 teardown（若某资源活过 backend → L3a）**：sweep 调 `owner->Release()` → 虚分发释放 allocation + 清空对象 `m_Allocation`(→VK_NULL_HANDLE) + 经幂等 `Free*` 摘除。之后对象 shared_ptr 归零 → L1 deleter → `Release()` 头判 `m_Allocation==null` → **直接 return；绝不触碰 GetApp/GetDevice（cpp:183）** → 无 double-free，也不在 pApp/device 已销毁后 deref。（**[AUDIT-R5-2]**：`m_Fontimage` 属 L1——逆序下 IMGUIContext 先于 backend 销毁，走场景 1 而非本场景。）
3. **move 后双释放**（moved-from 与 moved-to 共享同一 allocation）：第一次幂等 `Free*`（find→vmaDestroy→erase）；第二次 `find==end` → 直接 return → 不再 vmaDestroy。登记表天然幂等兜底；另配 delete-move 作纵深（见 Risk）。
4. **别名池 ownerless**：只有单个物理 `m_AliasedPoolAllocation` 进登记表（AllocateMemory ownerless），虚拟子分配是簿记、非独立 VMA allocation、不各自进表。sweep `Free*`（find→erase）；稍后别的管理器 `FreeAliasedPool→FreeMemory(pool)` → `find==end` → 跳过 → 不 double-free 共享块。

**L1 的关键前置（改造）**：`VulkanBuffer::Release`（cpp:79-89）判 `if(m_Allocation)`（非 m_Buffer），且仅当非空才取 `GetApp()`；`VulkanTexture::Release`（cpp:181-198）头部 `if(m_Allocation==VK_NULL_HANDLE) return;`，把 `:183` 无条件 `auto device=GetDevice()` 移入 `if(m_ImageView)` 分支，保持 view→image 顺序（:185→:191）。

**用户两点要求的最终落实（2026-08-23 拍板）**：① L1（对象 count 归零即释放）与 ② L3a（登记 VMA allocation，teardown 扫残留必释放）**都做，分工互补**——L1 管"死在 teardown 前"（count 先归零），L3a 管"活到 teardown 时"（count 仍>0）。任何把 L3a 表述为"前瞻性防御/0 命中/必要性弱"或引用"backend 销毁后不该再用、UAF 应得"的说法**全部作废**——weak 方案扫不到死亡/存活对象是 bug；对象活到 teardown（若某资源，如比 backend 更晚销毁的模块仍持有）时 L1 尚未触发、必须由 L3a sweep 强制释放并清空对象句柄，才不致"对象日后析构时对已销毁 backend 释放 → 悬垂/UAF/double-free"。（**[AUDIT-R5-2]**：`m_Fontimage` 经核查属 **L1**——逆序下 IMGUIContext(index7) 先于 backend(index2) 销毁，其在 backend 前析构走 L1 deleter，非"活到 teardown"场景。）

**实现注意**：L3a 登记表 `m_Allocations` 以 `VmaAllocation` 为 key（指针型，有 hash/equality，可编译）；value 含 `owner`（`VulkanSubobjectBase*`，owner==null 表示 ownerless 别名池/裸资源）。`CreateGPUBuffer`/`CreateGPUTexture` 的 `new + 二参 deleter` 会保留具体类型 deleter（`castl=std`，`std::make_shared` 无带 deleter 重载，必须 new+deleter）。扫描置于 `VulkanMemoryManager::Release()` 的 `vmaDestroyAllocator` 之前。
**未闭合（独立 scope，不阻塞本核心）**：`VulkanLinearMemoryManager`（staging 页）绕过 VMM 直连 `vmaCreateBuffer`/`vmaFreeMemory`（经 `GetAllocator()` 共享 `m_Allocator`），其 page 分配不进登记表、sweep 扫不到。需确认其在 `:575` 前释放（或另案处理），否则 `vmaDestroyAllocator` 仍可能断言。`VulkanMemoryManager` 有 `=default` move ctor/assign，加入持分配的 `unordered_map` 成员后不可 move，需确认无 move VMM 路径。

## Risks / Trade-offs

- **[Risk] L1 释放时 `GetApp()` 的 pApp 可能已失效**（对象在 backend `Release()` 之后才析构）→ 缓解：**实现上仅当 `m_Allocation`/`m_ImageView` 非空才取 `GetApp()`/`GetDevice()`**，后端 teardown 后不会 deref 悬垂 pApp（对抗验证[H2]：`VulkanTexture::Release()` 现无条件 `GetDevice()`，VulkanTexture.cpp:183，需修正）。当前 7 测试对象在 `main()` 返回前销毁、早于 backend 析构，安全。
- **[Risk] L3a sweep 调 `owner->Release()` 可能 UAF 用户仍要用的对象**（对象活到 teardown 仍被用户持有）→ 缓解：用户拍板②明确"未释放拖到 teardown 必须被扫到并释放"，此为**预期行为**；sweep 经 `owner->Release()` 清空对象 `m_Allocation`（幂等判空 + 判非空才取 GetApp/GetDevice），对象之后若再析构 → `Release()` 见 null 直接 return → **不 double-free、不 deref 已销毁 pApp**（时序：sweep 在 `:575`，早于 device destroy `:581`）。登记表以 `VmaAllocation` 为 key（可编译），扫描置于 `m_MemoryManager.Release()`（`vmaDestroyAllocator`）之前。
- **[Risk] L1B 覆盖"graph-local 不应走此路径"**（误用 deleter 释放 graph-local 资源）→ 缓解：graph-local 资源是 `GraphResourceManager` 持有的 descriptor，不是 `shared_ptr<VulkanBuffer>`，不会踩到 deleter；只影响 `CreateGPUBuffer`/`CreateGPUTexture` 路径。
- **[Risk] 方案 B 必须放弃 `make_shared` 改用 custom deleter（对抗验证[AUDIT-7]）** → 缓解：`castl::shared_ptr`=**std::shared_ptr**（`CACore/CMakeLists.txt:55 USING_EASTL=0`、`CASharedPtr.h:7 #include <memory>`），`std::make_shared` 无带 deleter 重载，无法在其结果上挂 deleter。实现改 `new VulkanBuffer()` + 二参 deleter。接口 `GPUBuffer`/`VulkanSubobjectBase` 无虚析构——deleter 显式调 `Release()`+`delete`，故非虚基类析构问题被 deleter 接管，不再依赖具体类型析构；但若未来用 `shared_ptr<GPUBuffer>(raw, default)` 以接口类型重建且无 deleter 接管，则 `~VulkanBuffer` 静默失效→泄漏回归（实现时加 `shared_ptr<GPUBuffer>` 赋值路径存活回归用例）。**注意**：旧后端 `CRenderBakend_Vulkan.cpp:30-33` 是"工厂 `NewGPUBuffer` 返回裸指针 + deleter 调 `ReleaseGPUBuffer(p)`"，非 `new VulkanBuffer()+delete p`——与本处方仅共享"二参构造+自定义 deleter"特性。
- **[Risk] L1-B deleter 与 L3a sweep 对同一对象二次 `Release()`（double-free）** → 缓解：登记表 `m_Allocations` 统一幂等（`Free*` = find-or-return→vmaDestroy→erase），"是否已释放"真值存表非对象成员；sweep 对带 owner 调 `owner->Release()`（清空对象 `m_Allocation`），之后对象再死 → deleter `Release()` 见 null 直接 return。所有释放路径必经同一幂等 `Free*`，天然消除 double-free（见 D5 double-free 消除）。
- **[Risk] `VulkanBuffer`/`VulkanTexture` 的 `=default` move 浅拷贝双释放** → 缓解：对象经 `new + 二参 deleter` 由 shared_ptr 转移指针（不转移对象），实际从不被 move；建议 `= delete` move ctor/assign 或自定义"move 置空源对象句柄"，彻底封死浅拷贝双释放（纵深防御，配合登记表幂等）。
- **[Risk] d3d12 窗口 UAF 根因与旧假设不符**（回调对象释放而非双销毁）→ 缓解：已按新栈重新定位；主修复=保证逆序 teardown 中回调对象在窗口销毁时仍有效；幂等 Release 仅作防御兜底，不作为根因修复。
- **[Risk] dump 捕获恢复与 auto-dismiss 冲突** → 缓解：hook 保留，二者正交（hook 只管弹窗；dump 由 abort 处理交给 MiniDump）。

## Migration Plan
1. ~~D1：诊断 VMA 泄漏对象~~（已完成 2026-08-23 spike，钉死为 `VulkanBuffer::Init`/`VulkanTexture::Init`）→ 修释放（**L1 方案B 自定义 deleter 自释放 + L3a 登记 VMA allocation 扫残留**，见 D5）。
2. D2：修 d3d12 窗口销毁（按新根因：逆序 teardown 回调对象生命周期）。
3. D3：恢复 MiniDump 捕获。
4. `python build.py --config Debug` → vulkan/d3d12 全量 7 测试 `--headless 1/200` 验收。
回滚：各点 `git revert`。

## Open Questions
- ~~**VMA 泄漏的具体对象**~~ **已钉死（2026-08-23 spike）**：`VulkanBuffer::Init` / `VulkanTexture::Init` 创建的 VMA allocation 泄漏。根因 = `VulkanBuffer`/`VulkanTexture`/`GPUBuffer`/`GPUTexture`/`VulkanSubobjectBase` 均**无析构函数调用 `Release()`** 且无外部调用者；`shared_ptr` 归零时对象销毁但 `m_Allocation` 从未 `vmaDestroyBuffer/Image`。**泄漏对象不局限于"恰好 3 个测试"（对抗验证[AUDIT-5]更正）**：凡经 `CreateGPUBuffer`/`CreateGPUTexture` 创建且未显式释放的对象（含 `IMGUIContext::m_Fontimage`，IMGUIContext.cpp:491 创建、:631 仅 `m_Fontimage={}` 丢弃 shared_ptr、`~VulkanTexture` trivial 不调 Release）均按同一机制泄漏。之前"恰好 3 个 / TestIMGUI 干净"表征不可靠——TestIMGUI 单独跑抛 unknown exception（未走完 `CreateGPUTexture`，属另一独立问题）。触发 `vmaDestroyAllocator` abort 的单测实测为 TestTriangleWithImageBuffer/TestDoublePass/TestComputeBuffer（exit 3）。**D3D12 对比修正（对抗验证[AUDIT-3]、[AUDIT-9]再更正）**：并非"D3D12 靠 COM 自释放而免于泄漏"——`GPUResource` 存裸指针 `D3D12MA::Allocation*`（GPUResource.h:19，非 ComPtr 且无析构），`D3DImageObject::Release()`（D3DImageObject.cpp:11-22）只释放 view 描述符、未释放 allocation 且不被析构调用。故 D3D12 wrapper 死亡时 allocation **同样泄漏**，与 Vulkan 类型B 同构。**但"分配器是否断言"不是区别（[AUDIT-9]）**：D3D12MA 也有与 VMA 完全相同的泄漏断言——`D3D12MemAlloc.cpp:8219` `D3D12MA_ASSERT(m_pMetadata->IsEmpty() && "Some allocations were not freed...")`（注释"THE MOST IMPORTANT ASSERT IN THE ENTIRE LIBRARY!"）、`:8271` `Unfreed committed allocations found!`、`:92` `#define D3D12MA_ASSERT(cond) assert(cond)`（Debug 无 NDEBUG 下 abort，Release/RelWithDebInfo 下 no-op）。故 VMA 与 D3D12MA **Debug 都 abort、Release 都静默**；先前"D3D12 静默泄漏退出码 0"实因：(1) 该运行进入窗口 UAF 先 exit 1 崩、或 (2) RelWithDebInfo 下断言无效。**L1B/L3a 对 D3D12 的同构泄漏仍有正当性**（见 [AUDIT-10] scope 决策）。
- d3d12 窗口 UAF 是否仅在"同一进程多测试 + 多帧"下才触发（单测试 @4000 干净）：**待复现确认触发条件**（2026-08-23 d3d12 all-7 headless 200 稳定 exit 1；栈为 `WindowSystem_ImplGlfw_WindowFocusCallback`(WindowSystem_Impl.cpp:67) → `std::function` invoker，`Rax=0xDDD…` 说明回调对象已被释放——与 design D2 假设的"`~shared_ptr→glfwDestroyWindow` 双销毁"**不一致**，是"销毁窗口时 GLFW 焦点回调打到了已释放的 `s_WindowSystem`/回调函数对象"。需按新根因重新定位。）
- 是否需把 `fix-vulkan-headless-window-teardown`（vulkan 后端 `Release()` 异常安全 2.2-2.6）一并纳入本 change：**待定**（其与 VMA 泄漏可能交集，但 VMA 泄漏根因已确定是本 change 独有的）。
