# codegen-framework Specification

## Purpose
TBD - created by archiving change engine-codegen-framework. Update Purpose after archive.
## Requirements
### Requirement: 统一 CodeGen 流水线从 annotated 头产出可读生成代码
系统 SHALL 提供一条统一 CodeGen 流水线：解析带 annotation 的 C++ 头文件，产出**线性类型表**（`{name, offset, type, kind, flags}` 数据，而非元编程），并由一个 emitter 生成可读的反射代码（`TypeDescriptor<T>` 线性数组）。同一张类型表 SHALL 可供后续序列化 / 哈希 emitter 复用。⚠ **`offset` 的单位定义为「字节」**（与 `offsetof` 同口径 —— 任务 9.5c 选①）。**已知局限（明写在规格里，不留悬空）**：**bit-field 按字节粒度合并** —— 位域偏移并非字节对齐，相邻位域会落在同一个字节偏移上；本 change **不拒绝**含 bit-field 的类型，也**不声称**能区分它们（未采用"含位域即拒绝发射"的方案②）。

#### Scenario: 解析一个 annotated 类型并生成线性描述
- **WHEN** 一个用 CA_* 标注的 C++ 类型（如含 `name/offset/type` 的字段）被 pipelined 解析
- **THEN** 生成的反射代码是线性数组，每个字段暴露 `name`, `offset`, `type`, `kind`, `flags`，且对同一注解展开两次产物逐字节一致

#### Scenario: 类型表只覆盖数据字段（方法 / 枚举 / 基类不在本 change 范围）
- **WHEN** 被解析类型包含方法 / 枚举 / 基类
- **THEN** 类型表**只**含数据字段；`methods()` / `enumInfo()` / `baseClass()` **不在本 change 实现范围**，故本条**不构成**可验收的 SHALL（原文的"（如实现支持）"是**不可能失败的验收条款**，已删除 —— 见 tasks §9.8）。

#### Scenario: 两条标注通道按成员可见性划分
- **WHEN** 被标注类型含**非 public** 成员
- **THEN** **类外**标注通道（`CA_REFLECTION(...)`、以及现形态的 `CA_REFLECT(...)` —— 纯文本、编译器 no-op）SHALL **非零退出**并提示"该类型需改用类内标注"，**不得产出编译不过的代码**；而**类内**标注通道（宏写在 class 体内、展开为 `friend` 声明）对私有成员**照常发射** —— 私有成员在该通道是正常的。⚠ **类内通道尚未实现**，其正向用例待该通道落地后补验（见 tasks §9.1）。
- **AND** ✅ **实现状态（2026-09-13）：已通过** —— 实测 `CodeGen.exe Tools/CodeGen/TestPrivate.h PrivateLike -I CACore/header --emit-reflection` → **exit 5**，stderr 为 `CodeGen: refusing to emit for 'codegen_private_test::PrivateLike' — it has non-public members … Use an in-class annotation …`；同命令**去掉** `--emit-reflection`（仅提取类型表）仍 **exit 0** 且 offset 不变（`pub 12 / m_a 16 / m_b 20 / m_c 24`）—— 提取与发射的区分成立；全 public 的 fixture 照常发射。**修前红态（实测）**：同一条命令 **exit 0** 并照常对私有成员发射 `offsetof`（即台账第 1 条记的那个形态）。⚠ **类内标注通道尚未实现**，其正向用例待该通道落地后补验（见 tasks §9.1）。

### Requirement: 反射标注用标记宏自动爬取 + 未迁移/外部走非侵入通道
系统 SHALL 提供 attribute 风格的**标记宏** `CA_REFLECT(...)`（可带 `serializable`/`ui`/`min=0` 等 options），由 libclang 从 **class 体 AST 自动提取**成员名 / type / offset（作者**不手写成员清单**）；⚠ **本 change 只提取数据字段** —— 方法 / 枚举 / 基类**不在实现范围**（工具侧无 `FunctionDecl` / `EnumDecl` / `BaseSpecifier` 处理，见 tasks §9.8），故不在本 SHALL 内；未迁移引擎类型与外部库类型 SHALL 仍可用 `CA_REFLECTION(Type, 成员...)` **非侵入式**声明（命名空间作用域、不改类型源码），两者喂**同一张类型表**、同一批 emitter。系统 SHALL NOT 依赖字面 `[[careflection::...]]` 或 `[[clang::annotate]]` attribute 作元数据载体（MSVC C5030 警告 / libclang 读自定义命名空间 attribute 弱 / `[[clang::annotate]]` AST 可见性 bug）。**本 change 以示例类型证明管道；引擎自有类型从 CA_REFLECTION 迁移到 CA_REFLECT 属后续 task，不在此 SHALL**。

