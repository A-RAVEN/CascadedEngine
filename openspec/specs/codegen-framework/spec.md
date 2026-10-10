# codegen-framework Specification

## Purpose
TBD - created by archiving change engine-codegen-framework. Update Purpose after archive.
## Requirements
### Requirement: parser 元数据来源为 libclang，引擎保持纯 MSVC

系统 SHALL 用 libclang（复用 CACodeGen 的 CLANG_LIB）作为元数据来源；引擎 SHALL 保持纯 MSVC 编译，CodeGen 作为**独立构建期程序**运行，**不进入**引擎 CMake 编译图，引擎 SHALL NOT 链接 libclang。

"独立"SHALL 落到构建系统的实处，而不只是说法：

- `Tools/CodeGen/CMakeLists.txt` SHALL 是**独立工程**（自带 `cmake_minimum_required` 与 `project()`，不引用父工程变量）；
- 顶层 `CMakeLists.txt` SHALL NOT `add_subdirectory("Tools/CodeGen")` —— 引擎那棵 ninja 树里 SHALL NOT 存在 CodeGen 目标；
- `build.py` SHALL 在**自己的构建目录**（`out/build/codegen-tool`）里先 configure、再编译该独立工程，然后才跑生成段、才编译引擎；
- 可执行文件 SHALL 落在 `out/build/<预设>/bin/CodeGen.exe`（既有文档与校验脚本按此路径找它），`libclang.dll` SHALL 被拷到它旁边。

**构建前置条件（由本 change 明确）**：引擎构建 SHALL 依赖"`CodeGen.exe` 能跑起来"（即 libclang 可用）。这一点 MUST 写进 `Tools/CodeGen/README.md`，并且"程序没建起来"与"引擎编译失败"在输出里 SHALL 能被区分开。

#### Scenario: 引擎不链接 libclang

- **WHEN** 执行全量构建（`python build.py`）
- **THEN** 引擎编译产物不含 libclang.dll 依赖

#### Scenario: 引擎那棵树里没有 CodeGen 目标

- **WHEN** 在引擎构建目录里查 ninja 目标（或直接 `cmake --build <引擎构建目录> --target CodeGen`）
- **THEN** 该目标 MUST NOT 存在；引擎的 configure/编译 MUST 与 `CodeGen.exe` 的源码是否改动无关

#### Scenario: 独立目录能建出程序

- **WHEN** 跑 `python build.py --config Debug`
- **THEN** MUST 在 `out/build/codegen-tool` 里 configure/编译它，`out/build/<预设>/bin/CodeGen.exe` 与其旁边的 `libclang.dll` MUST 存在

#### Scenario: 没装 libclang 时的失败信息

- **WHEN** libclang 不可用（`CALIB_CLANG_DIR` 指向的目录里没有 `libclang.dll`）
- **THEN** 构建 MUST 在生成段失败并说清"生成用的程序没建起来、怎么配 libclang 路径"，MUST NOT 报成引擎编译错误

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

生成代码 SHALL 被至少一个真实引擎目标的编译单元 `#include`，并在该编译单元内对生成表做编译期断言。仅有"与黄金样本逐字节对拍"不算被消费。

由于生成物**不再入库**（见下一条），"删掉入库生成物即构建失败"这条判据 SHALL 由"**生成失败/生成不出该类型即构建失败**"取代：生成段任何一条非零退出或对拍失败，构建 SHALL 以非零退出结束，且 MUST NOT 继续把引擎编译当作"通过"。

#### Scenario: 生成不出来就不许继续

- **WHEN** 清单里某个类型被生成器拒绝（非零退出）
- **THEN** 构建 MUST 以该失败结束并点名类型与退出码；引擎编译 MUST NOT 被报成成功

#### Scenario: 消费单元真的包含它

- **WHEN** 复核某迁移类型是否被真实目标消费
- **THEN** 该模块里 MUST 有编译单元的 `#include` 它的生成头，并有编译期断言

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

### Requirement: 容器的磁盘形状是对外契约

`Tools/CodeGen/README.md` MUST 写明容器成员在磁盘上的形状（先写元素个数、再按规范顺序逐个写元素；`map` 写的是键值对数组）以及排序规则与拒绝条件，供外部使用者按此解析生成的数据。

