## Why

构建耗时难以忍受，根子在两件事上：**生成代码被放进源码树**（于是要入库、要对拍、要在改了类型之后记得重新生成并提交），以及**生成这段活干得太慢**（一个类型要启动三次 `CodeGen.exe`、同一份头被解析三遍；而改动前的代码是**纯串行**的 —— `git show HEAD:build.py` 的内层 `for emit_flag, suffix in CODEGEN_EMITTERS:` 每行顺序起 3 个进程、行与行之间也顺序跑，任何时刻只有 1 个进程在跑，32 核的机器闲着）。

实测读数（2026-10-04，本机 Debug 预设）：

- 全量重生成：63 个类型 × 3 种产物 = **189 次解析**；改动前的实现是**串行**的（源码读数，见上）。耗时以**当场成对读数**为准 —— 早先记在这里的"469.4 秒"没有任何日志留档，已作废；
- 什么都没改的构建（工作区当前实现，实测 `codegen 1.4s / 编译 2.3s / 合计 3.7s`，见下条 3.7 秒那次读数）：其中"问一句 ninja 要不要重建 `CodeGen.exe`"（含 vcvars 环境准备）占了大头，具体秒数同样以当场成对读数为准（旧工件里的 2.36 秒没有留档）；
- `--commit-generated` 每次都会把生成的产物**逐个覆盖写**，字节没变也写 → 这些文件被 `#include`，修改时间一刷新，引用它们的编译单元就被判过期、白编一遍。

## What Changes

- **生成代码不再进源码树**：`CodeGen.exe` 的产物只落构建目录（`out/build/<预设>/codegen/<模块>/`），各模块的 include 路径从源码树的 `generated/` 改成**构建目录里对应的那一层**。仓库里不再有生成代码，也不再有"入库副本过期"这回事。
- **删掉 `--commit-generated` 开关、删掉入库副本的逐字节对拍**：生成物既然只活在构建目录，就不需要"写回仓库"和"比对仓库那份"。
- **保留"生成器一变就全量重生成"这条安全规则**（脚本无法判断你改的是注释还是出码逻辑，保守做法必须留），但**把全量重生成做快**：
  - **一次解析出三份产物**：`CodeGen.exe` 增加"一次调用同时出反射表 / 读写代码 / 哈希代码"的模式，把 189 次解析降到 **63 次**；
  - **多条并行**：按机器核数并行多个类型（改动前是纯串行，一次只跑一个进程）。
- **生成段每次构建都跑**（因为产物不再入库，必须现场生成），但**输入没变的条目照旧跳过**（账本机制保留）。
- **固定开销按时间戳跳过**（已做，本 change 固化并写文档）：CMake 输入没变就跳过 configure；`CodeGen.exe` 的源文件没变就跳过"问 ninja 要不要重建它"。
- **进度可见**：生成段每条打印一行带序号的进度并立刻刷盘（已做）。
- **交付三档耗时读数**（全量 / 改一个被标注的头 / 什么都不改）写进 `Tools/CodeGen/README.md`，改动前后各一组。
- **同步另一个 change**：`retire-legacy-serde-hash` 里若干条任务写的是"生成物入库"与"删掉入库生成物即构建失败"，本 change 落地时必须同步改那些条目（否则两个 change 互相打脸）。

**BREAKING（构建方式）**：构建从此**必须能跑 `CodeGen.exe`**，也就是**必须装好 libclang**；`--no-codegen`（只编译、不跑生成段）这条快路失去意义，应当删除。生成代码的字节内容不变，引擎的运行时行为不变。

## Capabilities

### New Capabilities

- `build-throughput`: 构建吞吐的口径与规则 —— 三档耗时怎么测、生成段什么时候跳过、全量重生成必须并发到什么程度、进度必须可见、固定开销什么时候允许跳过。

### Modified Capabilities

- `codegen-framework`: 生成代码的**存放位置**从"源码树的 `generated/`"改成"构建目录"；随之删除"入库副本"与 `--commit-generated`；`CodeGen.exe` 增加"一次出三份产物"的调用方式；工具是独立工程（自带 `project()`、独立构建目录）。

## Impact

- 改动的文件：`build.py`（生成段：去掉写库与对拍、加"一次出三份"的调用、行级并发）、`Tools/CodeGen/CodeGenMain.cpp`（一次出三份）、`Tools/CodeGen/CMakeLists.txt` 与顶层 `CMakeLists.txt`（工具独立工程）、各模块 `CMakeLists.txt`（include 路径改指构建目录）、`Tools/CodeGen/README.md`、`Tools/CodeGen/verify_readme.py`。
- **源码树里的 189 个生成文件将被删除**（各模块 `generated/` 目录），它们的内容不再进版本库。注意：这 189 份里**只有 18 份真在版本库里**（实测 `git ls-files -- "*/generated/*"` 18 行），另外 171 份从来没入库（`D3D12RenderBackend/generated`、`ShaderCompilerSlang/generated`、`VulkanRenderBackendNew/generated` 在 `git status` 里是未跟踪）—— 删之前必须先快照到仓库外，否则 `git checkout` 取不回来。
- 连带影响：新克隆必须先构建一次才能让 IDE 找到生成头；CI/本地都必须有 libclang。另一个 change（`retire-legacy-serde-hash`）里"生成物入库"相关的条目要同步改。
- 生成物字节、引擎运行时行为、`CoreTests` 用例数：都不变。
