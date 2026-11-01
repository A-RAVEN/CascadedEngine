# csharp-window-runtime Specification

## Purpose
TBD - created by archiving change csharp-window-creation. Update Purpose after archive.
## Requirements
### Requirement: C# 能创建一个窗口并拿到句柄

托管侧 MUST 能通过生成的绑定调用 `IWindowSystem::NewWindow`，拿到一个非零的窗口句柄；该调用 MUST 真的在引擎侧建出一个窗口，而不是只把参数收下。句柄 MUST 是定宽整数 + 代际号的组合，MUST NOT 把引擎侧的对象地址直接交给托管侧。`NewWindow` 在 C++ 侧有 5 个默认实参，C ABI 面带不了默认值，所以托管侧 MUST 显式传全部参数：除 `void* self` 之外，`width`、`height` 两个整数，名字拆成 `char const*` + `uint64_t` 两个，`visible`、`focused`、`decorate`、`floating` 各一个整数 —— 合计 8 个（以生成物里的签名为准）。

#### Scenario: 建窗口后 C# 与 C++ 两侧的窗口数同步变化

- **WHEN** 托管侧按生成物里的完整签名调用 `NewWindow`，随后引擎侧读一次 `GetWindowCount()`
- **THEN** 托管侧拿到的句柄 MUST 非零，且引擎侧的窗口计数 MUST 比调用前多 1（调用前的值由实跑读数给出，不预设）

#### Scenario: 新窗口的尺寸能从托管侧读回

- **WHEN** 用刚拿到的句柄调 `GetWindowSize`
- **THEN** 读回的两个整数 MUST 是 `800` 与 `600`，且 MUST 与同一时刻引擎侧读到的值逐字相同

### Requirement: C# 关窗后引擎侧必须真的回收

托管侧调用 `CloseWindow()` 之后，引擎侧下一次泵事件时该窗口 MUST 被判为"该关闭"，且引擎侧 MUST 真的把它从持有列表里移除并析构，而不是仅仅置一个标志。回收只在引擎侧的 `UpdateSystem()` 里发生，所以验收 MUST 显式调它一次 —— MUST NOT 假定它会自己跑。

#### Scenario: 泵帧之前两侧都能读到"该关闭"

- **WHEN** 托管侧对刚建的窗口调 `CloseWindow()`，在引擎侧泵帧**之前**，两侧各读一次 `WindowShouldClose()`
- **THEN** 两次读数 MUST 都为真。这一步 MUST 排在泵帧之前 —— 一旦窗口被回收，句柄就失效了，再读只能拿到错误码

#### Scenario: 泵帧之后窗口计数回到建窗之前

- **WHEN** 引擎侧调一次 `UpdateSystem()`，再读 `GetWindowCount()`
- **THEN** 窗口计数 MUST 回到建窗之前那个值

#### Scenario: 回收之后旧句柄必须报失效而不是崩溃

- **WHEN** 窗口已被回收，托管侧再用那个句柄调任一方法
- **THEN** 调用 MUST 返回"句柄已失效"错误码且 MUST NOT 崩溃，进程 MUST 继续跑完当前测试

### Requirement: 托管侧只能看到被投影的方法

托管侧能调的 `IWindow` 与 `IWindowSystem` 方法集合 MUST 与生成物里的函数表逐条对应；被 `CA_BIND_SKIP` 排除的方法 MUST NOT 以任何形式出现在托管侧可调用的面上（包括不出现名字、不出现空实现）。比对时 MUST 剔除伴生函数（错误通道 `LastError*`、释放入口 `Release*`、两段式的 `Copy*` 与 `*Len`、回调注销伴生）—— 它们本来就不在公开表面上，不剔除的话集合永远不等。`IWindowSystem` 的 10 个回调与 `GetMonitor` 现在都必须可见，MUST NOT 再因"本轮未实现"被排除。

#### Scenario: 排除的方法在托管侧不可见

- **WHEN** 复核托管侧的绑定产物，剔除伴生函数后比对方法名集合
- **THEN** 两边集合 MUST 逐条相同；`IWindowSystem` 的 10 个回调与 `GetMonitor` MUST 全部出现，仍被排除的其他接口方法 MUST 一条都不出现

