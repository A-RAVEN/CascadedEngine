## Why

`RenderBackend_Vulkan` **没有显式析构调 `Release()`**（对比 D3D12 `~RenderBackend_D3D12(){ Release(); }`），导致 backend 的 `vk::Device`/`vk::Instance`/各 manager/窗口 surface 资源在销毁时**从未被释放**（泄漏）。同时它的 `Release()` 本身有健壮性缺陷：**不幂等**（`Release()` 只应跑一次完整清理）；**Init 失败路径双重清理/空指针风险**（`:387-401` catch 遗漏 `m_GPUFrameManager.Release()`、早期异常时对未初始化的子对象调 `Release()` 会 null-deref）；以及 **非异常安全**（`Release()` 内含可抛 `vk::SystemError` 的两处 `waitIdle` — `m_GPUFrameManager.WaitIdle()`:485 与 `m_Device.waitIdle()`:583，仅 device-lost 时抛；`vkDevice::destroy` 等 `destroy*` 是 void no-throw。若析构 `Release()` 中 `waitIdle` 抛——虽被 `~RenderBackend_Vulkan` try/catch 吞掉不 `std::terminate`，但会**截断清理**（`m_Released` 已置 true → 二次 Release() 早退 → 窗口/device/instance 等跳过 → 泄漏）。故需 per-step try/catch 让 device-lost 时仍**继续清理**）。

**边界说明（诚实记录）**：vulkan headless teardown **崩溃**（device=0xDD 的 UAF，`--backend vulkan --test TestIMGUI`）的**根因是模块正序 teardown**——device 拥有者 `VulkanRenderBackend`(index 2) 先于使用者 `IMGUIContext`(index 7) 被 `delete`。该崩溃**不由本 change 修复**（本 change 的 dtor→`Release()` 方案实测无法消除它，且在"模块仍正序"前提下不成立），**由 `fix-reverse-module-teardown-order`**（逆向 teardown，device 最后一个销毁）根治。本 change 只负责 vulkan 后端自身的 teardown 健壮性 + 资源清理，二者协同：逆向 teardown 让 dtor→`Release()` 得以安全执行销毁 device。

## What Changes

- 给 `RenderBackend_Vulkan` 添加显式析构 `~RenderBackend_Vulkan(){ Release(); }`（.h 声明 + .cpp 定义），镜像 D3D12 形态但**针对 Vulkan 修正为异常安全**。修复 device/instance/各 manager/窗口 surface 从未销毁的泄漏。**配合逆向 teardown change**：IMGUIContext 先析构（device 活）→ 无 UAF；backend 后析构 → `Release()` 安全销毁 device/instance。
- **Release() 异常安全（新增，本 change 主力）**：把 `Release()` 内**会抛** `vk::SystemError` 的两处 `waitIdle`（`m_GPUFrameManager.WaitIdle()`:485、`m_Device.waitIdle()`:583，仅 device-lost 时抛；`destroy*` 是 void no-throw 不需包）包 per-step try/catch——`~RenderBackend_Vulkan` 已 catch `Release()` 不 terminate，价值是 device-lost 时**继续清理**（窗口/device/instance 不因抛而半清理泄漏）；per-step 包（非外层单包，因 `m_Released` 前置 true）使 catch 后仍走窗口释放 + `m_Device.destroy()`。
- **后端级幂等 guard**：加 `m_Released` 成员，`Release()` 入口 `if (m_Released) return; m_Released = true;`；Init 失败路径（`createInstance` catch、无物理设备早退、`:387-401` stepped cleanup）设该标志，避免析构→`Release()` 双重销毁 + `m_GPUFrameManager.WaitIdle()`（:485）对未 Init 的 frame manager NULL DEREF。
- **Release() body GetApp() guard**：`Release()` 内**真正 deref pApp** 的 4 处（`m_GPUFrameManager.WaitIdle()`:485、`m_PipelineLibrary.Release()`:541、`m_CommandListManager.Release()`:569、`m_SamplerManager.Release()`:572）加 `if (m_X.GetApp())`，封堵 pre-device Init 抛（:216/:256/:258/:261）/Init 未调用时 pApp=null 的 NULL DEREF（main() 顶层无 catch，该路径真实可达）。`m_PipelineLibraryCache`/`m_MemoryManager`/`m_DescriptorSetLayoutContainer` 的 Release 天然不 deref pApp，无需 guard。
- **Init-fail 修补**：`:387-401` catch 加 `m_GPUFrameManager.Release()`（修 REL-2 泄漏），并对 `m_PipelineLibrary`/各 manager 的 `Release()` 前 guard `GetApp()` 非空（修 G2 早期异常 null-deref）。
- 回归：与 fix-reverse-module-teardown-order 协同——vulkan headless `TestIMGUI`/`TestSimpleTriangle` 1/200/4000 退出码 0 + 无新 `crash_*.dmp` + validation log 无新增错误；d3d12 双后端不回归。

## Capabilities

### New Capabilities
无

### Modified Capabilities
无

> 边界说明：内部 teardown 健壮性/泄漏修复，无对外行为契约变化。teardown 崩溃根因（模块正序）在 `fix-reverse-module-teardown-order` 处理。窗口级 guard / GPUGraph 无改动。

## Impact

- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.{h,cpp}` — 添加显式析构→`Release()`（异常安全）+ 后端级 `m_Released` 幂等 guard + Init 失败路径补 `m_GPUFrameManager.Release()` 及 `GetApp()` 空指针 guard + `Release()` 内两处 `waitIdle` 包 per-step try/catch + Release() body 4 处 deref pApp 加 `GetApp()` guard。
- 协同：`fix-reverse-module-teardown-order`（`CACore/private/CaModuleManager.cpp` 逆向 teardown）是本 change 中 dtor→`Release()` 得以安全销毁 device 的前提。
- 回归：vulkan + d3d12 headless（`TestIMGUI`/`TestSimpleTriangle`）1/200/4000，`Tools/read_dump.py` 快读任何新 dump。
