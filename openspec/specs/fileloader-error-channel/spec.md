# fileloader-error-channel Specification

## Purpose
约束 CACore FileLoader 三个读写函数的失败显式化：打不开、读不足、写失败都必须抛出 `cacore::file_error` 并点名路径，空文件合法。本能力当前零活跃调用方，函数行为由 CoreTests 用例钉死，不得静默返回空容器或丢弃数据。

## Requirements

### Requirement: FileLoader 读取失败必须显式抛出并点名路径
`cacore::LoadStringFile` / `cacore::LoadBinaryFile` 在文件打不开时 MUST 抛出 `cacore::file_error`（派生 `std::runtime_error`，定义于 `CACore/header/FileLoader.h`），消息 MUST 含动作、路径与原因；读取未完成（二进制按尺寸读后实际读到的字节不足；文本读取流状态坏且非正常结束）MUST 同样抛出。空文件 MUST 合法（返回空容器，不抛）。函数签名 MUST NOT 改变。MUST NOT 复用 `ca_io::io_error`（CACore 为底层静态库，不得反向依赖 Interface/IOManager 与 ThreadManager）。注：2026-11-20 实测全仓无活跃调用方——`ShaderProcessor/Main.cpp` 不在构建（根 CMakeLists 无其 add_subdirectory、build.ninja 零命中），且其调用写的是全仓不存在的 `fileloading_utils::LoadStringFile`（构建外死代码）；本要求约束函数自身行为，由 CoreTests 用例验证，本批不改 ShaderProcessor。

#### Scenario: 缺失路径读取被点名拒绝
- **WHEN** 对不存在的文件路径调用 `LoadStringFile` 或 `LoadBinaryFile`
- **THEN** MUST 抛出 `cacore::file_error` 且消息含该路径；MUST NOT 静默返回空容器

#### Scenario: 空文件合法
- **WHEN** 对存在的 0 字节文件调用 `LoadBinaryFile`
- **THEN** MUST 返回空容器且不抛异常

#### Scenario: 读取不足被点名拒绝
- **WHEN** 二进制读取过程中实际读到的字节少于按文件尺寸预期的字节
- **THEN** MUST 抛出 `cacore::file_error` 点名路径与原因；MUST NOT 把未填充的缓冲当完整内容返回

### Requirement: FileLoader 写入失败必须显式抛出并点名路径
`cacore::WriteBinaryFile` 在目标打不开、`write()` 后流状态坏、`flush()`/`close()` 后流状态坏时 MUST 抛出 `cacore::file_error`，消息 MUST 含动作、路径与原因；MUST NOT 静默丢弃数据。

#### Scenario: 写目标打不开被点名拒绝
- **WHEN** 对父目录不存在的路径调用 `WriteBinaryFile`
- **THEN** MUST 抛出 `cacore::file_error` 且消息含该路径；MUST NOT 静默返回
