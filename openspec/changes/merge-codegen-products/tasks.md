# 任务清单

判据都要能在"问题还在"的状态下变红。构建一律 `python build.py`（本项目唯一合法入口），
工具是 `out/build/x64-debug/bin/CodeGen.exe`，解析环境由 `build.py` 自己推导。

## 0. 前置（不做完不许动工具）

- [ ] 0.1 确认隔壁那份 change（`clear-codegen-parse-blockers`）的改动**已提交或已备份**：它与本 change 要改的
      文件重叠（`GPUPipelineInstance.{h,cpp}`、`GPUGraph.h`、`CPipelineStateObjectAssertions.cpp`、
      `GeneratedHashTest.cpp`、`build.py`、`Tools/CodeGen/README.md`）。
      **怎么算做完**：给出证据 —— `git status --porcelain` 里那批文件的状态，或者一份放到 `%TEMP%` 的备份路径与件数。
      ⚠ 没做这一条就往下走，一旦中途按"回滚"操作会把上一份成果一起抹掉（design D9）。

## 1. 固化基线（动工具之前）

- [ ] 1.0 **核对被本改动推翻的基线条文**：`improve-build-time` 在 2026-10-10 归档之后，基线里多了两条明文写着
      "一次调用出三份产物"的要求（`openspec/specs/codegen-framework/spec.md` 的「一个类型一次调用出三份产物」、
      `openspec/specs/build-throughput/spec.md` 的「一个类型只解析一次，一次出三份产物」）。本 change 用
      **RENAMED + MODIFIED** 改它们。
      **怎么算做完**：① `git grep -n "三份" -- openspec/specs/` 列出全部残留，逐条说明"这条由本 change 的哪个 delta 改掉"
      或"这条讲的是别的东西（例如三节各自的拒绝条件）不动"；② 两条 RENAMED 的 FROM 标题与基线**逐字相同**
      （`git grep -n -F "<FROM 标题>" -- openspec/specs/<能力>/spec.md` 命中 1 行），TO 标题与 MODIFIED 里的标题逐字一致；
      ③ `openspec validate merge-codegen-products` 通过。
- [ ] 1.1 把当前构建目录里的 228 份产物**保持目录结构**复制到 `%TEMP%\codegen_baseline_merge\`，
      并记下取数状态：HEAD 提交号、`git status --porcelain | Measure-Object` 的条数、件数与总字节。
      **怎么算做完**：贴出四条读数（改造前实测：228 份 / 386,180 字节 / 7,497 行；HEAD 与改动条数当场取），
      且复制后的件数与原目录一致；写进 `evidence/baseline-products.md`。
- [ ] 1.2 复核 design D3 的前提：接入清单 **76 行**逐行都带 `serializable`。
      **怎么算做完**：**按清单里的头文件逐个统计**（不是全仓 grep —— 全仓会把夹具、归档与别的 change 的文档
      一起数进来，实测全仓 `git grep -c` 合计 151 处、限定到清单那 18 个头才是 76），贴出命令与逐文件计数；
      再加一条更强的事实：`python build.py --config Debug --codegen-all` 里 76 行**全部 exit 0**
      （若某行没带 `serializable`，它会在 `--emit-all` 下以退出码 9 失败）。
- [ ] 1.3 记**改前读数**，写进 `evidence/baseline-products.md`：
      ① 产物件数/字节/行数；② 源码 include 行数（按后缀分）；③ 消费文件的节种类分布；④ **十条拒绝码的改前实测**
      （2 / 5 / 6 / 9 / 10 / 13 / 14 / 15 / 16 / 17，各贴退出码与 stderr 原文）；⑤ 扫描器的读数。
      **怎么算做完**：五组读数贴出来（改造前实测：228 份 / 386,180 字节 / 7,497 行；include **270** 行 =
      Reflection 80 + Serialization 89 + Hash 101，分布在 **26** 个文件；节种类分布 **12 / 6 / 8**；
      扫描器退出码 0、命中 0 行）。⚠ ④ 是"逐条与改造前相同"这句判据的参照侧，缺了它那条判据无法判定。

## 2. 工具：一个类型一份产物

- [ ] 2.1 改 `Tools/CodeGen/CodeGenMain.cpp`：`--emit-all --out-dir` 下按 `<类型名>.codegen.h` 写**一份**文件，
      节顺序反射 → 序列化 → 哈希，文件头注释 / `#pragma once` / 运行时头 include 各一次，每节保留自己的注释行。
      **怎么算做完**：单跑一个类型，目录里只有那一份；贴出文件列表 + 文件头 12 行 + 三节的注释行；
      并用一条命令核"顺序与去重"（例如统计 `#pragma once` 出现次数 = 1、`CodeGenSerialization.h` 出现次数 = 1）。
