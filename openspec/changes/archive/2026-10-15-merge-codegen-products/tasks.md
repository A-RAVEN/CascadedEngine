# 任务清单

判据都要能在"问题还在"的状态下变红。构建一律 `python build.py`（本项目唯一合法入口），
工具是 `out/build/x64-debug/bin/CodeGen.exe`，解析环境由 `build.py` 自己推导。

## 0. 前置（不做完不许动工具）

- [x] 0.1 确认隔壁那份 change（`clear-codegen-parse-blockers`）的改动**已提交或已备份**：它与本 change 要改的
      文件重叠（`GPUPipelineInstance.{h,cpp}`、`GPUGraph.h`、`CPipelineStateObjectAssertions.cpp`、
      `GeneratedHashTest.cpp`、`build.py`、`Tools/CodeGen/README.md`）。
      **怎么算做完**：给出证据 —— `git status --porcelain` 里那批文件的状态，或者一份放到 `%TEMP%` 的备份路径与件数。
      ⚠ 没做这一条就往下走，一旦中途按"回滚"操作会把上一份成果一起抹掉（design D9）。
      **实测读数**：那 7 个文件全部在提交 `9955a9c` 里，`git status --porcelain -- <那批文件>` 为空；
      当时整个工作区只有本 change 的目录未跟踪（`git status --porcelain` 计 1 条）。

## 1. 固化基线（动工具之前）

- [x] 1.0 **核对被本改动推翻的基线条文**：`improve-build-time` 在 2026-10-10 归档之后，基线里多了两条明文写着
      "一次调用出三份产物"的要求（`openspec/specs/codegen-framework/spec.md` 的「一个类型一次调用出三份产物」、
      `openspec/specs/build-throughput/spec.md` 的「一个类型只解析一次，一次出三份产物」）。本 change 用
      **RENAMED + MODIFIED** 改它们。
      **怎么算做完**：① `git grep -n "三份" -- openspec/specs/` 列出全部残留，逐条说明"这条由本 change 的哪个 delta 改掉"
      或"这条讲的是别的东西（例如三节各自的拒绝条件）不动"；② 两条 RENAMED 的 FROM 标题与基线**逐字相同**
      （`git grep -n -F "<FROM 标题>" -- openspec/specs/<能力>/spec.md` 命中 1 行），TO 标题与 MODIFIED 里的标题逐字一致；
      ③ `openspec validate merge-codegen-products` 通过。
      **核对结果**：`git grep -n "三份" -- openspec/specs/` 命中 16 行 —— A 组 6 行
      （`codegen-framework`）、B 组 4 行（`build-throughput`）由本 change 的 delta 改掉，
      C 组 6 行（`codegen-runtime` 里"旧的三份头"＝`Reflection.h`/`Serialization.h`/`Hasher.h`）与
      `codegen-framework` 第 384 行（"三份产物的拒绝条件"＝三节各自的拒发码）**不动**。
      两条 RENAMED 的 FROM 标题在基线里各命中 1 行、TO 与 MODIFIED 标题逐字相同（脚本比对 `True`）。
      **顺带查出并修掉两处同类失效**（都写进 `evidence/baseline-products.md` 的「1.0」一节，含逐行对拍）：
      ① `codegen-framework` 的「空输出目录按缺值拒绝」场景里「会让三份产物落到盘符根且退出码为 0」
      → 「会让产物落到盘符根且退出码为 0」（要求本身一字未动）；
      ② `build-throughput` 的「生成段并行执行多个类型」整条改成 **MODIFIED**，只动两处 ——
      例子 `#include "UniformElement.Serialization.h"` → `#include "UniformElement.codegen.h"`，
      以及「清单里有 48 份生成头被别的头直接 `#include`」→「（`Compiler.h` 一个头就是 24 份，改造前同一个头是 48 份）」
      （48 是 `Compiler.h` 一个头的读数，合并成一份后同一个头是 24）。
