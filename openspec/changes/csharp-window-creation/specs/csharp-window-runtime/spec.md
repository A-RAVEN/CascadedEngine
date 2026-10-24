## ADDED Requirements

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

托管侧能调的 `IWindow` 与 `IWindowSystem` 方法集合 MUST 与生成物里的函数表逐条对应；被 `CA_BIND_SKIP` 排除的方法 MUST NOT 以任何形式出现在托管侧可调用的面上（包括不出现名字、不出现空实现）。比对时 MUST 剔除伴生函数（错误通道 `LastError*`、释放入口 `Release*`、两段式的 `Copy*` 与 `*Len`）—— 它们本来就不在函数表里，不剔除的话集合永远不等。

#### Scenario: 排除的方法在托管侧不可见

- **WHEN** 复核托管侧的绑定产物，剔除伴生函数后比对方法名集合
- **THEN** 两边集合 MUST 逐条相同；被排除的 11 条 `IWindowSystem` 方法与 1 条 `IWindow` 方法 MUST 一条都不出现

### Requirement: 端到端读数必须两侧可比

建窗、读尺寸、关窗这三步的读数 MUST 在引擎侧与托管侧各打一份，且 MUST 能逐字比对；MUST NOT 只在一侧打读数然后声称"两边一致"。两侧日志写在同一个文件里（托管侧经引擎给的日志回调写进同一条 stdout），所以判据 MUST 是"同一份日志里，带托管侧前缀的那一行与不带前缀的那一行数值相同"，MUST NOT 假定存在两份独立日志。

#### Scenario: 同一份日志里两侧读数成对出现

- **WHEN** 跑托管侧建窗那条测试，抓它的日志文件
- **THEN** 里面 MUST 各出现一行带托管侧前缀的窗口尺寸读数与一行引擎侧的窗口尺寸读数，两行数值 MUST 相同；任一行缺失即算不合格
