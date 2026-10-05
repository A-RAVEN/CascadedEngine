> 构建只走 `python build.py`（项目唯一合法入口）。本 change 只动构建期：**不改生成物字节、不改引擎运行时行为、不改 `CoreTests` 用例数**。
> 每条都要求"改前 / 改后"两个读数；只给一个数的，不算做完。
> 第 6 节是**返工**：2026-10-04 的对抗审查在已落地的代码与已勾的条目里查出的问题，逐条修复并重取读数。代码与工件都保留，不动现有工作区。

## 1. 一次调用出三份产物（先做，后面的读数都建立在它上面）

- [x] 1.1 `CodeGen.exe` 支持"一次调用同时出反射表 / 读写代码 / 哈希代码"（输出目录由参数给出），文件名与字节不变
  **怎么算做完**：拿同一个类型，分别用新方式与旧的三次单发射器方式各生成一遍，三份产物**逐字节相同**（贴对拍输出）；**改之前**：一个类型要启动三次（贴读数）
  实测：`EmitSample` 用 `--emit-all --out-dir .review_tmp/readme/emit-all` 与三次单发射器各写一个 `-o`，三份 SHA256 两两相等（Reflection 1176 B / Serialization 1547 B / Hash 1310 B，`identical=True`）。
  **改之前**：`git show HEAD:build.py` 的内层 `for emit_flag, suffix in CODEGEN_EMITTERS:` 每行**顺序起 3 个进程**（源码读数，63 行 × 3 = 189 次调用）。
  ⚠ 我先前在这里写的"`build2` 日志 183 行 `matches committed` = 3 次调用"**作废**：那份日志本身就是改后版本跑出来的；而且一次 `--emit-all` 同样打 3 行 `matches committed`，两种实现数出来都是 183 —— 它证明不了调用次数（对抗审查 2026-10-04）。
  ⚠ 样本只覆盖一个零容器的标量夹具（`EmitSample` 四个标量成员），63 行里 34 行的容器分支没被覆盖 → 返工 6.6。
- [x] 1.2 同步命令行接口文档与校验脚本：`Tools/CodeGen/README.md` 的接口表、退出码表、示例命令；`verify_readme.py` 的示例
  **怎么算做完**：README 里的命令原样可跑（`python Tools/CodeGen/verify_readme.py` → 0 处不符，贴最后一行）；"一次给两个**不同**发射标志 → 退出码 10"这条既有规则仍成立（贴那一条命令与退出码）
  实测：`README: 18 documented command(s) run, 0 mismatch(es)`（新增的 `--emit-all` 示例在其中）；`--emit-serialization --emit-hash` → `OK exit=10 documented=10`。
  ⚠ 这条判据只比**退出码**（`verify_readme.py` 自述不比 stdout 文本），所以 README 里序列化/哈希产物示例（`Serialization.h`、`::cacore::serializer`、`Hasher.h`、`custom_hash_trait`）、字段行格式、反射字段行的 7 个实参**已经与真实输出不符，判据照样全绿** → 返工 6.5、6.7。
- [x] 1.3 全量生成的调用次数从 189 降到 63
  **怎么算做完**：全量生成一次，数出 `CodeGen.exe` 的启动次数 = **63**（用 `CODEGEN_TRACE_CMD=1` 数 `cmd:` 行，贴命令与输出）；**改之前** = 63 行 × 3 = 189 次调用（依据是 `git show HEAD:build.py` 里 `for emit_flag, suffix in CODEGEN_EMITTERS:` 每行顺序起 3 个进程的源码读数）。
  ⚠ 不许再用"日志里 `matches committed` 的行数"当调用次数 —— 两种实现每条都打 3 行（对抗审查 2026-10-04）。
  **实测**：`CODEGEN_TRACE_CMD=1 python build.py --config Debug --codegen-all`，日志里 `cmd: ` 行 **63** 条、`[n/63]` 进度行 63 条、清单 63 行、`codegen: 重跑 63 条、跳过 0 条`；**改之前** = 63 × 3 = 189 次（`git show HEAD:build.py` 的 `for emit_flag, suffix in CODEGEN_EMITTERS:` 每行顺序起 3 个进程）。
- [x] 1.4 归属目录已不在引擎构建树里的清单行仍能拿到编译环境（生成器独立成工程后，`Tools/CodeGen` 在引擎的 `compile_commands.json` 里是 0 个编译单元）
  **怎么算做完**：全量生成 MUST 不再出现 `no compile env for module 'Tools/CodeGen'`（贴改前那条失败的原文与改后的构建结果）；`EmitSample` 与 `Kinds` 的六份产物 MUST 与改动前逐字节相同（贴对拍读数）
  实测：改前 `CODEGEN FAILED (2) — no compile env for module 'Tools/CodeGen' (type EmitSample) / (type Kinds)`；加借用环境后同一条命令 `BUILD SUCCESSFUL`，`codegen: 重跑 2 条、跳过 61 条`、`[耗时] configure 0.0s / codegen 1.4s / 编译 2.3s / 合计 3.7s`；六份产物对拍 `identical_to_golden=True`。
  ⚠ `identical_to_golden=True` 比的是**工作区的入库副本**，而其中四份（`EmitSample.Serialization/Hash.h`、`Kinds.Serialization/Hash.h`）本身就落在本批未提交改动里被改过 —— 基准被污染，"与改动前相同"必须改用 `git show HEAD:<路径>` 的字节重取 → 返工 6.8。
  ⚠ 借用环境还绕开了更根本的问题：生成器独立成工程后，它自己那份 `compile_commands.json` 从来没导出过 → 返工 6.8。

## 2. 生成段并行处理多个类型

- [x] 2.1 生成段按核数并行多个类型（上限有界、可解释），失败消息与账本写入按清单顺序汇总
  **怎么算做完**：执行期间 `CodeGen.exe` 进程数 MUST 大于 3（执行中采样进程数，贴读数）；全量生成耗时 MUST 明显低于**同一台机器上当场测出的串行基线**（把并发上限临时压到 1 跑一次全量、再按上限跑一次，贴两条 `[耗时]` 行与它们之差）。
  ⚠ 不许再拿 **469.4 秒**当基线：那个数字没有任何日志留档，而且同一批工件对"什么都不改"给了 3.5 秒与 59.6 秒两个差 17 倍的数（对抗审查 2026-10-04）。基线只能是当场跑出来的成对读数。
  **实测（当场成对）**：执行期间每 3 秒采样一次 `CodeGen.exe` 进程数 —— 稳定在 **8** 个（`t=6s…t=27s` 全是 8，开工前与收尾各为 0），大于 3。全量生成两条 `[耗时]`：把 `CODEGEN_MAX_WORKERS` 临时压到 1 → `codegen 149.0s / 合计 151.2s`；按上限 8 → `codegen 32.5s / 合计 34.8s`；**差 116.5 秒，快 4.6 倍**。两次都是 `重跑 63 条、跳过 0 条`、`BUILD SUCCESSFUL`。量完 `build.py` 已按 SHA256 原样还原（`DA923CCB…E96E`，改前先 `Copy-Item` 到 `%TEMP%`），并发上限回到 8；日志里开工先打印 `codegen: 并发 8 条（逻辑核 32，上限 8）`，上限有据可查。
