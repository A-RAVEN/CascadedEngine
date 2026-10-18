## 1. 审计（把"每个纯虚方法"变成一行结论）

- [x] 1.1 数出审计范围并贴进对话：**按类体**数 6 个被审计接口各自的纯虚方法条数（`CRenderBackend` / `IWindowSystem` / `IShaderCompilerManager` / `IOManager` / `IResource` / `IReadbackToken`）。⚠ **不许用 `git grep -c 'virtual .*= *0;'` 逐文件数** —— 实测那样会把同文件里的其它类算进来（`CRenderBackend.h` 有 13 条但 `CRenderBackend` 本类只有 10 条，另 3 条属于 `IReadbackToken`），也会漏掉跨行声明（`WindowSystem.h` 里 `NewWindow` 的 `= 0;` 不在 `virtual` 那一行）。**怎么算做完**：6 个逐接口数字 + 合计贴出，并写清数法（解析 `class X { … };` 的类体、把跨行的 `= 0;` 也算上）；数与脚本 2.1 的解析结果必须一致。→ 实测 10 + 23 + 3 + 3 + 2 + 3 = **44**（类体解析器自检：`CRenderBackend`=10、`IReadbackToken`=3，与头文件里的类边界一致）
- [x] 1.2 逐方法写审计行到 `evidence/bindable-audit.md`（一条方法一行，列固定为：`接口::方法` / C++ 形状 / 投影形状 / 是否拷贝 / 谁分配 / 可绑性 / 理由或建议 / 失败上报方式）。**怎么算做完**：6 个接口的**每一条**纯虚方法都有且只有一行；"投影形状"列只出现 blittable 类型、`X const* + count`、`uint64_t 句柄`、`char const* + len` 四类写法（出现容器类型名即不合格）；"不可绑"的行都有理由与建议。→ 44 行（与 1.1 的合计一致）；不可绑 7 行、每行都有理由与建议；投影形状列无容器类型名（`castl::` 只出现在"C++ 形状"列）
- [x] 1.3 从审计表里汇总两张清单：**不可绑清单**（每条：理由 + 改法建议 + 归属哪个后续 change）与**需快路清单**（每条：建议的 `...Span` 重载签名）。**怎么算做完**：两张清单贴在报告里；不可绑清单里每一条都能对应到 spec 里"四类不可投影"中的一类（对不上就说明矩阵漏了一类，回头补 spec 而不是硬塞）。⚠ 判据改前/改后对照：改前是"对应到 spec 里『三类不可投影』中的一类"，改后是"对应到『四类』"——依据是审计实做时发现 `Release` / `Initialize` 属于第 ④ 类"语义越界"（绑得了但不许绑），三类放不下；新判据更严（多一类必须解释），不放松。→ 不可绑 7 条：② 类 5 条、④ 类 2 条、① 类 0 条；需快路 0 条（并写明"范围不含热路径，规则保留"）

## 2. 核对装置（会让审计表变红的东西）

- [x] 2.1 写 `Tools/CodeGen/check_bindable_audit.py`：**解析类体**（定位 `class X` 到它自己的闭合 `};`，同文件多个类要分开）列出每个被审计接口的纯虚方法，把跨行的 `virtual … = 0;` 也算上；与 `evidence/bindable-audit.md` 的方法列对账；`--list` 打印全表。**三种非零退出**：① 接口里有方法但表里没有（点名"哪个接口的哪个方法"）；② 表里标"不可绑"但理由或建议为空（点名该行）；③ 表里出现接口中不存在的方法（防僵尸行）。退出码 `0/1/2`（2 = 找不到接口头，判据自身失效）。**怎么算做完**：跑一遍当前状态 → 退出码 0，并贴出"扫描 N 个接口、对账 M 条方法、命中 0 处问题"这一行读数（N、M 都要非 0，且 M 必须等于 1.1 的合计）。→ 实测 `扫描 7 个接口、对账 60 条方法、命中 0 处问题`，退出码 0（修脚本时踩了三个解析坑并逐个修掉：注释里的示例签名、虚析构实现体吞掉下一条声明、`public:` 与紧跟的第一条方法同块 —— 见脚本里 `header_methods` 的注释）
- [x] 2.2 脚本自检（红绿成对）：从审计表里**删掉一行** → 脚本必须非零退出并点名那个方法；补回 → 退出码 0。再临时给某行标"不可绑"但清空理由 → 必须非零。**怎么算做完**：三组读数（删行红 / 补回绿 / 无理由红）都贴出，且临时改动全部还原（`git status` 里看不到审计表的意外改动）。→ 删 `IWindow::Focus` 行 → `✗ 缺少审计行：IWindow::Focus（头文件 1 条、表里 0 条）` 退出码 1；补回 → 60 条 / 0 处 / 退出码 0；把 `CRenderBackend::Release` 的理由清成 `—` → `✗ 不可绑但没有理由或建议：CRenderBackend::Release` 退出码 1；还原后哈希回到 `987A1CAF486B59C7`（与改动前逐字节一致）
- [x] 2.3 脚本自检（防僵尸行）：在审计表里**加一行**接口中不存在的方法 → 脚本必须非零并点名；删掉 → 绿。**怎么算做完**：两组读数贴出。→ 加 `IWindow::FocusThisMethodDoesNotExist` → `✗ 僵尸行：审计表第里出现了接口中不存在的方法 IWindow::FocusThisMethodDoesNotExist` 退出码 1；删掉 → 60 条 / 0 处 / 退出码 0，哈希回到 `987A1CAF486B59C7`

