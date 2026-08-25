## Context

`CAModuleManager::~CAModuleManager`（`CACore/private/CaModuleManager.cpp:58-78`）把模块实例按**注册序（正序）**逐一 `ReleaseModuleInstance`（`delete`，`CAModuleImplementation.h:52`），随后 `m_Modules.clear(... mod.Shutdown(this))` 卸载 DLL 模块。注册序（vulkan，`Main.cpp:1000-1021`）：

```
0 TimerSystem_Impl   1 ThreadManager   2 VulkanRenderBackend
3 ShaderCompilerSlang 4 WindowSystem   5 IOManager_FS
6 CAGeneralReourceSystem   7 IMGUIContext
```

正序 teardown 意味着 `VulkanRenderBackend`(2) 先于 `IMGUIContext`(7) 被 `delete`。崩溃链（dump 铁证，见 proposal/round-2 Review Log）：backend 被 free（0xDD）时 IMGUIContext 仍持有 device 依赖对象（窗口 handle、`m_Fontimage` 纹理、index/vertex buffer、shader struct）；`~IMGUIContext`（`IMGUIContext.cpp`）销毁 `m_WindowHandles`（`vector<shared_ptr<WindowHandle>>`）→ `~VulkanWindowHandle` → `Release()`（`VulkanWindowHandle.cpp:50`，`m_Released==false`）→ `CleanupSwapchain`（:230）→ `device.destroyImageView()`,其中 `device = GetDevice() = pApp->GetVulkanDevice()`（`VulkanSubobjectBase.cpp:13-16`）= 以**裸 `pApp`** 解引用 backend → 读 freed backend → device=0xDD → `VkLayer_khronos_validation!DestroyImageView` AV（`chassis.cpp:2013`）。

**D3D12 反证**（正序但 exit 0）：`WindowContext`（`D3D12RenderBackend/private/WindowContext.h`）用 `ComPtr<IDXGISwapChain4>` + `ComPtr<ID3D12Resource>`（COM refcount），析构直接释放 COM 引用、**从不 deref backend**；且 `RenderBackend_D3D12::Release()`（:238-257）只置 `m_Device=nullptr`、**从不 `Release()` device**（device 被 COM 引用计数保住）。→ **模块顺序是前提，Vulkan"无引用计数 + 裸 pApp 回引"才是触发**。不能在 Vulkan 内部补丁消除（IMGUIContext 持有多种 device 对象、`pApp` 裸回引，任何在 backend 先毁后释放使用者的修法都会把 0xDD 挪到下一对象），须让 device 使用者在 device 拥有者**之前**销毁。

## Goals / Non-Goals

**Goals:**
- 使 `CAModuleManager` 实例销毁按**注册序逆序**执行：`IMGUIContext`(7) 先于 `VulkanRenderBackend`(2)/`D3D12RenderBackend` 析构，使所有 device 使用者在 device 存活期内释放其 device 对象（窗口/纹理/缓冲），消除 `pApp` 悬垂 UAF。
- 维持模块注册"先依赖、后依赖者"的 DI 不变式，使逆序 teardown = "先依赖者、后依赖"（正确的组合根销毁次序）。
- 与 `fix-vulkan-headless-window-teardown` 协同：保留其 `~RenderBackend_Vulkan(){Release();}`，但现在 backend 最后析构、可安全销毁 device；并给其 `Release()` 内可抛 `vk::SystemError` 的调用（`m_GPUFrameManager.WaitIdle()`、`m_Device.waitIdle()`、`destroy*`）包 try/catch（析构不抛、不 terminate）。
- 回归：vulkan + d3d12 headless `TestIMGUI`/`TestSimpleTriangle` 1/200/4000 退出码 0 + 无新 dump + 无 validation-log 错误；全量 7 测试不回归。

**Non-Goals:**
- 不改模块注册/Init 次序（保持"依赖先注册"）；只在 teardown 反转。
- 不重构模块管理器（不加依赖拓扑排序/图；逆序已覆盖当前注册无环情形）。
- 不为 Vulkan 手搓引用计数/COM（不可行，见 Risks）。
- 不处理 validation layer 其它告警（VUID-vkCmdDrawIndexed-None-04007/07312 为 IMGUI draw-bind 独立问题，另案）。
- 不回收"device 泄漏"之外的历史泄漏（本次仅保证不崩 + backend 析构安全销毁 device）。

## Decisions

### D1: 反转实例销毁循环（主修复）
`CaModuleManager.cpp:60-63` 改为逆序：
```cpp
for (int i = static_cast<int>(m_Factories.size()) - 1; i >= 0; --i)
    m_Factories[i]->ReleaseModuleInstance(m_FactoryInstances[i]);
```
**理由**：正序 teardown 把"device 拥有者"先于"device 使用者"销毁，是构造序误用于销毁；逆序恢复"先销毁依赖者、后销毁依赖"的组合根语义。逆序下任一 index `i` 的模块销毁时，其依赖（更低 index）均已存在且未销毁 → teardown 安全。
**备选（否决）**：在 Vulkan backend 内打补丁（只标窗口/只不毁 device）——不完整（IMGUIContext 有窗口+字体+缓冲多种 device 对象，`pApp` 都悬垂，0xDD 只是挪动）。模块拓扑排序——超出必要（无环即可逆序）。

