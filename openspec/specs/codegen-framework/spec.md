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
- **THEN** **类外**标注通道（`CA_REFLECTION(...)`、以及现形态的 `CA_REFLECT(...)` —— 纯文本、编译器 no-op）SHALL **非零退出**并提示"该类型需改用类内标注"，**不得产出编译不过的代码**；而**类内**标注通道（宏写在 class 体内、展开为 `friend` 声明）对私有成员**照常发射** —— 私有成员在该通道是正常的。⚠ **类内通道尚未实现**，其正向用例待该通道做完后补验（见 tasks §9.1）。
- **AND** ✅ **实现状态（2026-09-13）：已通过** —— 实测 `CodeGen.exe Tools/CodeGen/TestPrivate.h PrivateLike -I CACore/header --emit-reflection` → **exit 5**，stderr 为 `CodeGen: refusing to emit for 'codegen_private_test::PrivateLike' — it has non-public members … Use an in-class annotation …`；同命令**去掉** `--emit-reflection`（仅提取类型表）仍 **exit 0** 且 offset 不变（`pub 12 / m_a 16 / m_b 20 / m_c 24`）—— 提取与发射的区分成立；全 public 的 fixture 照常发射。**修前改之前（实测）**：同一条命令 **exit 0** 并照常对私有成员发射 `offsetof`（即台账第 1 条记的那个形态）。⚠ **类内标注通道尚未实现**，其正向用例待该通道做完后补验（见 tasks §9.1）。

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

#### Scenario: 与注解对不上（**不在本 change 实现范围**）
- **WHEN** 生成物与源码注解**对不上**（字段增删）
- **THEN** **真实类型**的对不上方向**不在本 change 实现范围**（本 change 不接线任何项目类型，见 proposal 的 Non-goals）；**但本 change 内的合成样本已实测跑通该路径** —— 改注解的成员数后 `python build.py --config Debug --no-configure CoreTests` 以 `error C2338: 'CodeGen ABI drift: generated field count != CA_REFLECTION member count'` 失败（见 tasks 的 9.16）。**注意措辞**：接住对不上的机制是 `static_assert_abi_consistency`，它**只**断言生成字段数等于 `CATypeDescriptor<T>::member_count`（另一条 `name != nullptr` 是非空检查）。
- **AND** 该 guard **只**断言生成字段数等于 `CATypeDescriptor<T>::member_count`（另一条 `name != nullptr` 是"生成头是否存在"的非空检查，**不做名字内容比对**）；**既不校验名字内容、也不校验 offset**。名字对不上与 offset 对不上**同样不在本 change 的覆盖范围**。
- **AND** 归属：随"真实类型正式接线"的**后续 change** 处理（本 change 不接线任何项目类型，见 proposal 的 Non-goals）。

### Requirement: build.py 接入 codegen 步骤
`build.py` SHALL 在 `patch_ninja()` 调用**之后**、引擎编译（`build(target)`）**之前**调用 codegen sidecar —— **configure 与 `--no-configure` 两条路径都要执行**（即 `codegen_failures = codegen()` 是**零缩进**的独立语句，位于 `if configure_step:` 块**之外**；任务 9.13 前它在块内，`--no-configure` 增量构建因此从不跑 codegen），且该步骤在 MSVC 环境下运行（继承 vcvars 与 sanitized PATH）。**本要求按引文定位实现位置，不写行号**（`build.py` 会被后续任务改动，行号必漂）。

#### Scenario: 构建顺序正确
- **WHEN** 执行 `python build.py`（无 `--no-configure`）
- **THEN** 日志显示 codegen 步骤在 configure/patch_ninja 之后、engine 编译之前执行，且未拆分出别的无关构建动作

#### Scenario: `--no-configure` 的增量构建也须走 codegen
- **WHEN** 执行 `python build.py --no-configure <target>`（文档化的增量构建路径）
- **THEN** codegen 步骤**确实执行过**：构建日志出现 `[1.5] CodeGen` 段；且删除 `out/build/<cfg>/codegen/` 下任一生成物后重跑，该生成物**被重新生成**，内容与全量构建**逐字节一致**。（单一结果，不留"要么…要么…"的分支 —— 分支式条款无法失败。）
- **AND** ✅ **实现状态（2026-09-13）：已通过** —— `codegen()` 已移出 `if configure_step:` 分支。**实测（先删掉生成物）**：`python build.py --config Debug --no-configure CoreTests` → 日志出现 `[1.5] CodeGen` 段、生成物**被重新生成**且与入库 golden **逐字节一致**、构建成功。**修前改之前（实测）**：同一条命令既无 `[1.5] CodeGen` 段、生成物**也未被重新生成**，而构建照样报 BUILD SUCCESSFUL —— 正是本条要禁止的静默。原文本条曾被写成"无 `--no-configure`"、对该路径沉默，该自限已解除。