- [x] 1.1 把当前构建目录里的 228 份产物**保持目录结构**复制到 `%TEMP%\codegen_baseline_merge\`，
      并记下取数状态：HEAD 提交号、`git status --porcelain | Measure-Object` 的条数、件数与总字节。
      **怎么算做完**：贴出四条读数（改造前实测：228 份 / 386,180 字节 / 7,497 行；HEAD 与改动条数当场取），
      且复制后的件数与原目录一致；写进 `evidence/baseline-products.md`。
- [x] 1.2 复核 design D3 的前提：接入清单 **76 行**逐行都带 `serializable`。
      **怎么算做完**：**按清单里的头文件逐个统计**（不是全仓 grep —— 全仓会把夹具、归档与别的 change 的文档
      一起数进来，实测全仓 `git grep -c` 合计 151 处、限定到清单那 18 个头才是 76），贴出命令与逐文件计数；
      再加一条更强的事实：`python build.py --config Debug --codegen-all` 里 76 行**全部 exit 0**
      （若某行没带 `serializable`，它会在 `--emit-all` 下以退出码 9 失败）。
- [x] 1.3 记**改前读数**，写进 `evidence/baseline-products.md`：
      ① 产物件数/字节/行数；② 源码 include 行数（按后缀分）；③ 消费文件的节种类分布；④ **十条拒绝码的改前实测**
      （2 / 5 / 6 / 9 / 10 / 13 / 14 / 15 / 16 / 17，各贴退出码与 stderr 原文）；⑤ 扫描器的读数。
      **怎么算做完**：五组读数贴出来（改造前实测：228 份 / 386,180 字节 / 7,497 行；include **267** 行 =
      Reflection 79 + Serialization 88 + Hash 100，分布在 **26** 个文件；去重后的"文件 × 类型"**137** 行；
      节种类分布 **12 个三节 / 4 个两节 / 1 个混合 / 9 个只含一节**；扫描器退出码 0、命中 0 行）。
      ⚠ ④ 是"逐条与改造前相同"这句判据的参照侧，缺了它那条判据无法判定。
      ⚠ 原稿写的 270 / 80 / 89 / 101 / 138 与"12-6-8"是施工**中途**取的数，收尾时没重量，实测对不上
      （差 3 行 / 1 行；根因是 `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`
      当时还挂着 `ViewRectData` 的三行 include，收尾后该文件是 27 行 / 9 个类型）。已按实测改正，
      改前 → 改后与根因见 `evidence/baseline-products.md` 的「1.3」一节。

## 2. 工具：一个类型一份产物

- [x] 2.1 改 `Tools/CodeGen/CodeGenMain.cpp`：`--emit-all --out-dir` 下按 `<类型名>.codegen.h` 写**一份**文件，
      节顺序反射 → 序列化 → 哈希，文件头注释 / `#pragma once` / 运行时头 include 各一次，每节保留自己的注释行。
      **怎么算做完**：单跑一个类型，目录里只有那一份；贴出文件列表 + 文件头 12 行 + 三节的注释行；
      并用一条命令核"顺序与去重"（例如统计 `#pragma once` 出现次数 = 1、`CodeGenSerialization.h` 出现次数 = 1）。
- [x] 2.2 标准输出与用法文本：`CodeGen wrote: ` 由三行变一行；`usage:` 两行同步；**`--emit-all` 的报错文本**
      里"一次调用写三份、三个旧后缀"那句也要改。
      **怎么算做完**：贴出单跑的完整 stdout（`CodeGen wrote: ` 恰好一行）与改后的 usage / 报错文本原文。
- [x] 2.3 **228 次逐节对拍**（核心不变量）：对 76 个类型逐一用 `-o <路径>` 单节输出，与 1.1 的基线逐字节比较。
      **怎么算做完**：`-o` 的输出**写到与基线无关的另一个 `%TEMP%` 目录**（写进基线目录就是自己跟自己比，
      这条判据就废了）；贴出「228 次比较、全部逐字节相同」的读数与所用命令；任何一次不同就停下查原因。
- [x] 2.4 拒绝语义逐条不变：2 / 5 / 6 / 9 / 10 / 13 / 14 / 15 / 16 / 17。
      **怎么算做完**：十条读数逐个贴出来，**并与 1.3④ 的改前读数逐条对照**；每条还要核**报错理由**（stderr 原文）
      而不只是退出码 —— 否则"跑了个别的错误也返回同一个码"会被当成通过；每条都**没写出文件**。
