# csharp-host 增量规范

## MODIFIED Requirements

### Requirement: 宿主必须登记渲染适配接口并同步手写 ABI

`CSharpHost` SHALL 在模块链接阶段使用模块管理器中的真实 `CRenderBackend` 与 `CThreadManager` 创建脚本渲染适配器，并登记后端接口默认实例。手写 `EngineApi` MUST 只在末尾追加脚本后端、图、目标和参数结构四个接口编号（3、4、5、6），版本与大小 MUST 由两侧同步更新；生成接口表的 ABI 版本 MUST 保持不变。

图、目标与参数结构接口属于目标类型表，宿主 MUST 允许在没有默认实例的情况下返回其函数表；实际对象 MUST 通过对应 `ResolveTargetHandle` 解析。后端接口 MUST 仍要求真实适配器实例，缺少后端或线程管理器时不得返回空壳或伪造成功。`sizeof(EngineApi)` 判据以两侧实测为准（现有 72 字节布局末尾追加一个 `uint32_t` 后仍为 72）。

#### Scenario: 四个渲染接口按编号取得真实函数表

- **WHEN** 托管侧按引擎下发的后端、图、目标和参数结构编号请求函数表
- **THEN** 后端表 MUST 对应已登记的真实适配器，图、目标与参数结构表 MUST 能用于目标句柄解析；手写 ABI 的版本和 `sizeof` 在两侧 MUST 一致，既有窗口编号与生成表版本 MUST 不变

#### Scenario: 缺少渲染依赖必须明确失败

- **WHEN** 后端或线程管理器不可用，而托管渲染探针请求后端接口
- **THEN** 宿主 MUST 报告适配器未登记及缺失依赖，不得返回空函数表后仍声称渲染可用；未启用渲染探针的既有窗口路径 MUST 保持原行为

### Requirement: 渲染表面必须参与启动、重绑和关闭清理

托管启动 SHALL 核对后端、图、目标和参数结构接口的布局清单，用户脚本只有在渲染后端及三个目标类型表均绑定成功后才可执行渲染探针。重绑 MUST 防止旧渲染表面对象被误用，关闭 MUST 按参数结构 → 图 → 离屏目标 → 后端表面的顺序释放脚本持有的渲染表面，再允许原生适配器销毁。

普通窗口脚本不得因为渲染接线而自动执行 GPU 提交。渲染探针 SHALL 使用独立于一次性 `OnEngineReady` 的入口和独立调用标记，只在对应测试（`TestCSharpTriangle` 或 `TestCSharpConstantColor`）显式设置各自探针开关且所需渲染表已绑好时执行；进程先前的脚本调用不得消耗或提前触发这次机会，两个探针彼此独立、互不消耗。

#### Scenario: 渲染表面未绑定完整不得执行探针

- **WHEN** 后端、图、目标或参数结构任一所需函数表缺失、布局不符或目标解析失败
- **THEN** `Host.Boot` 或绑定过程 MUST 给出具体错误且不执行对应渲染探针，不得退化成只记录成功的日志

#### Scenario: 关闭顺序覆盖所有渲染资源

- **WHEN** 脚本已保留离屏目标与参数结构并结束测试进程
- **THEN** 托管 `Shutdown` MUST 按参数结构 → 图 → 离屏目标 → 后端表面的顺序逐项释放，随后原生适配器才能销毁；关闭后旧表面再次调用 MUST 得到明确失效异常

#### Scenario: 未启用渲染探针时不增加 GPU 工作

- **WHEN** 现有窗口或回调测试未设置任何渲染探针开关
- **THEN** 用户脚本 MUST NOT 创建离屏目标、参数结构、建图或提交，既有窗口计数、回调和测试日志语义 MUST 保持不变

#### Scenario: 全量运行也能在自己的测试中执行探针

- **WHEN** 先前的 `TestScriptHost` 已调用过一次性 `OnEngineReady`，随后 `TestCSharpTriangle` 或 `TestCSharpConstantColor` 在自己的 Tick 前设置各自渲染探针开关
- **THEN** 独立渲染入口 MUST 仍能各自执行一次，提交、等待和像素证据 MUST 写入对应测试的同一次运行，不得落入先前测试日志

### Requirement: 既有宿主失败检查必须使用当前 ABI 与受控失败语义

`Tools/check_csharp_host_failures.py` SHALL 随手写 ABI 更新版本不符断言，不得继续期待过时的版本号。故意让托管 `Tick` 抛异常时，引擎 MUST 收到错误码 4 与异常文本、当前测试操作 MUST 失败且进程 MUST NOT 崩溃；`GPUBackendTester` 按既有测试结果退出码约定返回 1，不得把受控失败要求成无条件退出 0。

#### Scenario: 常量色变更把 ABI 升到 7 后失败检查仍点名当前版本

- **WHEN** 手写 ABI 升到版本 7 后用运行时开关构造 ABI 不符
- **THEN** ABI 场景 MUST 点名当前引擎版本 7 并以 3 拒绝启动；异常场景 MUST 以 1 报告当前操作失败、保留错误码 4 与异常文本且不产生新崩溃转储，随后正常全量回归 MUST 全部通过
