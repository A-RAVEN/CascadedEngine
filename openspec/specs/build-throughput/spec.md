# build-throughput Specification

## Purpose
TBD - created by archiving change improve-build-time. Update Purpose after archive.
## Requirements
### Requirement: 构建耗时按三个场景测量并留读数

构建脚本 MUST 能给出三个场景的耗时读数，每一条都 MUST 是可复现的命令加真实数字，而不是描述：

- **全量**：`CodeGen.exe` 的源码变过（或显式要求全量）时的完整构建；
- **改一个被标注的头**：只改一个被标注头的字节，其余不动；
- **什么都不改**：连续两次构建的第二次。

MUST 打印分段耗时（configure / 生成段 / 编译 / 合计），三段分开记，不许只给一个总数。

#### Scenario: 一次构建就能看出时间花在哪

- **WHEN** 跑 `python build.py --config Debug`
- **THEN** 末尾 MUST 打印一行 `[耗时] configure ?s / codegen ?s / 编译 ?s / 合计 ?s`

#### Scenario: 三档读数写进对外文档

- **WHEN** 复核"构建到底快了多少"
- **THEN** `Tools/CodeGen/README.md` 里 MUST 有这三条命令与它们当场跑出来的数字（改动前后各一组）

### Requirement: 一个类型只解析一次，一次出三份产物

生成段 MUST 对每个被标注类型只调用 `CodeGen.exe` **一次**，由这一次调用同时产出反射表、读写代码、哈希代码三份产物（产物字节与文件名都不变）。MUST NOT 为了出三份产物而把同一份头解析三遍。

#### Scenario: 调用次数

- **WHEN** 跑一次全量生成（63 个类型）
- **THEN** `CodeGen.exe` 的调用次数 MUST 是 63（用 `CODEGEN_TRACE_CMD=1` 数 `cmd:` 行，不是 189）；**改之前** = 63 行 × 3 = 189 次（依据是 `git show HEAD:build.py` 里 `for emit_flag, suffix in CODEGEN_EMITTERS:` 每行顺序起 3 个进程的源码读数）—— MUST NOT 拿"日志里 `matches committed` 的行数"当调用次数，两种实现每条都打 3 行

#### Scenario: 产物不变

- **WHEN** 用"一次出三份"的方式生成一遍
- **THEN** 三份产物的字节 MUST 与原来逐个生成时**逐字节相同**（贴对拍读数）；消费单元的编译期断言 MUST 照旧通过

#### Scenario: 接口与文档同步

- **WHEN** 复核命令行接口
- **THEN** `Tools/CodeGen/README.md` 的接口表、退出码表与示例 MUST 反映新的调用方式；"一次给两个不同的发射标志 → 退出码 10"这条既有规则 MUST 保持；README 里的命令原样可跑（`verify_readme.py` 0 处不符）

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
- **THEN** MUST NOT 先删掉这一行的产物 —— 产物目录本身在被解析头的包含路径上（`Compiler.h` 直接 `#include "UniformElement.Serialization.h"`），先删会让解析当场失败；"工具退出码 0 却没写产物" MUST 用工具自己报出的写出名单（`CodeGen wrote: <路径>`）判断，缺一份即这一行失败

#### Scenario: 空的产物目录也能一次构建成功

- **WHEN** 把整个 `out/build/<预设>/codegen/` 删掉，然后跑一次 `python build.py --config Debug`（等价于新克隆的第一次构建）
- **THEN** MUST 一次成功（`BUILD SUCCESSFUL`），MUST NOT 需要"再跑一次就好" —— 清单里有 48 份生成头被别的头直接 `#include`，所以生成段 MUST 在跑之前先给每份产物放占位文件（只在缺失或仍是占位内容时放），失败的行的占位文件 MUST 被删掉

### Requirement: 生成器变了就全量重生成（刻意保留的安全规则）

账本 MUST 把 `CodeGen.exe` 的内容哈希作为每条记录的一项：它一变，清单里每一条 MUST 重跑。MUST NOT 用"先试跑一个类型比较字节"之类的办法跳过这一步 —— 那种办法要求试跑覆盖所有形状，一旦漏掉就是**静默的漏重生成**。输入没变时（工具与头都没动）MUST 照旧跳过，并且 MUST 允许显式强制全量。