- [ ] 2.2 标准输出与用法文本：`CodeGen wrote: ` 由三行变一行；`usage:` 两行同步；**`--emit-all` 的报错文本**
      里"一次调用写三份、三个旧后缀"那句也要改。
      **怎么算做完**：贴出单跑的完整 stdout（`CodeGen wrote: ` 恰好一行）与改后的 usage / 报错文本原文。
- [ ] 2.3 **228 次逐节对拍**（核心不变量）：对 76 个类型逐一用 `-o <路径>` 单节输出，与 1.1 的基线逐字节比较。
      **怎么算做完**：`-o` 的输出**写到与基线无关的另一个 `%TEMP%` 目录**（写进基线目录就是自己跟自己比，
      这条判据就废了）；贴出「228 次比较、全部逐字节相同」的读数与所用命令；任何一次不同就停下查原因。
- [ ] 2.4 拒绝语义逐条不变：2 / 5 / 6 / 9 / 10 / 13 / 14 / 15 / 16 / 17。
      **怎么算做完**：十条读数逐个贴出来，**并与 1.3④ 的改前读数逐条对照**；每条还要核**报错理由**（stderr 原文）
      而不只是退出码 —— 否则"跑了个别的错误也返回同一个码"会被当成通过；每条都**没写出文件**。
- [ ] 2.5 搭配关系不动：`--emit-reflection --out-dir <目录>` 仍然是退出码 2（工具原话
      `--out-dir only applies together with --emit-all; use -o <file> for a single emitter.`）；
      单节输出仍然走 `-o`，且 `-o` 的产物与 1.1 的基线逐字节相同。
      **怎么算做完**：两条读数 —— 退出码 2 那一条（贴 stderr），以及 `-o` 单节输出与基线某个文件 `Get-FileHash` 相同。

## 3. 构建脚本与旧产物清理

- [ ] 3.1 `build.py`：`CODEGEN_PRODUCTS` 从三个后缀改为一份；`outputs` 映射、占位文件、`absent` 核对、
      每份存在性检查、账本 `codegen.stamp.json` 的 `out` 映射按一份改；失败信息保持原样。
      **怎么算做完**：`python build.py --config Debug --codegen-all` → 76 行逐条 exit 0、`CODEGEN FAILED` 0 次、
      `BUILD SUCCESSFUL`；贴出账本里一行的 JSON 片段（只有一份产物）。
      另加一条**防"空检查"**的实测：把产物名单里的键名故意改错一个字，构建必须仍然失败或报出"没写出产物"
      （若改错键名后 76 行照样 exit 0，说明核对被改成了空检查）。
- [ ] 3.2 清理覆盖**所有预设**：生成段跑之前清掉 `out/build/*/codegen/**/<类型名>.{Reflection,Serialization,Hash}.h`。
      **怎么算做完**：**每个存在的预设目录各造一个**旧命名文件（x64-debug 与 x64-relWithDebugInfo 都要），
      跑一次构建后它们**都消失**（贴出每个目录的前后两次 `Test-Path`）；并贴出"所有预设里旧后缀文件数 = 0"的读数。

## 4. 消费点（一个类型一行 include）