### Requirement: 端到端读数必须两侧可比

建窗、读尺寸、关窗这三步的读数 MUST 在引擎侧与托管侧各打一份，且 MUST 能逐字比对；MUST NOT 只在一侧打读数然后声称"两边一致"。两侧日志写在同一个文件里（托管侧经引擎给的日志回调写进同一条 stdout），所以判据 MUST 是"同一份日志里，带托管侧前缀的那一行与不带前缀的那一行数值相同"，MUST NOT 假定存在两份独立日志。

#### Scenario: 同一份日志里两侧读数成对出现

- **WHEN** 跑托管侧建窗那条测试，抓它的日志文件
- **THEN** 里面 MUST 各出现一行带托管侧前缀的窗口尺寸读数与一行引擎侧的窗口尺寸读数，两行数值 MUST 相同；任一行缺失即算不合格

### Requirement: 托管侧表面必须镜像原始 C++ 接口声明

生成的托管产物 MUST 让使用者看到**与原始 C++ 接口声明同形**的表面：**类型名逐字相同**；方法名相同；参数个数与语义相同；返回类型语义相同。`T&` 出参按 C# 惯例给出参或元组返回值，字符串还原为 `string`，`void*` 还原为 `IntPtr`，`bool` 还原为 `bool`，`castl::weak_ptr<T>` 返回还原为可空对象引用。`castl::function<void(...)>` 参数 MUST 还原为参数类型与顺序一致的 `Action<...>` 并允许 `null`；回调中的 `IWindow*` MUST 还原为 `IWindow` 表面对象。`MonitorInfo` 与 `FloatRect` MUST 以保留原始字段名和顺序的公开记录类型出现。

该表面里 MUST NOT 出现 `delegate* unmanaged`、`void*`、`uint64` 句柄、回调注册号、错误码参数或错误码返回值——这些只允许留在 `internal` 的 native 层。回调注册号、注销伴生和保活表同样不得成为公开 API。

#### Scenario: 表面与声明逐方法对拍

- **WHEN** 从 `WindowSystem/header/CAWindow/WindowSystem.h` 取出 `IWindow` 的每个纯虚方法与 `IWindowSystem` 的每个未排除方法，按投影表转成期望的 C# 签名，再与生成的 `.cs` 逐条比对
- **THEN** 每条方法的名字、参数个数、参数种类与返回种类 MUST 全部相符；把 `.cs` 里任一方法改成同名的 `public static delegate* unmanaged<…>` 字段 MUST 让这条判据报错并点名该条

#### Scenario: 回调与显示器类型不得退化成原生令牌

- **WHEN** 检查 10 个 `Set*Callback` 与 `GetMonitor` 的公开 C# 签名
- **THEN** 回调 MUST 使用类型匹配的 `Action<...>`，`GetMonitor` MUST 返回 `MonitorInfo`；公开签名中 MUST NOT 出现原生函数指针、用户令牌、注册号或匿名字节数组

### Requirement: 同接口的多个实现必须可区分

引擎侧支持同一接口注册多个实现（`CAModuleManager` 的实例表是 `接口类型 → { 实例名 → void* }`）。托管侧登记与查找接口实例时 MUST 带上实例名这一维，MUST NOT 只按"接口编号"定位；`IWindow::GetWindowSystem()` 这类"返回本对象所属的那个实现"的方法 MUST 还原到发出该调用的那一个实例上，两个 window system 同时注册时 MUST NOT 张冠李戴。

#### Scenario: 两个实现并存时不串台

- **WHEN** 把同一个接口的两个实现分别以不同实例名登记，再对第二个实例上的窗口调 `GetWindowSystem()`
- **THEN** 返回的 MUST 是第二个实例；按第一个实例名登记的那一份 MUST 不受影响

### Requirement: 窗口与输入回调必须送达 C# 并保持参数与窗口身份

`IWindowSystem` 的 `SetWindowFocusCallback`、`SetCursorEnterCallback`、`SetCursorPosCallback`、`SetMouseButtonCallback`、`SetScrollCallback`、`SetWindowCloseCallback`、`SetWindowPosCallback`、`SetWindowSizeCallback`、`SetKeyCallback`、`SetCharCallback` MUST 都能通过生成绑定注册 C# 委托。原生实现触发对应事件时，托管委托 MUST 收到真实的 `IWindow` 表面对象和原始标量参数，参数顺序 MUST 与 C++ 声明一致。回调只能在引擎主线程进入托管侧，其他线程 MUST NOT 直接调用用户委托。