### Requirement: 生成物必须被真实引擎目标消费

生成代码 SHALL 被至少一个真实引擎目标的编译单元 `#include`，并在该编译单元内对生成表做编译期断言。仅有"与黄金样本逐字节对拍"不算被消费 —— 删掉生成物必须有构建反应。

#### Scenario: 真实类型的生成物进入真实编译单元

- **WHEN** 构建 `Rendering` 目标，其中 `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp` 包含真实类型 `graphics_backend::GPUBufferDescriptor` 的定义与它的生成反射头
- **THEN** 该编译单元编译通过，且生成表的字段数与手写 `CA_REFLECTION` 成员列表一致（不一致即编译错误）

#### Scenario: 生成物缺失时构建失败

- **WHEN** 删除或改名入库的生成头 `Interface/RenderInterface/generated/GPUBufferDescriptor.Reflection.h`，再构建
- **THEN** 消费该头的编译单元以"找不到文件"失败，构建整体失败（不得静默通过）

#### Scenario: 注解对不上时构建失败

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
- **THEN** 工具非零退出并报出该文件的错误级诊断，**不得**返回 exit 0「no annotated types」（当前实现返回 exit 0，为本 requirement 要修掉的改之前）

#### Scenario: include 找不到且没有标注

- **WHEN** 输入头文件 `#include` 了一个不存在的头文件，且文件里没有任何标注
- **THEN** 工具非零退出并点名该 include，**不得**返回 exit 0

#### Scenario: 标记宏未定义

- **WHEN** 输入头文件用了 `CA_REFLECT(...)` 却没有包含定义该宏的 `CodeGenMeta.h`
- **THEN** 工具非零退出并报出错误级诊断（宏被当成普通标识符解析），**不得**返回 exit 0「no annotated types」

#### Scenario: 真的没有标注仍然放行

- **WHEN** 输入头文件语法正确、include 全部解析成功，文件里确实没有任何标注
- **THEN** 工具返回 exit 0、不写出任何产物，构建继续（空清单是合法配置，既有语义不变）

### Requirement: 字段种类必须按规范化类型判定

工具的字段分类 MUST 先取类型的规范化形式再判定种类。类型别名 MUST NOT 因为"拼写不是内建名"而落进 `TypeKind::Opaque`。

#### Scenario: 别名与内建写法得到同一个种类

- **WHEN** 同一个结构体里同时有 `unsigned int` 与 `uint32_t` 两个成员（实测现状：`unsigned int` → `UInt`，`uint32_t` → `Opaque`）
- **THEN** 两个成员的种类都是 `TypeKind::UInt`；`unsigned long long` 与 `uint64_t` 同理都是 `UInt`

#### Scenario: 内建类型与枚举不受影响

- **WHEN** 结构体里有 `int` / `float` / `double` / `bool` / 作用域枚举成员
- **THEN** 种类与改动前一致（`Int` / `Float` / `Double` / `Bool` / `Enum`），既有生成物逐字节不变

### Requirement: 容器必须被识别，并在字段表里携带元素描述

工具 MUST 按**结构**识别容器（与运行时同口径：有 `size()` 且有 `value_type` 的记录类型、以及 C 数组），MUST NOT 依赖 `std::vector` / `eastl::vector` 这类名字。字段表 MUST 给出容器成员的**元素描述**（元素种类、元素类型名、是否嵌套容器、元素里是否含指针 / 句柄 / 包装器），并标出容器是否无序。

#### Scenario: 容器不再被记成 Opaque

- **WHEN** 结构体里有 `castl::vector<unsigned>`、`castl::string`、`castl::unordered_map<unsigned, unsigned>`、`castl::set<int>` 成员（实测现状：四者都是 `TypeKind::Opaque`）
- **THEN** 这四个成员都带 `Container` 标志（`TypeKind` 不改：枚举里没有 `Map` / `Set`，`Vector` / `Array` 已被 glm 向量与 C 数组占用），且 `vector` 的元素描述给出元素种类 `UInt`；`unordered_map` 另带 `Unordered` 标志并给出 `key` / `element` 两层描述

