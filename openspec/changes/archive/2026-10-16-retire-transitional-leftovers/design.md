## Context

旧设施（`Reflection.h` / `Serialization.h` / `Hasher.h` 三份头 + `CA_REFLECTION` + `CATypeDescriptor` + `cacore::serializer` / `deserializer`）在 `retire-legacy-serde-hash`（2026-10-05 归档，53/53）里已经删干净：三个路径在文件系统上不存在，相关符号在源码树里各 0 处（2026-10-15 实测，见 proposal 的 Why）。

**退场时留下的东西分三类**，本 change 只收第一、三类：

| 类 | 东西 | 现状 | 本 change |
|---|---|---|---|
| 过渡装置 | `build.py` 的 `CODEGEN_LEGACY_SUFFIXES` + `cleanup_legacy_codegen_products()`（常量与函数连同注释 35 行，加调用点与打印行共 41 行） | 每次构建空转（无旧命名产物可删） | **删** |
| 过渡期文字 | `HashKeys.h` / `CodeGenHashFwd.h` 开头两处提已删的头；三份主规格里 9 处"现在时"的旧设施陈述（逐条见下面的 D3）、**六份**主规格的 `Purpose` 还是 `TBD - created by archiving change …` | 与代码相反 | **改** |
| 看着像旧账、其实现行 | 单节发射开关、`check_legacy_symbols.py` 守卫、`SubmitAndWait`、`CA_PRIVATE_REFLECTION` 的 5 处解释性注释 | 都在用/都有用 | **不动**，只把"为什么留"写进 README |

约束：本仓库的规格是**归档时**由 delta 覆盖进主规格的；`MODIFIED` 必须带**完整**的更新后内容，漏搬的场景会在归档时被丢掉。

## Goals / Non-Goals

**Goals:**

- 让"旧设施已退场"这件事在**代码、注释、规格**三处说的是同一件事：不再有空的过渡装置、不再有"现在时"的旧设施陈述、不再有指向已删符号的判据。
- 把"为什么留着单节开关与旧符号守卫"写成对外文档，避免下一个人把它们当旧账删掉（删了会让 `codegen-framework` 里"三节由同一段发射代码产出"的判据没法复核）。
- 全程不改行为：工具命令行、退出码表、接入清单、生成物形状、任何哈希值都不动。

**Non-Goals:**

- 不删单节发射开关（`--emit-reflection` / `--emit-serialization` / `--emit-hash` / `-o`）与 `Tools/CodeGen/check_legacy_symbols.py`（理由见 proposal 的 Non-goals）。
- 不重写 `codegen-type-migration` 里其余仍然成立的要求（嵌套类型标注、模块范围、D3D12 推导环境、被解析门拒收的键类型）——借清理之名改行为是禁的。
- 不动仓库外那份全局规则文件的**其它**内容（只改末尾那张过期的表，且要用户点头）。
- 不动渲染后端、测试器、TODO 清单里与本主题无关的条目。

## Decisions

### D1 删清理壳，而不是"留着以防万一"

实测它今天**没有可删的东西**（源码树旧命名 include 0 处、构建目录旧命名产物 0 个），而且只在 `build.py` 内部被引用（4 处）。替代方案"留着，防止别人机器上残留旧命名产物"被否：残留文件没有任何 `#include` 指向它们，存在与否不影响构建，而留着意味着**每次构建都去扫所有预设的 `out/build/*/codegen/` 目录**（成本常付、收益为零）。真要清一次，一条 `rm` 就够。

副作用与处置：删掉后，别人检出目录里若还留着旧命名文件，不再被自动清（可核依据：源码树 `#include "*.Reflection.h"` 一类的旧命名引用 0 处、`out/build/*/codegen/` 下旧命名产物 0 个 —— 没有任何编译单元指向它们）—— 写进 `Risks`，并由 tasks 的"删前 / 删后两条读数 + 构建绿"兜住。

### D2 注释：改"现在时"的假陈述，留"过去时"的历史

