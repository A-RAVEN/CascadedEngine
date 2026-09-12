# 004 · CodeGen 工程拆分 —— 符合 OpenSpec 规范、单-agent 可完成、可测试的 proposal 序列

> **状态**：探索中（未达 OpenSpec proposal 水位）
> **日期**：2026-09-06
> **关联**：[[001-csharp-scripting-framework.md]]、[[002-reflection-via-codegen.md]]、[[003-hash-equality-via-codegen.md]]
> **来源**：一次 `/opsx:explore` 讨论，用 workflow（`decompose-codegen-proposals`，93 agent）对"把 CodeGen 工作拆成 OpenSpec proposal + agent 级 task"做架构拆解 + 逐 task 对抗验证。==本文是"怎么拆、拆到多细、验收怎么设"的参考模板。==

---

## 1. 背景与目标

把引擎 CodeGen 框架工作拆成**符合 OpenSpec 规范**的 proposal，且每个 proposal 的 **tasks 能量刚好让一个 agent 单次执行完成**，并有**对应的验收和测试目标**——不希望在执行时漫无目的地写一大坨无法测试的代码。

## 2. 结论（诚实记录）

第一版拆解 = 8 个 proposal / 88 个 task。对抗验证判 **REVISE hard**：在"单 agent 一次完成 + 可测试"这条标准上**不达标**。量化：
- **~35 个 task 过大**（一个塞 3-5 件事）→ 需拆成 2-5 个。
- **~55 个 task 不可测试**（假 oracle：`exit 0` 恒真、`code review`/`documented in design.md` 是散文、`file(GLOB_RECURSE)` 空文件也过、`BUILD SUCCESSFUL` 无条件、VUID baseline 从不提交、`byte-identical` 反序列化不可满足）。
- **边界不合规**（01/02/03 在盘上不存在、slug 用 task 序号、缺强制收尾 Review closure、文件路径全错、codegen emitter 与 C# runtime 两个 workstream 混淆、config.yaml 过时）。
- 更有价值的是抓到一批**会误导实现的事实陷阱**（见 §4）。

## 3. 合格 task 的标准（从失败提炼的可复用规矩）

一个 task 算"合格、能被 agent 一次做完、可测"，要同时满足：

1. **单 agent 一次完成的连贯改动**：一个 task = 一个文件/一个函数/一个结构的一处修改，不是"实现 X 并接入 Y 并测试 Z"。太大就拆成 2-4 个（每个仍是连贯单体）。
2. **真 oracle，不是"恒真"**：
   - ❌ `exit 0`（tester 的 `runTestWithDiagnostics` 丢弃 exitCode=1，恒真）、`BUILD SUCCESSFUL`（build.py:235 无条件）、`code review`、`documented in design.md`。
   - ✅ 会**失败**的断言：`static_assert`、`result.json status=='pass'`、字节/PNG compare、哈希/相等 oracle、`hash(a)==hash(b)`+`a==b`、VUID diff（但见下"基线要存在"）、snapshot-of-generated-code（'generates expected code'）。
3. **命名真实存在的文件**：`CACore/header/Reflection.h`（不是根目录 `Reflection.h`）、`Interface/RenderInterface/header/ShaderResourceHandle.h`、`CACore/header/Serialization.h`、`ExternalLib/CMakeLists.txt`（不是死的 `CMake/ExternalProjects.cmake`）。
4. **先提交可对比的 baseline**：`test_output/` 被 git-ignore，`*_validation.log` 从不提交 → 任何"无新增 VUID vs baseline"的 gate 都得**前置一个"提交基线"的 task**，否则跑不起来。同样，`crash_*.dmp` 基线、PNG 基线同理。
5. **无假前提**：实现前先核实源码事实（见 §4），不要按"以为是对的"写——否则验收断言本身就是假的。
6. **别用 GLOB 验证**：`file(GLOB_RECURSE *.cpp)` 对"文件缺失"也会通过；用显式源清单或 grep build.ninja 的 `<File>.cpp.obj`。
7. **编译证明显式实例化**：header-only 模板不会产生待查符号；`extern "C"`/`__declspec(dllexport)`/显式实例化 + `dumpbin` 才是真证明。
8. **强制收尾**：每个 proposal 末尾必须有 **Review & Adversarial Verify closure** task（可审计 workflow + Review Log），且它必须是**最后一个**，不能被 build gate 顶掉。

