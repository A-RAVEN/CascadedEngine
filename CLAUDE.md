# CascadedEngine Development Guidelines

> 技术栈 / 项目结构 / 常用命令。工作规则见下方「工作规则」节；构建 / 测试 / 调试见「构建 / 测试 / 调试」节。本文件人工维护，描述与当前代码库保持一致（2026-10-25）。
>
> **规则的长文说明、事故记录与旧规则原文**统一放在 **`docs/agent-rules-rationale.md`** —— 本文件只留条文，一节一条，不展开理由。

## Active Technologies
- C++20（CMake 3.12+，MSVC / Ninja 工具链）；外部依赖经 CMake FetchContent 拉取（`CMake/ExternalProjects.cmake`），**非 vcpkg**。

## Project Structure

```text
CACore/                     模块框架（模块注册、逆向销毁）
VulkanRenderBackendNew/     Vulkan 渲染后端（主）
D3D12RenderBackend/         D3D12 渲染后端
Interface/                  GPU 渲染接口（GPUBackend、WindowHandle 等）
Test/                       GPUBackendTester / 各后端 Tester
openspec/                   OpenSpec 变更与规格文档
CAResources/ ShaderCompilerSlang/ ShaderProcessor/ WindowSystem/ IMGUIContext/ ...  其余模块
```

## Commands

- `/commit-from-propose`: 根据 OpenSpec proposal 和本地 git diff 生成中文 commit message 并提交。可选指定 change name

## 文档地图（找东西先看这张表，别临场 grep）

写东西**只能**写到下面对应的位置；找东西**先**看这里。位置写错的产物等于没写 —— 归档后没人会去翻。

| 要找 / 要写什么 | 在哪 | 说明 |
|---|---|---|
| **项目往哪走：现在哪个阶段、下一个阶段做什么** | `docs/ROADMAP.md` | 只记阶段与方向，**不记任务**（任务在 `tasks.md`）、不记欠账（在 `docs/TODO.md`）。产品目标那一格现在是空的，补它只需要一句话 |
| **这条要求到底存不存在** | `openspec/specs/<能力>/spec.md` | **已生效的规范只有这里算数**。49 个能力目录，一个能力一份 `spec.md`，`### Requirement` + `#### Scenario`。C# 相关三份：`csharp-bindable-surface`（跨界的形状规矩）、`csharp-bindings`（生成器两侧产物规矩）、`csharp-host`（宿主与运行时）。**索引 / 路线图不算规范**，别往这里放（放进来 `openspec validate --all` 会一直红） |
| **正在做的改动** | `openspec/changes/<change>/` | `proposal.md` / `design.md` / `specs/` / `tasks.md`（`tasks.md` 是唯一进度事实源，`- [ ]`/`- [x]`） |
| **做完的改动** | `openspec/changes/archive/<日期>-<change>/` | 现有 64 个；里面的 `evidence/` 放实测记录（例如唯一一份可绑定面审计表 `…csharp-bindable-surface/evidence/bindable-audit.md`） |
| **设计笔记 / 探索结论（还没成 spec）** | `TODO/<序号>-<kebab>.md` | 索引在 `TODO/README.md`。C# 绑定这套的原始目标与决策在 `TODO/001-csharp-scripting-framework.md`（"生成 C# 函数和包装""IDisposable 包装"这些要求最早写在这里）。状态栏写明是否已达 proposal 水位 |
| **测试怎么写、分几层、现有几条** | `docs/testing.md` | 三层：① CoreTests ② GPUBackendTester ③ `Tools/check_*.py` + `Tools/CodeGen/check_*.py`。新功能至少在一层留一条能变红的断言 |
| **跨 change 的待办账** | `docs/TODO.md` | **只放跨 change 的账**；某个 change 范围内的未修项写回它自己的 `tasks.md`（用户 2026-10-25 裁定） |
| **试过并失败的做法** | `docs/failed-approaches.md` | 提方案前先读；同一条重提要给出"这次哪里不同" |
| **读崩溃转储** | `docs/debugging.md` | 配套 `Tools/read_dump.py` |
| **子项目清单：每个模块是什么、怎么依赖** | `docs/project-baseline.md` | 子项目索引（原 `openspec/specs/project-baseline/spec.md`，2026-10-25 移到 `docs/` —— 它是索引不是能力规范） |
| **Vulkan 后端还差 D3D12 哪些** | `docs/vulkan-backend-alignment.md` | 按阶段排的对齐路线图与差距清单（原 `openspec/specs/vulkan-backend-alignment/spec.md`，同一天移到 `docs/`） |
| **工作规则为什么这么定** | `docs/agent-rules-rationale.md` | 本文件「工作规则」一节的扩展：事故记录、旧规则原文、七问推导、判据三件套。**改规则时条文改本文件、理由补那份** |
| **对外文档（给使用者看）** | 与代码同目录 | `Tools/CodeGen/README.md`（生成器的能力边界、参数、退出码、示例 + 真实输出）、`CSharpHost/README.md`（接口编号表、环境变量表）、`docs/vulkan-api-docs/README.md` |

