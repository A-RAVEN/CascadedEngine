# csharp-host 增量

## RENAMED Requirements

- FROM: `### Requirement: 宿主必须登记渲染适配接口并同步手写 ABI`
- TO: `### Requirement: 宿主登记与跨边界函数表由生成登记表提供`

## MODIFIED Requirements

### Requirement: 宿主登记与跨边界函数表由生成登记表提供
`CSharpHost` SHALL 在模块链接阶段使用模块管理器中的真实 `CRenderBackend` 与 `CThreadManager` 创建脚本渲染适配器，并登记后端接口默认实例。接口编号与取表路由 MUST 由带编号的 `CA_BIND` 接口标注声明、由绑定生成器发射为宿主登记表与 `EngineApi` 两侧对照；宿主 MUST NOT 保留手写的逐接口 `interfaceId` 分支分派，两侧 MUST NOT 保留手写的 `EngineApi` 镜像 struct。`EngineApi` 的版本与大小 MUST 以生成常量单点定义、两侧对生成值断言；ABI 升到版本 9 后 `sizeof(EngineApi)` MUST 仍为 80 字节（14 个字段的顺序与宽度与 v8 一致，字节布局不变），生成接口表的 ABI 版本 MUST 保持不变。`CSharpHost/README.md` 全文、`Tools/check_csharp_host_readme.py` 与 `Tools/check_csharp_host_failures.py` 三处 MUST 同步为同一 v9=80 读数，任一残留旧值即非零。

图、目标、参数结构与纹理接口属于目标类型表，宿主 MUST 允许在没有默认实例的情况下返回其函数表；实际对象 MUST 通过对应 `ResolveTargetHandle` 解析。后端接口 MUST 仍要求真实适配器实例，缺少后端或线程管理器时不得返回空壳或伪造成功。托管侧绑定失败文本 MUST 按登记表逐项点名是哪个接口（例如点名 `IScriptTexture`），不得退化为笼统错误。

#### Scenario: 五个渲染接口按编号取得真实函数表
- **WHEN** 托管侧按生成登记表下发的后端、图、目标、参数结构和纹理编号请求函数表
- **THEN** 后端表 MUST 对应已登记的真实适配器，图、目标、参数结构与纹理表 MUST 能用于目标句柄解析；`EngineApi` 版本、大小与七个接口编号在两侧 MUST 来自同一份生成常量，既有窗口编号（1、2）与生成接口表版本 MUST 不变

#### Scenario: 缺少渲染依赖必须明确失败
- **WHEN** 后端或线程管理器不可用，而托管渲染探针请求后端接口
- **THEN** 宿主 MUST 报告适配器未登记及缺失依赖，不得返回空函数表后仍声称渲染可用；未启用渲染探针的既有窗口路径 MUST 保持原行为

#### Scenario: 单张表缺失必须点名接口
- **WHEN** 登记表里某个渲染接口的工厂或取表路由被移除（演练或变异），托管侧执行渲染绑定
- **THEN** 绑定失败文本 MUST 点名该接口的名字（与移除项一致），对应渲染探针 MUST 报明确失败而不是静默跳过；窗口路径的既有行为与计数 MUST 不变

### Requirement: 既有宿主失败检查必须使用当前 ABI 与受控失败语义
`Tools/check_csharp_host_failures.py` SHALL 随 ABI（自本 change 起为生成常量）更新版本不符断言，不得继续期待过时的版本号。故意让托管 `Tick` 抛异常时，引擎 MUST 收到错误码 4 与异常文本、当前测试操作 MUST 失败且进程 MUST NOT 崩溃；`GPUBackendTester` 按既有测试结果退出码约定返回 1，不得把受控失败要求成无条件退出 0。

#### Scenario: 接口合并变更把 ABI 升到 9 后失败检查仍点名当前版本
- **WHEN** 生成的 ABI 版本升到 9 后用运行时开关构造 ABI 不符
- **THEN** ABI 场景 MUST 点名引擎与托管两侧的版本读数（沿用"开关打印版本 + 1"的既有构造，期望以实跑输出为准）并以 3 拒绝启动；异常场景 MUST 以 1 报告当前操作失败、保留错误码 4 与异常文本且不产生新崩溃转储，随后正常全量回归 MUST 全部通过