- [x] 2.2 进度实时可见：每条打印 `[n/63] 类型名` 并立刻刷盘
  **怎么算做完**：输出重定向到文件，**执行期间**读该文件 MUST 已能看到递增的 `[n/63]` 行（贴执行中那一次的 tail）；**改之前**：执行期间 0 行（贴读数）
  实测（生成段正在跑的时候读同一份重定向日志，每 3 秒一次）：`t=6s 10 行 → t=9s 18 → t=12s 29 → t=15s 43 → t=18s 46 → t=21s 48 → t=24s 54 → t=27s 59 → t=30s 63`。**改之前**：`git show HEAD:build.py` 里根本没有 `[n/63]` 这种进度行（`Select-String '\[\d+/63\]'` 命中 0），执行期间是 0 行。

## 3. 生成物出仓：源码树里不再有生成代码

- [x] 3.1 各模块 include 路径从源码树 `generated/` 改成 `${CMAKE_BINARY_DIR}/codegen/<模块>`
  **怎么算做完**：逐条列出改动前指向源码树 `generated/` 的 include 行（**6 处**：`CACore/CMakeLists.txt`、`Interface/RenderInterface/CMakeLists.txt`、`Interface/ShaderCompiler/CMakeLists.txt`、`D3D12RenderBackend/CMakeLists.txt`、`VulkanRenderBackendNew/CmakeLists.txt`、以及 `Test/CoreTests/CMakeLists.txt` 里指向 `Tools/CodeGen/generated` 的那一处），改后这 6 处逐个 MUST 指到构建目录里对应的那一层（贴改前 / 改后原文）；构建 MUST 通过。
  ⚠ 判据用的 grep 必须能看见第 5 处：那个文件名是 `CmakeLists.txt`（小写 m），`git grep -- "*/CMakeLists.txt"` 命不中它（对抗审查 2026-10-04 实测）。
  ⚠ 第 6 处（`Test/CoreTests`）跟着 6.8 一起变：那两行夹具的归属改成 `Test/CoreTests` 之后，产物落在 `Test/CoreTests/generated/`，这一处 include 必须同步，漏了就是 CoreTests 编不过。
  **改前 → 改后（6 处）**：`${CMAKE_SOURCE_DIR}/CACore/generated` → `${CMAKE_BINARY_DIR}/codegen/CACore`；`PUBLIC generated` → `PUBLIC ${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface`；`${CMAKE_SOURCE_DIR}/ShaderCompilerSlang/generated` → `${CMAKE_BINARY_DIR}/codegen/ShaderCompilerSlang`；`PRIVATE "generated"` → `PRIVATE ${CMAKE_BINARY_DIR}/codegen/D3D12RenderBackend`；`PRIVATE "generated"` → `PRIVATE ${CMAKE_BINARY_DIR}/codegen/VulkanRenderBackendNew`；`${CMAKE_SOURCE_DIR}/Test/CoreTests/generated` 与 `${CMAKE_SOURCE_DIR}/Interface/RenderInterface/generated` → `${CMAKE_BINARY_DIR}/codegen/Test_CoreTests` 与 `${CMAKE_BINARY_DIR}/codegen/Interface_RenderInterface`。
  **读数**：`git grep -n "SOURCE_DIR.*generated" -- "*.txt" "*.cmake"` **零命中**；`git grep -n "generated" -- "*/CMakeLists.txt" CMakeLists.txt` 只剩 **4 处注释**里的字样（正文里已无 include 路径）；`"*/CmakeLists.txt"` 那条现在是空的。构建：`BUILD SUCCESSFUL`，`[耗时] configure 0.0s / codegen 19.4s / 编译 57.2s / 合计 76.6s`（改 include 路径让引用它的编译单元重编了一遍）。**产物一个字节没变**：构建目录里 189 份产物与 6.9 的快照**逐字节相同 189 / 不同 0 / 缺失 0**。
  **⚠ 这一步炸出一个真缺陷（已就地修掉）**：`build.py` 原来在跑工具**之前先删掉这一行的三份旧产物**（为的是防"工具退出码 0 却没写产物"被当成"与入库副本一致"的假绿）。include 路径改指构建目录之后，这个删除动作本身就把解析弄坏了 —— `Interface/ShaderCompiler/header/Compiler.h` 直接 `#include "UniformElement.Serialization.h"` 等 **48 份生成头**，而它们就在那个目录里。实测失败原文：`CodeGen 'UniformElement' --emit-all exit 6: … unresolved include (fatal): Interface/ShaderCompiler/header/Compiler.h:653:10: fatal error: 'UniformElement.Serialization.h' file not found`，随后引擎编译也红（`fatal error C1083`）。
  **修法**：不再预删；改成让工具自己报出它**真正写出了哪些产物**（新增 `CodeGen wrote: <路径>` 行，与 `CodeGen deps:` 并列），`build.py` 要求这一行的三份产物都在名单里，缺一份就判这一行失败。**红绿证据**：临时加一行 `("CACore/header/Reflection.h", "NoSuchAnnotatedType", "CACore")`（该头无标注 → 工具退 0、不写产物）→ `CODEGEN FAILED (1) … CodeGen 'NoSuchAnnotatedType' --emit-all: 工具退出码 0，却没有写出这些产物：…`、无裸 traceback；删掉探针行后 `重跑 0 条、跳过 63 条`、`BUILD SUCCESSFUL`。
  **顺带说明**：那两次失败构建把 `D3D12RenderBackend`（33 份）与 `VulkanRenderBackendNew`（36 份）的产物删掉了（旧代码的预删 + 后续解析失败），**正是 6.9 的快照把它们补回来的** —— 这条恢复路径也因此被真实验证过一次。
- [x] 3.2 删除源码树里各模块的 `generated/` 目录（**盘上 189 个文件，其中只有 18 个在版本库里**，171 个从未入库）
  **怎么算做完**：文件系统枚举这些目录 MUST 不存在；`git ls-files | Select-String "generated/"` MUST 为空（贴两条读数）；**改之前**：189 个在盘上、18 个在索引里（贴计数）。
  ⚠ 因此 design 里"回滚第 3 步需要把这 189 个文件从版本库恢复（`git checkout`）"对其中的 171 个做不到 —— 删之前 MUST 先把 189 份快照到仓库外（见 5.3）。
  **改之前读数**：盘上 6 个目录共 **189** 份（`CACore 6 / D3D12RenderBackend 33 / Interface/RenderInterface 36 / ShaderCompilerSlang 72 / Test/CoreTests 6 / VulkanRenderBackendNew 36`）；`git ls-files | generated/` = **18** 行（`CACore/generated` 3 + `Interface/RenderInterface/generated` 9 + `Tools/CodeGen/generated` 6）。
  **改之后读数**：六个目录**一个都不在盘上**；`git ls-files | Select-String "generated/"` = **0**。
  **⚠ 顺带动了索引（不是提交）**：`git ls-files` 读的是索引，光删磁盘上的文件它照样列出来（会显示成 `D`），所以对这 18 条跑了 `git rm -r --cached --quiet -- CACore/generated Interface/RenderInterface/generated Tools/CodeGen/generated`（退出码 0）。**只改了索引、没有任何提交**；现在 `git status` 里是 18 条 `D ` 开头的暂存删除。第一次跑时把 `D3D12RenderBackend/generated` 这类**从未入库**的路径也写进 pathspec，`git` 直接 `fatal: pathspec … did not match any files` 整条命令不执行 —— 只列真正在索引里的三个路径才行。