### D2: 依赖方向不变式（安全性依据）
**不变式**：模块仅依赖**更低注册 index** 的模块（DI 先注册依赖再注册依赖者）。逆序 teardown 下，模块 `i` 销毁时其全部依赖（`< i`）仍存活 → 安全。`Main.cpp` 注册序符合该不变式（IMGUIContext 7→backend 2 / WindowSystem 4 / CAGeneralReourceSystem 6；CAGeneralReourceSystem 6→backend 2 / IOManager_FS 5；WindowSystem 4→backend 2）。**需审计任务**：逐模块 grep 其 `Release()`/`Shutdown` 是否 deref 了**更高 index** 的 peer（若某模块依赖更高 index，则逆序会破坏它——若发现，需单独处理/或该模块需在更早注册）。

### D3: 保持第二阶段（DLL Shutdown）不变，待审计
`:72-75 m_Modules.clear(... Shutdown)` 是**已加载 DLL 模块**（`CAModule::Shutdown` → `TryRelease` + `FreeLibrary`），与实例销毁（`m_Factories`）不同源。初判无需反转（DLL 卸载次序独立于实例注册次序），但列为审计任务确认其 `TryRelease` 不依赖实例存活。

### D4: 与 fix-vulkan-headless-window-teardown 协同（去重）
保留 `~RenderBackend_Vulkan(){Release();}`（由 fix-vulkan-headless-window-teardown 加，现在 backend 最后析构）：IMGUIContext 先析构（device 活）→ no UAF；backend 后析构 → `Release()` 安全销毁 device/instance。**`Release()` 的异常安全 try/catch 由 fix-vulkan-headless-window-teardown 的 design D4 / task 2.2 负责实现，本 change 不重复实现**，仅作为协同前提（确认其把 `m_GPUFrameManager.WaitIdle()`、`m_Device.waitIdle()`、各 `destroy*` 包 try/catch，避免析构抛 vk::SystemError → `std::terminate`；当前修复版停在 WaitIdle 抛异常被吞后跳过窗口循环，逆序后不再导致 UAF，但 try/catch 仍防 terminate/泄漏）。

### D5: 回归验收（D3）
- `python build.py --config Debug` 全量构建。
- vulkan headless `TestIMGUI` + `TestSimpleTriangle` 1/200/4000：退出码 0 + 无新 `crash_*.dmp` + 扫描 `test_output/*_validation.log` 无新增 VUID/object-tracking（**用 `TestIMGUI` —— 这是真正触发崩溃路径的测试，此前误用 `TestSimpleTriangle` 是假阴性来源，已作废**）。
- d3d12 双后端 headless 1/200/4000 不回归。
- 任何新 dump 用 `Tools/read_dump.py` 快读（cdb 交叉验证）。

## Risks / Trade-offs

- **[Risk] 逆序 teardown 可能破坏某模块对"更高 index peer"的正序依赖** → 缓解：D2 审计任务逐模块核对；若发现，视情况调整该模块注册位置或仅对该依赖做显式续期（不牺牲全局逆序）。当前注册序符合"依赖先注册"不变式，预期无。
- **[Risk] 其它模块 teardown 现有怪癖被暴露**：正序下某些模块可能习惯性地依赖早毁的 peer（例如某模块 `Release()` 在正序中被早毁 peer 触发 skip）；逆序会改变其触发时机 → 缓解：全量回归 + 逐模块 `Release()`/`Shutdown` 读一遍；`m_Modules`(DLL) 第二阶段保持并审计。
- **[Risk] vulkan 析构抛异常**（若无 4 的 try/catch）→ 缓解：D4。
- **[Risk] 手搓 COM/引用计数** → 否决（Vulkan 对象生命周期与 COM 不符，大动且易错）。
- **[Risk] IMGUI draw-bind VUID（04007/07312）会污染 validation-log 回归判读** → 缓解：判读时区分该既有 VUID 与本 change 新增错误；该问题另案。

## Migration Plan
1. 改 `CaModuleManager.cpp:60-63` 逆序。
2. 给 vulkan `RenderBackend_Vulkan::Release()` 加 try/catch（协同 fix-vulkan-headless-window-teardown）。
3. 构建 + vulkan/d3d12 headless `TestIMGUI`/`TestSimpleTriangle` 回归。
4. 模块 teardown 依赖审计（D2/D3）。
回滚：`git revert` 本 change（单点改动，低风险）。

## Open Questions
- **（对抗审查反馈，2026-08-23）已确认存在"依赖更高 index"的实例**：`VulkanRenderBackend`（index 2，低）依赖**更高** index 的 `ShaderCompilerSlang`(3) 与 `WindowSystem`(4)。即 D2 不变式"后来者依赖先来者"**不严格成立**——逆序把 backend(2) 排在 3/4 之后销毁，需审计 backend 析构（`~RenderBackend_Vulkan`/`Release()`）是否依赖 3/4 存活（尤其其 `Release()` 是否用 ShaderCompilerSlang 管线/WindowSystem）。若依赖，需处理（如仅对 device 相关 segment 逆序、或 backend 注册提前）。**这是 D2 审计任务 3.1 的重点。**
- `m_Modules`(DLL) 第二阶段是否需逆序（D3 审计）？
- 是否应顺带把 `ThreadManager`/`TimerSystem_Impl` 之后（index 0/1）在逆序中最后销毁的依赖暴露出来（如它们自身 teardown 是否引用更高 index）？
