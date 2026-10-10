## RENAMED Requirements

- FROM: `### Requirement: 一个类型只解析一次，一次出三份产物`
- TO: `### Requirement: 一个类型只解析一次，一次出一份产物（三节）`

## MODIFIED Requirements

### Requirement: 一个类型只解析一次，一次出一份产物（三节）

生成段 MUST 对每个被标注类型只调用 `CodeGen.exe` **一次**，由这一次调用同时产出反射表、读写代码、哈希代码三节，写进**一份** `<类型名>.codegen.h`。MUST NOT 为了出三节而把同一份头解析三遍，也 MUST NOT 改成"每节一次调用、写三个文件"。三节的内容 MUST 与改造前由同一段发射代码产出（判据：`-o` 的单节输出与改造前那三份同名产物逐字节相同）。

#### Scenario: 调用次数

- **WHEN** 跑一次全量生成（接入清单 **76** 个类型，`CODEGEN_TRACE_CMD=1` 数 `cmd:` 行）
- **THEN** `CodeGen.exe` 的调用次数 MUST 是 **76**（不是 228）；**改造前**是 189 次（63 个类型 × 3 个发射器逐个起进程，依据是 `git show HEAD:build.py` 里 `for emit_flag, suffix in CODEGEN_EMITTERS:` 的源码读数）—— MUST NOT 拿"日志里 `matches committed` 的行数"当调用次数，两种实现每条都打 3 行

#### Scenario: 产物内容不变

- **WHEN** 用"一次调用出一份文件"的方式生成一遍
- **THEN** 用 `-o` 单节输出的字节 MUST 与改造前逐个生成时**逐字节相同**（228 次对拍）；消费单元的编译期断言 MUST 照旧通过

#### Scenario: 接口与文档同步

- **WHEN** 复核命令行接口
- **THEN** `Tools/CodeGen/README.md` 的接口表、退出码表与示例 MUST 反映"一次调用写一份文件"；"一次给两个不同的发射标志 → 退出码 10" 这条既有规则 MUST 保持；README 里的命令原样可跑（`verify_readme.py` 0 处不符）