- [x] 2.5 搭配关系不动：`--emit-reflection --out-dir <目录>` 仍然是退出码 2（工具原话
      `--out-dir only applies together with --emit-all; use -o <file> for a single emitter.`）；
      单节输出仍然走 `-o`，且 `-o` 的产物与 1.1 的基线逐字节相同。
      **怎么算做完**：两条读数 —— 退出码 2 那一条（贴 stderr），以及 `-o` 单节输出与基线某个文件 `Get-FileHash` 相同。

## 3. 构建脚本与旧产物清理

- [x] 3.1 `build.py`：`CODEGEN_PRODUCTS` 从三个后缀改为一份；`outputs` 映射、占位文件、`absent` 核对、
      每份存在性检查、账本 `codegen.stamp.json` 的 `out` 映射按一份改；失败信息保持原样。
      **怎么算做完**：`python build.py --config Debug --codegen-all` → 76 行逐条 exit 0、`CODEGEN FAILED` 0 次、
      `BUILD SUCCESSFUL`；贴出账本里一行的 JSON 片段（只有一份产物）。
      另加一条**防"空检查"**的实测：把产物名单里的键名故意改错一个字，构建必须仍然失败或报出"没写出产物"
      （若改错键名后 76 行照样 exit 0，说明核对被改成了空检查）。
- [x] 3.2 清理覆盖**所有预设**：生成段跑之前清掉 `out/build/*/codegen/**/<类型名>.{Reflection,Serialization,Hash}.h`。
      **怎么算做完**：**每个存在的预设目录各造一个**旧命名文件（x64-debug 与 x64-relWithDebugInfo 都要），
      跑一次构建后它们**都消失**（贴出每个目录的前后两次 `Test-Path`）；并贴出"所有预设里旧后缀文件数 = 0"的读数。

## 4. 消费点（一个类型一行 include）

