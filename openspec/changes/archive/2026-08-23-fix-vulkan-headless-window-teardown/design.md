## Context

`RenderBackend_Vulkan`（`VulkanRenderBackendNew/private/RenderBackend_Vulkan.{h,cpp}`）没有显式析构调 `Release()`（`RenderBackend_Vulkan.h` 原为 `RenderBackend_Vulkan() = default;`，无 `~RenderBackend_Vulkan`；对比 D3D12 `~RenderBackend_D3D12(){ Release(); }` `RenderBackend_D3D12.cpp:259-262`）。因此 `Release()` 实际是从未被调用的**死代码**——`vk::Device`/`vk::Instance`/各 manager/窗口 surface 在销毁时从未被释放（泄漏）。而它内部存在健壮性缺陷：不幂等、Init 失败路径双重清理/空指针、非异常安全。

**与 teardown 崩溃的关系（诚实修正）**：vulkan headless teardown 崩溃（`--backend vulkan --test TestIMGUI`，device=0xDD UAF，`~IMGUIContext → ~VulkanWindowHandle → CleanupSwapchain → GetDevice()=pApp->GetVulkanDevice()` 解引用已 freed backend）的**根因是模块正序 teardown**（`~CAModuleManager` 正序 `ReleaseModuleInstance`：backend index 2 先于 IMGUIContext index 7 被 delete；Vulkan 无引用计数 + 裸 `pApp` 回引；D3D12 靠 COM refcount 幸存）。**本 change 的 dtor→`Release()` 方案实测无法消除该崩溃**（dump `crash_20260822_174423`：修复版仍 exit 1，`Release()` 停在 `m_GPUFrameManager.WaitIdle()` 抛 `vk::SystemError`、被析构吞掉后跳过窗口循环，窗口未标记 `m_Released` → 仍在 `~IMGUIContext` 走 `CleanupSwapchain`；且即使修好窗口，IMGUIContext 还有字体纹理/缓冲等 device 对象会 next 撞上 0xDD）。正确地根在 `fix-reverse-module-teardown-order`（逆向 teardown，device 最后销毁）。**本 design 只管 vulkan 后端自身的 teardown 健壮性 + 资源清理，并与逆向 teardown 协同。**

## Goals / Non-Goals

**Goals:**
- 用显式析构触发 `Release()`（镜像 D3D12 形态），修复 device/instance/各 manager/窗口 surface 从未销毁的泄漏。
- 把 `Release()` 修成**异常安全 + 幂等 + init-fail 健壮**：可抛 vk::* 调用包 try/catch（析构不抛不 terminate）；`m_Released` 幂等 guard；Init 失败路径补 `m_GPUFrameManager.Release()` + `GetApp()` 空指针 guard。
- 协同逆向 teardown change，使 dtor→`Release()` 在 device 最后销毁的前提下安全执行。

**Non-Goals:**
- **不修 teardown 崩溃**（device=0xDD UAF）——那是「模块正序 teardown」的根因，属 `fix-reverse-module-teardown-order`。本 change 不做顺序调整、不猜"让窗口先释放"。
- 不重构模块系统；不是 vk 对象引用计数（手搓 COM，不可行）。
- 不处理 IMGUI draw-bind VUID（VUID-vkCmdDrawIndexed-None-04007/07312，独立问题）。

## Decisions

### D1: 显式析构 → Release()（修泄漏，非修崩溃）
`~RenderBackend_Vulkan(){ try { Release(); } catch(...) { log } }`。**修正措辞**：这镜像 D3D12 的"析构调 Release()"形态，但**其价值是修 device/instance 泄漏 + 让 teardown 收敛**；不是修崩溃。它在逆向 teardown（device 最后销毁）下才真正安全执行。**析构 catch 已防 terminate**（:458-469 try{Release()}catch(vk::SystemError)+catch(...) 覆盖全部异常类型，不逃逸 noexcept 析构、不 std::terminate）。`Release()` 内两处 `waitIdle` 抛 `vk::SystemError` 时被此析构 catch 吞——但会**截断清理**（m_Released 已置 true，二次 Release() 早退 → 窗口/device/instance 跳过泄漏），故 D2 需 per-step try/catch 继续清理。

