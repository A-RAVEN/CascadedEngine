## ADDED Requirements

### Requirement: CMake 输出可见

`build.py` SHALL 在运行 cmake configure 和 cmake build 时，通过 bat 文件内部重定向将 cmake/ninja 的完整输出记录到日志文件，并在每个阶段完成后将日志内容打印到终端。

#### Scenario: 配置完成

- **WHEN** 用户运行 `python build.py` 且 cmake configure 完成
- **THEN** 终端显示 cmake configure 的完整日志输出，包括编译器检测、依赖配置等信息

#### Scenario: 配置失败

- **WHEN** 用户运行 `python build.py` 且 cmake configure 失败
- **THEN** 终端显示 cmake configure 的完整错误日志，明确指示失败原因

#### Scenario: 编译完成

- **WHEN** 用户运行 `python build.py` 且所有目标编译完成
- **THEN** 终端在构建完成后显示 ninja 的完整编译日志

#### Scenario: 编译失败

- **WHEN** 用户运行 `python build.py` 且有编译错误
- **THEN** 终端在构建完成后显示完整的编译错误信息（如 `error C2535`, `FAILED:` 等）

### Requirement: 构建结果基于退出码

`build.py` SHALL 基于 cmake/ninja 的实际退出码判断构建是否成功，而非无条件报告成功。

#### Scenario: cmake configure 失败时退出

- **WHEN** cmake configure 步骤返回非零退出码
- **THEN** `build.py` 在打印 configure 日志后，以非零退出码退出，且不尝试执行 build 步骤

#### Scenario: cmake build 失败时退出

- **WHEN** cmake build 步骤返回非零退出码
- **THEN** `build.py` 在打印 build 日志后，输出 "BUILD FAILED" 并以非零退出码退出

#### Scenario: 全部步骤成功

- **WHEN** cmake configure 和 build 均返回零退出码
- **THEN** `build.py` 输出 "BUILD SUCCESSFUL" 并以零退出码退出

### Requirement: Configure 和 Build 日志分离

`build.py` SHALL 为 configure 阶段和 build 阶段使用独立的日志文件，确保两个阶段的输出不会互相覆盖。

#### Scenario: 两个阶段分别记录

- **WHEN** 用户运行 `python build.py`
- **THEN** configure 的输出写入独立日志文件（如 `_build_configure.log`），build 的输出写入另一个日志文件（如 `_build_build.log`），两个日志内容互不覆盖
- **AND** 每个阶段完成后 Python 读取并打印对应日志文件，随后删除

### Requirement: 日志文件缺失时的容错

`build.py` SHALL 在日志文件不存在时不会崩溃，而是报告日志缺失并使用已有的退出码信息判断结果。

#### Scenario: bat 执行失败导致无日志

- **WHEN** cmd.exe 或 bat 文件在写入日志前就失败了
- **THEN** `build.py` 打印 "日志文件未生成" 警告，并仍然根据退出码正确退出

### Requirement: 无网络时 configure 失败应报告明确错误

`build.py` SHALL 在 cmake configure 因依赖下载失败时，将错误信息输出给用户。

#### Scenario: 依赖下载失败

- **WHEN** cmake configure 因 FetchContent/CPM 无法从 GitHub 下载依赖而失败
- **THEN** 终端显示实际的错误信息（如 "Failed to clone repository", "SSL_ERROR_SYSCALL"），而非沉默退出或误报成功