- [x] 3.3 `build.py` 删掉"写库"与"逐字节对拍"两段，并删掉 `--commit-generated`
  **怎么算做完**：`git grep -n -F "commit_generated" -- build.py` MUST 命中 0 行；`git grep -n -F "copyfile" -- build.py` MUST 命中 0 行（贴读数）
  ⚠ 连带项，缺一样就是只删了一半（对抗审查 2026-10-04）：账本指纹里的 `golden` 一项与跳过条件里的 `all(cached["golden"].values())` 必须同时删掉 —— 入库副本没有之后它们恒为假，什么都不改也会每次全量重跑；两条 `run python build.py --config ... --commit-generated` 提示语必须一起清掉，否则是在教人用一个已经删掉的开关。
  **实测**：`git grep -n -F "commit_generated" -- build.py` = **0**；`git grep -n -F "copyfile" -- build.py` = **0**；另外把连带项一并收干净 —— `git grep -n "golden" -- build.py` = **0**（指纹里的 `golden` 项、跳过条件里的 `all(cached["golden"].values())`、`codegen_generated_dir()` 的旧语义、文件头与清单注释里的"入库副本"说法，全部改掉；该函数现在返回**构建目录**，只是名字沿用），`git grep -n -F -- "--commit-generated" -- build.py` = **0**。
  **构建读数**：`python build.py --config Debug` → `BUILD SUCCESSFUL`，`[耗时] configure 0.0s / codegen 28.3s / 编译 56.9s / 合计 85.2s`；每份产物打印成 `  generated <构建目录里的路径> (<字节数>)`。**产物与快照逐字节相同 189 / 不同 0 / 缺失 0**，`CoreTests exit=0`。
- [x] 3.4 删掉 `--no-codegen` 与 `--commit-generated` 两个开关，并在传它们时**明确拒绝**
  **怎么算做完**：`python build.py --config Debug --no-codegen` 与 `python build.py --config Debug --commit-generated` 各自 MUST 非零退出并打印"该开关已随生成物入库一起删除"（贴两条命令与原文）。
  ⚠ `build.py` 现在对未知开关是**静默忽略**的（`git grep -n -F "unknown" -- build.py` 零命中），所以只把解析删掉，会变成"照常构建、什么也不说"（对抗审查 2026-10-04）。
  **实测**（两条都是前台跑）：
  `python build.py --config Debug --no-codegen` → `ERROR: --no-codegen 已随「生成物入库」一起删除 —— 生成物只落构建目录、每次构建现场生成，` / `既没有「只编译、不生成」这条快路，也没有「把产物写回仓库」这个动作。` / `要强制全量生成用 --codegen-all.`，**退出码 1**；
  `python build.py --config Debug --commit-generated` → 同一段话（只是开关名不同），**退出码 1**。
  连带：`run_codegen` 这个变量与"跳过生成段"那条分支一并删除（`git grep -n "run_codegen" -- build.py` = 0），生成段现在**无条件每次都跑**；README 的开关表里那两行换成了一段"已删除"的说明（第 12 行教人用 `--no-codegen` 只重建工具那句也删了）。
- [x] 3.5 README 写清新的前置条件：**构建必须能跑 `CodeGen.exe`（必须装 libclang）**；新克隆第一次构建之前 IDE 找不到生成头
  **怎么算做完**：README 顶部逐字含这两句（带文件限定的 grep 命中）；"程序没建起来"与"引擎编译失败"在输出里可区分（贴两种失败各自的输出）
  **实测（写进 README 的正文）**：`- **构建引擎必须能跑 \`CodeGen.exe\`，也就是必须装好 libclang。**` 与 `- **新克隆第一次构建之前，IDE 找不到那些生成头。**`（`Tools/CodeGen/README.md` §零，两处各命中 1 行）。
  **两种失败的输出（都当场跑过）**：
  ① *程序起不来* —— 把 `out/build/x64-debug/bin/libclang.dll` 挪走再强制全量生成：`CODEGEN FAILED (63) — the generated headers are not in the state this build expects`，明细是 `CodeGen 起不来（EmitSample）：退出码 3221225781 (0xC0000135) 是 Windows 的异常码，不是工具自己的退出码。多半是 CodeGen.exe 旁边缺 libclang.dll —— 检查 …\bin\libclang.dll 在不在，以及 CALIB_CLANG_DIR 指向的目录`（**这条提示是本次新加的**：原来只印一个数字 3221225781、stderr 是空的，等于什么都没说），退出码 1；
  ② *引擎编译失败* —— 临时给 `Test/CoreTests/CodeGenMetaTest.cpp` 加一行 `#error`：`FAILED: Test/CoreTests/CMakeFiles/CoreTests.dir/CodeGenMetaTest.cpp.obj` / `fatal error C1189: #error:  3.5 探针：故意制造一次引擎编译失败` / `ninja: build stopped: subcommand failed.`（该文件已按 SHA256 原样还原）。两种失败的形状完全不同：一个在生成段、一个在编译段。
  **⚠ 顺带炸出并修掉冷启动死循环**：`Interface/ShaderCompiler/header/Compiler.h` 直接 `#include` 了 **48 份生成头**，而生成这些类型的行又必须先解析这个头 —— 构建目录空的时候谁也出不来。实测把 `ShaderCompilerSlang` 的 72 份产物挪走 → **47 行**以退出码 6 失败（顺着 include 链扩散，不是那一个模块的 24 行）。修法：登记一行时先给它的三份产物放**占位文件**（只在缺失或仍是占位内容时放），真正的产物随后覆盖它；失败的行的占位文件删掉。**验证**：把整个 `out/build/x64-debug/codegen/` 删掉再构建 → `重跑 63 条、跳过 0 条`、`BUILD SUCCESSFUL`，189 份产物与 6.9 的快照**逐字节相同** —— "新克隆先跑一次构建"这句话现在是真的。

## 4. 固定开销与账本（已在工作区，本组只做收口与读数）

- [x] 4.1 CMake 输入没变就跳过 configure，并打印跳过了什么；`--configure` 可强制
  **怎么算做完**：连续两次构建，第二次 configure 段耗时 = **0.0s** 且打印"CMake 配置输入都没动"（贴 `[耗时]` 行）；改任一 `CMakeLists.txt` 后 MUST 重新配置并点名该文件（贴输出）
  **实测**（三次前台构建）：
  ① 什么都不改 → `(CMake 配置输入都没动 —— 跳过 configure，要强制重配就加 --configure)`，`[耗时] configure 0.0s / codegen 42.4s / 编译 43.9s / 合计 86.3s`；
  ② 只把 `Interface/ShaderCompiler/CMakeLists.txt` 的时间戳刷新（内容一字未改）→ `(CMake 配置输入变了：Interface\ShaderCompiler\CMakeLists.txt —— 重新 configure)`，`[耗时] configure 26.7s / …`（**点名了是哪个文件**）；
  ③ `python build.py --config Debug --configure` → 不打印"没动"那行、直接重配，`[耗时] configure 30.7s / codegen 5.7s / 编译 2.3s / 合计 38.7s`、`BUILD SUCCESSFUL`（强制开关有效）。
