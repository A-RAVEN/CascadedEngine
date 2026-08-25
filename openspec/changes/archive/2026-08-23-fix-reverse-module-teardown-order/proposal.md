## Why

vulkan headless（`--backend vulkan --test TestIMGUI --headless 1`）在进程 teardown 时确定性崩溃（ACCESS_VIOLATION，Rcx=device=`0xDDDD...`）。根因经 dump + 对抗验证确认：**engine 模块 teardown 正序销毁**，device 拥有者 `VulkanRenderBackend`（注册 index 2）先于 device 使用者 `IMGUIContext`（index 7）被 `delete`；Vulkan 子对象无引用计数、经裸 `pApp` 回引（`VulkanSubobjectBase::GetDevice() = pApp->GetVulkanDevice()`），backend 一 freed（0xDD），`~IMGUIContext` 销毁其窗口/字体/缓冲时 deref pApp → UAF。D3D12 同样正序、却**不崩**，靠 COM 引用计数子对象保住 device、且 `WindowContext` 析构从不 deref backend——证明**模块顺序是前提而非根因，Vulkan 无引用计数 + 裸回引才是触发**。正确的根治 = **逆向 teardown**（新建，而非在 backend 内部打补丁），让 device 使用者在 device 拥有者之前被销毁。

## What Changes

- **BREAKING（内部语义）**：`CAModuleManager::~CAModuleManager` 的实例销毁循环（`CaModuleManager.cpp:60-63`）从**正序**改为**逆序**——`releaseInstances` 从尾部（最后注册者，device 使用者）向前部（先注册者，device 拥有者）删除。使 IMGUIContext（index 7）在 VulkanRenderBackend（index 2）/D3D12RenderBackend 之前析构，销毁其全部 device 对象时 backend 仍在、`pApp` 有效。
- 保留/协调 `fix-vulkan-headless-window-teardown` 已加的 `~RenderBackend_Vulkan(){Release();}`（现在 backend 最后析构、安全销毁 device）：逆向 teardown 消除崩溃，该析构负担 device/instance 销毁（修泄漏）。但需把其 `Release()` 内 `m_GPUFrameManager.WaitIdle()`（=`vkDeviceWaitIdle`，throwing 模式）等在 teardown 可能抛 `vk::SystemError` 的调用包 try/catch，避免析构抛异常 → `std::terminate`。
- 回归：vulkan headless `TestIMGUI` 1/200/4000 退出码 0 + 无新 `crash_*.dmp` + validation log 无新错误；d3d12 双后端不回归；全量 7 测试不回归。

## Capabilities

### New Capabilities
- `module-lifecycle`: 模块管理器施行的**安全 teardown 次序**契约——实例销毁按注册序的**逆序**执行，保证"依赖他人者先于被依赖者"析构（device 使用者先于 device 拥有者），且该次序无语义回归。供测试与后续模块审计校验。

### Modified Capabilities
无（内部 teardown 次序修复，无对外行为契约变化）。

> 边界说明：本 change 修的是 engine 级 teardown 次序（崩溃根因）；vulkan 后端析构健壮性（`Release()` 异常安全）与 device 泄漏修复由 `fix-vulkan-headless-window-teardown` 承担并与之协同。窗口级 guard / GPUGraph 无改动。

## Impact

- `CACore/private/CaModuleManager.cpp` — 反转 `~CAModuleManager` 的实例销毁循环（:60-63）。同时复核 `m_Modules.clear(... Shutdown)`（:72-75 第二阶段，DLL 模块 TryRelease+FreeLibrary）是否需要同步反转或保持。
- 回归范围：全部模块 teardown 次序（TimerSystem/ThreadManager/backend/ShaderCompilerSlang/WindowSystem/IOManager_FS/CAGeneralReourceSystem/IMGUIContext）。需逐模块确认其 `Release()`/`Shutdown` 不依赖"被正序先毁的 peer 仍存活"（依赖分析见 design）。
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` — 仅配合性改动（`Release()` 内 waitIdle/destroy 包 try/catch，避免析构 terminate），与 fix-vulkan-headless-window-teardown 协同。
- 回归：vulkan + d3d12 headless `TestIMGUI`/`TestSimpleTriangle` 1/200/4000；`Tools/read_dump.py` 快读任何新 dump。
