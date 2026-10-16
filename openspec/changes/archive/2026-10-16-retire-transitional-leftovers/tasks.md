## 1. 先量：那个装置确实在空转（改动前的证据）

- [x] 1.1 数出三条读数并贴进对话：① 源码树里旧命名 include（`#include "*.Reflection.h"` / `.Serialization.h` / `.Hash.h`）**0 处**；② 构建目录 `out/build/*/codegen/` 下旧命名产物文件 **0 个**；③ 该装置在仓库里的引用点（`CODEGEN_LEGACY_SUFFIXES` / `cleanup_legacy_codegen_products`）**只在 `build.py` 里 4 处**。**怎么算做完**：三条数字与命令一起贴出，并写进 `evidence/pre-change-readings.md`（本 change 下新建该目录）；任一条不为 0 就停下——那说明还有东西在依赖它，本 change 的前提不成立。

## 2. 删掉空转的过渡装置（代码）

- [x] 2.1 删 `build.py` 的 `CODEGEN_LEGACY_SUFFIXES` 常量（182 行）、`cleanup_legacy_codegen_products()` 函数（185–215 行）、它的调用点（600 行）与打印行（601–605 行）。**怎么算做完**：`git grep -n -E 'cleanup_legacy_codegen_products|CODEGEN_LEGACY_SUFFIXES' -- build.py` 输出为空（**必须带 `-- build.py` 限定**：本 change 自己的工件里会提到这两个名字，全仓判据会命中自己）；并贴出行数对照 —— 改动前实测三个读数：函数体 185–215 行 = **31 行**、常量与函数连同注释 182–215 行 = **35 行**、加调用点与打印行 = **41 行**（proposal / design / 本文件里的"过渡装置行数"一律以 41 行为准）。
- [x] 2.2 删完立刻构建：`python build.py --config Debug --codegen-all` → `BUILD SUCCESSFUL`，codegen 段 **76 行进度逐条 exit 0**、`CODEGEN FAILED` 0 处；`out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**，且 `(& 'out\build\x64-debug\bin\CoreTests.exe') | Select-String -Pattern 'PASSED$'` 的行数 = **12**、含 `FAILED` 的行数 = **0**（注意：输出里**没有** `PASSED 12` 这样的字面量，必须按行数数）。**怎么算做完**：两组读数贴出并写进 `evidence/pre-change-readings.md`（这是"删对了、没删到在用的东西"的判据）。
- [x] 2.3 复核"没删过头"：单节发射开关（`--emit-reflection` / `--emit-serialization` / `--emit-hash`）、`-o`、`Tools/CodeGen/check_legacy_symbols.py`、`SubmitAndWait`（定位：`GeneralResources/header/CAResource/IResource.h`、`IOManager_FS/IOManager.cpp`、`Test/CoreTests/ResourceBatchTest.cpp` —— **不在** `Test/GPUBackendTester` 下，那里 0 处）、`CA_PRIVATE_REFLECTION` 那 5 处解释性注释都还在。**怎么算做完**：五个东西各给一条**带文件限定**的 `grep` 读数（数量不为 0），并与改动前逐个相等（改动前的读数先在 1.1 落盘）。

## 3. 改掉过期注释（代码注释）

- [x] 3.1 `CACore/header/HashKeys.h` 开头那句改成现状：点名"`Hasher.h` / `Serialization.h` / `Reflection.h` 已随 `retire-legacy-serde-hash` 删除"。**怎么算做完**：`git grep -n -F '待删的头' -- CACore/header/HashKeys.h` 输出为空（**判据限定到该文件**：归档 change 与本 change 自己的工件里也有这个串，全仓判据会命中自己）；改动前后整行原文都贴出来。
- [x] 3.2 `CACore/header/CodeGenHashFwd.h` 开头那个例子换成一个**现存的**头（或去掉例子）。**怎么算做完**：`git grep -n -F 'Hasher.h' -- CACore/header/CodeGenHashFwd.h` 输出为空；`git grep -n -F 'Hasher.h' -- CACore/header/HashKeys.h` 恰好 **2 处** —— 3.1 改写后的正文（它要点名 `Hasher.h` 已删除）+ 那句历史陈述"它原来住在 `Hasher.h` 里"（design 的 D2 明确保留它，因为它是过去时、准确、且解释了这段代码为什么在这个文件里）；两处都逐字贴出。

## 4. 把过渡期的规格收回去（delta + Purpose）

- [x] 4.1 六份主规格的 `Purpose`：`openspec/specs/` 下实测有**六份**还留着归档时那句 `TBD - created by archiving change … Update Purpose after archive.`（`build-throughput`、`codegen-framework`、`codegen-runtime`、`codegen-type-migration`、`vma-vulkan-function-binding`、`vulkan-backend-teardown`），逐份照**它自己的要求**写一两句 Purpose（门禁：delta 只覆盖 Requirements 段，这一行不会被归档合并到，必须就地改；Purpose 里 MUST NOT 出现该规格里没有的能力）。**怎么算做完**：`git grep -n -F 'Update Purpose after archive'` 输出为空；六份改后的 Purpose 原文一起贴出，并与该规格里的 `### Requirement:` 标题逐份对照（不许多写、不许少写）。
- [x] 4.2 delta 逐条对账（防归档丢场景）：三份 delta 里每条 `MODIFIED` 的场景数，与主规格里同名要求的场景数**逐个相等**（只差被点名删掉的那一条）。**怎么算做完**：贴出一张"要求名 / 主规格场景数 / delta 场景数 / 差异说明"的表；任何一处差异不在本 change 的清单里，就补回 delta。
- [x] 4.3 复核 delta 里每条改动都点名了**今天的**代码出处（`CodeGenMeta.h` 的守卫实现、`CPipelineStateObjectAssertions.cpp` 等三份断言文件、`CodeGenSerialization.h` 的 `hasClear` 分支、`CodeGenRuntimeTest.cpp` 的冻结字面量、`Tools/CodeGen/CodeGenMain.cpp` 里"元素是内含无序容器的结构体"的拒收 0 处 —— 撤掉退出码 12 那条场景的依据就在它）。**怎么算做完**：逐个 `grep` 这些出处仍在（数量不为 0；最后一条要求**恰好 0 处**并贴出命令），读数贴出。
- [x] 4.4 `openspec validate retire-transitional-leftovers` → **valid**。**怎么算做完**：贴出 validate 输出。
- [x] 4.5 备查清单落盘（归档那一步由用户点头，不在本 change 内执行）：把归档后要在**主规格**上成立的 grep 清单写进 `evidence/archive-time-check.md` —— `CA_REFLECTION` 只允许出现在"防回流守卫"（`Tools/CodeGen/check_legacy_symbols.py` 的符号表与 `openspec/specs/codegen-runtime/spec.md` 里那条守卫要求）与历史理由里、`CATypeDescriptor` 只允许出现在同一处守卫、`cacore::aggregateHasher` 0 处、`双份描述` 0 处、`cacore::has_custom_hash_func`（不带 `codegen::` 的旧写法）0 处。**怎么算做完**：文件在仓库里；并写明"归档前这条在主规格上必然是红的 —— delta 还没合并进去，这正是本 change 要收的东西"，避免下一个人拿它当"没做干净"的证据。