### D2: `Release()` 异常安全（本 change 主力）——**只包会抛的，逐步包**
**勘误（对抗审查 Round-3）**：`vk::*` 的 **void destroy 包装是 noexcept、不会抛**（`vkDestroyDevice`/`vkDestroyInstance`/`vkDestroyImageView`/`vkDestroySwapchainKHR`/`vkDestroyFramebuffer`/`vkDestroyRenderPass` 等均返回 void），**只有返回 `VkResult` 的调用会抛**——本 change 相关的是**两处 `waitIdle`**：`m_GPUFrameManager.WaitIdle()`（= `GetDevice().waitIdle()`，:485）和 `m_Device.waitIdle()`（:583）。其余 `destroy*` **不需要**包 try/catch（包了也是多余）。**触发条件 = 仅 device-lost（真实 Vulkan 失败模式）期间 teardown 抛；正常逆向 teardown（device 存活）下 `waitIdle` 返回 VK_SUCCESS 不抛。**

**逐步 try/catch（关键）**：因为 `m_Released=true` 在 Release() 顶部先设置，若用**单个外层 try/catch 包整个 body**，一旦某抛点 abort，第二次 Release() 会因 `m_Released` 早退 → 永远停在半清理状态。所以必须 **per-step 包**，catch 后继续：
```cpp
if (m_Released) return;  m_Released = true;
try { m_GPUFrameManager.WaitIdle(); }          // :485 vkDeviceWaitIdle 可抛（device-lost）
catch (vk::SystemError const& e) { CA_LOG_WARN("Release: WaitIdle threw ({}); continuing", e.what()); }
catch (...) { CA_LOG_WARN("Release: WaitIdle threw unknown; continuing"); }
// ...（窗口循环：m_Released 标记 + CleanupSwapchain，窗口释放为 void 不抛）...
// ...（各 manager Release() 见 D6 需 GetApp() guard）...
if (m_Device) {
    try { m_Device.waitIdle(); } catch (vk::SystemError const&) {}   // :583 必须也包——device-lost 时这里会二次抛
    m_Device.destroy();  // noexcept，无需包
    m_Device = nullptr;
}
if (m_DebugMessenger) { m_VulkanInstance.destroyDebugUtilsMessengerEXT(m_DebugMessenger); m_DebugMessenger = nullptr; }
if (m_VulkanInstance) { m_VulkanInstance.destroy(); m_VulkanInstance = nullptr; }
```
**理由**：Vulkan vk::* 在 throwing 模式抛 `vk::SystemError`，而 D3D12 是 HRESULT 不抛——"镜像 D3D12 析构"对 Vulkan 必须改造成异常安全。析构内吞异常（记录），不传播；**`waitIdle` 抛后清理仍要继续到窗口释放 + device destroy**。

### D3: 后端级 `m_Released` 幂等 guard（**覆盖有缺口，对抗审查 Round-3**）
`Release()` 入口 `if (m_Released) return; m_Released = true;`，且 Init 失败路径（`createInstance` catch :253、无物理设备早退 :268、`:387-401` stepped cleanup :411）都设 `m_Released = true`。**理由**：新增析构→`Release()` 后，Init 失败的 backend 析构时再调 `Release()` 会二次销毁成员 + `m_GPUFrameManager.WaitIdle()`(:485) 对未 Init 的 frame manager 解引用 `pApp=nullptr` NULL DEREF。幂等 guard 阻止。与窗口级 `m_Released`（`VulkanWindowHandle`）是不同对象，不冲突。经 Round-0/1 对抗验证：guard 措辞（设 `m_Released=true`，非 `m_Initialized`）正确且必备。**覆盖缺口**：`m_Released=true` 目前只在被 try 包住的 Init 区（:253/:268/:411）设；pre-device 可抛点（:216 dispatcher.init、:256 instance init、:258 createDebugUtilsMessengerEXT、:261 enumeratePhysicalDevices、:172-214 filesystem 块）在 try 之外，抛时 `m_Released` 仍 false + 六子对象 pApp=null → 析构→Release()(:485) NULL DEREF。**修法**：把 pre-device 未守护区包 `catch(...){ m_Released=true; }`（或靠 D6 4 处 GetApp guard 兜底）。