#### Scenario: 工具变了必须全量

- **WHEN** 改一处 `Tools/CodeGen/CodeGenMain.cpp`（哪怕只是注释或错误提示）后跑构建
- **THEN** 生成段 MUST 对**所有**条目重跑，并打印"重跑 63 条、跳过 0 条"；**改之前**任何工具改动也都全量（贴读数）—— 这条是**要保留的行为**，不是缺陷

#### Scenario: 输入没变就跳过

- **WHEN** 连续两次构建之间不改任何东西
- **THEN** 生成段 MUST 打印"重跑 0 条、跳过 63 条"，且生成段耗时 MUST 是秒级

#### Scenario: 可以强制全量

- **WHEN** 传 `--codegen-all`
- **THEN** MUST 忽略账本、清单每一条都重跑

### Requirement: 「输入没变」的判据必须覆盖生成器真正读过的每一个文件

跳过判据 MUST 覆盖所有能改变产物字节的输入，至少：工具二进制、**生成器这一次真正读过的每一个文件**（由生成器自己报出，不是人手列清单）、该模块推导出的编译环境、上一轮的产物。MUST NOT 只哈希清单里的那一个文件 —— 那样做的实测后果是：改 `CACore/header/CACore/CAHash.h` 这类不在清单里、却被清单头 include 的头时，63 行里有 62 行仍被判"没变"而跳过，编译继续吃旧产物，构建却报成功（静默假绿）。

判据 MUST 按文件**内容**（哈希）判，MUST NOT 按修改时间判 —— `git checkout`、复制文件、切分支都会刷新时间，按时间判会把用不上这个头的行也一起重跑。

#### Scenario: 改依赖头必须重跑

- **WHEN** 改一个**不在清单里、但被某个清单头 include** 的头
- **THEN** 引用它的那些条目 MUST 重跑（贴改前的"跳过 N 条"与改后的"重跑 M 条"两条读数）

#### Scenario: 包含目录里多出同名头也必须重跑

- **WHEN** 在某个包含目录里新建一个同名头文件，把原来被读到的那个挡住
- **THEN** 相关条目 MUST 重跑（贴读数）—— 只看上一轮的名单看不出这种情况

#### Scenario: 什么都不改仍然跳过

- **WHEN** 连续两次构建之间不改任何东西
- **THEN** MUST 照旧全部跳过（贴原文），且产物字节不变

### Requirement: 常态构建的固定开销按时间戳跳过

构建脚本 MUST 在输入未变时跳过两类固定开销，并 MUST 把"跳过了什么"打印出来（不许静默）：

- **configure**：所有 `CMakeLists.txt` / `*.cmake` / `CMakePresets.json` 都比 `out/build/<预设>/build.ninja` 旧时跳过；
- **"问 ninja 要不要重建 `CodeGen.exe`"**：`Tools/CodeGen` 下的源文件都比 `CodeGen.exe` 旧时跳过（代价以当场成对读数为准 —— 旧工件里记的 2.36 秒没有日志留档）。

强制重配 MUST 有显式开关（`--configure`）。

#### Scenario: 什么都不改的构建

- **WHEN** 连续两次跑 `python build.py --config Debug`
- **THEN** 输出 MUST 含"CMake 配置输入都没动 —— 跳过 configure"与"`CodeGen.exe` 的源文件没动 → 跳过"这两行，且 configure 段耗时 MUST 为 0.0s

#### Scenario: 输入变了就不许跳

- **WHEN** 改了任一 `CMakeLists.txt`
- **THEN** MUST 重新 configure，并打印是哪个文件变了

### Requirement: 生成段的进度必须实时可见

生成段 MUST 每条打印一行带序号的进度（`[n/总数] 类型名`）并立刻刷盘，使输出重定向到文件时也能在执行期间看到进度。

#### Scenario: 重定向也能看进度

- **WHEN** 把构建输出重定向到文件，并在**执行期间**读该文件
- **THEN** MUST 已能看到递增的 `[n/63]` 行；**改之前**：执行期间该文件是 0 行（贴读数）