- [x] 4.1 改那 **12 个含三节**的文件（各删两行、每个类型留一行）：`CACore/private/TestWrapperAssertions.cpp`、
      `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`、
      `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h`、
      `Interface/RenderInterface/header/{CPipelineStateObject,CVertexInputDescriptor,GPUGraph,ShaderProvider,ShaderResourceHandle,TextureSampler}.h`、
      `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、`Test/CoreTests/FriendLikeTest.cpp`、
      `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h`。
      **怎么算做完**：逐文件贴出 include 行数（改前 → 改后），例如 `Compiler.h` 那类多类型头是 48 → 24。
- [x] 4.2 改那 **5 个含两节**的文件（每个类型删一行）：`Interface/ShaderCompiler/header/Compiler.h`（48 → 24）、
      `Interface/RenderInterface/private/TestContainerAssertions.cpp`（7 → 4；它 4 个类型里有 3 个是两节，
      `Containers` 只有一节，那一行只改名不删行）、
      `Test/CoreTests/{GeneratedHashContainerTest,PipelineStateObjectTest,ResourceBatchTest}.cpp`
      （4 → 2、12 → 6、2 → 1）。
      **怎么算做完**：逐文件贴出 include 行数（改前 → 改后），与上面括号里的读数逐个对上。
      ⚠ 原稿把 `ShaderCompilerSlang/private/ShaderStructsCodeGenAssertions.cpp` 算进"两节"，
      实测它 24 行 / 24 个类型、**每个类型只有一节**（24 行全是 `.Reflection.h`），
      按动作属于"只改名字"，已移到 4.3；原稿的"6 个"因此变"5 个"。
- [x] 4.3 改那 **9 个只含一节**的文件（只改名字，每个文件的行数不变）：
      `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`、
      `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp`、
      `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp`、
      `ShaderCompilerSlang/private/ShaderStructsCodeGenAssertions.cpp`（24 行，从 4.2 挪来）、
      `Test/CoreTests/{CodeGenKindsTest,EmitReflectionTest,GeneratedContainerTest,GeneratedHashTest,GeneratedSerializerTest}.cpp`。
      **怎么算做完**：逐文件贴出 include 行数（改前 → 改后，两边相同）—— 只有名字变、没有行被删。
- [x] 4.4 复核：源码树里旧后缀 include **零命中**、且终态行数符合预期。
      **怎么算做完**：`git grep -n -E 'include ".*\.(Reflection|Serialization|Hash)\.h"' -- '*.h' '*.cpp'` → **0 命中**
      （命令限定在源码树的两个后缀；`openspec/` 归档与文档/脚本里的历史引用不算，它们要点名排除而不是假装不存在）；
      并贴出终态 include 行数 = **137**（"文件 × 类型"一行；改造前 267 行）。
      再跑一次构建 —— 漏改的会在解析/编译期当场失败。

## 5. 收尾验证与文档

- [x] 5.1 `python build.py --config Debug` → `BUILD SUCCESSFUL`，codegen 段 76 行逐条 exit 0。
      **怎么算做完**：贴出 codegen 合计读数与构建结论。
- [x] 5.2 `out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED` **12** 行、`FAILED` 0。
      **怎么算做完**：贴出三个数。
- [x] 5.3 两个后端 tester 各跑一次（vulkan / d3d12 `--headless 10`），判据是 `test_output/result.json` 里
      **8 条 status 都是 pass**（不是退出码 —— `Main.cpp` 的 `main` 以 `return EXIT_SUCCESS;` 结束）。
      **怎么算做完**：两次各留一份 `result.json` 快照，贴出条数与 pass 数。
- [x] 5.4 `python Tools/CodeGen/check_legacy_symbols.py` → 退出码 0、命中 **0 行**。
      **怎么算做完**：贴出它自己那行读数；⚠ 这个脚本扫的是"源码 + 构建目录"，它打印的是**文件数**而不是产物件数，
      不许拿它的数与"228 → 76"并排当同一件事说。
- [x] 5.5 把被本改动推翻的既有判据改掉：该 change 已经归档，探针现在住在
      `openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_parse_gate.py`；
      把里面逐字写着 `PRODUCTS_EXPECTED = 3` 的那一行改成一份，文件里其它"三份"字样同步
      （文档注释里"各写出 3 份产物"、`CONTROLS` 上方那句"不产出 3 份产物"）。
      **怎么算做完**：改后原样跑该探针，绿态判据不再报"只写出 1 份产物（应为 3 份）"（贴出运行读数）。
      ⚠ **归档把它的路径深度改了**：脚本里逐字写着 `REPO = Path(__file__).resolve().parents[4]`，
      那层数是按归档前的位置（`openspec/changes/<change>/evidence/`）写的，归档后同一个表达式落到
      `openspec/` 而不是仓库根。跑之前必须把这层数改回来、并同步文档注释里的两条用法路径，
      否则它连目标头都找不到 —— 这是归档造成的，不是本改动。这一条与本条的判据一起改，
      改前 → 改后写进 `evidence/baseline-products.md`。
- [x] 5.6 文档与用户可见文本：`Tools/CodeGen/README.md`（接口表、`--out-dir` 写几个文件、产物命名、
      "三份产物"字样、§六 构建期怎么用它、示例里出现的产物名）、工具里 `--emit-all` 的报错文本、
      `CLAUDE.md`（先核有没有提产物命名，没提就记一句"核过、无需改"）、
      仓库脚本 `.claude/workflows/scripts/verify-codegen-tasks-order.js`（它仍以 `EmitSample.Reflection.h` 为动作对象）。
      **怎么算做完**：`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`；并逐条 grep 新写进去的锚点
      （锚点要具体，例如 `<类型名>.codegen.h` 与"只写一份"的完整搭配，**不许**用"一份"这种会恒真的词）。
      ⚠ `verify_readme.py` 只跑示例、不比对描述性段落，所以它通过**不等于**文档改对了。
- [x] 5.7 报告（不改）：`~/.dsh/AGENTS.md` 里那张对照表仍逐字写着 `<类型>.{Reflection,Serialization,Hash}.h`，
      它在仓库之外，本 change 只报告给用户、不擅自改。
      **怎么算做完**：贴出该行原文与"建议改成什么"，其余不动。
- [x] 5.8 实物读数复核：构建目录里 `*.codegen.h` **76 份**、所有预设里旧后缀 **0 份**、源码 include **137 行**。
      **怎么算做完**：三组读数贴出来，与 1.3 的改前读数并排对照。
- [x] 5.9 `openspec validate merge-codegen-products` → **valid**；工件点名的文件都存在。
      **怎么算做完**：贴出 validate 输出与文件存在性检查。
- [x] 5.10 **末条：编译验证与修复** —— 再跑一次 `python build.py --config Debug`；若失败，分析并修复到
      `BUILD SUCCESSFUL`（修根因，不许把判据改松）。
      **怎么算做完**：最后一次构建输出为 `BUILD SUCCESSFUL`。

## Review Log

**这一轮关掉了什么**（26 条全勾，读数都贴在 `evidence/baseline-products.md`）

- **工具**（`Tools/CodeGen/CodeGenMain.cpp`）：`--emit-all --out-dir` 从"写三份 `<类型>.<发射器>.h`"改成
  "写**一份** `<类型名>.codegen.h`"（三节固定顺序 反射 → 序列化 → 哈希，文件头注释 / `#pragma once` /
  三个运行时头的 include 各一次），三节正文由**同一段发射代码**产出；用法文本与 `--emit-all` 的报错文本同步；
  **判定逻辑与十条拒绝码一字未动**（2.4 退出码与 stderr 逐字对照，10/10 相同）。
