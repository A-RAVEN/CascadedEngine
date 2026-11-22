# csharp-script 增量

## RENAMED Requirements

- FROM: `### Requirement: 脚本入口必须有固定签名，且只在一个时机被调用`
- TO: `### Requirement: 脚本入口与生命周期钩子必须有固定签名和调用时机`

## MODIFIED Requirements

### Requirement: 脚本入口与生命周期钩子必须有固定签名和调用时机

`Scripting/Managed/UserScript.cs` SHALL 提供一个静态类，内含唯一入口
`public static void OnEngineReady(IWindowSystem windows)`。`Host.cs` SHALL 在**编号 1、2 两张函数表都绑定成功之后**（可核条件：`s_system != null`；每次绑定开始时必须先把它置空，绑定成功再赋值）调用它，且**整个进程生命周期内只调一次**。

> 「只调一次」不是外部要求，是本轮的设计选择（理由见 `design.md` 的 D2：每次绑定后都调会让脚本越建越多窗口）。

**生命周期钩子（csharp-scene-foundation 新增，additive）**：`UserScript` SHALL 另提供 `public static void OnStart(Scene scene)` 与 `public static void OnUpdate(Scene scene, double dt)` 两个钩子，**默认空实现**（既有脚本与测试不受影响）。场景运行时（`SceneRuntime`）SHALL 在注册活动场景时调一次 `OnStart`、在有活动场景的每帧调 `OnUpdate(scene, dt)`；无活动场景时 SHALL 不调任何生命周期钩子（零行为）。`Host.Tick` SHALL 把引擎传入的 `deltaSeconds` 向下传给场景运行时（此前该值仅用于日志、被丢弃），`OnUpdate` 收到的 `dt` MUST 等于该帧 `deltaSeconds`。`OnEngineReady` 的一次性语义与下方全部既有场景判据 MUST 保持不变。

#### Scenario: 两张表都绑定成功之后被调用一次

- **WHEN** 引擎已登记 `IWindow` 与 `IWindowSystem` 两个实例，`Host.cs` 第一次把两张表都绑定成功
- **THEN** 同一次 `Tick()` 里 `UserScript.OnEngineReady(...)` 被调用一次，参数是刚建好的 `IWindowSystem` 表面

#### Scenario: 之后的重绑不再调用脚本

- **WHEN** 引擎侧默认实例指针变化导致 `Host.cs` 重新绑定第二次
- **THEN** `UserScript.OnEngineReady(...)` **不会**再被调用（脚本不会重复建窗，`GetWindowCount` 因此不会因为重绑再涨）

#### Scenario: 还没绑好时不调用

- **WHEN** 引擎尚未登记任何一个实例（两张表都拿不到）
- **THEN** `UserScript.OnEngineReady(...)` 不被调用，`Tick()` 仍返回本次序号（第一次为 1，`--test TestScriptHost` 断言的正是这个值）。**注意**：这一路的 `LastError()` **不是空的** —— 实跑 `--test TestScriptHost` 打的是「接口编号 1 下没有登记实例名」，那是引擎侧登记缺失的正常报错，不是脚本造成的，所以本条不把"`LastError()` 为空"当判据

#### Scenario: 只绑到一半时不调用

- **WHEN** 编号 1 的 `IWindow` 已登记、编号 2 的 `IWindowSystem` 还没登记
- **THEN** `UserScript.OnEngineReady(...)` 不被调用（`s_system` 仍是空），不会把空引用交给脚本

#### Scenario: 重绑失败时不许拿旧对象冒充"两张表都绑好"

- **WHEN** 引擎换了默认实例，`Host.cs` 重新绑定但这次只取到编号 1 的表（编号 2 取不到）
- **THEN** `s_system` 必须已被置空，`UserScript.OnEngineReady(...)` **不**被调用（判据：`UserScript.CalledCount` 不涨），不许沿用上一次绑定留下的旧对象

#### Scenario: 注册场景时 OnStart 调一次

- **WHEN** 经 `SceneRuntime.Register` 注册一个活动场景
- **THEN** `UserScript.OnStart(scene)` MUST 被调用一次，参数是刚注册的场景；重复注册同一场景不重复调 OnStart

#### Scenario: 有活动场景时每帧 OnUpdate 收到该帧 dt

- **WHEN** 注册活动场景后引擎连续 Tick 若干帧（每帧 deltaSeconds 已知）
- **THEN** 每帧 MUST 调一次 `UserScript.OnUpdate(scene, dt)`，且收到的 dt MUST 等于该帧引擎传入的 deltaSeconds（读数行可核 dt>0 与调用计数）

#### Scenario: 无活动场景时生命周期钩子零调用

- **WHEN** 未注册任何场景跑现有用例
- **THEN** `OnStart`/`OnUpdate` MUST 都不被调用，`Host.Tick` 行为与改前一致（既有 TestScriptHost/TestCSharpWindow 判据不变）

#### Scenario: 默认空钩子不破坏既有脚本

- **WHEN** 既有脚本只实现 OnEngineReady、未覆写 OnStart/OnUpdate
- **THEN** 默认空实现 MUST 使注册场景后调用 OnStart/OnUpdate 不抛异常、不改变既有脚本行为