**判断一条要求"算不算数"的顺序**：先查 `openspec/specs/`（生效规范）→ 再查 `openspec/changes/<change>/specs/`（本次改动引入的增量）→ 最后查 `TODO/*.md`（只是设计笔记，**不构成要求**，要变成要求得走 proposal）。三处都没有，才可以说"没有这条要求"。

## 文档维护

> 每份文档只有一个职责、一个更新触发点、一条能失败的过期判据。三样缺一样，这份文档就会烂。

| 文档 | 职责（写什么 / 不写什么） | 必须更新的时候 | 过期怎么发现 |
|---|---|---|---|
| `docs/ROADMAP.md` | 只写阶段与方向。**不写任务、不写欠账、不写改动细节** | 一个 change 归档时；阶段状态变化时；下一阶段立项时 | `python Tools/check_roadmap.py`（规格见该文件第五节，**脚本尚未实现**） |
| `openspec/changes/<change>/tasks.md` | 唯一进度事实源 | 每做完一条，当轮就勾 | `openspec instructions apply --change <名字> --json` 的 remaining |
| `docs/TODO.md` | 只放跨 change 的账。**不抄改法与验收**（那是 `tasks.md` 的事） | 发现跨 change 未修项时；修好即删该条 | 每条「相关」点名的路径能 `Test-Path` |
| `TODO/<序号>-<kebab>.md` 与 `TODO/README.md` | 设计笔记，不构成要求 | 新增 / 升级 / 废弃一篇时，同步索引与状态栏 | 索引里列出的文件名都存在 |
| `docs/vulkan-backend-alignment.md` | 后端对齐的阶段差距表 | 某个差距项状态变化时 | 表里每个 change 名能在 `openspec/changes/` 找到 |
| `docs/project-baseline.md` | 子项目索引 | 新增或删除子项目时 | 表里的模块目录都存在 |
| `docs/testing.md` | 测试分层、清单、最小集合 | 增删测试时 | 清单条目与 `GPUBackendTester.exe --list` 输出一致 |
| `docs/failed-approaches.md` | 失败方案台账 | 试过并失败时 | 只要求每条有「何时可重新考虑」，不审内容 |
| 与代码同目录的 `README.md`（`Tools/CodeGen/`、`CSharpHost/`、`docs/vulkan-api-docs/`） | 对外能力、参数、退出码、示例 | **同一次提交里**改 | `python Tools/CodeGen/verify_readme.py` |
| `CLAUDE.md` | 规则与上面这张地图 | 规则变化时 | 地图里每个路径都存在 |

**三条硬规则**：

1. **一件事只记一处**。同一个未修项不许同时出现在 `docs/TODO.md` 和某个 change 的 `tasks.md`：属于那个 change 就只写 `tasks.md`；跨 change 才写 `TODO.md`，且只写一行指针 + 状态，改法与验收留在那边。
2. **指向别的文档必须写路径**，不许写「详见相关文档」这种指代 —— 读者不知道是哪份，等于没指。
3. **过期判据必须能失败**。「读起来还准确」「内容完整」不算判据；能核的只有三种：路径存在、数字与命令输出一致、表里的名字能在目录里找到。

