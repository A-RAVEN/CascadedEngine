# csharp-host 增量规范（csharp-render-structured-buffer）

## MODIFIED Requirements

### Requirement: 渲染表面必须参与启动、重绑和关闭清理
托管启动 SHALL 核对后端、图、目标和参数结构接口的布局清单，用户脚本只有在渲染后端及三个目标类型表均绑定成功后才可执行渲染探针。重绑 MUST 防止旧渲染表面对象被误用，关闭 MUST 按参数结构 → 图 → 离屏目标 → 后端表面的顺序释放脚本持有的渲染表面，再允许原生适配器销毁。

普通窗口脚本不得因为渲染接线而自动执行 GPU 提交。渲染探针 SHALL 使用独立于一次性 `OnEngineReady` 的入口和独立调用标记，只在对应测试（`TestCSharpTriangle`、`TestCSharpConstantColor` 或 `TestCSharpStructuredBuffer`）显式设置各自探针开关且所需渲染表已绑好时执行；进程先前的脚本调用不得消耗或提前触发这次机会，三个探针彼此独立、互不消耗。

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
- **WHEN** 先前的 `TestScriptHost` 已调用过一次性 `OnEngineReady`，随后 `TestCSharpTriangle`、`TestCSharpConstantColor` 或 `TestCSharpStructuredBuffer` 在自己的 Tick 前设置各自渲染探针开关
- **THEN** 独立渲染入口 MUST 仍能各自执行一次，提交、等待和像素证据 MUST 写入对应测试的同一次运行，不得落入先前测试日志