## 5. 对外文档：为什么留着 + 章节号

- [x] 5.1 `Tools/CodeGen/README.md` 新增一节，写清单节发射开关（`--emit-reflection` / `--emit-serialization` / `--emit-hash` / `-o`）与 `Tools/CodeGen/check_legacy_symbols.py` **为什么留着**，并写明"不许当旧账删"。**怎么算做完**：贴出新增小节原文，并逐条指出它写清了三件事 —— ① 单节开关承载哪一条规格判据（`codegen-framework` 里"三节与改造前逐字节相同"）；② 守卫脚本做什么、三个退出码（0 / 1 / 2）各是什么；③ 删了会出什么问题。另外附一条**附加**读数：`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`（它只比对 fenced block 里以 `CodeGen.exe ` 开头的命令行，不读正文，单独拿它当本条的判据是恒真的）。
- [x] 5.2 修 `Tools/CodeGen/README.md` 里重复的章节号：把第二个「`## 五、调用示例（每条都在本机实跑过，输出逐字照抄）`」改成「六、」，并把其后的「`## 六、构建期怎么用它`」到「`## 十、哈希键的来源盘点：check_hash_sources.py`」整段顺延。**用引文定位，不写行号**（本仓库的规矩：引用锚点必须逐字可 grep）。**怎么算做完**：`Select-String -LiteralPath "Tools\CodeGen\README.md" -Pattern '^## '` 出来的章节号**无重复**；`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)` 作附加读数（示例命令仍逐条可跑）。

## 6. 仓库外那张过期的表（需用户点头）

- [x] 6.1 全局规则文件 `~/.dsh/AGENTS.md` 末尾"两套反射 / 序列化设施"那张表：改成现状（一套设施 + 旧三头已删除；现实是 `CA_REFLECT` 铺开、生成物用新运行时）。**⚠ 这是仓库外文件，动手前要用户明确点头**（上一轮问过一次；本轮按 goal 的"其余一律不停"执行，理由与改前改后原文都贴在报告里，一句话可回退）。**怎么算做完**：该文件里 `绝大多数引擎类型仍用老宏` 与 `新生成物目前仍调用旧运行时` 两句 **0 处**；贴出改后的表。

## 7. 收尾：全量验证

- [x] 7.1 本 change 改动的三份 delta 里，"现在时"的旧设施陈述清零：先跑 `git grep -n -E 'CA_REFLECTION|CATypeDescriptor|cacore::serializer|cacore::deserializer|visit_members|aggregateHasher' -- openspec/changes/retire-transitional-leftovers/specs/` 贴出**全部命中行数**，再逐行对一份表（写到 `evidence/specs-stale-statements.md`，四列：文件:行号 / 命中原文逐字 / 分类（防回流守卫 或 历史理由）/ 判它属于哪一类的依据），**行数不等于表行数即未完成**。主规格侧的同一份清单归 4.5（它要等归档合并后才成立，现在跑必然是红的）。**怎么算做完**：命令输出 + 表格贴出，两者行数相等；任何一条既不是"防回流守卫"也不是"历史理由"即为未完成。
- [x] 7.2 末条：再跑一次 `python build.py --config Debug` → `BUILD SUCCESSFUL`；`out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**，`Select-String -Pattern 'PASSED$'` 行数 = **12**、含 `FAILED` 的行数 = **0**；两个后端各跑一次并**各写一份独立报告**（`test_output/result.json` 每次运行都被截断重写，跑完第二个后端就只剩它那一份）：`GPUBackendTester.exe --backend vulkan --headless 10 --report %TEMP%\result-vulkan.json` 与 `--backend d3d12 --headless 10 --report %TEMP%\result-d3d12.json`，两份各自退出码 **0**、各自 **8 条 status 全 pass**。若失败，分析并修复到全绿（修根因，不许把判据改松）。**怎么算做完**：四组读数（构建 / CoreTests / 两份 result.json）贴出，并落进 `evidence/after-change-readings.md`；再与 1.1、2.2 落盘的改动前读数逐项对照。
