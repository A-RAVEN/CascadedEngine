## ADDED Requirements

### Requirement: 后端析构触发 Release 释放自有资源
`RenderBackend_Vulkan` SHALL 提供显式析构 `~RenderBackend_Vulkan()`，其体内调用 `Release()`（镜像 D3D12 `~RenderBackend_D3D12{ Release(); }`），使 backend 拥有的 `vk::Device`/`vk::Instance`/各 manager/窗口 surface 在销毁时被释放，而非原先因无析构调 `Release()` 而泄漏为死代码。

#### Scenario: 进程退出时后端资源被释放
- **WHEN** 模块管理器逆序销毁 `VulkanRenderBackend`（在 device 使用者 IMGUIContext 之后，device 仍存活）
- **THEN** 析构调用 `Release()`，`m_Device.destroy()`、`m_VulkanInstance.destroy()`、各 manager 及窗口 surface 被销毁，不泄漏

#### Scenario: 析构不逃逸异常
- **WHEN** `Release()` 内某处 `waitIdle` 抛出 `vk::SystemError`（device-lost / out-of-device-memory / out-of-host-memory / unknown / validation-failed 等失败码）
- **THEN** 该异常已被 `Release()` 的 per-step try/catch 捕获并记录（`:493-505`、`:614-625`）；任何漏网的异常再被析构 `catch(vk::SystemError)`+`catch(...)`（`:462-469`）兜底——均不逃逸 noexcept 析构、不触发 `std::terminate`

### Requirement: Release 幂等且 init-fail 不双重销毁
`Release()` SHALL 通过后端级 `m_Released` 幂等 guard（入口 `if (m_Released) return; m_Released = true;`）保证只执行一次完整清理。Init 失败路径（createInstance catch、无物理设备早退、stepped cleanup catch）SHALL 均置 `m_Released = true`，使失败 backend 的析构调用 `Release()` 时早退、不二次销毁成员。

#### Scenario: Init 失败后析构不重复清理
- **WHEN** `Init()` 在 createInstance/无物理设备/stepped cleanup 任一路径失败并已置 `m_Released = true`
- **THEN** 析构调用 `Release()` 入口即返回，不重复销毁已含部分释放的成员

#### Scenario: 正常成功路径仅清理一次
- **WHEN** `Init()` 成功且析构调用 `Release()`
- **THEN** 清理恰好执行一次，随后任何第二次 `Release()` 均因 `m_Released` 早退

### Requirement: Release 对 init-fail/pre-device 空指针鲁棒
`Release()` body SHALL 对真正解引用 pApp 的子对象调用加 `if (m_X.GetApp())` guard（`m_GPUFrameManager.WaitIdle()`、`m_PipelineLibrary.Release()`、`m_CommandListManager.Release()`、`m_SamplerManager.Release()`），并对其余 manager 采用已知不 deref pApp 的 Release（`m_PipelineLibraryCache`/`m_MemoryManager`/`m_DescriptorSetLayoutContainer` 天然安全）。`m_Device`/`m_DebugMessenger`/`m_VulkanInstance` 清理前判空。如此在 pre-device 可抛点（dispatcher.init、createDebugUtilsMessengerEXT、enumeratePhysicalDevices、filesystem 块）抛异常、`m_Released` 仍为 false 且子对象未 InitSubObj 时，析构→`Release()` 不 NULL DEREF、不崩溃。

#### Scenario: pre-device init 抛异常后析构不崩溃
- **WHEN** 预 device 区的可抛点（如 `createDebugUtilsMessengerEXT` / `enumeratePhysicalDevices`）抛异常，子对象 pApp 均为 null
- **THEN** 析构调用 `Release()`，各 `GetApp()` guard 短路、各 manager Release 为 no-op、device/debugMessenger/instance 判空跳过，全程无 NULL DEREF

### Requirement: 后端剥离设备使用者顺序由模块逆序销毁保证
后端析构→`Release()` 销毁 device 的安全执行以"device 最后销毁"为前提：模块管理器 SHALL 按注册逆序销毁模块实例，使 device 使用者（IMGUIContext index 7）先于 device 拥有者（VulkanRenderBackend index 2）销毁（由 `fix-reverse-module-teardown-order` 提供的 `module-lifecycle` 契约保证）。本变更不改变模块销毁次序。

#### Scenario: 后端析构时无 device 使用者存活
- **WHEN** 逆序销毁执行到 `VulkanRenderBackend`（其注册序早于 IMGUIContext——具体 index 取决于各模块 factory 数，但"device 使用者后注册、先被销毁"的次序既约成立）
- **THEN** IMGUIContext（注册序更靠后）已先析构并释放其窗口句柄，后端 `Release()` 销毁 device/instance 时无 device 使用者、不 UAF