#### Scenario: 文档写明容器格式且示例可跑

- **WHEN** 运行 `python Tools/CodeGen/verify_readme.py`
- **THEN** 文档里新增的容器示例命令逐条执行且退出码与文档一致（0 处不符）；用 grep 能在 README 里找到"无序容器按排序后的数组写出"这条规则

### Requirement: 生成物不再调用旧运行时（原「统一 CodeGen 流水线从 annotated 头产出可读生成代码」的能力边界收窄）

生成的序列化与哈希代码 MUST 由 CodeGen 自己的运行时驱动：读写自带游标、容器判据与哈希钩子都来自新运行时，MUST NOT 引用 `cacore::serializer`、`cacore::deserializer`、`careflection::containerStates`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`。

序列化入口 MUST 接收缓冲区（写）或缓冲区加显式读位置（读），使生成代码能在流中间调用另一个类型的生成代码。

#### Scenario: 生成物零引用旧符号

- **WHEN** 对仓库里所有生成物逐个搜上面五个符号
- **THEN** 零命中（带文件限定）；现状每个符号都被引用，故该判据在改动前为红

#### Scenario: 递归形状不再被拒绝

- **WHEN** 容器的元素是另一个已标注的结构体，且该结构体内部含无序容器
- **THEN** 出码成功、往返一致、字节与插入顺序无关；`3.2` 里那条"元素是内含无序容器的结构体必须拒绝（退出码 12）"的场景**作废**

### Requirement: 拒绝清单收窄（原「会静默出错的容器组合必须被拒绝」）

下列拒绝 MUST 保留：参与排序的一侧没有全序（退出码 17）、元素是裸指针或托管指针（15）、重复键容器（14）、键或元素不可默认构造（16）、按位存储容器（13）。每条拒绝的非零退出 MUST 在 stderr 里**点名成员名、涉事的类型（元素类型 / 键类型 / 容器类型）与原因** —— 这句是从基线里那条被收窄的要求搬过来的，不许在收窄时丢掉。

**撤除**：元素是"内部含无序容器的结构体"（原退出码 12）—— 递归读写做完后这个形状由生成代码支持。

**作废**：本条原先那条「元素是内含无序容器的结构体时必须拒绝 / 生成序列化与哈希的调用 MUST 非零退出」的 Scenario，**自本条改写后作废**，不再保留 —— 递归读写做完之后这个形状由生成代码支持（见上面「撤除」）。

**新增**：含非 public 成员的类型要么提供 friend 入口（照 `Tools/CodeGen/README.md` 的写法），要么保持拒绝（5）；含指针成员的类型保持拒绝，README 里写明替代做法。

#### Scenario: 拒绝集合逐条与 README 的退出码表一致

- **WHEN** 对每个拒绝形状跑一次探针，并把退出码与 `Tools/CodeGen/README.md` 的退出码表逐条对照
- **THEN** 逐条一致；退出码 12 从表里删除，且已有一条"同样的夹具现在出码成功"的读数

#### Scenario: 撤除 12 之后，原来那条夹具要改成能通过

- **WHEN** 用改前触发退出码 12 的那个夹具（`struct Inner { castl::unordered_map<unsigned,unsigned> m; }; struct Outer { castl::vector<Inner> items; };` 这一形状）在改动后再跑一次
- **THEN** 出码成功、往返一致，且两个只有内层插入顺序不同的对象写出的字节逐字节相同；"改前 exit 12 + stderr 点名"与"改后 exit 0 + 往返一致"两个读数都要留档

### Requirement: 反射标注通道支持嵌套类型（原「反射标注用标记宏自动爬取」）

标记 MUST 能标注命名空间作用域之外的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）。类型名的解析口径（短名或全名）MUST 在 README 里写明并与命令行、接入清单一致。

#### Scenario: 嵌套类型三个发射器都成功

- **WHEN** 对上述两个嵌套类型请求三个发射器
- **THEN** 全部退出码 0；现状是 4（类型未标注），故判据在改动前为红

#### Scenario: 环境能解析 D3D12 自己的头

- **WHEN** 用推导环境解析 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h`
- **THEN** 退出码 0；现状是 6（`directx/d3dx12.h` 找不到）