## Code Style

C++（CMake 构建）：遵循标准 C++ 约定。

## Recent Changes

变更记录以 OpenSpec 为准：已归档见 `openspec/changes/archive/`，进行中见 `openspec/changes/`。

<!-- MANUAL ADDITIONS START -->
## 工作规则

> 每条只写条文与一句为什么。展开说明、事故复盘、旧规则原文见 **`docs/agent-rules-rationale.md`** 对应小节。

- **禁止编译/配置**: 除非用户明确说明，不要尝试配置或编译此项目。构建的唯一合法路径是 `python build.py`；若当前 change 的 tasks 明确包含编译验证步骤，可直接执行、无需再问。
- **explore 模式允许改 OpenSpec 工件**（**OpenSpec 原生规范，不是例外**）: `openspec-explore` 这个 skill 的原文就是「You MAY create OpenSpec artifacts (proposals, designs, specs) if the user asks—that's capturing thinking, not implementing」。explore 只禁止**写实现代码**。⚠ 我此前编造过一条"必须先退出 explore 才能改工件"的限制，OpenSpec 里没有这条 —— 详见 `docs/agent-rules-rationale.md` §8。
- **禁止手动敲构建命令**: 只允许走项目脚本（`python build.py`），禁止 `ninja` / `cmake --build` / `cmake -S`。手动命令会只构建部分目标，产生"测试跑的是旧产物"的不一致；脚本不满足需求就扩展脚本。
- **禁止 sleep 轮询异步任务**: Workflow / Agent / Bash 异步任务完成后会自动通知，不许 `sleep N` 循环等待。
- **构建必须前台跑**（用户 2026-10-05 原话：「你以后build不要弄后台任务，直接前台跑吧」）: `python build.py` 一律前台执行、当场看输出，禁止 `run_in_background`；跑几分钟也照样等完。
- **审查闭环已改为按需**（用户 2026-09-20 明令）: 不再强制在 `tasks.md` 末尾放审查任务、不再跑审查 workflow、不再派 `[AUDIT]` 链条。现行做法见 `docs/agent-rules-rationale.md` §1：① 发现问题就地修；② 一轮做完写一条 Review Log（记账，不是仪式）；③ 是否做对抗审查由用户决定。
- **goal 驱动执行型 change**: 需要多轮的 change 建 goal，objective **只指向 `tasks.md`**、不复制清单、不写过程指标、不写自我限缩的括注。
  - **一轮 = 把当轮能做的全部做完**: 只勾一两条就进下一轮算违规，除非逐条给出"当轮不可执行"的**命令与真实输出**。
  - **开工冻结可执行队列、收尾做队列比对**: `python Tools/spin_check.py queue start/end <轮次>`；`still open` 里有没标注"被挡住/已作废"的条目时，禁止报"完成"或"有进展"。
  - **停只有两个**: ① 清单做完；② 真障碍（附命令与真实输出）。模式、轮次、配额、"该汇报了"都不算。
  - **goal 循环 = apply 模式**: 交付物是代码就必须改代码，不许拿"explore 模式 / 先只讨论"当接口；不许一边接 goal 一边空转。
  - **空转即中断**: 一轮里没有任何一条从"未做"变"已做"，且它的判据在缺陷仍在时跑过红，就必须标 `blocked` 并写清卡在哪，不许只写"本轮空转"再继续。
  - **写 goal 前过七问**（终态 / 谁验收 / 分解来源 / 无过程指标 / 无自我限缩 / 停止条件 / 范围决策是否已定），逐条写在对话里。
  - 其余细则（队列机制、七问原文、四次同类错、收尾口径）见 `docs/agent-rules-rationale.md` §2。