#### Scenario: 标记宏标 type 自动反射
- **WHEN** 一个 `CA_REFLECT(...)` 标注的示例类型被 pipelined 解析
- **THEN** libclang 从 class 体 AST 提取**数据字段**（含**未在标记宏里手写**的成员），且生成类型表字段数 == `::CATypeDescriptor<Type>::member_count`（ABI guard 通过，绑定到既有反射源而非循环复用同一工具）

#### Scenario: 未迁移/外部类型非侵入反射
- **WHEN** 一个类型仅以 `CA_REFLECTION(Type, a, b)` 声明（未改类型源码）
- **THEN** 该类型进入字段-only 表，以声明顺序产出 `a, b`（offset 由生成代码 `offsetof` 兜底）

### Requirement: parser 元数据来源为 libclang，引擎保持纯 MSVC
系统 SHALL 用 libclang（复用 CACodeGen 的 CLANG_LIB）作为元数据来源；引擎 SHALL 保持纯 MSVC 编译，CodeGen 作为**独立构建期工具**运行，**不进入**引擎 CMake 编译图，引擎 SHALL NOT 链接 libclang。

#### Scenario: 引擎不链接 libclang
- **WHEN** 执行全量构建（`python build.py`）
- **THEN** 引擎编译产物不含 libclang.dll 依赖，且引擎当前不改动现有反射运行时路径（CA_REFLECTION 行为不变）

#### Scenario: 构建期 tool 由脚本调用
- **WHEN** 未满足注解的条件（空白名单）运行 codegen
- **THEN** codegen 无输入时正常退出（EXIT 0）且不生成文件，不阻断 build

### Requirement: 保留 CA_REFLECTION 运行时语义
系统 SHALL 保留现有 CA_REFLECTION 的运行时行为（序列化 + 每帧哈希走 `visit_members`），生成元数据 SHALL 是**加法**，不替换、不改变现有反射路径。

#### Scenario: 关键路径行为不变
- **WHEN** 插入 codegen 生成元数据后跑现有 tester（`--headless`）
- **THEN** 构建成功且引擎产物**不含 libclang.dll 依赖**（证明 codegen 为纯构建期 addon、零运行时链接 → 现有反射运行时路径不变）；headless vulkan/d3d12 各 exit 0 / 无崩溃，`result.json` status == 'pass'。
  > 注：`test_output/` 被 git-ignore、VUID 验证日志不可提交（TODO/004 §3.4），故本条取**自洽可证伪断言**；若需严格"无新增 VUID vs 历史验证日志"diff，须前置"提交对照数据" task（可选强化，非本条验收门槛）。——RC-H 调和

### Requirement: 生成源码以显式源清单纳入构建
系统 SHALL 用**显式方式**纳入生成的源文件（非 `file(GLOB_RECURSE)`）：生成的**头文件**经显式 include 路径 + `#include` 纳入；生成的 **.cpp**（若存在）经显式 `target_sources` 纳入（禁 `file(GLOB_RECURSE *.cpp)`）。生成的反射代码 SHALL 与现有 `CATypeDescriptor<T>` 做构建期一致性校验。——RC-F 调和（诚实前提：本 change 生成物为头文件）。⚠ **该"一致性校验"的实际范围**：**只**断言生成字段数等于 `CATypeDescriptor<T>::member_count`（另有一条 `name != nullptr` 的非空检查），**不比名字内容、不比 offset** —— 详见下一个 Scenario。

#### Scenario: 缺失生成文件被构建捕获
- **WHEN** 生成源文件缺失
- **THEN** 构建以失败而非静默通过：缺失生成头 → 经显式 `#include` 报编译错误；缺失生成 .cpp → 经显式 `target_sources` 报构建错误。**该方向已验**（4.2：删生成头 → `C1083`）。