- [x] 4.2 `CodeGen.exe` 的源文件没变就跳过"问 ninja 要不要重建它"
  **怎么算做完**：连续两次构建，第二次 MUST 打印"`CodeGen.exe` 的源文件没动 → 跳过"，并给出**当场成对读数**（先让这条判据失效跑一次、拿到"问 ninja"那一次的耗时，再让它生效跑一次，贴两条 `[耗时]` 行与差值）。
  ⚠ 2.36 秒同样没有日志留档，只能当参考、不能当判据（对抗审查 2026-10-04）。
  **实测（当场成对）**：连跑两次都打印 `(工具的源文件都没动 -> 跳过 \`cmake --build --target CodeGen\`，省一次 vcvars + ninja)`，`[耗时] configure 0.0s / codegen 1.2s / 编译 2.5s / 合计 3.8s` 与 `… codegen 1.6s / 编译 3.8s / 合计 5.3s`；把判据临时置假（`if False and sid_path.exists() …`，跑完按 SHA256 原样还原）→ `[耗时] configure 0.0s / codegen 7.0s / 编译 3.2s / 合计 10.2s`。**生成段差 ≈ 5.8 秒**，差值就是"问一次 ninja"的代价。
  **⚠ 这一步查出判据本身盯错了文件（已修）**：原来把**引擎的** `out/build/<预设>/build.ninja` 也算进"工具的源输入"，改成工具那棵树的 `out/build/codegen-tool/build.ninja` 也一样不对 —— 它们由 configure 重写，而 ninja 回"no work to do"时它们**不会**跟着变新，于是这条跳过**永远不再成立**（实测：连续两次构建都不打印跳过）。现在判据里不含任何 `build.ninja`，只看 `Tools/CodeGen` 下的 `.cpp/.h/.hpp/.txt/.cmake` 有没有比 `CodeGen.exe` 新 —— 那才是"工具要不要重建"的真判据。
- [x] 4.3 账本跳过没变的条目；**保留**"生成器变了就全量重生成"这条规则
  **怎么算做完**：什么都不改时打印"重跑 0 条、跳过 63 条"（贴原文）；改一处 `CodeGenMain.cpp`（注释也算）后打印"重跑 63 条、跳过 0 条"（贴原文）—— 后者是**要保留的安全行为**，MUST NOT 用"试跑一个类型比字节"之类的办法绕过。
  ⚠ 这条判据**测不出真正的漏洞**：跳过只看清单里那一份头的哈希，改它 include 的依赖头照样判"没变"→ 跳过 → 静默假绿。必须与返工 6.1 一起收口后，再取这两个读数（现在什么都不改会打印"重跑 1 条、跳过 62 条"，与判据写的 0/63 不符，也要一并解释清）。
  **实测**（6.1 已落地之后）：什么都不改 → `codegen: 重跑 0 条、跳过 63 条`（`[耗时] configure 0.0s / codegen 1.5s / 编译 2.8s / 合计 4.3s`）；给 `Tools/CodeGen/CodeGenMain.cpp` **只加一行注释** → `codegen: 重跑 63 条、跳过 0 条`（`codegen 38.8s / 合计 101.0s`），按 SHA256 原样还原该文件后 → `重跑 1 条、跳过 62 条`（那一条是依赖构建树里 `cmake_pch.hxx` 的 RenderInterface 行，见 6.1 的"如实记录"），再跑一次 → `重跑 0 条、跳过 63 条`。**改动一行注释就全量重生成这条规则确实还在**，没有被换掉。
  另：还原源码后为了不留下"二进制与源码对不上"，把该文件的时间戳刷新了一次 → 工具重建 → `重跑 63 条、跳过 0 条`（第二次独立证据）。
- [x] 4.4 `--codegen-all` 可强制全量
  **怎么算做完**：带它跑一次，生成段打印"重跑 63 条、跳过 0 条"（贴原文）
  **实测**：`python build.py --config Debug --codegen-all` → `codegen: 重跑 63 条、跳过 0 条`、`[耗时] configure 0.0s / codegen 49.1s / 编译 49.9s / 合计 99.0s`、`BUILD SUCCESSFUL`（账本明明是全绿的，它照样每条都重跑）。

## 5. 读数进文档与收尾

- [x] 5.1 `Tools/CodeGen/README.md` 增加"构建耗时"一节：三档场景（全量 / 改一个被标注的头 / 什么都不改）各给一条可复制命令与当场跑出来的数字，并给出改动前后对照
  **怎么算做完**：README 那一节逐字含这三条命令与它们的实测输出行；`verify_readme.py` → 0 处不符（贴最后一行）
  **实测**：README §六 新增 `### 构建耗时：三档，照着跑就能复现`，三条命令与其 `[耗时]` 行都逐字在里面（`python build.py --config Debug --codegen-all` 命中 1 处、`python build.py --config Debug` 命中 5 处；`codegen 49.1s`、`codegen 5.4s`、`codegen 1.5s` 都在）：
  ① 全量 `--codegen-all` → `重跑 63 条、跳过 0 条`，`configure 0.0s / codegen 49.1s / 编译 49.9s / 合计 99.0s`；
  ② 给 `GPUBuffer.h` 加一行注释 → `重跑 1 条、跳过 62 条`，`configure 0.0s / codegen 5.4s / 编译 55.2s / 合计 60.6s`；
  ③ 什么都不改 → `重跑 0 条、跳过 63 条`，`configure 0.0s / codegen 1.5s / 编译 2.8s / 合计 4.3s`。
  对照写的是**成对读数**：全量 49.1 秒（并发 8）对 149.0 秒（并发压到 1），并说明从前行与行之间是串行的、每类型 3 次调用（63 → 189）。那句没有留档的 469.4 秒没有出现在这一节里，README 里其它地方也不再把它当既成事实。
  另外把同一节下面的"接入清单：四列"改对了：第三列现在是**归属模块目录**（决定编译环境**和**产物落点），第四列是**可选的限定名**（不再是"生成物入库目录"）。
  **读数**：`python Tools/CodeGen/verify_readme.py` → `README: 20 documented command(s) run, 0 mismatch(es)`。