- **修复即断言（red-green）**: 每处修复必须同时交出 ① 一条可执行判据 ② 该判据在缺陷仍在时**变红的实测输出**。判据三件套：**作用对象**（指名具体文件/被消费的产物）、**要判的属性**（不是字符串命中）、**红态实测输出**（只贴绿不算证据）。构造不出红态就在 Review Log 明写「未能证明此判据可失败」。
  - `git grep -F "<引文>"` **必须带 `-- <目标文件>`**，全仓跑会被文档自己的同一句话喂绿。
  - 反例构造：主动造"原缺陷仍在、而新判据通过"的状态；造得出来就是假绿，回去重做。
  - 净进展：每轮 Review Log 回答"相比上轮实质变化是什么"；交付代码的 change **一轮没改代码就是空转轮**（判据用 `Tools/spin_check.py` 的轮界哈希，不靠 `git diff`）。
  - 同根因第二次出现就改"写这类东西的方式"，并记成一条独立 `[AUDIT]` task。
  - 详见 `docs/agent-rules-rationale.md` §3。
- **引用锚点必须逐字可 grep**: 文档里定位代码一律用「文件 + 逐字引文」，**禁止行号**；引文从目标文件复制、写完当场 `git grep -n -F` 自检，零命中即坏锚点。转述必须显式标注「这是转述、非逐字引文」。三次踩坑记录见 `docs/agent-rules-rationale.md` §4。
- **新功能必须同步对外文档**（用户 2026-09-20 要求）: 每做完一条对外可见的能力，**同一次提交里**把「做什么/不做什么、接口、退出码、可复制运行的示例 + 真实输出」写进**与代码同目录**的文档，不许只写进 change 工件。五条细则与 `Tools/CodeGen/` 的踩坑见 `docs/agent-rules-rationale.md` §5。
- **失败方案台账 = `docs/failed-approaches.md`**: 提任何方案前先读；只追加、每条 ≤3 行、禁行号、禁过程叙述、每条写「何时可以重新考虑」；与台账某条实质相同时须给出"这次哪里不同"的可核证据，给不出即作废。台账本身不作为审查对象。细则见 `docs/agent-rules-rationale.md` §6。
- **所有 bug 默认是我的**: 禁止 "pre-existing bug" / "已有问题" / "之前就存在" 等表述；默认是我的改动引入的，除非做过 git stash + 重新编译 + 测试对比仍未排除 —— 那时也说"baseline 中同样存在"。
- **审查必须做 API 外部正确性验证**: 对代码里每个外部 API（Vulkan/vk.hpp、VMA、slang、GLFW 等）联网核对参数类型/个数/顺序、枚举与 flag 组合、返回值与错误处理、使用前提（扩展/feature/生命周期），结果里**引用文档网址**，格式如 `[VkCmdPipelineBarrier](https://docs.vulkan.org/spec/latest/chapters/synchron.html#vkCmdPipelineBarrier)`。
- **对抗者必须复查文档引用真实性**: 复查审查者给的每个 URL 是否真实存在、被引用的 API 是否真在该文档里、语义是否与描述一致，并注明复查结论。
- **联网搜索必须用 MCP 工具**: 上述 API 文档验证禁用 WebSearch/WebFetch，改用 MCP 工具（`web-reader` 的 `webReader`、`web-search-prime` 的 `web_search_prime`）。

## TODO 设计文档

- **位置与命名**: `TODO/` 下，一份文档记录一个探索主题，用 `<序号>-<kebab-case>.md` 命名，序号递增。目录索引见 `TODO/README.md`。
- **TODO ≠ spec**: TODO 是探索/设计笔记（讨论、权衡、待确认点），**不是** OpenSpec 规格。规格只在 `openspec/changes/` 与 `openspec/specs/`。TODO 是"种子"。
- **升级路径**: 只有当某篇 TODO 文档成熟到"决策收敛、未决问题能写成 proposal 的 design"时，才用 `/opsx:propose` 建 OpenSpec change 并把该文档**标记已升级**；否则继续留在 TODO 演进，**不建 change**。
- **文档结构**: 每篇头部元信息（`状态`：探索中 / 待确认 / 可转 proposal（标注 READY FOR PROPOSAL）/ 已废弃；`日期`；`关联`）；正文含：背景与目标、现状核实、关键决策轴与结论、已核实事实（带来源 file:line / URL）、**被反驳/排除的结论**、对抗验证结论、未决问题、升级建议。
- **更新规则**: 思考结论变化时**修改原文档**（更新状态、合回修正），不另开重复文档；每轮对抗验证后把修正合回原文档；未决问题维护在文档"未决问题"节。
- **诚实要求**: 记录被驳回/待确认的结论，不粉饰；未决问题不假装已定。凡涉及外部 API（hostfxr/Mono/NativeAOT 等）的文档论点，照常经 MCP 工具联网核实（见上「联网搜索必须用 MCP 工具」）。

