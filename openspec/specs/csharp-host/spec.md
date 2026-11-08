# csharp-host Specification

## Purpose

引擎把 .NET 运行时拉进本进程、加载托管程序集、与它双向调用的最小通道，以及它在缺运行时 / 缺程序集 / 版本或 ABI 不符 / 托管抛异常时的失败语义。
## Requirements
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

### Requirement: 宿主登记与跨边界函数表由生成登记表提供
`CSharpHost` SHALL 在模块链接阶段使用模块管理器中的真实 `CRenderBackend` 与 `CThreadManager` 创建脚本渲染适配器，并登记后端接口默认实例。接口编号 MUST 由入库锁文件 `Tools/CodeGen/interface-ids.lock` 声明（段内唯一、段内从段起点连续；默认构建只核对、不写锁；发号只经显式 `--update-ids`），取表路由与编号常量由绑定生成器从锁文件与**无参数** `CA_BIND` 标注发射为宿主登记表与 `EngineApi` 两侧对照——在标注上写编号 MUST 被生成期拒绝；宿主 MUST NOT 保留手写的逐接口 `interfaceId` 分支分派，两侧 MUST NOT 保留手写的 `EngineApi` 镜像 struct。`EngineApi` 的版本与大小 MUST 以生成常量单点定义、两侧对生成值断言；ABI 升到版本 9 后 `sizeof(EngineApi)` MUST 仍为 80 字节（14 个字段的顺序与宽度与 v8 一致，字节布局不变），生成接口表的 ABI 版本 MUST 保持不变。`CSharpHost/README.md` 全文、`Tools/check_csharp_host_readme.py` 与 `Tools/check_csharp_host_failures.py` 三处 MUST 同步为同一 v9=80 读数，任一残留旧值即非零。

图、目标、参数结构与纹理接口属于目标类型表，宿主 MUST 允许在没有默认实例的情况下返回其函数表；实际对象 MUST 通过对应 `ResolveTargetHandle` 解析。后端接口 MUST 仍要求真实适配器实例，缺少后端或线程管理器时不得返回空壳或伪造成功。托管侧绑定失败文本 MUST 按登记表逐项点名是哪个接口（例如点名 `IScriptTexture`），不得退化为笼统错误。

#### Scenario: 五个渲染接口按编号取得真实函数表
- **WHEN** 托管侧按生成登记表下发的后端、图、目标、参数结构和纹理编号请求函数表
- **THEN** 后端表 MUST 对应已登记的真实适配器，图、目标、参数结构与纹理表 MUST 能用于目标句柄解析；`EngineApi` 版本、大小与七个接口编号在两侧 MUST 来自同一份"锁文件派生的生成常量"，既有窗口编号（1、2）与生成接口表版本 MUST 不变

#### Scenario: 编号来源换锁后产物逐字节不变
- **WHEN** 编号声明从 `CA_BIND(编号)` 标注迁移为锁文件承载（本批迁移），迁移前后各构建一次
- **THEN** 迁移完成的构建 MUST 全绿，且登记表、接口表与 `EngineApi` 全部既有产物 MUST 与迁移前逐字节一致（编号值与绑定一个都没换的铁证）；在标注上重写编号（回退尝试）MUST 被生成期拒绝并点名

#### Scenario: 缺少渲染依赖必须明确失败
- **WHEN** 后端或线程管理器不可用，而托管渲染探针请求后端接口
- **THEN** 宿主 MUST 报告适配器未登记及缺失依赖，不得返回空函数表后仍声称渲染可用；未启用渲染探针的既有窗口路径 MUST 保持原行为

#### Scenario: 单张表缺失必须点名接口
- **WHEN** 登记表里某个渲染接口的工厂或取表路由被移除（演练或变异，例如锁文件删掉该接口行），托管侧执行渲染绑定
- **THEN** 绑定失败文本 MUST 点名该接口的名字（与移除项一致），对应渲染探针 MUST 报明确失败而不是静默跳过；窗口路径的既有行为与计数 MUST 不变

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