#### Scenario: 与注解漂移（**不在本 change 实现范围**）
- **WHEN** 生成物与源码注解**漂移**（字段增删）
- **THEN** **真实类型**的漂移方向**不在本 change 实现范围**（本 change 不接线任何项目类型，见 proposal 的 Non-goals）；**但本 change 内的合成样本已实测跑通该路径** —— 改注解的成员数后 `python build.py --config Debug --no-configure CoreTests` 以 `error C2338: 'CodeGen ABI drift: generated field count != CA_REFLECTION member count'` 失败（见 tasks 的 9.16）。**注意措辞**：接住漂移的机制是 `static_assert_abi_consistency`，它**只**断言生成字段数等于 `CATypeDescriptor<T>::member_count`（另一条 `name != nullptr` 是非空检查）。
- **AND** 该 guard **只**断言生成字段数等于 `CATypeDescriptor<T>::member_count`（另一条 `name != nullptr` 是"生成头是否存在"的非空检查，**不做名字内容比对**）；**既不校验名字内容、也不校验 offset**。名字漂移与 offset 漂移**同样不在本 change 的覆盖范围**。
- **AND** 归属：随"真实类型正式接线"的**后续 change** 处理（本 change 不接线任何项目类型，见 proposal 的 Non-goals）。

### Requirement: build.py 接入 codegen 步骤
`build.py` SHALL 在 `patch_ninja()` 调用**之后**、引擎编译（`build(target)`）**之前**调用 codegen sidecar —— **configure 与 `--no-configure` 两条路径都要执行**（即 `codegen_failures = codegen()` 是**零缩进**的独立语句，位于 `if configure_step:` 块**之外**；任务 9.13 前它在块内，`--no-configure` 增量构建因此从不跑 codegen），且该步骤在 MSVC 环境下运行（继承 vcvars 与 sanitized PATH）。**本要求按引文定位实现位置，不写行号**（`build.py` 会被后续任务改动，行号必漂）。

#### Scenario: 构建顺序正确
- **WHEN** 执行 `python build.py`（无 `--no-configure`）
- **THEN** 日志显示 codegen 步骤在 configure/patch_ninja 之后、engine 编译之前执行，且未拆分出别的无关构建动作

#### Scenario: `--no-configure` 的增量构建也须走 codegen
- **WHEN** 执行 `python build.py --no-configure <target>`（文档化的增量构建路径）
- **THEN** codegen 步骤**确实执行过**：构建日志出现 `[1.5] CodeGen` 段；且删除 `out/build/<cfg>/codegen/` 下任一生成物后重跑，该生成物**被重新生成**，内容与全量构建**逐字节一致**。（单一结果，不留"要么…要么…"的分支 —— 分支式条款无法失败。）
- **AND** ✅ **实现状态（2026-09-13）：已通过** —— `codegen()` 已移出 `if configure_step:` 分支。**实测（先删掉生成物）**：`python build.py --config Debug --no-configure CoreTests` → 日志出现 `[1.5] CodeGen` 段、生成物**被重新生成**且与入库 golden **逐字节一致**、构建成功。**修前红态（实测）**：同一条命令既无 `[1.5] CodeGen` 段、生成物**也未被重新生成**，而构建照样报 BUILD SUCCESSFUL —— 正是本条要禁止的静默。原文本条曾被写成"无 `--no-configure`"、对该路径沉默，该自限已解除。

### Requirement: 生成物必须被真实引擎目标消费

生成代码 SHALL 被至少一个真实引擎目标的编译单元 `#include`，并在该编译单元内对生成表做编译期断言。仅有"与黄金样本逐字节对拍"不算被消费 —— 删掉生成物必须有构建反应。

#### Scenario: 真实类型的生成物进入真实编译单元

- **WHEN** 构建 `Rendering` 目标，其中 `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp` 包含真实类型 `graphics_backend::GPUBufferDescriptor` 的定义与它的生成反射头
- **THEN** 该编译单元编译通过，且生成表的字段数与手写 `CA_REFLECTION` 成员列表一致（不一致即编译错误）

#### Scenario: 生成物缺失时构建失败

- **WHEN** 删除或改名入库的生成头 `Interface/RenderInterface/generated/GPUBufferDescriptor.Reflection.h`，再构建
- **THEN** 消费该头的编译单元以"找不到文件"失败，构建整体失败（不得静默通过）

#### Scenario: 注解漂移时构建失败

