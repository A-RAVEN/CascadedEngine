## MODIFIED Requirements

### Requirement: parser 元数据来源为 libclang，引擎保持纯 MSVC

系统 SHALL 用 libclang（复用 CACodeGen 的 CLANG_LIB）作为元数据来源；引擎 SHALL 保持纯 MSVC 编译，CodeGen 作为**独立构建期程序**运行，**不进入**引擎 CMake 编译图，引擎 SHALL NOT 链接 libclang。

"独立"SHALL 落到构建系统的实处，而不只是说法：

- `Tools/CodeGen/CMakeLists.txt` SHALL 是**独立工程**（自带 `cmake_minimum_required` 与 `project()`，不引用父工程变量）；
- 顶层 `CMakeLists.txt` SHALL NOT `add_subdirectory("Tools/CodeGen")` —— 引擎那棵 ninja 树里 SHALL NOT 存在 CodeGen 目标；
- `build.py` SHALL 在**自己的构建目录**（`out/build/codegen-tool`）里先 configure、再编译该独立工程，然后才跑生成段、才编译引擎；
- 可执行文件 SHALL 落在 `out/build/<预设>/bin/CodeGen.exe`（既有文档与校验脚本按此路径找它），`libclang.dll` SHALL 被拷到它旁边。

**构建前置条件（由本 change 明确）**：引擎构建 SHALL 依赖"`CodeGen.exe` 能跑起来"（即 libclang 可用）。这一点 MUST 写进 `Tools/CodeGen/README.md`，并且"程序没建起来"与"引擎编译失败"在输出里 SHALL 能被区分开。

#### Scenario: 引擎不链接 libclang

- **WHEN** 执行全量构建（`python build.py`）
- **THEN** 引擎编译产物不含 libclang.dll 依赖

#### Scenario: 引擎那棵树里没有 CodeGen 目标

- **WHEN** 在引擎构建目录里查 ninja 目标（或直接 `cmake --build <引擎构建目录> --target CodeGen`）
- **THEN** 该目标 MUST NOT 存在；引擎的 configure/编译 MUST 与 `CodeGen.exe` 的源码是否改动无关

#### Scenario: 独立目录能建出程序

- **WHEN** 跑 `python build.py --config Debug`
- **THEN** MUST 在 `out/build/codegen-tool` 里 configure/编译它，`out/build/<预设>/bin/CodeGen.exe` 与其旁边的 `libclang.dll` MUST 存在

#### Scenario: 没装 libclang 时的失败信息

- **WHEN** libclang 不可用（`CALIB_CLANG_DIR` 指向的目录里没有 `libclang.dll`）
- **THEN** 构建 MUST 在生成段失败并说清"生成用的程序没建起来、怎么配 libclang 路径"，MUST NOT 报成引擎编译错误

### Requirement: 生成物必须被真实引擎目标消费

生成代码 SHALL 被至少一个真实引擎目标的编译单元 `#include`，并在该编译单元内对生成表做编译期断言。仅有"与黄金样本逐字节对拍"不算被消费。

由于生成物**不再入库**（见下一条），"删掉入库生成物即构建失败"这条判据 SHALL 由"**生成失败/生成不出该类型即构建失败**"取代：生成段任何一条非零退出或对拍失败，构建 SHALL 以非零退出结束，且 MUST NOT 继续把引擎编译当作"通过"。

#### Scenario: 生成不出来就不许继续

- **WHEN** 清单里某个类型被生成器拒绝（非零退出）
- **THEN** 构建 MUST 以该失败结束并点名类型与退出码；引擎编译 MUST NOT 被报成成功

#### Scenario: 消费单元真的包含它

- **WHEN** 复核某迁移类型是否被真实目标消费
- **THEN** 该模块里 MUST 有编译单元的 `#include` 它的生成头，并有编译期断言

## ADDED Requirements

### Requirement: 生成代码只落构建目录，源码树里没有生成物

生成物 SHALL 只写进构建目录（`out/build/<预设>/codegen/<模块>/`），并且各模块的 include 路径 SHALL 指向构建目录里对应的那一层。源码树里 SHALL NOT 存在生成代码（各模块的 `generated/` 目录 SHALL 被删除），仓库也 SHALL NOT 跟踪任何生成文件。