- [x] 5.2 同步另一个 change：`retire-legacy-serde-hash` 里**会被本 change 打破**的判据
  **怎么算做完**：逐条贴出改前 / 改后原文，且改后每条的判据在"产物已出仓"的状态下**仍能变红**：① `3.1` / `3.2`（把 `Tools/CodeGen/generated`、`Interface/RenderInterface/generated`、`CACore/generated` 当扫描目标的逐符号扫描）；② `3.4` 与 `10.3`（打印的"扫描 N 个文件"）；③ `9.4`（"N 与开工时红态那次相同"）；④ `10.4`（"删掉入库生成物后该目标编译失败"）；⑤ `10.6`（"构建失败并给出加 `--commit-generated` 的提示" —— 该开关本次被删，这条判据在终态下只能红，必须重写）。
  ⚠ 不要动 `8.3`：它全文没有"入库"字样，本来就不需要改（对抗审查 2026-10-04）。
  ⚠ 交下来的示例措辞"删掉构建目录里的生成物即构建失败"在现状下**恒真** —— `build.py` 删掉产物后只会让该条重跑并重新生成，构建照样成功。要写成"生成不出该类型（工具非零退出）即构建失败"。
  ⚠ 连带：`Tools/CodeGen/check_legacy_symbols.py` 的 `GENERATED_DIRS` 写死那三个源码树目录，脚本里还明写"不使用 out/build/<cfg>/codegen"，目录一删它直接 `SystemExit(2)`（不是变恒真，是硬错）→ 返工 6.10。
  **改前 → 改后（逐条，原文对照）**：
  ① `tasks.md` 3.1 的判据：改前 `git grep -n -F "::cacore::serializer" -- Tools/CodeGen/generated Interface/RenderInterface/generated CACore/generated … **不许**用 out/build/.../codegen 当扫描目标 —— 那个目录被 gitignore、今天就是零命中，用它当判据等于恒真` → 改后 `-- out/build/x64-debug/codegen … 且 **N MUST NOT 为 0**`，并把那句"不许用 out/build"换成了它今天为什么不再恒真（生成物只活在构建目录；脚本找不到产物目录就退 2）；
  ② 3.2：改前 `限定到上面三个入库目录` → 改后 `限定到构建目录里的生成物 out/build/<预设>/codegen/<模块>/`；
  ③ 3.4：改前 `逐符号扫描（生成物三个入库目录 + 引擎源码后缀白名单）` → 改后 `逐符号扫描（构建目录里的生成物 + 引擎源码后缀白名单）`；
  ④ 8.1：改前 `生成物入库` → 改后 `生成物落在构建目录（out/build/<预设>/codegen/Interface_RenderInterface/）`；
  ⑤ 9.4：改前 `N 与开工时红态那次相同` → 改后 `N MUST NOT 为 0、并与上一次跑同一条命令的 N 相同`（扫描目标变了，N 不可能等于旧读数）；
  ⑥ 10.4：改前 `删掉入库生成物后该目标编译失败（红），恢复后构建成功（绿）` → 改后 `生成不出该类型时（工具非零退出）构建失败并点名类型与退出码（红）；生成成功时该目标编译通过（绿）`；
  ⑦ 10.6：改前 `一条"生成物被真实目标消费"的判据（沿用既有做法：删掉入库生成物即构建失败，并给出加 --commit-generated 的提示）` → 改后 `把某个被标注类型从接入清单里去掉（或让它触发一条拒绝）→ 构建失败并点名类型与退出码；加回去 → 构建成功`；
  ⑧ 10.7：`生成物入库` → `生成物落构建目录，不再"入库"`；
  ⑨ `proposal.md` 与 `specs/codegen-runtime/spec.md` 里同一句扫描目标（`生成物目录（Tools/CodeGen/generated/…）`）→ 改成 `构建目录 out/build/<预设>/codegen/<模块>/`，并把 `N 与开工时那次红态运行的 N 相同` 改成 `N MUST NOT 为 0`（spec 里另加"脚本在找不到任何生成物目录时 MUST 以退出码 2 报错，MUST NOT 把空集当成 0 命中"）。
  **没动的**：8.3（全文没有"入库"字样，本来就不需要改）。每条被改的判据改后**仍能变红**：①③⑥⑦ 的红态在本次实测里都出现过（例：删产物后退出码 6、清单少一行时 `CODEGEN FAILED (1)`）。
  **校验**：`openspec validate retire-legacy-serde-hash` 通过、`openspec validate improve-build-time` 通过；`retire-legacy-serde-hash` 里对旧扫描目标与 `--commit-generated` 的残留命中，只剩上面那两条标注里"改前是什么"的引用。
- [x] 5.3 收尾验证：完整构建 + `CoreTests`
  **怎么算做完**：`python build.py --config Debug` → `BUILD SUCCESSFUL`；`CoreTests.exe` 退出码 0、用例数不少于改前（改前实测 9 条 PASSED，逐条列出）；构建目录里的 189 份产物与**仓库外的快照**逐字节相同 —— 快照路径 `C:\Users\76956\CascadedEngine-generated-snapshot\`，逐文件清单是它下面的 `manifest.sha256`（189 行，整份清单的 SHA256 `96b8c4d704f28296527994a673feb05de6faf64bb8fd59765d7b62f44a0b27e3`），比对时按**相对路径**对（产物现在落在 `out/build/<预设>/codegen/<模块>/`，快照按源码树的目录结构存）。
  ⚠ 拿工作区的入库副本当"改动前"是**无效基准**：那 18 份里有几份本身就落在本批未提交改动里被改过（对抗审查 2026-10-04 实测 `EmitSample.Serialization.h`、`EmitSample.Hash.h`、`Kinds.Serialization.h`、`Kinds.Hash.h` 与 HEAD 不同）。
  **实测**：
  ① `python build.py --config Debug` → `(CMake 配置输入都没动 —— 跳过 configure…)`、`(工具的源文件都没动 -> 跳过 …)`、`codegen: 重跑 0 条、跳过 63 条`、`[耗时] configure 0.0s / codegen 1.2s / 编译 2.3s / 合计 3.5s`、**`BUILD SUCCESSFUL`**；
  ② `CoreTests.exe` 退出码 **0**，**9 条用例全 PASSED**（逐条：`CodeGen TypeRegistry test`、`CodeGen serialization roundtrip`、`CodeGen serialization test`、`CodeGen hash/equality test`、`CodeGen hash test`、`CodeGen container serialization`、`CodeGen nested container + element hash`、`CodeGen runtime read/write`、`CPipelineStateObject migration`），与改前的 9 条一致、无减少；
  ③ 产物与快照：拿 `C:\Users\76956\CascadedEngine-generated-snapshot\manifest.sha256`（189 行）逐条核对构建目录里的产物 → **逐字节相同 189 / 不同 0 / 缺失 0**，构建目录里正好 189 份文件；
  ④ 工作区同时确认：源码树里那六个 `generated/` 目录**一个都不在**，`git status` 里 18 条是**只暂存了的删除**（没有提交）。

## 6. 返工：对抗审查查出的问题（2026-10-04）

> 来源：5 路只读对抗审查 + 逐条证伪，共 20 条结论（9 高 / 8 中 / 3 低）。这里只收"要动手改"的那些；已在 1-5 节就地改掉措辞的，不再重复列。

- [x] 6.1 账本按生成器**实际读过的文件**算"输入"，并盯住包含目录的增删（design D8）
  **怎么算做完**：① `CodeGen.exe` 输出它这一次真正读过的文件名单（libclang 的 `clang_getInclusions`），账本按名单**逐个文件**存内容哈希；② 改一个**不在清单里、但被清单头 include 的**头（例如 `CACore/header/CACore/CAHash.h`）之后，引用它的那几行 MUST 重跑（贴改前"跳过 N 条"与改后"重跑 M 条"两条读数）；③ 在某个包含目录里新加一个同名头、把原来被读到的那个挡住，相关行也 MUST 重跑（贴读数）—— 只看老名单看不出这种情况；④ 反过来，什么都不改时 MUST 仍打印"重跑 0 条、跳过 63 条"（贴原文）；⑤ 这一条 MUST NOT 改变任何产物的字节（贴对拍读数）。
  ⚠ 顺序硬约束：MUST 在 3.3 删掉"入库副本逐字节对拍"**之前**落地 —— 那是当前唯一的兜底。判据 MUST 按内容哈希、MUST NOT 按修改时间（`git checkout`、复制文件都会刷新时间，按时间判会白跑一片）。
  **实测**：账本 52.4 KB → **3600.9 KB**（63 行共 21099 条依赖，其中清单之外的仓库内头 **154 个**，`CACore/header/CodeGenMeta.h` 被全部 63 行读到）。把 `CACore/header/CACore/CAHash.h`（**不在清单里**，被 56 行读到）加一行注释：改动前那次构建是"重跑 0 条、跳过 63 条"，改完是"**重跑 56 条、跳过 7 条**"；原样还原后又是 56/7。在 `D3D12RenderBackend/generated` 里新增一个文件 → "**重跑 11 条、跳过 52 条**"，删掉它 → 又是 11/52（不是 63，说明判据能区分开）。随后不改任何东西 → "重跑 0 条、跳过 63 条"（`codegen 1.4s / 合计 3.8s`）。上面每一次构建都成功，即 63 × 3 份产物与入库副本的逐字节对拍全部通过 —— 产物字节没被这一条改动动到。`CAHash.h` 已按 SHA256 原样还原（`2AB7077C…E8D4`，先用 `Copy-Item` 存到 `%TEMP%`）。
  ⚠ 如实记录：全量重生成之后的**第一次**"什么都不改"的构建会重跑 1 条 —— 那一行依赖构建树里由构建自己重写的 `out/build/x64-debug/Interface/RenderInterface/CMakeFiles/Rendering.dir/cmake_pch.hxx`（RenderInterface 的强制包含），第二次起稳定为 0/63。这是保守判据的正常代价，不是缺陷。
  ⚠ "改之前会漏"没法用现在的代码再测一遍（判据已经改掉）：依据是 2026-10-04 对抗审查独立测得的"63 行里 62 行会被跳过"。
- [x] 6.2 `--out-dir=` 与 `-o` 给空值时按"缺值"处理
  **怎么算做完**：`CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-all --out-dir=` MUST 非零退出（2）并说明目录为空；`-o ''` 同样 MUST 被拒（现在它静默退回标准输出、退出 0）。盘符根 MUST 不再出现 `EmitSample.*.h`（贴命令与退出码；审查时它确实在 `E:\` 根写下了三份、退出码 0）。
  **实测**（用 `subprocess` 传真正的空参数，`-o ''` 这种形态 PowerShell 传给原生程序时会丢掉空串）：`['-o', '']` → **exit 2**，`CodeGen: -o needs a non-empty output path.`；`['--out-dir=', '--emit-all']` → **exit 2**，`CodeGen: --out-dir needs a non-empty directory name.`；`['--out-dir', '', '--emit-all']` → **exit 2**，同一句。三种形态 stdout 都是 0 字节（改之前 `-o ''` 是 exit 0 且把产物打到标准输出）。`E:\` 根现在 `EmitSample.*.h` **0 个**。
- [x] 6.3 把"`--emit-all` 与三次单发射的接受集合不同"写进 README〔`Tools/CodeGen/README.md`〕
  **怎么算做完**：接口那一节逐字写明"`--emit-all` 会让三份产物的拒绝条件**全部**生效，所以标注没带 `serializable` 的类型（退出码 9）、含 `vector<bool>`（13）或指针成员（15）的类型在 `--emit-all` 下会被拒，而这些类型用单发射反射仍能出码"；并贴两条对照命令与退出码（归档夹具 `uisample.h` 的 `NoSer`：`--emit-reflection` → 0、`--emit-all` → 9）。
  **实测**：§二 接口那节新增一段（加粗那句"**`--emit-all` 不是"逐个发射"的同义词：它把三份产物的拒绝条件全部打开**"），并贴了两条**可跑**的对照命令，它们进了 `verify_readme.py` 的自动检查：`… uisample.h NoSer … --emit-reflection -o .review_tmp/readme/no-ser/NoSer.Reflection.h` → `OK exit=0 documented=0`（产物 879 字节）；`… --emit-all --out-dir .review_tmp/readme/no-ser` → `OK exit=9 documented=9`，真实报错是 `CodeGen: refusing --emit-serialization for 'audit_ns::NoSer' — the type's CA_REFLECT marker does not carry the `serializable` option (marker options seen: 'ui')…`，一份产物都不写。文档化的命令数因此 18 → **20**，`README: 20 documented command(s) run, 0 mismatch(es)`。
  **顺带补的一件事**：`--emit-all` 现在会把读过的文件以 `CodeGen deps: <路径>` 打到标准输出（6.1 加的），接口表里那一行已经写明，否则读者会以为那是杂音。