**⚠️ 覆盖缺口（实测/对抗 Round-3 确认）**：`m_Released=true` 目前只在**被 try 包住的 Init 区**（:246-255、:267、:387-411）设。但 Init() 有**位于 try 之外**、可抛 vk::* 的点：**:216 `VULKAN_HPP_DEFAULT_DISPATCHER.init()`、:172-214 std::filesystem/shader-importer 块、:256-258 `createDebugUtilsMessengerEXT`、:261 `enumeratePhysicalDevices`**。这些抛异常时 `m_Released` 仍 =false **且** 六个子对象 `pApp=null`（`m_GPUFrameManager` 未 InitSubObj）→ 析构→`Release()`→`WaitIdle()` **NULL DEREF**。此外**默认构造 + Init 从未调用**也有同样问题（m_Released 恒 false）。
**修法（二选一）**：
- **一次性根治**：把 pre-device 未守护 Init 区（:172-214、:216、:256-261）包进 `catch(...){ m_Released=true; CA_LOG_ERR(...); }`（或 `try`），使任何 pre-device 抛异常都设 `m_Released=true` → 析构 `Release()` 早退。
- **配合 D6**：`Release()` body 对每个 deref pApp 的子对象 `Release()`/`WaitIdle()` 都 guard `GetApp()`，使即便 m_Released=false 也不崩（只是清理不完整）。

### D4: Init-fail 修补（REL-2 + G2）
`:387-401` catch 里、`m_Device.destroy()` 之前加 `m_GPUFrameManager.Release()`（修 REL-2 init-fail frame manager 泄漏）；`VulkanGPUFrameManager::Release()`（`m_FrameContexts` 为空即 no-op）无需 guard，可直接调。同时对 `m_PipelineLibrary`/`m_CommandListManager` 等 `Release()` 前 guard `m_X.GetApp()` 非空（修 G2 早期异常时 null-deref——`VulkanCommandListManager::Release`/`VulkanPipelineLibrary::Release` 顶部都无条件 `GetDevice()`，pApp=null 即崩）。**本 D4 仅覆盖 `:387-401` stepped-cleanup catch 路径；正常 Release() body 的同类 guard 见 D6**。

### D6: `Release()` body GetApp() guard（对抗 Round-3 新增，HIGH；对抗 Round-2 缩减范围）
正常 `Release()` body 中**真正 deref pApp** 的 4 处需 guard：`m_GPUFrameManager.WaitIdle()`(:485)、`m_PipelineLibrary.Release()`(:541)、`m_CommandListManager.Release()`(:569)、`m_SamplerManager.Release()`(:572)——它们内部 `GetDevice()=pApp->GetVulkanDevice()`（VulkanSubobjectBase.cpp:13-16）deref pApp。`m_GPUFrameManager.WaitIdle()`=`GetDevice().waitIdle()`（VulkanFrameManager.cpp:305-308）。**对抗核正：`m_PipelineLibraryCache.Release()`(:538 仅 `ClearCache()` PipelineLibraryCache.cpp:12-17)、`m_MemoryManager.Release()`(:575 仅判 `m_Allocator`)、`m_DescriptorSetLayoutContainer.Release()`(:578 继承空 `Release(){}` VulkanSubobjectBase.h:18) 均天然不 deref pApp，无需 guard**（勿再写"6 处全 deref"）。**修法**：给上述 4 处 deref pApp 的调用加 `if (m_X.GetApp())`（镜像 :401-406 的 D4 模式）；`m_GPUFrameManager.GetApp()` 合法（`VulkanFrameManager.h:79` 继承 VulkanSubobjectBase，`GetApp()` public）。**触发（真实可达）**：main() 顶层无 catch，pre-device 可抛点（:216 dispatcher.init / :256 instance / :258 createDebugUtilsMessengerEXT / :261 enumeratePhysicalDevices）在 try 之外 → `m_Released` 仍 false + 子对象 pApp=null → 析构→Release()(:485/:541/:569/:572) NULL DEREF——D1 析构改动自身新引入的可达崩溃。窗口循环 `weakHandle.lock()`（m_WindowHandles）在 Init fail 路径为空、`m_GPUFrameManager.Release()` 空 m_FrameContexts 即 no-op，本已安全，无需额外 guard。

