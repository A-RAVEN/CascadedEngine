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

### Requirement: 生成段并行执行多个类型

生成段 MUST 并行处理多个类型，并发上限 MUST 有界且可解释（不与编译争抢内存与磁盘到互相拖慢）。失败消息与账本写入 MUST 按清单顺序汇总，保证输出稳定可读。改动前的实现是**纯串行**（`git show HEAD:build.py` 的内层循环每行顺序起 3 个进程，任何时刻只有 1 个进程）。

#### Scenario: 并发确实在跑

- **WHEN** 生成段执行期间观察进程
- **THEN** MUST 同时存在多个 `CodeGen.exe` 进程（数量大于 3，贴读数）

#### Scenario: 全量重生成明显变快

- **WHEN** 在同一台机器上，先把并发上限压到 1 跑一次全量、再按上限跑一次全量
- **THEN** 后者的生成段耗时 MUST 明显低于前者；**两条 `[耗时]` 行与其差值都要贴**，并写进 README。MUST NOT 拿一个没有日志留档的历史数字（例如旧的 469.4 秒）当基线

#### Scenario: 某一行失败不许拖垮整段

- **WHEN** 某一行在比对产物时抛错（例如工具退出码 0 却没有写出产物）
- **THEN** 构建 MUST 以 codegen 的失败信息结束（MUST NOT 是裸的 Python traceback），其余行 MUST 照常跑完，账本 MUST NOT 因这次异常被截断重写

#### Scenario: 不许预删产物

- **WHEN** 复核生成段在跑工具之前对产物目录做了什么
- **THEN** MUST NOT 先删掉这一行的产物 —— 产物目录本身在被解析头的包含路径上（`Compiler.h` 直接 `#include "UniformElement.codegen.h"`），先删会让解析当场失败；"工具退出码 0 却没写产物" MUST 用工具自己报出的写出名单（`CodeGen wrote: <路径>`）判断，缺一份即这一行失败

#### Scenario: 空的产物目录也能一次构建成功

- **WHEN** 把整个 `out/build/<预设>/codegen/` 删掉，然后跑一次 `python build.py --config Debug`（等价于新克隆的第一次构建）
- **THEN** MUST 一次成功（`BUILD SUCCESSFUL`），MUST NOT 需要"再跑一次就好" —— 清单里有生成头被别的头直接 `#include`（`Interface/ShaderCompiler/header/Compiler.h` 一个头就是 24 份，改造前同一个头是 48 份），所以生成段 MUST 在跑之前先给每份产物放占位文件（只在缺失或仍是占位内容时放），失败的行的占位文件 MUST 被删掉
