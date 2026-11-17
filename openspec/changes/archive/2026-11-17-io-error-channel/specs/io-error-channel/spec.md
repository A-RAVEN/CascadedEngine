# io-error-channel 增量

## ADDED Requirements

### Requirement: IO 批读写的失败必须显式报告
`ca_io::IOBatch::Read` / `ca_io::WBatch::Write` MUST 返回 `bool` 报告本次读写是否被来源接受；同名模板便利重载 MUST 返回 `bool` 转发结果，MUST NOT 丢弃失败。`IOBatch`/`WBatch` MUST 提供来源路径访问器（`SourcePath()`），使上层失败消息能点名文件。读越界（批当前位置 + 请求长度超出文件尺寸）MUST 返回 `false` 且 MUST NOT 推进批内读位置。文件尺寸探测失败（路径不存在、路径是目录等）MUST 在批创建时以 `ca_io::io_error` 抛出并点名路径，MUST NOT 把未类型化的 `std::filesystem` 异常逃逸给调用方。`SubmitAndWait` 执行期发现的失败（源文件打不开、目标打不开、读写中途错误——含读任务 `gcount()` 短读与写任务流状态失败）MUST 在调用线程以 `ca_io::io_error` 抛出并点名路径；工作线程内 MUST NOT 直接向任务系统抛异常（捕获为值、调用线程抛）。

#### Scenario: 越界读被拒绝并报告
- **WHEN** 批当前位置 + 请求长度超出文件尺寸时调用 `Read`
- **THEN** `Read` MUST 返回 `false`、批内位置不变，上层读对象的反序列化 MUST 失败——MUST NOT 静默丢弃请求并让目标内存保留未定义数据

#### Scenario: 不存在的路径在批创建时点名失败
- **WHEN** 对不存在的文件路径调用 `IOManager::Batch`
- **THEN** MUST 抛出 `ca_io::io_error` 且消息含该路径；MUST NOT 抛出未类型化的 `std::filesystem::filesystem_error`

#### Scenario: 提交期文件打不开在调用线程失败
- **WHEN** 批创建后源文件被删除或打不开，`SubmitAndWait` 执行时 `is_open()` 为 false
- **THEN** MUST 在调用线程抛出 `ca_io::io_error` 点名路径；MUST NOT 静默留下未填充的目标内存并报告成功

#### Scenario: 写目标打不开被拒绝
- **WHEN** `WriteBatch` 的目标路径无法打开（如父目录不存在）
- **THEN** `SubmitAndWait` MUST 抛出 `ca_io::io_error` 点名路径；MUST NOT 静默丢弃已排队的写

### Requirement: 流式反序列化与缓冲区反序列化必须有同一失败语义
`cacore::codegen::reader` 对流式来源（只提供 `Read(size, dest)` 的来源，如 `IOBatch`）MUST 把 `Read` 的 `false` 返回值传播为读对象自己的失败（`read_raw` 返回 `false` 并置失败位），与缓冲区来源的越界失败语义一致。生成代码与生成产物 MUST NOT 因此改动，序列化字节契约 MUST 逐字节不变（`ResourceBatchTest` 现有新旧字节对拍断言保持绿）。

#### Scenario: 流式来源拒绝读时反序列化失败
- **WHEN** 流式来源的 `Read` 返回 `false`（越界或来源错误）
- **THEN** `reader::read_raw` MUST 返回 `false` 并置失败位，整条 `deserialize` MUST 返回 `false`；MUST NOT 无条件返回 `true` 并继续推进读位置

#### Scenario: 生成物字节契约不受影响
- **WHEN** 全量重建后比对 CodeGen 产物逐字节基线
- **THEN** 全部生成物 MUST 逐字节不变（本批只改 `CACore/header/CodeGenSerialization.h` 运行时，不碰生成器与生成物）

### Requirement: 资源加载失败必须透出到调用方
`TResource::Deserialize` 在生成代码反序列化返回 `false` 时 MUST 以 `ca_io::io_error` 抛出并点名资源来源（经 `SourcePath()`）；`ResourceManagingSystem` 的加载与 `SerializeAll` 路径 MUST NOT 捕获并吞掉该类异常。截短或损坏的资产文件 MUST NOT 把未定义数据静默灌进资源对象。注：`IResourceReadOnly` 当前全仓无实现（2026-11-11 复核 verified），本要求不点名它。

#### Scenario: 缺失的资产文件加载被点名拒绝
- **WHEN** 经 tester 触发 `GetOrLoadResource` 加载一个资源字典中不存在且磁盘上也不存在的资产路径
- **THEN** MUST 抛出 `ca_io::io_error` 且消息含该路径；MUST NOT 返回字段值为默认/垃圾的资源对象
- **AND** 截短/损坏载荷的失败透出由 Requirement 1 与 Requirement 2 的链（来源报告 → 读对象传播 → 资源层抛出）保证，stub 层红绿用例覆盖

#### Scenario: 完好资产加载不受影响
- **WHEN** 资产文件完好时跑两后端全部 headless 测试
- **THEN** 17/17 全绿（现有 16 个用例保持改前结果 + 新增 `TestIOErrorChannel` 亦绿；2026-11-16 `--list` 实测现有 16 个）