- [x] 6.4 生成段并发里任一行抛异常必须收敛成一条失败
  **怎么算做完**：构造一行抛异常（例如让某一行的产物在比对前消失），构建 MUST 以 codegen 的失败信息结束（**不是**裸 Python traceback），**其余行照常跑完**，账本 MUST NOT 因异常被截断重写（贴改前的 traceback 原文与改后的输出）。
  **怎么造的**：临时往清单加一行 `("CACore/header/Reflection.h", "NoSuchAnnotatedType", "CACore")`（这个头没有任何标注，工具走"空名单"路径：**退出码 0、一份产物都不写**），并在 `CACore/generated/` 放三份同名的假入库副本 —— 不放副本的话先撞上"没有入库副本"那条检查，走不到出事的那一行。第一次这么造**没复现**（报的是三条 `no committed copy`），加上假副本才复现，这一步记在这里免得后人以为一次就成。
  **改前（裸 traceback）**：`File "...\build.py", line 664, in run_row` / `if out_file.read_bytes() != golden.read_bytes():` / `FileNotFoundError: [Errno 2] No such file or directory: '…\codegen\CACore\NoSuchAnnotatedType.Reflection.h'`；同一份日志里 —— **进度行只有 11 条**（64 行里只开了 11 行，其余被 `Executor.map` 取消）、`CODEGEN FAILED` 块 **0 个**（用户看不到"哪一行、为什么"）。
  **改后**：`run_row` 把一切异常收敛成一条失败（`run_row_inner` 保留原逻辑），`main` 里也兜住 `codegen()`；同一份探针下 —— 进度行 **64 条**（其余行全部跑完）、日志里 `Traceback` 出现 **0** 次、末尾打出 `CODEGEN FAILED (1) — build not reported as successful`，失败原文是 `CodeGen 'NoSuchAnnotatedType' 这一行出错：FileNotFoundError: …（工具退出码 0 却没写出产物时多半是这个类型的标注被删了）`。
  **账本**：探针状态下账本仍是 **63** 行（那一行没被记账 ⇒ 下轮会重跑，其余 63 行照旧在），不是被截断重写。
  **收尾**：探针行与三份假副本都已删除（`NoSuchAnnotatedType` 在 `build.py` 里命中 0、`CACore/generated` 里 0 个残留），随后一次构建 `codegen: 重跑 63 条、跳过 0 条`、`BUILD SUCCESSFUL`（全量重跑是因为删掉那三份假副本又改动了 `CACore/generated` 的目录清单 —— 正是 6.1 的判据在起作用）。
- [x] 6.5 `verify_readme.py` 增加文本比对〔`Tools/CodeGen/verify_readme.py`〕
  **怎么算做完**：对每个示例取真实 stdout（或 `-o` 产物）里的关键行（`#include`、`namespace`、特化名、字段行）与 README 片段逐字比；把 README 人为改坏一处（例如 `CodeGenHash.h` 改回 `Hasher.h`）时脚本 MUST 报 MISMATCH 并退非零（贴红态与还原后的绿态）。
  **实测（红态 → 绿态）**：把哈希产物示例里的 `#include "CodeGenHash.h"` 改回 `#include "Hasher.h"` → `MISMATCH  exit=0 documented=0 text=4  CodeGen.exe … reflect.h Player … --emit-hash` / `README shows a line the tool does not produce: #include "Hasher.h"` / `README: 18 documented command(s) run, 1 mismatch(es)`、**脚本退出码 1**；改回 `CodeGenHash.h` → `README: 18 documented command(s) run, 0 mismatch(es)`、退出码 0。
  **覆盖情况**：18 条命令里 **16 条**现在有文本比对（`text=` 计数 1–31 行不等）；两条没有 —— 写进 `-o` 的那条（比对的是产物文件内容，README 那块是说明文字）与 `--emit-all` 那条（README 那块是文件清单，属于散文）。
  **顺带查出的两处 README 陈旧**（已就地改掉，属 6.7 的同一类，记在这里以免丢失）：① §三 的 `Player` 字段表示例只写到 `  field id 0 int 17`，真实输出后面还有 13 项容器标志；② 示例 8 的 `unresolved include (fatal): …CodeGenMeta.h:16:10` 行号已经漂到 `14:10`，而且工具现在会多打两行"前几条错误（不在被解析的头里…）"。
  **实现要点（写下来免得后人踩）**：命令与它文档化的输出块按**块内第几条命令 ↔ 第几个后续块**配对（§三 一次列四条命令、产物在很远的下方，只按"命令后面第一个块"会把 `--emit-hash` 对到反射示例上）；比对前把空白折叠 —— README 会把长报错**折行**排版、字段表用空格对齐，而工具是一行单空格，按原样比会把每个折行示例都误报成不符。
