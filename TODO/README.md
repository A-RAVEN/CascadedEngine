# C# 脚本运行时沉淀（TODO 设计文档 · 给接手者）

> 本文件是**唯一入口 + 行动清单**。任何接手的 agent / 人：读完本文件 + 到那步再翻对应 detail doc 即可上手，
> **不需要任何外部对话上下文**。详细设计见 001-004；本文件只管「现在在干嘛、下一步干嘛、怎么算完成」。
> 状态：001-004 是「探索中」设计笔记，**不是 OpenSpec 规格**。

## 在干嘛（TL;DR）

引擎接 **C# 运行时**（CoreCLR + AOT）调 IMGUI / RenderBackend+GPUGraph / ThreadManager / 资源系统；
同时把 **反射 / 哈希+相等 / 序列化** 做成**一套统一 CodeGen 框架**（C++ 侧标注 → parser → 类型表 → 生成可读代码，替代晦涩模板元编程）。
一条 CodeGen 管道，四个 emitter = 绑定 / 反射 / 序列化 / 哈希+相等。
**codegen（Phase 1）与 C# 运行时（Phase 2-6）是两个 workstream，别混。**

## 进度 & 现在该做什么

```
[x] 第 0 步  决策门调查：核实 CA_REFLECTION 真实形状 + 锁定 parser 元数据来源   完成 ✅（2026-09-06）
[x] 第 1 步  /opsx:propose engine-codegen-framework   （唯一 codegen proposal，4/4 工件已建）   ←【你在这：/opsx:apply 执行 tasks 1-8】
[ ] 第 2 步  csharp-hostfxr-bootstrap                 （把 CoreCLR 加载进引擎）
[ ] 第 3 步  csharp-bindings-generator                （C-ABI 函数表 + C# 包装生成）
[ ] 第 4 步  first-surface-spike-imgui                （C# 第一面：IMGUI 编辑窗口回调跑通）
[ ] 第 5 步  threading-ownership                      （线程拓扑 + SafeHandle/GCHandle/pin）
[ ] 第 6 步  hot-reload-collectible-alc               【GATE: 先解未决 #3 进程分拆；曾为明确 non-goal】
```

### 第 0 步结论（已核实，2026-09-06，依据见 002 §7.1 + 004 §6.1）

- **决策门 1**：CA_REFLECTION = **字段 only + 纯编译期**，**不够当完整 parser**，且因 runtime 承重（序列化 + 每帧哈希）**不可替换**。file:line 依据见 `TODO/002 §7.1`。
- **决策门 2**：parser 元数据来源 = **libclang**（复用 CACodeGen 的 CLANG_LIB 18.1.8），**引擎保持纯 MSVC**，parser 作**独立构建期 sidecar**。**标注形态（2026-09-06 定）**：`[[clang::annotate]]`/字面 attribute **不用**（已核实 MSVC C5030 警告 + libclang 自定义命名空间 attribute 弱 + `[[clang::annotate]]` AST 可见性 bug llvm#87813）；改用**标记宏 `CA_REFLECT(...)` + libclang AST 爬 class 体**；**`CA_REFLECTION` 保留为外部库非侵入通道**。见 `TODO/002 §7.1` + [engine-codegen-framework] design D1/D2/spec.
- **下一步就做 /opsx:propose engine-codegen-framework**（只建这一个 codegen proposal）；Phase 1 前置未决点（树边界 / 枚举范围 / patch_ninja / 一致性模型 / `CA_REFLECT` 语义）见 `TODO/004 §7` 与 design Open Questions。

## 硬规则（句句要守，来自 CLAUDE.md）

- 构建只 `python build.py`；**禁**手动 ninja / `cmake --build` / `cmake -S`。
- 外部 API 核查用 **MCP 工具**（web-reader / web-search-prime），**禁** WebFetch / WebSearch。
- 所有 bug **默认是我的改动引入**；禁 "pre-existing" 措辞。
- 任何 Change 的**最后一个 task 必须是 Review & Adversarial Verify closure**。
- 不改无关文件；不成对不建 proposal 之外的散 change（codegen 只建 `engine-codegen-framework` 一个，不为 002/003 各建）。

## task 质量标准 + 陷阱

- **好 task（`TODO/004 §3`）**：单 agent 一次完成的连贯改动 + **真 oracle**（非 exit 0 / code review / BUILD SUCCESSFUL）+ 命名真实存在的文件 + 先提交可对比 baseline + 无假前提。太大就拆成 2-4 个。
- **别踩陷阱（`TODO/004 §4`）**：`Reflection.h:350` 插入点是 **managed_wrapper_traits 非 custom_hash_trait**；
  `Hasher.h` 的 `operator==` 不按 CompareMode 分支；`aggregateHasher` 对 trivially-copyable 做**含 padding 的 raw-bytes**；
  `WriteReadbackPNG` 返回值在 Main.cpp:1101 被忽略；`NameHash` 不是 uint64（约 48B）；
  `--headless-timeout` 被解析但从未消费。

## doc map（到某一步才翻，别通读）

| 文档 | 内容 | 何时读 |
|---|---|---|
| **004** | 工程拆分 / task 标准(§3) / 事实陷阱(§4) / 边界(§5) | **任何 proposal 前必读** |
| **001** | C# 运行时设计细节 + 运行时"内部面顺序" | 做 Phase 2-6 时 |
| **002** | 反射 CodeGen 化设计 | 做 Phase 1 反射部分时 |
| **003** | 哈希 + 相等设计 | 做 Phase 1 哈希部分时 |

## 两个"顺序"别混

- **proposal 顺序** = 上面「进度」清单（先建哪个 change）。
- **C# 运行时"内部的面顺序"** = 001 的 roadmap（IMGUI spike → graph → task → resource → reload）。在 Phase 4 之后用，**不是** proposal 顺序。

## 完成一步后（保持本文件是唯一权威）

- 把对应的 `[ ]` 勾成 `[x]`，并更新「现在该做什么」指向下一个未勾步骤。
- 把结论记进对应 detail doc（002/003/004），**不要另起新文档**。
- 若发现边界 / 顺序要改，改**本文件**，不要散记。