- [ ] 4.1 改那 **12 个含三节**的文件（各删两行、每个类型留一行）：`CACore/private/TestWrapperAssertions.cpp`、
      `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`、
      `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h`、
      `Interface/RenderInterface/header/{CPipelineStateObject,CVertexInputDescriptor,GPUGraph,ShaderProvider,ShaderResourceHandle,TextureSampler}.h`、
      `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、`Test/CoreTests/FriendLikeTest.cpp`、
      `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h`。
      **怎么算做完**：逐文件贴出 include 行数（改前 → 改后），例如 `Compiler.h` 那类多类型头是 48 → 24。
- [ ] 4.2 改那 **6 个含两节**的文件（各删一行）：`Interface/ShaderCompiler/header/Compiler.h`、
      `Interface/RenderInterface/private/TestContainerAssertions.cpp`、
      `ShaderCompilerSlang/private/ShaderStructsCodeGenAssertions.cpp`、
      `Test/CoreTests/{GeneratedHashContainerTest,PipelineStateObjectTest,ResourceBatchTest}.cpp`。
      **怎么算做完**：同上（`Compiler.h` 改前 48 行、改后 24 行）。
- [ ] 4.3 改那 **8 个只含一节**的文件（只改名字）：`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`、
      `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp`、
      `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp`、
      `Test/CoreTests/{CodeGenKindsTest,EmitReflectionTest,GeneratedContainerTest,GeneratedHashTest,GeneratedSerializerTest}.cpp`。
      **怎么算做完**：同上（每个文件行数不变，只有名字变）。
- [ ] 4.4 复核：源码树里旧后缀 include **零命中**、且终态行数符合预期。
      **怎么算做完**：`git grep -n -E 'include ".*\.(Reflection|Serialization|Hash)\.h"' -- '*.h' '*.cpp'` → **0 命中**
      （命令限定在源码树的两个后缀；`openspec/` 归档与文档/脚本里的历史引用不算，它们要点名排除而不是假装不存在）；
      并贴出终态 include 行数 = **138**（"文件 × 类型"一行；改造前 270 行）。
      再跑一次构建 —— 漏改的会在解析/编译期当场失败。

## 5. 收尾验证与文档

- [ ] 5.1 `python build.py --config Debug` → `BUILD SUCCESSFUL`，codegen 段 76 行逐条 exit 0。
      **怎么算做完**：贴出 codegen 合计读数与构建结论。
- [ ] 5.2 `out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED` **12** 行、`FAILED` 0。
      **怎么算做完**：贴出三个数。
- [ ] 5.3 两个后端 tester 各跑一次（vulkan / d3d12 `--headless 10`），判据是 `test_output/result.json` 里
      **8 条 status 都是 pass**（不是退出码 —— `Main.cpp` 的 `main` 以 `return EXIT_SUCCESS;` 结束）。
      **怎么算做完**：两次各留一份 `result.json` 快照，贴出条数与 pass 数。
- [ ] 5.4 `python Tools/CodeGen/check_legacy_symbols.py` → 退出码 0、命中 **0 行**。
      **怎么算做完**：贴出它自己那行读数；⚠ 这个脚本扫的是"源码 + 构建目录"，它打印的是**文件数**而不是产物件数，
      不许拿它的数与"228 → 76"并排当同一件事说。
- [ ] 5.5 把被本改动推翻的既有判据改掉：`openspec/changes/clear-codegen-parse-blockers/evidence/probe_parse_gate.py`
      里的 `PRODUCTS_EXPECTED = 3`（第 36 行）改成一份，注释里的"三份"字样同步。
      **怎么算做完**：改后原样跑该探针，绿态判据不再报"只写出 1 份产物（应为 3 份）"（贴出运行读数）。
- [ ] 5.6 文档与用户可见文本：`Tools/CodeGen/README.md`（接口表、`--out-dir` 写几个文件、产物命名、
      "三份产物"字样、§六 构建期怎么用它、示例里出现的产物名）、工具里 `--emit-all` 的报错文本、
      `CLAUDE.md`（先核有没有提产物命名，没提就记一句"核过、无需改"）、
      仓库脚本 `.claude/workflows/scripts/verify-codegen-tasks-order.js`（它仍以 `EmitSample.Reflection.h` 为动作对象）。
      **怎么算做完**：`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`；并逐条 grep 新写进去的锚点
      （锚点要具体，例如 `<类型名>.codegen.h` 与"只写一份"的完整搭配，**不许**用"一份"这种会恒真的词）。
      ⚠ `verify_readme.py` 只跑示例、不比对描述性段落，所以它通过**不等于**文档改对了。
- [ ] 5.7 报告（不改）：`~/.dsh/AGENTS.md` 里那张对照表仍逐字写着 `<类型>.{Reflection,Serialization,Hash}.h`，
      它在仓库之外，本 change 只报告给用户、不擅自改。
      **怎么算做完**：贴出该行原文与"建议改成什么"，其余不动。
- [ ] 5.8 实物读数复核：构建目录里 `*.codegen.h` **76 份**、所有预设里旧后缀 **0 份**、源码 include **138 行**。
      **怎么算做完**：三组读数贴出来，与 1.3 的改前读数并排对照。
- [ ] 5.9 `openspec validate merge-codegen-products` → **valid**；工件点名的文件都存在。
      **怎么算做完**：贴出 validate 输出与文件存在性检查。
- [ ] 5.10 **末条：编译验证与修复** —— 再跑一次 `python build.py --config Debug`；若失败，分析并修复到
      `BUILD SUCCESSFUL`（修根因，不许把判据改松）。
      **怎么算做完**：最后一次构建输出为 `BUILD SUCCESSFUL`。