- [x] 6.6 1.1 的字节对拍扩到容器 / 包装器 / 嵌套容器类型
  **怎么算做完**：至少再取三个形状不同的清单类型（一个含 `vector`/`map`、一个包装类型如 `Holder`、一个嵌套容器如 `Outer`），各自用 `--emit-all` 与三次单发射对拍，产物逐字节相同（贴对拍读数）；并列出 63 行里**还剩哪些**发射分支没被任何对拍覆盖。
  **对拍读数**（脚本 `%TEMP%\ibt66_probe.py`，对拍前先删掉两边的同名产物；环境取自 `build.py` 自己推导的那份）：

  | 类型 | 形状 | Reflection | Serialization | Hash |
  |---|---|---|---|---|
  | `Containers` | 含 `vector`/`map`/`set` | 相同 6958 B | 相同 2531 B | 相同 2477 B |
  | `Holder` | 包装类型（`NameHash`/`PathHash`/`HashObj`） | 相同 1729 B | 相同 1486 B | 相同 1218 B |
  | `Outer` | 容器里装结构体、结构体里还有无序容器 | 相同 1019 B | 相同 1306 B | 相同 1013 B |
  | `FixedArrays` | 定长数组 / C 数组 | 相同 1510 B | 相同 1414 B | 相同 1155 B |
  | `ShaderStructData` | 字符串 / 枚举 / 结构体混合 | 相同 2712 B | 相同 1908 B | 相同 1781 B |

  合计 **15 份产物，15 份逐字节相同**。
  **还剩哪些发射分支没被覆盖**（扫全部 63 行的 189 份产物，数每个记号出现在多少份里）：

  | 记号 | 出现份数 |
  |---|---|
  | `TypeKind::Int` / `UInt` / `Struct` | 16 / 43 / 53 |
  | `TypeKind::Enum` / `Float` / `Bool` / `Array` / `Double` | 20 / 5 / 5 / 3 / 2 |
  | `TypeKind::Opaque` | **0** |
  | `FieldFlags::Container` / `Unordered` / `Wrapper` | 34 / 7 / 5 |
  | `FieldFlags::HasOpaqueElement` | **0** |
  | 哈希：`cgEqualValue` / `cgEqualContainer` / 浮点 NaN 分支 / `cgHashContainerValue` | 56 / 34 / 4 / 65 份 |

  **`TypeKind::Opaque` 不是"不可达"，是真没覆盖**：写一个含 `int&` 成员的头，工具**接受**（退出码 0）并把它发成 `TypeKind::Opaque`。探针头本体：`#pragma once` / `#include "CodeGenMeta.h"` / `CA_REFLECT(serializable)` / `struct ProbeRef { int a; int& r; float f; };`，命令 `CodeGen.exe <该头> ProbeRef -I CACore/header -I Tools/CodeGen/clang_shim --emit-all --out-dir <目录>`，产物里那一行是
  `{ "r", "int &", offsetof(ProbeRef, r), TypeKind::Opaque, FieldFlags::HashInclude, nullptr, nullptr },`。
  63 行里没有任何一行有引用成员，所以这条分支从来没被端到端验过。（顺带记一句：`cgWriteValue` 对引用是按 `remove_cvref_t` 之后的 `int` 走标量路径写的，也就是"表里记 Opaque、运行时按标量写"这两件事对不上 —— 这属于新运行时的语义问题，归 `retire-legacy-serde-hash`，不在这里处置。）
  **`FieldFlags::HasOpaqueElement` 大概是不可达**：它只在"容器的元素是不透明类型或托管指针"时置位，而那种形状在发射之前就被退出码 15 拒了。**这一条我没有单独构造验证，标为未证实。**
  **顺手清掉的垃圾**：`out/build/x64-debug/codegen/Tools_CodeGen/` 里还留着 6.8 改归属之前的 6 份旧产物（内容与 `Test_CoreTests/` 那 6 份逐字节相同），已删除；构建目录里的产物现在是 **189 份**（63 × 3），不多不少。
- [x] 6.7 README 的示例文本与真实输出对齐〔`Tools/CodeGen/README.md`〕
  **怎么算做完**：逐处贴改前 / 改后原文：序列化产物（`Serialization.h`→`CodeGenSerialization.h`、`::cacore::serializer`→`::cacore::codegen::writer` + `cgWriteValue`）、哈希产物（`Hasher.h`→`CodeGenHash.h`、`namespace cacore`→`cacore::codegen`、`custom_hash_trait`→`CodeGenEqual` / `cgHashContainerValue`）、反射字段行漏掉的 `, nullptr, nullptr`、字段表那一长串 `container=… elemManagedPtr=` 标志；改完 `python Tools/CodeGen/verify_readme.py` MUST 0 处不符。
  **改前 → 改后（逐处）**：
  ① `#include "Serialization.h"` → `#include "CodeGenSerialization.h"`；
  ② `::cacore::serializer<ByteBuffer> s(buffer);` / `s.serialize(obj.id);` → `::cacore::codegen::writer<ByteBuffer> w(buffer);` / `cgWriteValue(w, obj.id);`（并补上真实的 `deserialize(Obj&, ByteBuffer const&, std::size_t&)` 三段签名）；
  ③ `#include "Hasher.h"` → `#include "CodeGenHash.h"`；`namespace cacore` → `namespace cacore::codegen`；原来只画了 `custom_hash_trait` 一个特化 → 补齐**两个**（先 `CodeGenEqual<>`、再 `custom_hash_trait<>`，哈希体是 `cgHashContainerValue(h, obj.id);`）；
  ④ 反射字段行 `{ "id", "int", offsetof(game::Player, id), TypeKind::Int, FieldFlags::HashInclude },` → 末尾补 `, nullptr, nullptr`（`register.h` 的 `Inventory` 两行同样补）；
  ⑤ 字段表示例 `  field a 0 int 17` → 补上真实的 13 项容器标志那一长串，并写明"每一项每条字段都会打，不是只在有容器时才出现"；
  ⑥ `产物里每个字段一行 s.serialize(obj.<成员>);` → `产物里每个字段一行 cgWriteValue(w, obj.<成员>);`；
  ⑦ 退出码表里删掉 `| 12 | 容器的元素是**结构体**…` 这一行（`Select-String 'return 12;' -- Tools/CodeGen/CodeGenMain.cpp` 命中 **0**，工具根本产不出 12）；形状表里"原退出码 12，已撤除"那句保留；
  ⑧ 退出码 2 那行补上 6.2 的两种空值形态（`--out-dir=` / `-o ''`）。
  **读数**：`python Tools/CodeGen/verify_readme.py` → `README: 18 documented command(s) run, 0 mismatch(es)`。上面每一段改后文本都是**当场跑出来的输出照抄**（`reflect.h` 的 `Player` 三个发射器、`register.h` 的 `Inventory`、`TestStruct.h` 的 `CodeGenSample`、`TestEmit.h` 的 `EmitSample`）。