### Requirement: 渲染表面必须参与启动、重绑和关闭清理
托管启动 SHALL 核对后端、图、目标、参数结构和纹理接口的布局清单，用户脚本只有在渲染后端及四个目标类型表均绑定成功后才可执行渲染探针。重绑 MUST 防止旧渲染表面对象被误用，关闭 MUST 按参数结构 → 纹理 → 图 → 离屏目标 → 后端表面的顺序释放脚本持有的渲染表面，再允许原生适配器销毁（纹理事务对象排在参数结构之后释放，因参数结构持有纹理引用）。

普通窗口脚本不得因为渲染接线而自动执行 GPU 提交。渲染探针 SHALL 使用独立于一次性 `OnEngineReady` 的入口和独立调用标记，只在对应测试（`TestCSharpTriangle`、`TestCSharpConstantColor`、`TestCSharpStructuredBuffer` 或 `TestCSharpTextureSampling`）显式设置各自探针开关且所需渲染表已绑好时执行；进程先前的脚本调用不得消耗或提前触发这次机会，四个探针彼此独立、互不消耗。

#### Scenario: 渲染表面未绑定完整不得执行探针
- **WHEN** 后端、图、目标、参数结构或纹理任一所需函数表缺失、布局不符或目标解析失败
- **THEN** `Host.Boot` 或绑定过程 MUST 给出具体错误且不执行对应渲染探针，不得退化成只记录成功的日志

#### Scenario: 关闭顺序覆盖所有渲染资源
- **WHEN** 脚本已保留离屏目标、参数结构与纹理对象并结束测试进程
- **THEN** 托管 `Shutdown` MUST 按参数结构 → 纹理 → 图 → 离屏目标 → 后端表面的顺序逐项释放，随后原生适配器才能销毁；关闭后旧表面或旧纹理再次调用 MUST 得到明确失效异常

#### Scenario: 未启用渲染探针时不增加 GPU 工作
- **WHEN** 现有窗口或回调测试未设置任何渲染探针开关
- **THEN** 用户脚本 MUST NOT 创建离屏目标、参数结构、纹理、建图或提交，既有窗口计数、回调和测试日志语义 MUST 保持不变

#### Scenario: 全量运行也能在自己的测试中执行探针
- **WHEN** 先前的 `TestScriptHost` 已调用过一次性 `OnEngineReady`，随后 `TestCSharpTriangle`、`TestCSharpConstantColor`、`TestCSharpStructuredBuffer` 或 `TestCSharpTextureSampling` 在自己的 Tick 前设置各自渲染探针开关
- **THEN** 独立渲染入口 MUST 仍能各自执行一次，提交、等待和像素证据 MUST 写入对应测试的同一次运行，不得落入先前测试日志

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
- **THEN** 五条场景各自的退出码与错误子串 MUST 被断言（前四条退出码 3、异常那条退出码 1（受控失败）且输出含错误码 4），任何一条不符 MUST 让脚本非零退出；脚本退出后 MUST 不留新的 `crash_*.dmp`、也不改动仓库文件

### Requirement: 既有宿主失败检查必须使用当前 ABI 与受控失败语义
`Tools/check_csharp_host_failures.py` SHALL 随 ABI（自本 change 起为生成常量）更新版本不符断言，不得继续期待过时的版本号。故意让托管 `Tick` 抛异常时，引擎 MUST 收到错误码 4 与异常文本、当前测试操作 MUST 失败且进程 MUST NOT 崩溃；`GPUBackendTester` 按既有测试结果退出码约定返回 1，不得把受控失败要求成无条件退出 0。

#### Scenario: 接口合并变更把 ABI 升到 9 后失败检查仍点名当前版本
- **WHEN** 生成的 ABI 版本升到 9 后用运行时开关构造 ABI 不符
- **THEN** ABI 场景 MUST 点名引擎与托管两侧的版本读数（沿用"开关打印版本 + 1"的既有构造，期望以实跑输出为准）并以 3 拒绝启动；异常场景 MUST 以 1 报告当前操作失败、保留错误码 4 与异常文本且不产生新崩溃转储，随后正常全量回归 MUST 全部通过

