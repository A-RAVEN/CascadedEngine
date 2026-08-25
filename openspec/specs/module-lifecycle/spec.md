## ADDED Requirements

### Requirement: 模块实例逆序销毁
模块管理器 SHALL 在析构时按**注册序的逆序**销毁各模块实例，使后注册者（依赖者）先于先注册者（依赖）被销毁。

#### Scenario: teardown 时设备使用者先于设备拥有者销毁
- **WHEN** `~CAModuleManager` 释放实例
- **THEN** 后注册的 `IMGUIContext`（index 7）先于先注册的 `VulkanRenderBackend`（index 2）/`D3D12RenderBackend` 被 `ReleaseModuleInstance`（`delete`）销毁

#### Scenario: 依赖者销毁时其依赖仍存活
- **WHEN** 任一 index `i` 的模块实例被销毁
- **THEN** 它依赖的更低 index 模块实例在此期间仍存活、可安全访问，不出现悬垂引用

### Requirement: 销毁顺序与构造顺序逆向且无回归
逆序销毁 SHALL 与"模块依赖先注册、依赖者后注册"的注册不变式一致，且不改变任何模块的 Init/注册次序。

#### Scenario: 销毁全程不崩溃、无泄漏回退
- **WHEN** 进程退出执行逆序模块销毁
- **THEN** 全部依赖者的 device 对象在 device 存活期内释放，进程退出码 0、无 crash、validation log 无新增错误、无因次序变更引入的 UAF

#### Scenario: Init/注册次序不变
- **WHEN** 模块管理器 Init（注册 + LinkModules）
- **THEN** 各模块仍按原有注册序初始化（逆序仅作用于销毁阶段，不影响 Init 次序）