- **WHEN** 改动 `graphics_backend::GPUBufferDescriptor` 的成员（增删或改名）而不重新生成
- **THEN** `build.py` 的逐字节对拍失败并点名该文件与不一致的原因，构建整体失败

#### Scenario: 生成表被替换成别的类型时失败

- **WHEN** 入库的生成头被换成另一个类型的生成结果
- **THEN** 编译期断言（类型名 / 字段数 / 成员名）失败，构建整体失败

### Requirement: 解析头文件所用的编译环境按模块推导，且不猜宏

`build.py` SHALL 从 `compile_commands.json` 推导每个模块的解析环境，**不得**依赖任何手写的头文件或 include 目录清单。模块根 MUST 按**路径段**匹配编译单元（不得用子串匹配），并 MUST 排除构建树下的第三方编译单元（`out/`）；include 目录取该模块全部编译单元的并集；宏只允许来自单一条编译单元；同一模块内出现多套宏时 MUST 拒绝构建并报出这些宏，不得任选一套。

#### Scenario: 模块根不得命中第三方编译单元

- **WHEN** 某个第三方依赖的源文件路径里含有与模块根相同的子串（实测 `Interface` 会命中 `out/build/<配置>/_deps/assimp-src/code/CApi/CInterfaceIOWrapper.cpp`）
- **THEN** 推导必须选中该模块自己的编译单元（路径第一段等于模块根且不在 `out/` 下），不得使用该第三方的宏与 include 目录

#### Scenario: include 目录取并集

- **WHEN** 某模块内只有一个子集编译单元带某个 include 目录（实测 `Interface` 代表编译单元 8 个目录、并集 13 个）
- **THEN** 推导出的环境使用并集，该模块的头文件仍能解析到 AST（工具返回"未标注"而不是解析失败）

#### Scenario: 同一模块多套宏时拒绝构建

- **WHEN** 某模块的编译单元之间存在两套不同的宏（实测 `Test` 有两套），且接入清单没有点名具体编译单元
- **THEN** 构建失败，错误信息列出这两套宏并要求点名编译单元；不得用其中任意一套静默继续

#### Scenario: 预编译头的强制包含被保留

- **WHEN** 该模块的编译单元使用 CMake 预编译头（`/FI` 与 `/Yu` 指向 `cmake_pch.hxx`）
- **THEN** 传给 libclang 的参数包含该头文件的 `-include`，且该模块头文件的解析结果与不补时一致（实测补与不补都是"未标注"）

#### Scenario: 环境残缺时工具拒绝出码

- **WHEN** 解析环境缺头文件（STL 不可达等），使 AST 被截断
- **THEN** 工具以 exit 6 拒绝出码并在 stderr 点名缺失的 include 与退化的成员，构建失败（既有行为，本 change 须保持）

### Requirement: 解析失败不得被报告成"没有标注"

"标注列表为空 → exit 0 且不产出"只允许在**文件确实解析成功**时发生。工具 MUST 在"空标注"的提前返回**之前**执行错误级诊断检查与保真门控；存在错误级诊断或未解析的 include 时 MUST 非零退出并点名文件与诊断。

#### Scenario: 文件有语法错误且没有标注

- **WHEN** 输入头文件里存在语法错误（如 `struct Broken { int a }` 缺分号）且文件里没有任何标注
- **THEN** 工具非零退出并报出该文件的错误级诊断，**不得**返回 exit 0「no annotated types」（当前实现返回 exit 0，为本 requirement 要修掉的红态）

#### Scenario: include 找不到且没有标注

- **WHEN** 输入头文件 `#include` 了一个不存在的头文件，且文件里没有任何标注
- **THEN** 工具非零退出并点名该 include，**不得**返回 exit 0

#### Scenario: 标记宏未定义

- **WHEN** 输入头文件用了 `CA_REFLECT(...)` 却没有包含定义该宏的 `CodeGenMeta.h`
- **THEN** 工具非零退出并报出错误级诊断（宏被当成普通标识符解析），**不得**返回 exit 0「no annotated types」

#### Scenario: 真的没有标注仍然放行

- **WHEN** 输入头文件语法正确、include 全部解析成功，文件里确实没有任何标注
- **THEN** 工具返回 exit 0、不写出任何产物，构建继续（空清单是合法配置，既有语义不变）