### D5: 协同 fix-reverse-module-teardown-order
本 change 的 dtor→`Release()` 只在 **device 最后销毁**（逆向 teardown）时才真正安全：届时 IMGUIContext 已析构，无 device 使用者。因此本 change 的回归验收以逆向 teardown change 落地为前提。

## Risks / Trade-offs

- **[Risk] `waitIdle` 抛异常 → 析构 terminate** → 缓解：D2 逐步 try/catch（`m_GPUFrameManager.WaitIdle()`、`m_Device.waitIdle()` 两处；**`destroy*` 是 void no-throw，不需包**）。注意必须逐步包，不能外层单包（m_Released 早设会导致二次调用半清理）。
- **[Risk] pre-device Init 未守护区抛异常 → 析构 Release() pApp-null NULL DEREF**（本 change 加 dtor→Release 新引入的**可达**崩溃）→ 缓解：D3 修法二选一（pre-device 区包 catch 设 m_Released，或 D6 4 处 GetApp guard）。
- **[Risk] 正常 Release() body unguarded pApp deref**（:485 WaitIdle / :541 PipelineLibrary / :569 CommandListManager / :572 SamplerManager）→ 缓解：D6 这 4 处每个 `if (m_X.GetApp())`。
- **[Risk] 与逆向 teardown 的耦合**：本 change 单独不消除崩溃（未逆向时 dtor→Release 仅把 0xDD 挪到 IMGUIContext 字体纹理/缓冲）→ 缓解：明确本 change 定位为"健壮性 + 泄漏"，崩溃归属逆向 teardown change；回归在逆向 teardown 落地后统跑。
- **[Risk] 手搓 COM/引用计数** → 否决（与 Vulkan 对象生命周期语义不符，大动易错）。
- **[Risk] 另一 tester（VulkanRendererBackendTester）显式 `pBackend->Release()`（Main.cpp:427）无保护** → 缓解：`Release()` 异常安全化（D2）后即便那里抛也被内部吞掉；如需彻底，可评估该调用点加 try/catch（本 change 不强制，记录）。

## Migration Plan
1. 本 change：给 `RenderBackend_Vulkan` 加析构→`Release()`（异常安全：两处 waitIdle :485/:583 逐步 per-step try/catch）+ 幂等 guard（D3，含 pre-device 缺口修法）+ init-fail 修补（D4）+ Release() body 4 处 deref pApp（:485/:541/:569/:572）`GetApp()` guard（D6）。
2. `fix-reverse-module-teardown-order`：`CaModuleManager.cpp:60-63` 逆向 teardown。
3. 两个 change 均落地后：`python build.py --config Debug` → vulkan/d3d12 headless 回归。
4. 回滚：单点 `git revert`。

## Open Questions
- ~~`window-release` 循环是否需各自 try/catch（destroyImageView 能否抛）~~：**已定论——`destroyImageView` 返回 void、no-throw，不需包。** 窗口循环里真正会抛的是其前置/后置的 `waitIdle`（已由 D2 覆盖）。
- `m_QueueContext`（InitSubObj 过但任何路径都不 release，小泄漏）/`m_ShaderImporter`（dangling registration）是否纳入本 change？——`m_QueueContext` 顺手补 `Release()`（TC-6 部分），非阻塞；`m_ShaderImporter` 仅在有 import 动作的 teardown 才需 unregister，无此场景，记录 Review Log，不展开。
- `:265-267` 销毁 `m_DebugMessenger`/`m_VulkanInstance` 后没置空 `m_DebugMessenger`（stale handle）→ 顺手置空（`m_DebugMessenger = nullptr`）。