#### Scenario: 嵌套容器能看出层级

- **WHEN** 成员类型是 `castl::vector<castl::vector<int>>`
- **THEN** 字段表里该成员带 `Container` 标志，其元素描述本身也是一个容器（同样带 `Container`，元素是 `Int`），**不借用 `TypeKind::Vector`**（那个值已被 glm 向量占用，与标准容器无关）

#### Scenario: 识别口径与运行时一致

- **WHEN** 同一个编译单元里既用工具产出的表判断"是不是容器"，又用运行时的 `containerStates<T>::is_container_with_size` 判断
- **THEN** 两侧对同一批类型给出相同结论（编译期断言通过）

#### Scenario: 元素里含指针或托管指针时被标出

- **WHEN** 成员类型是 `castl::vector<int*>`、`castl::unordered_map<int, void*>`，或 `castl::vector<castl::shared_ptr<T>>`（引擎里真实存在这种成员）
- **THEN** 该成员都带 `Container` 标志，且元素描述里标明元素含裸指针或托管指针（`HasOpaqueElement` 置位），供拒绝路径使用

#### Scenario: 多维 C 数组按维度逐层描述

- **WHEN** 成员类型是 `int a[2][3]`
- **THEN** 表的元素描述给出的是**直接元素类型**（`int[3]`）而不是最内层的 `int`，并且 README 的磁盘契约写明"先写第一维、再逐个写内层数组"（运行时 `containerInfo` 取的是最内层类型，照抄会让描述与真实字节不符）

### Requirement: 无序容器的序列化与哈希必须走规范顺序

对无序容器（`unordered_map` / `unordered_set`），生成的代码 MUST 在写出与哈希之前把内容摊平成**按规范顺序排好的元素数组**（`map` 按 key、`set` 按元素本身），再按数组的形状写出 / 累加哈希。读回来 MUST 以逐个插入的方式还原，且与写入顺序无关。有序容器（`vector` / `array` / `string` / `deque` / `list` / `map` / `set`）**不排序**：写出的字节与改动前逐字节相同（任务 3.5 对拍）。有序容器与无序容器一样，**元素按值参与哈希**（元素用自己的哈希特化，浮点先规范化 —— 见下面「元素里的浮点也要按值参与哈希」），所以有序容器里含浮点时，哈希值与改动前**允许不同**：这是 D5 定下来的"所有容器一视同仁、不留例外"，不是行为回退。

#### Scenario: 插入顺序不影响写出的字节

- **WHEN** 两个 `castl::unordered_map<uint32_t, uint32_t>` 内容相同、但插入顺序不同，各自序列化
- **THEN** 两份产物逐字节相同

#### Scenario: 插入顺序不影响哈希

- **WHEN** 上述两个内容相同、插入顺序不同的对象各自算哈希
- **THEN** 两个哈希相同（与生成的相等判断"内容比较"保持一致）

#### Scenario: 内容不同则字节与哈希都不同

- **WHEN** 两个 `unordered_map` 内容不同（多一对、少一对、或值不同）各写一次
- **THEN** 字节不同且哈希不同 —— 这是防止"顺序无关"判据恒真的对照组

#### Scenario: 元素是内含无序容器的结构体时必须拒绝

- **WHEN** 容器的元素是**结构体**，而该结构体（逐层展开后）含无序容器，例如 `struct Inner { castl::unordered_map<unsigned, unsigned> m; }; struct Outer { castl::vector<Inner> items; };`
- **THEN** 生成序列化与哈希的调用 MUST 非零退出，并在 stderr 点名**成员名**、**元素类型**与**那个无序容器**；MUST NOT 输出"看着能编译、字节却随插入历史变化"的代码
- **理由（实测限制）**：这类元素只能由生成代码递归写出其字段，而生成代码拿不到旧运行时的读写游标 —— `cacore::serializer` 的缓冲区与 `cacore::deserializer` 的偏移都在 `private:` 段且没有公开取用方法（`CACore/header/Serialization.h`），所以既不能把当前缓冲区交给元素类型的生成序列化器，也不能在正确偏移上新建一个反序列化器。design 的失败方案表对这个形状写的是「逐层摊平（**或加一位标志并入拒绝**）」，本 change 取拒绝
- **对照组**：元素是结构体但内部只有标量与**有序**容器时（如 `struct Plain { unsigned a; castl::map<unsigned,int> ordered; };`）MUST 照常出码（exit 0）—— 拒绝不得误伤普通结构体元素
- **已知边界**：元素类型若不在本次扫描的头文件里，工具看不到它的字段、无法判定，此时按"未发现"放行

