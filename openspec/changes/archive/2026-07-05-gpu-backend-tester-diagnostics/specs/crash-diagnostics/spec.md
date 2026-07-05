# Crash Diagnostics

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: MiniDump 自动生成

系统 SHALL 在进程启动时注册未处理异常处理器（`SetUnhandledExceptionFilter`）。当测试发生 crash（SIGSEGV 等）时，SHALL 自动生成 MiniDump 文件（`.dmp`），同时输出文本调用栈到 stderr。

生成的 `.dmp` 文件 SHALL 存放在当前工作目录，文件名格式为 `crash_YYYYMMDD_HHMMSS.dmp`。文本调用栈 SHALL 包含异常类型、异常地址、以及至少 16 帧的原始调用栈地址和所在模块名。

#### Scenario: 测试崩溃生成 dump

- **WHEN** 某个测试函数发生 access violation
- **THEN** 系统输出异常信息到 stderr（包含异常代码和地址），输出调用栈帧列表，生成 .dmp 文件到当前目录

#### Scenario: 正常退出不生成 dump

- **WHEN** 所有测试正常完成
- **THEN** 不生成任何 .dmp 文件

### Requirement: 文本调用栈输出

系统 SHALL 使用 `CaptureStackBackTrace` API 在崩溃时输出至少 16 帧调用栈。每帧 SHALL 包含序号、返回地址和通过 `GetModuleFileName` 获取的模块名。

文本调用栈 SHALL 输出到 stderr，无需 PDB 符号文件即可输出地址和模块名。完整的符号解析（函数名+行号）由 cdb 分析 .dmp 文件时完成。

#### Scenario: 崩溃调用栈可读

- **WHEN** 崩溃发生，`CaptureStackBackTrace` 返回 20 帧
- **THEN** stderr 输出 16 帧，每行格式为 `  [N] 0xADDRESS - module.dll`

#### Scenario: 异常处理器自身不崩溃

- **WHEN** 异常处理器（crash handler）执行过程中发生二次异常
- **THEN** 处理器不进行递归处理，直接返回 `EXCEPTION_CONTINUE_SEARCH` 让系统默认处理

### Requirement: 集成现有 MiniDump 代码

系统 SHALL 复用 `Test/VulkanRendererBackendTester/private/MiniDump.{h,cpp}` 中的 `MiniDump` 类。该代码 SHALL 被复制（非移动）到 `Test/GPUBackendTester/private/` 目录，原始文件不做修改，副本做以下扩展：
- 增加文本调用栈输出（`CaptureStackBackTrace` + `GetModuleHandleEx` + `GetModuleFileName`）
- 将 `FatalAppExit`（弹消息框）替换为 `TerminateProcess`（直接退出），避免 headless/CI 阻塞
- crash handler 文本输出使用 `WriteFile(GetStdHandle(STD_ERROR_HANDLE), ...)`，禁止 `fprintf`

CMakeLists.txt SHALL 新增 `dbghelp.lib` 链接依赖（MiniDumpWriteDump 所需）。

#### Scenario: MiniDump 文件复制

- **WHEN** change 实施完成
- **THEN** `Test/GPUBackendTester/private/MiniDump.{h,cpp}` 存在且包含上述扩展；CMakeLists.txt 包含 `dbghelp.lib` 链接