#### Scenario: 十类回调都收到真实读数

- **WHEN** 测试逐类注册回调并由测试确定性触发对应原生事件
- **THEN** 每类委托 MUST 被实际调用，收到的窗口身份与标量参数 MUST 和触发输入一致，MUST NOT 只检查生成物里有方法名

#### Scenario: 回调中的窗口对象可调用但离回调即失效

- **WHEN** 委托在回调内调用 `IWindow` 的方法并保存该对象，返回后再次调用
- **THEN** 回调内 MUST 读到真实窗口数据，返回后的调用 MUST 抛出明确失效异常且不得访问悬空原生指针

### Requirement: 回调替换、置空、释放和异常必须有确定行为

重复设置同一个 C# 回调 MUST 用新委托替换旧委托，传入 `null` MUST 清空原生回调，释放 `IWindowSystem` 表面对象 MUST 注销它仍持有的全部回调。上述操作完成后 MUST NOT 再调用旧委托，也 MUST NOT 留下对应保活项。回调执行期间修改同一个注册 MUST 显式失败；用户委托抛出的异常 MUST 被桥捕获并在下一次主线程调用中上报，不得让异常或进程崩溃穿过原生边界。

#### Scenario: 替换、置空和释放后旧委托不再执行

- **WHEN** 分别执行委托替换、传入 `null`、释放窗口系统表面对象，并在每一步后触发原事件
- **THEN** 替换后只有新委托执行，置空与释放后旧委托都不再执行，注册表中的对应保活项 MUST 已清理

#### Scenario: CSharpHost 关闭前先执行托管注销

- **WHEN** 引擎进入 `CSharpHost::Shutdown`
- **THEN** 它 MUST 在调用 `hostfxr_close` 前调用托管 `Host.Shutdown`，由该入口释放窗口系统表面对象并注销仍存活的回调，旧委托在后续事件中不得再执行

#### Scenario: 用户异常后测试可以继续

- **WHEN** 一个窗口回调抛出包含指定文本的异常，随后主线程调用 `UpdateSystem`
- **THEN** MUST 抛出 `IWindowSystemException` 并保留指定文本，进程 MUST 不崩溃，后续正常回调 MUST 仍能执行

### Requirement: GetMonitor 必须返回完整显示器信息

`GetMonitor` MUST 通过生成绑定返回 `MonitorInfo`，其中 `m_MonitorRect`、`m_WorkAreaRect` 的 `x`、`y`、`width`、`height` 以及 `m_DPIScale` MUST 与同一时刻 C++ 调用的字段值逐项一致。布局与字段映射 MUST 来自同源生成，MUST NOT 依赖真实显示器的固定数值或全零结果。绑定层 MUST 在调用原生实现前校验显示器编号，负数或不小于 `GetMonitorCount()` 的编号 MUST 通过统一错误通道上报，MUST NOT 被原生 clamp 静默改成另一个显示器。

#### Scenario: 固定非零嵌套记录逐字段一致

- **WHEN** 用固定且非零的嵌套显示器信息经生成绑定调用 `GetMonitor`
- **THEN** 返回的两个矩形与 DPI 的每个字段 MUST 与夹具输入完全一致；任一字段被清零、错序或遗漏时测试 MUST 失败

#### Scenario: 真实窗口系统两侧同刻对拍

- **WHEN** 在 `TestCSharpWindow` 中由 C++ 与 C# 分别调用 `GetMonitor` 并记录全部字段
- **THEN** 两侧同刻读数 MUST 逐字段一致，且日志 MUST 同时包含两侧结果，不能只断言 C# 方法存在

#### Scenario: 无效显示器编号必须报错而不是返回另一个显示器

- **WHEN** C# 用负数或不小于 `GetMonitorCount()` 的编号调用 `GetMonitor`
- **THEN** 调用 MUST 抛出 `IWindowSystemException` 并给出无效编号错误，MUST NOT 返回 clamp 后的合法显示器或把全零结果当成功