随之删除：`build.py` 的 `--commit-generated` 开关、入库副本的逐字节对拍、以及它带来的"入库副本过期"这类错误提示。

#### Scenario: 仓库里没有生成代码

- **WHEN** 用文件系统枚举各模块的 `generated/` 目录
- **THEN** 这些目录 MUST NOT 存在；`git ls-files` 里 MUST NOT 有生成头

#### Scenario: 编译器读的是构建目录那份

- **WHEN** 复核某模块的 include 路径
- **THEN** MUST 指向 `${CMAKE_BINARY_DIR}/codegen/<模块>`，而不是源码树里的 `generated/`

#### Scenario: 改完类型不需要"记得重新生成"

- **WHEN** 给一个被标注类型加一个成员，然后直接跑构建
- **THEN** 因为是每次构建现场生成，MUST 不需要任何"写回仓库"的动作；生成段会重跑该条，产物与消费单元断言一致即通过

#### Scenario: 旧开关与旧快路不再存在

- **WHEN** 传 `--commit-generated` 或 `--no-codegen`
- **THEN** 构建 MUST 明确拒绝并说明这两个开关已随"生成物入库"一起删除（生成物不入库，就不存在"只编译不生成"这条快路）

### Requirement: 一个类型一次调用出三份产物

`CodeGen.exe` SHALL 支持在**一次调用**里同时产出反射表、读写代码、哈希代码三份产物（输出目录由参数给出），产物文件名与字节 SHALL 与逐个生成时完全一致。命令行接口、退出码表与 `Tools/CodeGen/README.md` 的示例 SHALL 同步更新；"一次给两个不同的发射标志 → 退出码 10"这条既有规则 SHALL 保留。

#### Scenario: 一次调用三份产物

- **WHEN** 用一个类型跑新调用方式
- **THEN** MUST 一次调用产出三份文件，且与用三次单发射器调用产出的字节**逐字节相同**

#### Scenario: 文档同步

- **WHEN** 复核 README
- **THEN** 接口表、退出码表与示例 MUST 反映新调用方式；README 里的命令原样可跑（`verify_readme.py` 0 处不符）

#### Scenario: 空输出目录按缺值拒绝

- **WHEN** 传 `--emit-all --out-dir=`（等号后面是空的）或 `-o ''`
- **THEN** MUST 以退出码 2 拒绝并说明目录/路径为空；MUST NOT 把产物写到**当前盘符的根目录**（实测未加这条检查时，`--out-dir=` 会让三份产物落到盘符根且退出码为 0）

#### Scenario: 接受集合的差异写在明处

- **WHEN** 复核 `--emit-all` 与逐个发射的关系
- **THEN** README MUST 写明：`--emit-all` 让三份产物的拒绝条件**全部**生效，所以标注没带 `serializable`（退出码 9）、含 `vector<bool>`（13）、含指针成员（15）的类型在 `--emit-all` 下会被拒，而这些类型用单发射反射仍能出码；并给出对照命令与退出码

### Requirement: 解析环境的归属是"真正编译这个类型的模块"

清单第三列 SHALL 指明**真正编译（消费）这个类型的模块目录**，`build.py` SHALL 按既有规则从该模块的 `compile_commands.json` 条目推导解析环境。SHALL NOT 为此维护任何手写的 include / 宏清单，也 SHALL NOT 拿生成器自己那份 `compile_commands.json` 当环境 —— 那份记的是"**怎么编译生成器**"（为链接 libclang 配的参数），与"怎么解析被标注的头"无关，生成器也不给自己出码。

#### Scenario: 夹具的归属是消费它的模块

- **WHEN** 复核 `Tools/CodeGen/TestEmit.h` 的 `EmitSample` 与 `TestKinds.h` 的 `Kinds` 这两行
- **THEN** 第三列 MUST 是消费这些生成头的模块（`Test/CoreTests`），MUST NOT 是靠一张借用表凑出来的目录；全量生成 MUST 不再出现 `no compile env for module`

#### Scenario: 环境推导规则没有被绕过

- **WHEN** 复核 `build.py` 里推导解析环境的那段
- **THEN** MUST NOT 存在"某目录借另一个目录的宏"这类手写映射；环境的输入只有 `compile_commands.json`