## 4. 抓到的"事实陷阱"（会误导实现，逐条）

对抗者逐条核对源码，这些是**错误前提**，不改会让人按错的方案写码：

- **`Reflection.h:350` 的门插入点是 `managed_wrapper_traits`/`type_desc`，不是 `custom_hash_trait`** ——"特化 custom_hash_trait 就能过 350 断言"是错的。
- **`Hasher.h` 的 `operator==` 并不按 `CompareMode` 分支** ——"单点删掉 SHA256 改 FullCompare"不成立；得先让 `operator==` 按 CompareMode 分支。
- **`aggregateHasher` 对 trivially-copyable 做的是含 padding 的 raw-bytes 哈希** ——"等逻辑值两布局 hash 相同"断言是**假**的。
- **`WriteReadbackPNG` 返回值在 Main.cpp:1101 被忽略**（空帧也 exit 0）——"非空硬失败"不成立，得先把 bool 传播成硬失败。
- **`NameHash` 不是 uint64（约 48B）** ——"16B 句柄三元组"前提不实，得先做 `ResourceHandleKeyData` 真实字节布局。
- **`--headless-timeout` 被解析但从未消费**（watchdog 已移除）——死锁会永远挂住；`DLL` 中不跑 hostfxr/managed（DllMain 死锁），须 CreateThread + mailbox。
- **`byte-identical` 反序列化 oracle 不可满足**（padding + 容器长度前缀不同）→ 用 **field-ORDER** 断言（经 `CATypeDescriptor`）。

## 5. 修正后的边界与阶段

> **codegen emitter 与 C# runtime 是两个 workstream**——别混成一个"CodeGen 框架"。

```
Phase 1: engine-codegen-framework   ← 唯一 codegen proposal，合并原 01/02/03
          （共享 core: parser + 类型表 + [[careflection(...)]] attribute + build.py hook + ABI guard
            + 反射 emitter，先证明反射；serialization/hash 作为 SAME proposal 内的 task，非分离 change）
Phase 2: csharp-hostfxr-bootstrap     ← C# 运行时 workstream，各自独立 proposal
Phase 3: csharp-bindings-generator
Phase 4: first-surface-spike-imgui
Phase 5: threading-ownership
Phase 6: hot-reload-collectible-alc   ← GATED: 依赖未决 #3(editor/game 进程分拆)，且曾为明确 non-goal
```

- **只建一个 codegen proposal**（`engine-codegen-framework`），不要为 002/003 各建一个 change——它们共享 parser/类型表/attribute/构建接入，是同一份的四个 emitter（绑定/反射/序列化/哈希+相等）。
- slug 用 OpenSpec 的 `YYYY-MM-DD-<slug>` / 描述性 kebab（`fix-*`/`harden-*`），把阶段序号放 tasks.md，不是放进 slug。
- 每个 proposal 末尾强制 Review & Adversarial Verify closure（最后一个 task）。

## 6. 开门动作：propose 之前先过 2 个决策门（**2026-09-06 已过**，结论见 `#6.1`）

