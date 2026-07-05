# Test Output

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: JSON 测试结果报告

系统 SHALL 支持 `--report <path>` 参数，在每个测试结束后输出结构化 JSON 结果文件。JSON 文件 SHALL 包含测试名称、状态（`pass`/`fail`/`crash`/`timeout`）、执行时间（毫秒）、退出码。

`--report` 参数值 SHALL 为输出文件路径。Headless 模式（`--headless > 0`）下若未指定 `--report`，SHALL 默认输出到 `test_output/result.json`。非 headless 模式下除非显式指定 `--report`，SHALL 不生成 JSON 报告文件。

#### Scenario: 全部测试通过

- **WHEN** `GPUBackendTester.exe --headless 10 --report result.json` 运行全部 7 个测试且均无异常
- **THEN** `result.json` 包含 7 个条目，每个状态为 `pass`

#### Scenario: Headless 模式默认生成 JSON

- **WHEN** `GPUBackendTester.exe --headless 10`（未指定 `--report`）
- **THEN** 生成 `test_output/result.json` 包含测试结果

#### Scenario: 部分测试异常

- **WHEN** TestSimpleTriangle 正常完成，TestTriangleWithConstantColor 抛出 std::exception
- **THEN** JSON 报告中 TestSimpleTriangle 状态为 `pass`，TestTriangleWithConstantColor 状态为 `fail`，error 字段包含异常消息

#### Scenario: 非 headless 模式不生成 JSON

- **WHEN** 未指定 `--headless` 且未指定 `--report`
- **THEN** 不生成任何 JSON 报告文件

### Requirement: 按测试分离 Application 日志

Headless 模式下（`--headless > 0`），系统 SHALL 将 CA_LOG 输出（stdout）重定向到 `test_output/<TestName>.log` 文件。stderr SHALL 不做重定向，保留指向控制台以确保 crash handler 的文本调用栈输出不被埋入日志文件。`test_output/` 目录 SHALL 自动创建（如不存在）。

非 headless 模式下 SHALL 不做日志重定向（保持 stdout 输出）。

#### Scenario: Headless 模式日志分离

- **WHEN** `GPUBackendTester.exe --headless 10 --test TestSimpleTriangle`
- **THEN** 生成 `test_output/TestSimpleTriangle.log` 包含该测试的所有 CA_LOG 输出；stderr 仍指向控制台

#### Scenario: 正常模式不分离日志

- **WHEN** 未指定 `--headless`
- **THEN** CA_LOG 输出到原本的 stdout/stderr，不生成 `test_output/` 目录

### Requirement: 按测试分离 Vulkan Validation 日志

Headless 模式下，系统 SHALL 将 Vulkan debug messenger 回调输出重定向到 `test_output/<TestName>_validation.log` 文件。

实现方式 SHALL 为：在 RenderBackend_Vulkan 的 debug messenger callback 中，检查全局文件指针，若非空则同时写入文件。

#### Scenario: Validation 日志写入文件

- **WHEN** headless 模式运行 `TestTriangleWithImageBuffer`，Vulkan validation layer 产生 5 条警告
- **THEN** `test_output/TestTriangleWithImageBuffer_validation.log` 包含这 5 条警告的完整文本

#### Scenario: 无 Validation 错误时生成空文件

- **WHEN** headless 模式运行测试且 Vulkan validation layer 未产生任何消息
- **THEN** `test_output/<TestName>_validation.log` 文件存在但为空