### Requirement: 容器读写不再要求容器有 `clear()`

无序容器仍然 MUST 在读之前清空并按规范顺序重建；但**定长数组**（没有 `clear()` 的类型，如 `castl::array`）MUST 按位置逐个覆写，并在读之前校验载荷长度与 extent 一致 —— MUST NOT 因为没有 `clear()` 就拒绝出码。

（本条是从基线里那条「读进非空目标时先清空（**已定死，不留选择**）」的 Scenario 收窄出来的：那条场景把"清空"写成了所有容器读路径的前提，而定长数组没有 `clear()`；收窄之后"先清空"只对无序容器成立，定长数组走按位置覆写。它是一条**新增**要求，不是改名 —— 基线里没有同名的 Requirement 可改。）

#### Scenario: 含 `castl::array` 的真实类型可出码

- **WHEN** 对含 `castl::array<T, N>` 成员的类型请求序列化并在真实目标里实例化读路径
- **THEN** 编译通过、往返一致；不再走"没有 `clear()` 就拒绝"这条路

### Requirement: 解析期垫片不得重定义编译器内建，且必须写在文档里

`build.py` 在模块推导环境之外额外喂给工具的 include 根（`CODEGEN_EXTRA_INCLUDES`）里属于"解析期垫片"的那部分 MUST 满足三条：① MUST NOT 定义编译器自带的函数（例如 clang 的 `__cpuid`）—— 那会让工具在保真门上报 `definition of builtin function`，需要某个内建时 MUST 让编译器自己的内建接手；② 为某个名字提供定义时，参数个数 MUST 按**真实调用点**的形状给 —— 实测 Windows SDK 的 `DirectXMath.h` 在 clang 身份下 `#include <cpuid.h>`，它的 `DirectXMathMisc.inl` 在 `#if defined(__clang__) || defined(__GNUC__)` 下调用的是 6 个参数的 `__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])`；③ 每个垫片文件 MUST 在 `Tools/CodeGen/README.md` 里写明三件事：它替代哪个头、为什么仓库里需要它（实测：vendored 的 LLVM 只有 `bin/x64/libclang.dll`、没有 clang 的资源头目录）、以及它的边界（只让 include 过得去、不参与任何布局计算）。本条约束的是垫片的形状，MUST NOT 被当成"放宽解析门"的手段 —— 诊断的判法不在本条范围内。

#### Scenario: 垫片不重定义内建

- **WHEN** 用推导环境解析 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`（它的 include 链会经由
  Windows SDK 的 `DirectXMath.h` 走到 `#include <cpuid.h>`）
- **THEN** 工具的诊断列表里不出现 `definition of builtin function`；**改动之前实测为红**，逐字是
  `Tools/CodeGen/clang_shim\cpuid.h:15:20: error: definition of builtin function '__cpuid'`

#### Scenario: 垫片覆盖真实调用形状

- **WHEN** 把 `_XM_AVX2_INTRINSICS_` 打开后解析同一批头（DirectXMath 在 clang 身份下走 6 参数那一支）
- **THEN** 解析不出现 `no matching function for call to '__cpuid_count'`；**改动之前实测为红**
  （现状垫片提供的是 3 个参数的函数），只留 include 守卫时是 `use of undeclared identifier '__cpuid_count'`，
  也红

#### Scenario: README 里有据可查

- **WHEN** 在 `Tools/CodeGen/README.md` 里找垫片那一段
- **THEN** 能查到垫片文件名（`Tools/CodeGen/clang_shim/cpuid.h`）、它替代的头（`cpuid.h`）、需要它的原因
  （vendored LLVM 没有 clang 资源目录）与边界（只让 include 过得去、不参与布局计算）；
  这条判据用 `git grep -n -F "<逐字引文>" -- Tools/CodeGen/README.md` 逐条可核

### Requirement: 生成代码只落构建目录，源码树里没有生成物

生成物 SHALL 只写进构建目录（`out/build/<预设>/codegen/<模块>/`），并且各模块的 include 路径 SHALL 指向构建目录里对应的那一层。源码树里 SHALL NOT 存在生成代码（各模块的 `generated/` 目录 SHALL 被删除），仓库也 SHALL NOT 跟踪任何生成文件。