1. ~~**核实 `CA_PRIVATE_REFLECTION`/`CA_REFLECTION` 真实形状**~~ → **已核实**：种子 = **数据成员 only** + **纯编译期**（无 methods/enums/base/access/per-member attribute、无 runtime name→TypeDescriptor 注册表），**不足够当完整 parser**，且因 runtime 承重（序列化 + 每帧哈希）**不可替换**。file:line 依据 → [[002]] §7.1。
2. ~~**锁定 parser 元数据来源**~~ → **已锁定 = libclang**（复用 CACodeGen 的 CLANG_LIB），引擎纯 MSVC，parser 作独立构建期 sidecar；**不用字面 `[[careflection::...]]`/`[[clang::annotate]]`**（已核实：MSVC C5030 警告 + libclang 自定义命名空间 attribute 弱 + `[[clang::annotate]]` AST 可见性 bug）；改用**标记宏 `CA_REFLECT(...)` + libclang AST 爬 class 体**；CA_REFLECTION **保留为外部库非侵入通道**。结论 + 依据 → [[002]] §7.1。

然后才 `/opsx:propose engine-codegen-framework`（只建这一个）。

### 6.1. 决策门结论速览（2026-09-06，经 workflow + 对抗回验 CONFIRMED）

| 决策 | 结论 | 关键依据 file:line |
|---|---|---|
| CA_REFLECTION 够当完整 parser? | **否**（字段 only + 纯编译期 + 构建期读不到） | `Reflection.h:58-65/:67-73/:82-90/:99/:122-133` |
| CA_REFLECTION 可替换? | **否**（runtime 承重：序列化 + 每帧哈希） | `Serialization.h:45-50/234-239`；`GPUGraph.h:167,224` 等 |
| parser 元数据来源 | **libclang**（CACodeGen 复用 CLANG_LIB 18.1.8） | `CACodeGen/.../LLVM/bin/x64/libclang.dll`；`ParserLibrary` 链 CLANG_LIB |
| engine 工具链 | **纯 MSVC**（parser 独立 sidecar，不进 CMake 图） | `build.py:95-96/:149` |
| clang::annotate / 字面 attribute | **不用**（已核实：MSVC C5030 警告 + libclang 自定义命名空间 attribute 弱 + `[[clang::annotate]]` AST 可见性 bug llvm#87813） | devcommunity C5030；llvm#87813；discourse |
| 标注形态 | **标记宏 `CA_REFLECT(...)` + libclang AST 爬 class 体**；`CA_REFLECTION` 保留为外部库非侵入通道 | [engine-codegen-framework] design D1/D2 |
| ParserLibrary 缺口 | skip 枚举/函数（`parser.cpp:189-192`）——C# 需枚举值表才要小改 | `parser.cpp:189-190/:191-192` |

## 7. 未决 / 待办

- **2 个决策门已过**（见 `#6`），下一步 = `/opsx:propose engine-codegen-framework`。
- `engine-codegen-framework` 的 task 列表需按 §3 的"合格 task 标准"逐条重拆（第一版的 ~30 task 大多还需要再细分 + 换成真 oracle + 先补 baseline）。
- C# 运行时各里程碑（Phase 2-6）的 task 同样按 §3 标准，且 hot-reload gate 在未决 #3(#5/#7)。
- **Phase 1 前置未决点**：① MSVC C5030 行为（clang::annotate 若启用须真编译）；② 树边界（预编译侧车由 build.py 调 vs add_subdirectory LLVM）；③ 枚举范围（需构建期枚举值表 vs 4 字节句柄）；④ 生成源码能否扛过 `patch_ninja()`（`build.py:155-181`）；⑤ 一致性/权威模型（codegen 快照 vs CA_REFLECTION 漂移，需 ABI assert 或单源树）。

## 8. 升级建议

- **此文档本身未达 proposal 水位**——它是"怎么拆"的模板/参考。真正要 propose 的是 **`engine-codegen-framework`**（Phase 1），推进前先过 §6 两个决策门。
- 若继续，把 [[001]]/[[002]]/[[003]] 的"升级建议"节统一指到此文 §5-§6 的边界 + 决策门，避免三份文档各自说"同一份 proposal"而产生分歧。