- [x] 6.8 把清单头两行的归属改成真正编译它们的模块，删掉临时借用表（design D1）
  **怎么算做完**：① `build.py` 里的 `CODEGEN_ENV_FALLBACK` MUST 被删除（`git grep -n -F "CODEGEN_ENV_FALLBACK" -- build.py` 命中 0 行）；② 清单里 `TestEmit.h` 与 `TestKinds.h` 两行的归属目录 MUST 是 `Test/CoreTests`，全量生成 MUST 不再出现 `no compile env for module`，并 MUST 打印这个模块推导出的环境摘要（贴两行读数）；③ 产物落点随之从 `Tools/CodeGen/generated/` 变成 `Test/CoreTests/generated/`（出仓后是 `out/build/<预设>/codegen/Test_CoreTests/`），消费方的 include 路径同步改（3.1 的站点之一）；④ 六份产物 MUST 与**改动前工作区里的那六份**逐字节相同 —— 基准是改动前 `Tools/CodeGen/generated/` 里的文件（它们是借用环境下生成的），做之前先把它们的 SHA256 抄下来、改完再对一遍（贴两组读数）。**不许拿 HEAD 的字节当基准**：那六份里有四份早在本次会话的另一个 change 里就被重新生成过（新产物 include 的 `CodeGenSerialization.h` 在 HEAD 上根本不存在），拿 HEAD 比是把一个做不到的判据写成验收；⑤ MUST 确认取的是更具体的 `Test/CoreTests` 而不是整个 `Test`（后者有两套宏会被拒绝，贴环境摘要行）。
  **实测**：`CODEGEN_ENV_FALLBACK` 在 `build.py` 里命中 **0** 行；全量生成打印 `env 'Test/CoreTests': 10 compile unit(s), 13 include dir(s), 7 macro(s), 0 forced include(s)`，`no compile env` 一次都没出现，`BUILD SUCCESSFUL`；同一次构建 `codegen: 重跑 2 条、跳过 61 条`（就是这两行换了归属键）。"取更具体的 `Test/CoreTests`"这条也实测了：`Test` 整个目录**被拒**（`module 'Test' has 2 different macro sets across its 13 compile unit(s): {-DFMT_SHARED, -DJPH_CROSS_P…}`），`Test/CoreTests` 正常推出 21 个参数。
  **基准对照（④）**：搬之前先把 `Tools/CodeGen/generated/` 六份的 SHA256 抄了下来，搬完 `Test/CoreTests/generated/` 六份与它们**完全一致**（`EmitSample.Reflection` `DFA6F72E…7BAD3`、`Serialization` `C5AF4600…BE52`、`Hash` `B6694D8A…7415`、`Kinds.Reflection` `DEB4D47F…D43A`、`Serialization` `79528C99…9023`、`Hash` `6C047D1D…96FF`），构建目录里新生成的六份也与这三组哈希**逐个相同** —— 换环境没有改动任何产物字节。搬完 CoreTests 仍编得过、跑得过（`CoreTests exit=0`）。
  **连带改动**：`Test/CoreTests/CMakeLists.txt` 的 include 路径从 `${CMAKE_SOURCE_DIR}/Tools/CodeGen/generated` 改成 `${CMAKE_SOURCE_DIR}/Test/CoreTests/generated`（就是 3.1 的第 6 处站点，3.1 再把它指到构建目录）；源码树里 `Tools/CodeGen/generated/` 已删除（产物搬走，不留旧副本）。
- [x] 6.9 3.2 之前的产物快照
  **怎么算做完**：在删除源码树 `generated/` 之前把 189 份复制到仓库外，并在 5.3 里点名该路径；贴复制命令与"快照 189 份，其中能从版本库取回的有多少份"的读数。
  **判据改了一处（留对照）**：原文写的是"其中 **18** 份与 `git show HEAD:` 逐字节相同"。**改后**：贴"快照 189 份 / 版本库里同名可取的 12 份 / 逐字节与 HEAD 相同 0 份"。**依据（实测）**：① 6.8 之后那 6 份夹具产物的路径从 `Tools/CodeGen/generated/` 变成了 `Test/CoreTests/generated/`，所以"同名可取"的是 **12** 份不是 18 份；② 这 12 份**没有一份**与 HEAD 相同 —— 本次会话里 `retire-legacy-serde-hash` 已经重写过发射器（新产物 include 的 `CodeGenSerialization.h` 在 HEAD 上都不存在），拿 HEAD 比本来就做不到。这不是放松：新判据多要求一份**逐文件清单**，5.3 要拿它核对，比"抄一个数"更严。
  **实测**：快照目录 `C:\Users\76956\CascadedEngine-generated-snapshot\`（仓库外），六个目录逐个复制 —— `CACore 6 / D3D12RenderBackend 33 / Interface/RenderInterface 36 / ShaderCompilerSlang 72 / Test/CoreTests 6 / VulkanRenderBackendNew 36`，**合计 189 份**，快照里数出来也是 189 份。对照结果：版本库里有同名文件的 **12** 份、与 HEAD 逐字节相同的 **0** 份。
  **清单**：`manifest.sha256`（189 行，每行"sha256 + 相对路径"），整份清单的 SHA256 = `96b8c4d704f28296527994a673feb05de6faf64bb8fd59765d7b62f44a0b27e3` —— 5.3 用它逐条核对产物。
- [x] 6.10 `check_legacy_symbols.py` 的扫描目标随产物出仓更新〔`Tools/CodeGen/check_legacy_symbols.py`〕
  **怎么算做完**：`GENERATED_DIRS` 改成新落点（`out/build/<预设>/codegen/<模块>`，模块名 = 归属目录把 `/` 换成 `_`），删掉"不使用 out/build"那句注释并写清新形式下的恒真风险；跑一次 MUST 打印"扫描 N 个文件、命中 N 行"，且 **扫描的文件数 MUST NOT 为 0**（空集就是恒真；贴读数）。
  **判据改了一处（留对照）**：原文写的是"命中 **0** 行"。**改后**：只要打印出"扫描 N 个文件、命中 M 行"且 **N ≠ 0**。**依据**：命中数是不是 0 属于 `retire-legacy-serde-hash` 那套判据的验收（现在它是 **62** —— 那份 change 还没做完，`Reflection.h` 还在），本 change 只负责"产物搬走之后这条扫描不会变成扫空集"。**不是放松**：原来那句"0 行"在本 change 里根本判不了（另一份 change 未完成），而新判据额外要求 N 与产物数对得上、并且下面用注入法证明了它扫得到生成物。
  **实测**：扫描根现场枚举为 `out\build\x64-debug\codegen`；`扫描 659 个文件、命中 62 行（未豁免）`，其中**生成物 189 份 + 仓库源码 470 份**（脚本自报的拆分，与构建目录里 63 × 3 = 189 份产物一致）。
  **"不是空集"的证明（注入 → 收回）**：往 `out/build/x64-debug/codegen/CACore/Holder.Reflection.h` 追加一行含 `cacore::serializer` 的注释 → 扫描命中变 **63** 行，且新命中就打在 `out/build/x64-debug/codegen/CACore/Holder.Reflection.h:31`（**生成物一侧真的被扫**，不是只扫源码）；还原该文件后回到 **62** 行、扫描文件数仍是 659。
  **按新写法写清的恒真风险**：换到构建目录之后，"目录被 gitignore、工具静默不生成时它恰好是空的"这个风险没了，换成"**根本没构建过 / 生成失败时目录不存在**"——所以 `generated_roots()` 在一个产物目录都找不到时直接报 `ERROR: 找不到任何生成物目录…先跑一次 python build.py --config Debug` 并**退出码 2**，不是当成"0 命中"。