#### Scenario: 元素本身就是容器时同样稳定

- **WHEN** 无序容器是**元素**或 **map 的值**，或 C 数组的元素本身是无序容器：`castl::vector<castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned, castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned,unsigned> m[2]`，各取两个只有内层插入顺序不同的对象
- **THEN** 三者的产物逐字节相同、哈希相同。实测现状：三者首个不同字节分别在 16 / 20 / 16（初稿那句"调用元素类型自己的生成序列化器"对这些形状落不下来）

#### Scenario: 元素里的浮点也要按值参与哈希

- **WHEN** 两个 `castl::unordered_map<uint32_t, float>` 内容只差在 `+0.0f` 与 `-0.0f`
- **THEN** 生成的相等判断为真时两个哈希必须相同。实测现状：相等判断为真（`==` 成立）而哈希不同（`1a2742e842fb0203` 与 `1a27c2e842fbdb83`），因为浮点规范化只写在顶层字段那一层；本 change 把规范化**递归到元素**（元素按值参与哈希），不再整块按字节算

#### Scenario: 有序容器里的浮点同样按值参与哈希（**已定死，不列为例外**）

- **WHEN** 有序容器含浮点且两个对象只差 `+0.0f` / `-0.0f`，例如引擎里真实存在的 `castl::vector<castl::vector<float>>`
- **THEN** 相等判断为真时哈希必须相同 —— 所有容器（有序无序一视同仁）都按元素显式调用元素类型的哈希特化，不因为"这个 change 只谈无序容器"就把它留成例外。实测现状：`vector<float>` 同内容不同哈希（`5f242d39c2422be4` 与 `5f23ad39c2415264`）

#### Scenario: 读回来的内容与写入一致（空目标）

- **WHEN** 把排序后写出的字节读回一个**空**容器
- **THEN** 读回的容器与写出的容器内容相等（顺序无关的比较）

#### Scenario: 读进非空目标时先清空（**已定死，不留选择**）

- **WHEN** 把同样的字节读进一个**已经有元素**的容器
- **THEN** 生成代码 MUST 先清空该容器再读（可用的前提是 `containerStates<T>::has_clear`；没有 `clear()` 的容器进拒绝路径），使读回来的内容**只由载荷决定**。依据：实测生成代码走的是 `cacore::deserializer`，它**不清空**目标容器（读进 3 元素目标后 size=5；元素个数为 0 的载荷读进 1 元素目标后 size 仍是 1，`clear` 只写在 `cacore::batch_deserializer` 里），而"还原"这个说法只有在清空之后才成立

#### Scenario: 有序容器的产物不变

- **WHEN** 序列化 `castl::vector<int>` / `castl::map<int, int>` / `castl::string`
- **THEN** 写出的形状与改动前一致（元素个数 + 按容器自身顺序逐个写），既有黄金样本不需要改动

### Requirement: 会静默出错的容器组合必须被拒绝