- 改：`HashKeys.h` 开头"旧驱动（`aggregateHasher` 那套）留在 `Hasher.h` 里等 9.3 删掉。本头**不依赖**那三份待删的头" —— 现在时，且那份头不存在；`CodeGenHashFwd.h` 开头"（例如旧设施的 `Hasher.h`）" —— 举的例子已不存在。
- 留：`HashKeys.h` 里那句"它原来住在 `Hasher.h` 里，与"要删的旧驱动"无关，所以随宿主类型一起搬到本头" —— 过去时，准确，而且解释了它为什么在这个文件里。**确定保留**（tasks 3.1/3.2 的判据按"这个头里恰好两处提 `Hasher.h`：3.1 改写后的正文 + 这一句历史陈述"写）。
- 留：`CA_PRIVATE_REFLECTION` 那 5 处注释 —— 宏已不存在，但那 5 处在解释"`CA_CODEGEN_FRIEND` 是它的等价物"，删了反而少一条线索。

判据：**这句话今天还成立吗**。不成立的改掉；成立的不动（不为了统一文风而改）。

### D3 规格：整条不成立就删，一句话不成立就改，其余逐条搬

分界与依据：

| 主规格 | 处置 | 依据（代码出处） |
|---|---|---|
| `codegen-framework`「保留 CA_REFLECTION 运行时语义」 | **删整条** | `CA_REFLECTION` / `visit_members` 源码树各 0 处；要求说的是"不替换现有反射路径"，而现有路径已经不存在 |
| `codegen-framework`「生成源码以显式源清单纳入构建」 | **改** | 守卫实现在 `CACore/header/CodeGenMeta.h`，只断言 `TypeDescriptor<T>::name != nullptr`；字段数/名字/offset 由 `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp` 与两个 `ShaderLibraryCodeGenAssertions.cpp` 对着写死的期望值比 |
| `codegen-framework`「无序容器的序列化与哈希必须走规范顺序」 | **改两条场景** | ① "元素是内含无序容器的结构体时必须拒绝"：`CodeGenMain.cpp` 里已无该拒收（0 处），替代行为写在 `codegen-runtime`「生成物的读写必须自带游标」；② "读进非空目标时先清空"的依据：读取路径在 `CACore/header/CodeGenSerialization.h`（有 `clear()` 的先清空、定长数组按位置覆写），不再引已删除的 `cacore::deserializer` |
| `codegen-runtime`「生成物的读写必须自带游标」里的对照场景 | **改** | 旧运行时已删除，跑不出对拍；对照物改成 `Test/CoreTests/CodeGenRuntimeTest.cpp` 里**冻结成字面量**的期望字节（那份文件今天就是这么写的） |
| `codegen-type-migration`「已有类型的迁移形态是双份描述」 | **删整条** | 老宏与 `CATypeDescriptor` 各 0 处；该条自己的收尾口径要求"白名单 0 行" |
| `codegen-type-migration`「私有成员与指针成员两类形状的处置必须写在明处」 | **改** | `CA_PRIVATE_REFLECTION` 已删除，今天的入口是 `CA_CODEGEN_FRIEND`（`CodeGenMeta.h` 里有定义与用法注释） |
| `codegen-type-migration`「迁移必须逐类型可核…」 | **改** | 判据里的 `cacore::aggregateHasher<>` / `cacore::has_custom_hash_func` 已随旧头删除；今天的判据是 `cacore::codegen::has_custom_hash_func<T, cacore::codegen::hasher<>>`（仓库里的断言文件用的就是这个） |
| 六份主规格的 `Purpose` | **直接改主规格** | delta 只覆盖 Requirements 段，`Purpose` 不会被归档合并到 → 必须就地改。实测六份都还留着 `TBD - created by archiving change …`（`build-throughput`、`codegen-framework`、`codegen-runtime`、`codegen-type-migration`、`vma-vulkan-function-binding`、`vulkan-backend-teardown`），每份照自己的要求写一句 |

**为什么 MODIFIED 要整条搬**：归档时 delta 的整条替换主规格里的同名整条；只搬一部分，没搬到的场景会静默消失 —— 那正是"清理"最不该造成的损失。tasks 里因此有一条**场景数对账**（delta 与主规格逐条相等，只差被点名删掉的那一条）。

