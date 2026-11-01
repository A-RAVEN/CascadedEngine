## Why

`IWindowSystem` 目前只有 5 个方法进入 C# 函数表，10 个窗口/输入回调和 `GetMonitor` 仍被 `CA_BIND_SKIP` 排除。托管侧能建窗和改窗，却不能接收焦点、光标、鼠标、键盘、关窗和尺寸变化通知，也读不到显示器矩形与 DPI，窗口通路还不具备编写交互式工具所需的基本反馈能力。

## What Changes

- 为 `IWindowSystem` 使用的 `castl::function<void(...)>` 实现真实回调投影：C ABI 使用函数指针与不透明用户令牌，生成两侧登记、保活、替换、清空和注销代码，不再生成拒绝分支或空调用桩。
- 让 `IWindowSystem` 的 10 个 `Set*Callback` 方法全部进入生成绑定与 C# 表面。回调里的 `IWindow*` 还原为受生命周期约束的 C# 窗口对象；标量参数保持原始类型与顺序，回调异常不得穿过原生边界。
- 明确回调生命周期：重复设置替换旧回调，传入空回调清除注册，窗口系统表面对象释放时注销仍存活的注册；在 `CSharpHost` 关闭运行时前调用托管 `Shutdown`，确保关闭路径也执行注销。替换、清空和释放后不得再调用旧托管委托，也不得遗留保活引用。
- 为 `MonitorInfo` 与其中的 `cacore::Rect<float>` 增加嵌套、纯标量记录镜像，让 `GetMonitor` 按值返回完整的显示器矩形、工作区和 DPI，并继续做两侧布局自检。绑定层必须先校验显示器编号，无效编号用统一错误通道上报，不得被原生 clamp 静默改成另一个显示器。
- 扩展绑定夹具、CoreTests 与 `TestCSharpWindow`，用确定性事件验证参数、窗口身份、替换/清空/释放、回调异常及显示器逐字段读数；同步可绑定面审计、生成器文档和测试文档。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `csharp-bindings`: 回调从“必须排除”改为生成真实注册与调用通路；嵌套纯标量记录镜像进入布局与按值返回规则。
- `csharp-bindable-surface`: 明确回调用户令牌、保活/注销、回调内借用对象与异常边界，以及嵌套记录的投影约束。
- `csharp-window-runtime`: `IWindowSystem` 的 10 个回调与 `GetMonitor` 从不可见改为可调用，并规定端到端行为。

## Non-goals

- 不做 `CRenderBackend` / `GPUGraph` 的 C# binding；该方向另由 `TODO/006-csharp-render-backend-binding.md` 记录。
- 不实现通用跨线程托管调度；本 change 只覆盖当前窗口系统的引擎主线程回调语义，其他线程不得直接闯入托管侧。
- 不实现带返回值的通用回调，也不处理 `IWindowSystem` 之外的新回调类型。
- 不支持含字符串、容器、指针或自定义所有权成员的通用嵌套结构体；本 change 只处理由纯标量记录递归组成的镜像。
- 不改变 `IWindowSystem` 的 C++ 方法签名，不新增窗口事件种类，不修改 GLFW 回调实现。
- 不放宽函数表版本检查，不生成只有签名、没有真实回调或返回值的桩。

## Impact

- `Tools/CodeGen/CodeGenMain.cpp`、`Tools/CodeGen/BindableShapes.h`、`Tools/CodeGen/BindableShapes.expected.txt`、`Tools/CodeGen/check_bindings.py` 与 `Tools/CodeGen/README.md`：回调和嵌套记录投影及判据。
- `WindowSystem/header/CAWindow/WindowSystem.h`：移除 11 个“本轮未实现”的排除标记；`WindowSystem/private/WindowSystem_Impl.*` 只作行为核对，不预设改接口。
- `CSharpHost/private/CSharpHost.cpp`：加载并调用托管 `Shutdown`，同步真实函数表大小断言。
- `Test/CoreTests/`、`Test/GPUBackendTester/Main.cpp`、`Test/GPUBackendTester/FakeWindowSystem.h`、`Scripting/Managed/Host.cs` 与 `Scripting/Managed/UserScript.cs`：确定性绑定测试和端到端探针；夹具提供非零显示器数据与公开事件触发方法。
- `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`：移除已落地形状的“本轮未实现”备注，保持逐方法审计一致。
- `docs/testing.md`、`docs/TODO.md` 与 `TODO/005-web-editor.md`：测试清单、最小验证集合及旧的未实现现状同步更新。