## 3. 规格与文档

- [x] 3.1 `openspec validate csharp-bindable-surface` → **valid**。**怎么算做完**：贴出 validate 输出。→ `Change 'csharp-bindable-surface' is valid`
- [x] 3.2 `Tools/CodeGen/README.md` 新增一节：投影矩阵（照 design D2 的表）、三类不可投影、快路约定、`check_bindable_audit.py` 的用法 / 参数 / 退出码 / 可直接复制运行的示例与真实输出。**怎么算做完**：贴出新增小节原文；`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`（附读数）。→ 新增「十三、可绑定面：C++ 接口形状 → C ABI 的投影规则」（含 14 行投影矩阵、四类不可投影、快路约定、脚本用法与退出码、已知限制四条）；示例命令原样跑过（输出就是文档里那一行）；`README: 20 documented command(s) run, 0 mismatch(es)`
- [x] 3.3 审计表与 spec 的一致性复核：spec 里"四类不可投影"逐条能在审计表里找到至少一个真实例子（模板方法 / 所有权不明裸指针 / 隐式转换重载 / 语义越界）；找不到任何真实例子的那一类，必须在审计表里明确写"本仓库暂无实例，规则保留"。**怎么算做完**：四类各自点名例子（或写明暂无实例），并说明依据。⚠ 判据改前/改后对照：改前写的是"三类"，改后是"四类"（第 ④ 类由 1.3 的实做结果补入），只加严不放松。→ ① 模板成员 = 0 处 ⇒ 写明"暂无实例 + 依据"；② 6 条；③ 唯一重载对是 `Readback` 的 `ImageHandle` / `BufferHandle`（无隐式互转）⇒ 写明"暂无实例 + 依据"；④ 2 条（`Release` / `Initialize`）；脚本仍 0 处问题

## 4. 收尾验证

- [x] 4.1 本轮只加了文档、规格、一个只读脚本与一张审计表 —— 复核"没碰代码"：`git status --porcelain` 里除 `openspec/changes/csharp-bindable-surface/**` 与 `Tools/CodeGen/**` 外，不许有 `.h` / `.cpp` 改动。**怎么算做完**：贴出 `git status` 输出并逐条分类。→ 6 条痕迹，逐条分类全部落在 `openspec/changes/csharp-bindable-surface/**`（4 改 + 1 新目录）与 `Tools/CodeGen/**`（1 改 + 1 新脚本）；`.h/.hpp/.cpp/.cxx/.cc` 改动 **0 条**
- [x] 4.2 末条：`python build.py --config Debug` → `BUILD SUCCESSFUL`；`out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED$` 行数 **12**、含 `FAILED` 行 **0**；两个后端各跑一次 `--headless 10`（各写独立 `--report`）→ 各自退出码 **0**、**8 条 status 全 pass**。若失败，分析并修复到全绿（修根因，不许把判据改松）。**怎么算做完**：四组读数贴出（构建 / CoreTests / 两份 report）。→ 构建 `BUILD SUCCESSFUL`（configure 0.0s / codegen 16.8s / 编译 12.6s / 合计 29.4s，退出码 0）；CoreTests 退出码 0、`PASSED$` 12 行、`FAILED` 0 行；vulkan 退出码 0、8/8 pass；d3d12 退出码 0、8/8 pass