随之删除：`build.py` 的 `--commit-generated` 开关、入库副本的逐字节对拍、以及它带来的"入库副本过期"这类错误提示。

#### Scenario: 仓库里没有生成代码

- **WHEN** 用文件系统枚举各模块的 `generated/` 目录
- **THEN** 这些目录 MUST NOT 存在；`git ls-files` 里 MUST NOT 有生成头

#### Scenario: 编译器读的是构建目录那份

- **WHEN** 复核某模块的 include 路径
- **THEN** MUST 指向 `${CMAKE_BINARY_DIR}/codegen/<模块>`，而不是源码树里的 `generated/`

#### Scenario: 改完类型不需要"记得重新生成"

- **WHEN** 给一个被标注类型加一个成员，然后直接跑构建
- **THEN** 因为是每次构建现场生成，MUST 不需要任何"写回仓库"的动作；生成段会重跑该条，产物与消费单元断言一致即通过

#### Scenario: 旧开关与旧快路不再存在

- **WHEN** 传 `--commit-generated` 或 `--no-codegen`
- **THEN** 构建 MUST 明确拒绝并说明这两个开关已随"生成物入库"一起删除（生成物不入库，就不存在"只编译不生成"这条快路）

### Requirement: 一个类型一次调用出三份产物

`CodeGen.exe` SHALL 支持在**一次调用**里同时产出反射表、读写代码、哈希代码三份产物（输出目录由参数给出），产物文件名与字节 SHALL 与逐个生成时完全一致。命令行接口、退出码表与 `Tools/CodeGen/README.md` 的示例 SHALL 同步更新；"一次给两个不同的发射标志 → 退出码 10"这条既有规则 SHALL 保留。

#### Scenario: 一次调用三份产物

- **WHEN** 用一个类型跑新调用方式
- **THEN** MUST 一次调用产出三份文件，且与用三次单发射器调用产出的字节**逐字节相同**

#### Scenario: 文档同步

- **WHEN** 复核 README
- **THEN** 接口表、退出码表与示例 MUST 反映新调用方式；README 里的命令原样可跑（`verify_readme.py` 0 处不符）

#### Scenario: 空输出目录按缺值拒绝

- **WHEN** 传 `--emit-all --out-dir=`（等号后面是空的）或 `-o ''`
- **THEN** MUST 以退出码 2 拒绝并说明目录/路径为空；MUST NOT 把产物写到**当前盘符的根目录**（实测未加这条检查时，`--out-dir=` 会让三份产物落到盘符根且退出码为 0）

#### Scenario: 接受集合的差异写在明处

- **WHEN** 复核 `--emit-all` 与逐个发射的关系
- **THEN** README MUST 写明：`--emit-all` 让三份产物的拒绝条件**全部**生效，所以标注没带 `serializable`（退出码 9）、含 `vector<bool>`（13）、含指针成员（15）的类型在 `--emit-all` 下会被拒，而这些类型用单发射反射仍能出码；并给出对照命令与退出码

### Requirement: 解析环境的归属是"真正编译这个类型的模块"

清单第三列 SHALL 指明**真正编译（消费）这个类型的模块目录**，`build.py` SHALL 按既有规则从该模块的 `compile_commands.json` 条目推导解析环境。SHALL NOT 为此维护任何手写的 include / 宏清单，也 SHALL NOT 拿生成器自己那份 `compile_commands.json` 当环境 —— 那份记的是"**怎么编译生成器**"（为链接 libclang 配的参数），与"怎么解析被标注的头"无关，生成器也不给自己出码。

#### Scenario: 夹具的归属是消费它的模块

- **WHEN** 复核 `Tools/CodeGen/TestEmit.h` 的 `EmitSample` 与 `TestKinds.h` 的 `Kinds` 这两行
- **THEN** 第三列 MUST 是消费这些生成头的模块（`Test/CoreTests`），MUST NOT 是靠一张借用表凑出来的目录；全量生成 MUST 不再出现 `no compile env for module`

#### Scenario: 环境推导规则没有被绕过

- **WHEN** 复核 `build.py` 里推导解析环境的那段
- **THEN** MUST NOT 存在"某目录借另一个目录的宏"这类手写映射；环境的输入只有 `compile_commands.json`