- **构建**（`build.py`）：`CODEGEN_PRODUCTS = ["codegen"]`、占位与逐份核对按一份、账本 `out` 一份
  （76 行逐行只有一项）、新增"清掉**所有预设**里旧命名的产物"（实测 230 份被清、三个预设都覆盖）。
- **消费点**：26 个文件 **267 行 → 137 行** include，旧后缀在源码树零命中（不是 include 的三处历史提及也一并改掉）。
- **归档探针**：`PRODUCTS_EXPECTED` 3 → 1，另修归档造成的 `REPO = …parents[4]`（归档后落到 `openspec/`）
  与两条用法路径；改后 `--expect green` 4/4 目标 + 3/3 对照全绿。
- **文档**：`Tools/CodeGen/README.md`（接口表两行、示例 10、账本段、接线第三步、构建耗时补一组读数）、
  `.claude/workflows/scripts/verify-codegen-tasks-order.js` 的动作对象；`CLAUDE.md` 核过、无需改。

**交付物**（本轮真正改动的文件）：`Tools/CodeGen/CodeGenMain.cpp`、`build.py`、26 个消费点、
`Tools/CodeGen/README.md`、`.claude/workflows/scripts/verify-codegen-tasks-order.js`、
归档的 `probe_parse_gate.py`，以及本 change 的工件与证据文件 —— 共 **35 个跟踪文件**（另加一份新证据文件）。

**净进展**：交付物是**代码**，本轮改了 30 个代码 / 脚本文件（`python Tools/spin_check.py` 式的固定后缀清单内），
不是文档轮。

**踩到并就地修掉的四处**

1. 我在 README 里把示例 6 的退出码 10 报错原文"顺手"改成了 `all three sections`，而工具那句并没有改 ——
   `verify_readme.py` 当场报 1 处不符。改回**照抄**。教训：报错文本属于"拒绝语义不动"（2.4 逐字对照过），
   文档只能抄，不能顺手润色。
2. 工件里的 `270 / 80 / 89 / 101`、`138`、"12-6-8"是施工**中途**取的数，实测是 `267 / 79 / 88 / 100`、`137`、
   "12 个三节 / 4 个两节 / 1 个混合 / 9 个单节"。已按实测改正（tasks / design / proposal 五处），
   根因是 `CPipelineStateObjectAssertions.cpp` 收尾时比中途少了 `ViewRectData` 的三行。
3. `ShaderCompilerSlang/private/ShaderStructsCodeGenAssertions.cpp` 被原稿算进"两节"，实测它 24 行 / 24 个类型、
   **每种只有一节**（全是 `.Reflection.h`），按动作属于"只改名字"：已从 4.2 移到 4.3。
4. 归档把探针的 `parents[4]` 变成了 `openspec/` 而不是仓库根，`cpuid.h` / `build.py` 里指向该 change 的注释路径
   也悬空 —— 与 5.5 的判据一起修掉。

**留给用户的（不在仓库里，只报告）**：`~/.dsh/AGENTS.md` 第 143 行那张对照表仍逐字写着
`<类型>.{Reflection,Serialization,Hash}.h`；它下面那段"已接入的真实类型只有 `GPUBufferDescriptor` 与几个
测试 / 夹具类型""新生成物目前仍**调用**旧运行时"也早已过期（清单 76 行、旧运行时已退役）。