下列组合 MUST 让工具以非零退出码拒绝出码，并在 stderr 点名成员与原因，MUST NOT 静默产出会丢数据的代码：① **参与排序的那一侧**没有全序（`map` 的键、`set` 的元素；`map` 的值不参与排序，不检查；判据须接受 `operator<=>`，含 NaN 的键判不出全序）；② 容器元素含**裸指针 / 托管指针（`shared_ptr` / `unique_ptr`）/ 句柄**且请求的是序列化产物；③ **重复键容器**（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset` —— `castl` 经 `using namespace std` / `eastl` 透出这些名字，它们是可达的）；④ 容器的键或元素**不可默认构造**（旧运行时的 pair 支持要求元素可默认构造）；⑤ **按位存储的容器**（`castl::vector<bool>` 一类：满足结构判据，却过不了旧运行时的逐元素循环）。

**包装器不在拒绝清单里**（`NameHash` / `PathHash` / `HashObj` 只记录，见字段表要求）。仅请求反射产物时，①②③④⑤ 都不受影响。⚠ **接入清单口径**：`build.py` 对每条清单条目固定跑三个发射器、任一次非零即整体构建失败，所以**进清单的类型必须三个发射器都能出**；只能出反射的类型不进清单（工具本身仍允许对它单独请求反射）。

#### Scenario: 指针作元素时序列化被拒绝

- **WHEN** 对含 `castl::vector<int*>` 或 `castl::vector<castl::shared_ptr<T>>` 成员的已标注类型请求 `--emit-serialization`
- **THEN** 工具非零退出，点名该成员路径与"元素含指针 / 托管指针，写出会丢数据"；同一类型的 `--emit-reflection` 仍然成功

#### Scenario: 指针作键时被拒绝

- **WHEN** 对含 `castl::unordered_map<int*, int>` 成员的已标注类型请求序列化或哈希
- **THEN** 工具非零退出并说明"键没有全序，写出的字节不可能稳定"

#### Scenario: 重复键容器被拒绝，且判据可按名判定

- **WHEN** 成员类型是 `std::multiset<int>` 或 `castl::multimap<int, int>`（实测 `castl::multimap` 可用，且 `set` 与 `multiset` 的运行时特征完全相同、结构上分不开）
- **THEN** 工具非零退出并说明"重复键容器没有规范顺序、本版本不支持"；判据取**规范化后的类型名**（`std::` / `eastl::` 形式），因此不受 `USING_EASTL` 开关影响

#### Scenario: 元素不可默认构造时被拒绝

- **WHEN** 容器的元素类型没有默认构造函数（如 `struct NoDef { int v; explicit NoDef(int); };` 放进 `castl::vector`）
- **THEN** 工具非零退出并说明原因；实测现状是照样出码，然后在消费方编译时才撞 `static assertion failed: 'object incompatible for reflection'`（写侧）与 `'Wrong'`（读侧）

#### Scenario: 包装器元素必须放行，不得被当成不透明

- **WHEN** 容器的元素或键是包装器（`cacore::NameHash` / `cacore::PathHash` / `cacore::HashObj`），例如引擎里真实存在的 `castl::unordered_map<cacore::NameHash, ShaderStructData>` 与 `castl::vector<cacore::NameHash>`
- **THEN** 类型带 `Wrapper` 记录但**不触发任何拒绝**，序列化产物照常生成（旧运行时用 `managed_wrapper_traits` 解包读写）；只有裸指针 / 托管指针 / 句柄才触发 ②

#### Scenario: 排序的比较判据必须接受 `operator<=>`

- **WHEN** 键或元素类型只提供了 `operator<=>`（引擎真实键类型 `NameHash` / `PathHash` / `CAHash::result_type` 都是这种）
- **THEN** 工具必须认定它有全序（C++20 里 `a < b` 会重写为 `(a <=> b) < 0`）；实测现状是这些头文件里 `operator<(` 零命中，按 `<` 判定会把引擎唯一的落盘资源类型整份误拒

#### Scenario: 全序判不出来的键必须拒绝

- **WHEN** 键是浮点且含 NaN（两个 NaN 互为"等价"但 `==` 为假，排序给不出稳定次序）
- **THEN** 工具拒绝出码并点名该键，不得按"能编译过 `a < b`"就放行；实测现状：两个 NaN 键的 `unordered_map` 摊平并 `std::sort` 后写出字节仍不同（首个不同字节在第 8）

#### Scenario: 按位存储的容器被拒绝

- **WHEN** 容器是按位存储的（`castl::vector<bool>` 一类）
- **THEN** 工具非零退出并点名该成员 —— 它满足"有 `size()` 且有 `value_type`"这条结构判据，但旧运行时的 `for (auto& item : object)` 绑不上按位代理，消费方一实例化就编译失败（实测 MSVC `error C2440`）；实测现状是工具 exit 0 照常出码

### Requirement: 容器的磁盘形状是对外契约

`Tools/CodeGen/README.md` MUST 写明容器成员在磁盘上的形状（先写元素个数、再按规范顺序逐个写元素；`map` 写的是键值对数组）以及排序规则与拒绝条件，供外部使用者按此解析生成的数据。

#### Scenario: 文档写明容器格式且示例可跑

- **WHEN** 运行 `python Tools/CodeGen/verify_readme.py`
- **THEN** 文档里新增的容器示例命令逐条执行且退出码与文档一致（0 处不符）；用 grep 能在 README 里找到"无序容器按排序后的数组写出"这条规则