**为什么不顺手把 `codegen-type-migration` 整份重写**：那份规格里"嵌套类型能标注""模块范围""D3D12 推导环境""被解析门拒收的键类型"四条描述的能力今天仍然成立，只是夹着历史实测读数。重写它们属于另一个 change 的范围（会改到规格的行为口径），本次只改**已经不成立**的句子。

### D4 单节开关与守卫：不删，但把理由写进 README

`codegen-framework` 里「三节与改造前由同一段发射代码产出」的判据写的是"`-o` 的单节输出与改造前那三份同名产物逐字节相同（76 个类型 × 3 节 = 228 次）"；`check_legacy_symbols.py` 是防旧符号回流的守卫（命中必须 0 行、扫到空集以退出码 2 报错）。两者都是**现行**装置。README 现在只说了单节开关"仍然可用"，没说它是判据载体 —— 补一小节，避免被当成旧账删掉。

### D5 仓库外那份全局表：单列一条，需要点头

`~/.dsh/AGENTS.md` 末尾那张"两套反射 / 序列化设施"的表整表过期（写着旧三头存在、"绝大多数引擎类型仍用老宏"、"新生成物仍调用旧运行时"）。它不在仓库里，改它等于改规则文件 → tasks 里单列一条、标注"仓库外 + 需你点头"，并给出判据（那两句 0 处）。

## Risks / Trade-offs

- **[删清理壳后，别的检出目录里残留的旧命名产物不再被自动清]** → 残留文件没有任何 `#include` 指向（源码树 0 处），不影响构建；tasks 给出"删前 / 删后旧命名文件计数 + 构建绿 + 测试绿"的读数；真需要清时一条 `rm` 即可。
- **[MODIFIED 漏搬场景 → 归档时静默丢内容]** → tasks 4.2 逐条对账场景数（delta 与主规格相等，只差被点名删掉的那一条），数字贴进 `evidence/`。
- **[把规格改成与代码不符]** → 每条改动都点名代码出处（见 D3 的表），tasks 里逐条跑 `grep` 复核这些出处仍然存在。
- **[借清理之名改行为]** → Non-goals 写死不改工具命令行 / 接入清单 / 生成物 / 哈希值；收尾判据含"构建 + `CoreTests`（12/0）+ 两个后端 tester（各 8 条 pass）+ 改造前后哈希读数逐字相同"。
- **[删过头：把现行装置当旧账删掉]** → 本 design 的 D4 与 proposal 的 Non-goals 逐个点名"不删什么、为什么"，tasks 里有一条专门核对这四个东西仍在（`grep` 得到读数）。

## Migration Plan

顺序（每步都能单独回退，整个 change 落在一个提交里）：

1. 先量"空转"的三条读数（改动前的证据）。
2. 删 `build.py` 的过渡装置 → 立即构建 + 跑 `CoreTests`（这是唯一有行为风险的一步，先用读数兜住）。
3. 改两处过期注释（无行为影响）。
4. 改两份主规格的 `Purpose` + 写三份 delta → 对账场景数 → `openspec validate`。
5. 改 `Tools/CodeGen/README.md`（新增"为什么留着"一节 + 修章节号）→ 跑 `verify_readme.py`。
6. 仓库外那张表（等你点头）。
7. 收尾：再跑一次构建 + `CoreTests` + 两个后端 tester。

回滚：整条 change 是纯删除与文字改动，回退到改动前的提交即可（无数据迁移、无格式变化）。

## Open Questions

- **仓库外那张表怎么改**：改成"一套设施 + 旧三头已删除"的现状陈述，还是整表删掉（它存在的意义是"别把两套混起来"，现在只剩一套）？我倾向改成现状陈述并保留"旧三头已删除"这一句 —— 需要你点头（改的是你本机的规则文件）。
- **`Purpose` 的写法口径**：本 change 先补一句话（现状 + 它管什么），不重写整份规格的 Purpose 风格；如果你要统一的 Purpose 写法，那是另一个 change。