## 构建 / 测试 / 调试

### 构建
- 只通过 `python build.py` 构建（见上"禁止手动敲构建命令"）。配置：`--config Debug`（启用 validation 层）或默认 RelWithDebInfo；`--no-configure` 跳过 cmake 配置；`--offline-fetch` 在本机连不上 github 时用（configure 不去碰依赖的远端 —— 跳过每个依赖的 update 步，并让 `stb` 用 `<构建目录>/_deps/stb-src` 那份已经拉好的源码；只有 `stb` 用 `GIT_TAG master`、每次 configure 都要联网）。从零构建（清掉构建目录里除 `_deps/` 之外的东西）实测：`--offline-fetch` 一次跑通，configure 52.3s / codegen 39.3s / 编译 97.5s / 合计 189.1s（接入清单此时 76 条、76 条全跑）；什么都不改再构建则 3.5s（2026-10-05 实测，三档读数在 `Tools/CodeGen/README.md` 的「构建耗时」一节）。一般不手动指定单目标。
- 产物在 `out/build/<preset>/bin/`：Debug → `x64-debug`，RelWithDebInfo → `x64-relWithDebugInfo`。

### 测试（GPUBackendTester）

> 测试分层、命名、红态构造要求、现有测试清单与「改完必须跑的最小集合」见 **`docs/testing.md`**。新功能必须在三层里至少一层留下一条能变红的断言；只贴日志不算测试。

- `Test/GPUBackendTester`，产物 `bin/GPUBackendTester.exe`。
- CLI：`--backend vulkan|d3d12`、`--test <name>`、`--headless <N>`（N 帧后退出；headless 写 `test_output/*.log` + `result.json`）、`--headless-timeout <N>`（被解析但当前未消费）、`--report <path>`、`--list`。不带 `--test` 顺序跑全部 **10** 个测试（2026-10-25 用 `--list` 实测：`TestSimpleTriangle` / `TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` / `TestTriangleWithImageBuffer` / `TestDoublePass` / `TestComputeBuffer` / `TestReadback` / `TestIMGUI` / `TestScriptHost` / `TestCSharpWindow`；上一版是 9 个，`TestCSharpWindow` 由 `csharp-window-creation` 的 2.7 加进来）。
- 退出码 0 = 正常；崩溃写 `crash_YYYYMMDD_HHMMSS.dmp`（当前目录）并在 stderr 打印**异常名/异常码/异常地址 + 崩溃所在模块**（tester 自带 MiniDump handler；**逐帧调用栈符号化需用 `Tools/read_dump.py` 或 cdb**，见下）。

### 调试（读 crash dump）
- **标准流程：先读 dump 到函数级根因，再谈处置决策**（不要把半成品根因抛给用户做 scope 决策）。
- 通用工具：`python Tools/read_dump.py <dump.dmp> [--pdb-dir <dir>...]` —— 自动解析异常流/模块表/故障线程 CONTEXT/内存，符号化崩溃地址 + 调用栈（函数 + 源文件:行号）；PDB 搜索路径自动从 dump 内模块路径推导。
- 符号化前置：验证 PDB 与崩溃二进制匹配（在 DLL 内找 `RSDS` 魔数后的 16 字节 GUID，再到 PDB 文件里搜索该字节序列）。
- cdb（Windows SDK Debugging Tools，可选）：`cdb -z <dump.dmp> -y <pdb目录> -c "!analyze -v; q"`。
- 技术细节（minidump 结构、dbghelp 用法、read_dump.py 原理）见 `docs/debugging.md`。
<!-- MANUAL ADDITIONS END -->
