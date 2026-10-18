## ADDED Requirements

### Requirement: 引擎必须能在本进程内拉起托管运行时并双向调用
引擎 MUST 能在一个普通模块的初始化阶段定位 `hostfxr`、按 `runtimeconfig.json` 拉起 .NET 运行时、加载一个托管程序集、取到 `[UnmanagedCallersOnly]` 入口并调用它；托管侧 MUST 能通过交给它的函数表回调引擎。最小双向通道 MUST 包含：引擎调托管的一次 `Boot`（带函数表指针）与一次 `Tick`（带一个数值），以及托管回调引擎的一次 `log` 与一次回值。

#### Scenario: 端到端一次调用有可核读数
- **WHEN** 引擎以 Debug 配置启动并执行一次 `Boot` + 一次 `Tick`
- **THEN** 引擎日志里 MUST 出现托管侧写入的一行日志，且 `Tick` 的回值 MUST 与托管侧打印的值一致（两处读数逐字可比）

### Requirement: 缺运行时或程序集必须报错并列出找过的路径
host 在找不到 `hostfxr`、找不到托管程序集、找不到 `runtimeconfig.json`、或配置里的 `TargetFramework` 在本机找不到匹配运行时的时候，MUST 让模块初始化失败（而不是继续跑），并 MUST 在错误信息里给出可核信息：缺运行时 / 缺文件时列出"找过哪些路径"（`nethost` 返回的路径 + `CASCADED_CSHARP_RUNTIMECONFIG` 指向的路径），版本不匹配时列出"本机实际有哪些运行时版本"。MUST NOT 在缺任何一样的情况下静默降级为"没有 C# 支持"。

#### Scenario: 删掉托管程序集后启动必须失败
- **WHEN** 把 `Scripting.Managed.dll` 从输出目录移走后再启动
- **THEN** 模块初始化失败，错误信息里 MUST 点名缺的文件名，进程退出码 MUST 非零

#### Scenario: 配置路径不存在时报出路径清单
- **WHEN** 把 `CASCADED_CSHARP_RUNTIMECONFIG` 设成一个不存在的路径后启动
- **THEN** 错误信息 MUST 同时出现该路径与 `nethost` 返回的 `hostfxr` 路径（两条都给，不是只写"加载失败"）

#### Scenario: 版本不匹配时列出本机可用版本
- **WHEN** 把配置里的 `TargetFramework` 改成 `net7.0`（本机没有 7.x 运行时）后启动
- **THEN** 错误信息 MUST 出现 `net7.0` 与本机实际存在的运行时版本清单（至少 8.0.6 / 9.0.12 两个）

### Requirement: 函数表必须带版本与大小自检
交给托管侧的函数表 MUST 以 `struct` 形式携带 `abi_version` 与 `size` 两个字段；托管侧 MUST 在 `Boot` 里比对这两个值，不符时 MUST 返回错误码，引擎侧 MUST 因此拒绝继续初始化。MUST NOT 在版本或大小不符时带着错位布局继续调用。

#### Scenario: 版本不符必须拒绝启动
- **WHEN** 临时把引擎侧的 `abi_version` 改成托管侧不认识的值
- **THEN** `Boot` 返回非零错误码，引擎侧初始化失败，错误信息里 MUST 出现两边的版本号

### Requirement: 托管异常不许穿过边界
托管侧每个被引擎调用的入口 MUST 捕获所有异常，并 MUST 转成"错误码 + 一条文本"返回；引擎侧 MUST 记录该文本并让当前操作失败。MUST NOT 让异常穿过 C ABI 边界。

#### Scenario: 托管方法抛异常时引擎收到错误码与文本
- **WHEN** 临时让托管侧的 `Tick` 抛一个异常
- **THEN** 引擎侧 MUST 收到非零错误码与一条非空文本，且进程 MUST NOT 崩溃

### Requirement: host 必须遵守既有的模块生命周期
host 模块 MUST 作为普通引擎模块加载与销毁（导出 `TryInit` / `TryLink` / `TryRelease`、参与模块逆序销毁），MUST NOT 另起一套加载/销毁次序。`TryRelease` 时 MUST 关闭运行时句柄并释放程序集引用。

#### Scenario: 关闭顺序与模块逆序一致
- **WHEN** 引擎正常关闭
- **THEN** host 模块的 `TryRelease` MUST 在它依赖的模块之后被调用，且日志里 MUST 出现"运行时已关闭"一行

### Requirement: 托管侧必须进构建，缺 SDK 必须失败
托管侧的 `.csproj` MUST 由 `build.py` 编译，产物（程序集 + `runtimeconfig.json`）MUST 落到 `build.py` 的输出目录并与 host 模块同目录。缺 .NET SDK 时构建 MUST 失败并说明缺什么；只有显式开关才允许跳过，且跳过时 MUST 打印"已跳过托管侧，C# 不可用"。

#### Scenario: 缺 SDK 时构建失败而不是静默跳过
- **WHEN** 在找不到 .NET SDK 的环境里跑 `python build.py --config Debug`
- **THEN** 构建失败，输出里 MUST 说明缺少 .NET SDK；MUST NOT 出现 `BUILD SUCCESSFUL` 而托管侧产物缺失

#### Scenario: 显式跳过时留下明确提示
- **WHEN** 用显式跳过开关构建
- **THEN** 构建 MUST 打印"已跳过托管侧，C# 不可用"，且 MUST 说明产物目录里没有托管程序集

### Requirement: 宿主必须有两层可失败的测试
C# 宿主的验证 MUST 落在 `docs/testing.md` 定义的第 ② 层（进程内断言）与第 ③ 层（进程级失败路径），并且每一条 MUST 能构造出"缺陷存在时变红"的状态。层 ② MUST 断言：取到 `IScriptHost`、`IsAvailable()` 为真、`LastError()` 为空、连续两次 `Tick` 的返回值单调递增（1、2）。层 ③ MUST 覆盖五条失败：缺程序集、配置路径不存在、配置要的运行时版本本机没有、函数表 ABI 不符、托管方法抛异常；其中"ABI 不符"与"抛异常"两条的构造 MUST 用运行时开关，MUST NOT 依赖临时改源码。层 ③ 的脚本跑完 MUST 把工作区与产物还原。

#### Scenario: 层 ② 的测试能单独变红
- **WHEN** 把托管侧 `Tick` 的返回值临时改成恒 0
- **THEN** 只有"宿主可用性"这一条测试变红，其余测试仍绿；还原后全部通过

#### Scenario: 层 ③ 的五条失败都断言退出码与错误子串
- **WHEN** 跑 `Tools/check_csharp_host_failures.py`
- **THEN** 五条场景各自的退出码与错误子串 MUST 被断言（前四条退出码 3、异常那条退出码 0 且输出含错误码 4），任何一条不符 MUST 让脚本非零退出；脚本退出后 MUST 不留新的 `crash_*.dmp`、也不改动仓库文件
