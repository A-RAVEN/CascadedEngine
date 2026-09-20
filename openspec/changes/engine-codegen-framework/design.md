# engine-codegen-framework — 设计

## Context

引擎现有的反射是"宏 + 模板"方案（`CACore/header/Reflection.h`），调查已确认它的天花板是**结构性的、不是作者水平问题**：

| 现状事实 | file:line |
|---|---|
| `CATypeDescriptor<T>` 主模板只存 `type/member_tuple_type/member_count/name`；`CATypeMemberDesc` 每字段只存 `{type, CAStructName<char...> 名字, offsetof}` | `Reflection.h:58-65 / :67-73` |
| `CA_REFLECTION` 只发字段清单（`decltype(declval<Type>().Member)` + `offsetof` + `#Member`），无 methods/enumerators/base/access/per-member attribute | `Reflection.h:76-98` |
| `CA_PRIVATE_REFLECTION(Type)` 只是 `friend struct ::CATypeDescriptor<Type>;` | `Reflection.h:99` |
| **构建期外部工具读不到**：无 runtime `name→TypeDescriptor` 注册表（grep registry 为空）；唯一 `log_reflection` 以 `return;` 死掉（`:122-133`），member 名只作为编译期 char-pack 类型存在（`:111-119`） | `Reflection.h:111-119 / :122-133` |
| **runtime 承重、不可替换**：`visit_members_type_desc` 用 `((uint8_t*)&obj)+offset`（`:383`）跑在序列化（`Serialization.h:45-50/234-239`）与每帧哈希（非 trivially-copyable 的 `HashObj<VertexInputsDescriptor>` `GPUGraph.h:167,224`、`HashObj<GPUShaderBindingKey>` `GPUGraphExecutor.h:36,79`、`HashObj<DescriptorSetDesc>` `ShaderBindingHolder.h:33`）上 | 见左 |

同时我们正规划接入 C# 脚本运行时（Phase 2-6），需要一条从带标注的 C++ 类型自动生成可读 C# 绑定的管道。二者共同指向同一结论：建**一套统一 CodeGen 框架**，一次解析、四个 emitter（反射 / C# 绑定 / 序列化 / 哈希+相等），共享同一张线性类型表。

## Goals / Non-Goals

**Goals:**
- 建一条 CodeGen 流水线：加标注（标记宏）的 C++ 头 → libclang 解析（**class 体 AST 自动爬成员**）→ **线性类型表** `{name, offset, type, kind, flags}`（数据，不是元编程）→ 生成可读代码。
- **annotation 形态 = 标记宏 `CA_REFLECT(...)`**（attribute 风格，可带 `serializable`/`ui` 等选项），libclang 从 class 体 AST 自动提取成员——作者不手写成员清单。**不用字面 `[[careflection::...]]`**（MSVC C5030 + libclang 自定义命名空间 attribute 弱，见 D1）。
- **双模式**：侵入式（自己仓内）= `CA_REFLECT` + AST 爬；非侵入式（外部库）= 保留 `CA_REFLECTION(Type, 成员...)` 宏，工具 grep 它产字段表。两者进同一张类型表。
- **先行验证反射 emitter**：产出可读的 `TypeDescriptor<T>` 线性数组（含字段名/offset/type/kind/flags），替代晦涩模板塔；暴露 constexpr 数组 + 可选 runtime 注册表。
- **工具链落地**：`build.py` 新增 codegen 步骤；CodeGen 作为独立构建期 sidecar，复用 CACodeGen 已跑通的 libclang（`-DCALIB_CLANG_DIR` 可改路径、不提交 100MB）；**引擎保持纯 MSVC**。
- 序列化 / 哈希+相等 emitter 作为**同 proposal 内后续 task**（共享同一 core），不单开 change。

**Non-Goals:**
- **不替换 CA_REFLECTION**：它 runtime 承重（序列化+每帧哈希）保留；兼作"未迁移引擎 + 外部库"过渡通道（见 D2 更正：它并非外部库专用宏）。`CA_REFLECT` 是引擎自有类型的**向前**标注形态；**引擎自有类型从 CA_REFLECTION → CA_REFLECT 的真实迁移不在本 change**（Phase-1 以示例类型证明管道，迁移属后续 task，见 OQ#5 已定论）。
- 不用 clang-cl 编译引擎、不让引擎链接 libclang、Phase 1 不 `add_subdirectory` LLVM 进引擎 CMake。
- **不启用字面 `[[clang::annotate]]` / `[[careflection::...]]` attribute**（MSVC C5030 警告 + libclang 读自定义命名空间 attribute 弱 + `[[clang::annotate]]` AST 可见性 bug，见 D1）。
- 不建 C# 运行时/CoreCLR/AOT/绑定生成器（Phase 2-6，各自独立 proposal，见 `TODO/004 §5`）。
- 不改现有 `HashObj` 序列化语义，不做运行时反射行为迁移。

## Decisions

### D1：parser 元数据来源 = libclang（复用 CACodeGen），引擎纯 MSVC
**为什么**：CA_REFLECTION 是纯编译期、字段-only、构建期外部工具读不到（见 Context），**不配当元数据来源**。而 libclang 的 C API 已在这台机器上**被真正用过**：
- `E:/Projects/CACodeGen/ExternalLibraries/LLVM/bin/x64/libclang.dll`（**18.1.8，100,587,008 B**）+ `lib/x64/libclang.lib` + `clang-c/` 头；`CLANG_LIB` 是 CMake imported target（`LLVM/CMakeLists.txt:12-13`）。
- `ParserLibrary`（CAParserLibrary SHARED，链 CLANG_LIB）真跑过（`clangLog.txt` 是真实 depfile）。
- libclang 的 cursor 遍历覆盖我们要的一切：字段（`clang_Type_getOffsetOf:3614`、`clang_getFieldDeclBitWidth:3069`）、基类（`CXCursor_CXXBaseSpecifier=44`、`clang_isVirtualBase:3700`）、枚举（`CXCursor_EnumDecl=5`、`clang_getEnumConstantDeclValue:3035`）、attribute（`clang_Cursor_hasAttrs:2347`）。⚠ **这一段讲的是 libclang 的能力上限，不是本 change 的交付范围**：本 change **只用到"字段"那一项**；基类 / 枚举 / attribute 的 cursor 处理**不在本 change 实现范围**（工具侧无对应处理，判据：`git grep -n -E "EnumDecl|BaseSpecifier|FunctionDecl" -- Tools/CodeGen` 零命中）。

**替代方案**（已核实后评估）：
- *读现有 CA_REFLECTION + 结构化绑定充当 parser* → 拒绝：它就是天花板（compile-time + fields-only），循环。
- *字面 `[[careflection::...]]` attribute* → **不用**：MSVC 会报 **C5030 警告**（[developercommunity](https://developercommunity.visualstudio.com/t/c-warning-c5030-generated-for-attribute-within-a-n/138429)，项目 `/WX-` 非致命但每用例都嗝）；且 **libclang C API 读自定义命名空间 attribute 弱**（[discourse](https://discourse.llvm.org/t/getting-custom-c-attribute-specifiers-from-ast/62083)；[llvm#87813](https://github.com/llvm/llvm-project/issues/87813) 连 `[[clang::annotate]]` 都在 ClassDecl 常不出现）。→ 改用**标记宏 `CA_REFLECT(...)` + libclang AST 爬 class 体**：标记宏 grep 可得、MSVC 无副作用，class 体由 libclang 自动提取。⚠ **本 change 只提取数据字段** —— 方法 / 枚举 / 基类**不在本 change 实现范围**（工具侧无对应 cursor 处理）。这也是 Unreal `UCLASS()/UPROPERTY()` 的成功先例。⚠ **本行三个 URL 的实际所用工具无落盘证据**（未记录何时、用什么工具核到），且它们**不**在 Round 6 的 URL 表（那张表只覆盖 4 个 URL）里 —— 引用时按"未经独立复查"对待。
- *`[[clang::annotate]]`* → 不用：AST 可见性 bug（llvm#87813）+ 不可在 MSVC 下保留字面形式，故弃。

**为什么引擎保持 MSVC**：`build.py` 的 `_write_bat` 里把 `CC` 与 `CXX` 指向 cl.exe 的完整路径（⚠ **这是转述、不是逐字引文** —— 源码里是 f-string 拼接，照抄不出来），以及 configure 用的 `-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl`（**引文锚点，不写行号 —— 该文件会被 tasks §9.9 / §9.13 改动**）；clang-cl **未安装**（`VC/Tools/Llvm` 只有 clang-format/clang-tidy）。→ CodeGen 作为**独立构建期 sidecar**，由 `build.py` 调（位置：`patch_ninja()` **之后**、`build(target)` **之前** —— **两条路径都执行**，该调用**不在** `if configure_step:` 块内），**不进引擎 CMake 编译图**。

### D2：discovery（哪些类型跨界）与 extraction（field/method/base/enum 细节）解耦，双模式
> ⚠ **范围注（2026-09-12）**：本节的 extraction **在本 change 只做 field**；method / base / enum 的提取**不在本 change 实现范围**（标题里的 "field/method/base/enum" 描述的是**解耦后的架构位置**，不是本次交付）。
> **诚实更正（2026-09-06 审查）**：`CA_REFLECTION`（及 `CA_REFLECTION_TEMPLATE`）**不是"外部库专用"宏**——它实际是引擎自有反射宏，遍布引擎源文件（`ShaderResourceHandle.h:180-181`、`ShaderBindingBuilder.h`、`CPipelineStateObject.h`、`uenum.h:142`、`CACore/CAHash.h:150`、`ShaderLibrary.h`、`VulkanPipelineObject.h` 等 16 个引擎源文件）。grep 它**不能隔离出外部库**，抓的是"**未迁移引擎 + 外部**"。故双模式应表述为过渡：
- **CA_REFLECT（引擎新标注）**：标记宏作 **discovery**（"这个类型跨界"，grep 可得 + 可带 options）；**extraction** 由 libclang 对标记类型做 **class 体 AST 爬取**（⚠ **本 change 只自动提取字段**；方法 / 枚举 / 基类不在本 change 实现范围）。成员清单从作者的手搬到前端的手。
- **CA_REFLECTION（未迁移引擎 + 外部库，过渡通道）**：保留现有宏，作 discovery +（字段名）来源——它命名空间作用域声明不改类型；工具 grep 到它 → 仅按成员名生成（offset 由生成代码 `offsetof` 兜底，若外部头可含再用 libclang AST 爬）。**Phase 1 用它证明外部/未迁移通道；引擎自有类型**从 CA_REFLECTION → CA_REFLECT 的**迁移属后续 task**（见 AUDIT-1：本 change 不承诺迁移，spec SHALL 已随之收窄）。
- 两条路径喂**同一张类型表**，进同一批 emitter。**不**自动反射 parser 所见（避免误绑内部类型），坚持"标过了才生成"（见 Open Question #4）。已实现的最小前置为 `CA_CODEGEN_REGISTER("QualifiedName")`（仅 discovery、无 options）；`CA_REFLECT` 在其上扩展 options 传导。

### D3：类型表 = 线性数据，不是元编程
生成产物是**一条线性数组**，每个字段 `{name, offset, type, kind, flags}`。⚠ **`offset` 的单位是字节**（与 `offsetof` 一致，任务 9.5c 选①）；**已知局限**：**bit-field 按字节粒度合并** —— 位域非字节对齐，相邻位域会塌到同一个字节偏移，本 change 不拒绝这类类型也不声称能区分它们。可读、可断点、drift 由"重新生成"变 build error。相比模板塔，可读性与解调性全胜。⚠ **本 change 的类型表只含字段** —— `methods()` / `enumInfo()` / `baseClass()` / `attributes()` 是**后续阶段的扩展位**（`CACore/header/CodeGenMeta.h` 中仅以注释预留、未实现），本 change **不发射、不承诺**。同时暴露 constexpr 数组（序列化/编译期用）+ 可选 runtime `name→TypeDescriptor*` 注册表（inspector/脚本用），复用现有 `IModuleManager` 的"按名解析"。

### D4：CA_REFLECTION 保留，生成元数据与其一致性靠 ABI guard
Phase 1 不改运行时反射行为。生成快照会与源码漂移 → 用**构建期 ABI assert** 把 codegen 元数据与现有 `CATypeDescriptor<T>` 的 `member_count` 对比（TODO/004 §5 提的 "ABI guard"；**实现只断言 `fieldCount == member_count`，另有 `name != nullptr` 的非空检查、不做名字内容比对，也不比 offset**），drift 报 build error。避免"单源树 vs 快照"悬而未决。

### D5：ParserLibrary 最小扩展 —— 枚举/函数补提取（**本 change 不实现，属后续阶段**）；per-member 选项不经 libclang attribute
`ParserLibrary` 目前 **skip 枚举与函数**（`parser.cpp:189-192`：`EnumDecl`/`FunctionDecl` 为 `empty break`；无 `EnumConstantDecl` 处理）。只有在 C# 侧需要**构建期枚举值表**时才需扩；若只当 4 字节句柄则不扩（magic_enum 已在 C++ 侧护 runtime 值，`uenum.h:4`）。Phase 1（反射 emitter）暂不扩，留作未决点，C# 绑定阶段（Phase 3）再定。⚠ **本 change 不实现枚举 / 函数补提取** —— 工具侧当前没有任何 `EnumDecl` / `FunctionDecl` / `BaseSpecifier` 处理（判据：`git grep -n -E "EnumDecl|FunctionDecl|BaseSpecifier" -- Tools/CodeGen` 零命中）。
**关键**：per-member / per-type 的 attribute **选项**（如 `serializable`、`min=0`、`marshal(as=...)`）走**标记宏 `CA_REFLECT(...)` 的文本参数**（读取该宏**调用**的实参字节），**不依赖 libclang 自定义命名空间 attribute 支持**（libclang 那层弱，见 D1）。AST 只负责爬 class 体结构（**本 change 只取字段**），attribute 语义参数由标记宏文本侧携带。⚠ **哪些类型被标注**这件事不靠文本判断：工具用 libclang 的详细预处理记录取"预处理器**真正展开过**的宏调用"，注释、字符串、被禁用的 `#if` 分支、没用到的 `#define` 体因此都不算标注（task 9.59；`CA_REFLECTION` 通道自 task 9.62 起同路 —— 该宏定义在 `CACore/header/Reflection.h:82`，用到它的头必须先有该定义）。

## Risks / Trade-offs

- **[树边界] CodeGen 工具在 CACodeGen tree、引擎在 CascadedEngine tree** → Phase 1 用"预编译 sidecar 由 `build.py` 调"（最便宜，无引擎 CMake 改动）；不 `add_subdirectory` LLVM。若将来要自含引擎，再改 add_subdirectory（膨胀引擎 CMake）。
- **[生成源码扛过 `patch_ninja`]** `build.py` 的 `patch_ninja()`（按引文定位，不写行号）会重写 `build.ninja`；生成的源码规则受该重写与 sanitized PATH 影响 → 需实测 hook，不能假设。作为 tasks 的验收点。
- **[attribute 选项走标记宏文本]** per-member/per-type 的 attribute **选项**由 `CA_REFLECT(...)` 调用处的实参字节携带（读取 clang 报告的**调用区间**内的文本），不经 libclang attribute → 需定义这些选项的语法/语义（如 `serializable, min=0, marshal(as=...)`），作为 codegen 输入契约，见 Open Question #5。
- **[一致性漂移]** 生成快照 vs CA_REFLECTION → 用 D4 的构建期 ABI assert 兜底。
- **[新增构建期依赖]** 复用 CACodeGen libclang 意味着引擎 build 依赖 CACodeGen 树/或一份 vendored 拷贝 → 见树边界风险；运行时依赖不变（引擎不链接 libclang）。

## Migration Plan

1. 通过 `build.py` 新增 codegen 步骤（`patch_ninja` 之后、`build` 之前），调用 sidecar 解析 `CA_REFLECT` 标记宏 + `CA_REFLECTION`（外部库）→ 产线性类型表 + 反射生成代码。
   **环境喂入（Round 4 实证，见 tasks Review Log）**：sidecar 需**拥有被解析头文件的 target 的 per-module 项目 include + define**（经 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` 读 `compile_commands.json`，取该 target 命令的 `/I`→`-I`、`/D`→`-D`、`/std:c++20`→`-std=c++20`，strip `/Zi /MDd /Ehsc /W3 /WX- /wdNNN /Fo /Fd /Yc /Yu /Fp`）。**绝不喂全 union**（`CA_STATIC_BUILD 0/1` 冲突宏 + 同名 basename + include-root 顺序敏感）；**绝不加 MSVC 工具链 stdlib `-isystem`**（⚠ **2026-09-08 按实测更正**：原文的"clang 消费不了 VC include/SDK、实测 parse 失败；clang 内置头即可"**两条都不成立** —— 实测喂入 vcvars 全部 INCLUDE **可正常 parse**、布局与不喂**逐字段相同**；vendored LLVM 树**递归 0 个 `stddef.h`**。**保留该规则的真实理由是"多余 + 引入 include-root 顺序风险"**）。运行时偏移由生成代码 `offsetof` 兜底、始终正确。
   ⚠ **保真度判据（2026-09-13 按实测重写）**：**退出码不能单独作保真度证据** —— 实测残缺环境（`-nostdinc++ -nostdinc`）下旧工具**同样 exit 0，但整张偏移表塌成 `0/0/0`**。现工具的门控是**属性判据**：解析是否被 **fatal 级诊断**截断（`-nostdinc++ -nostdinc`、`-nostdlibinc`、跨 `--target` 只喂 SDK `-isystem` 三种构造实测**均 exit 6**，并指名未解析的 include 与退化的字段；环境完整时即使带 `-nostdinc` **拼写也 exit 0** —— 判据不看参数名）。**已修（原写「本 change 未修」，现已落盘）**：`_msvc_to_clang_env` 遇 `/MDd`、`/MTd` 补出 `-D_DEBUG -D_ITERATOR_DEBUG_LEVEL=2`（原实现 strip `/MDd`，使提取期偏移算成 `/MD` 口径，与 x64-debug 真值差 8 字节）。**实测两组数字**：同一夹具 `Tools/CodeGen/TestStdLayout.h` 的 `tail` 在 `/MD` 规则下 **40**、在 `/MDd` 规则下 **48**。**仍未覆盖**：从某 target 的 `compile_commands.json` 条目取编译命令那一段在本 change 内**无演练对象**，该限制在 tasks 中就地标注，此处不重复声称。
2. 生成的**头文件**加入引擎源清单（**显式 include 路径 + `#include`**——缺失即编译错误，非 GLOB 静默跳过）；生成的 **.cpp**（若存在）用**显式 `target_sources`**（非 GLOB——避免 `file(GLOB_RECURSE)` 对"文件缺失"也通过，见 TODO/004 §3.6）。——RC-F 调和（本 change 生成物为头文件，target_sources 对 header 缺失无效，需 include 路径 + `#include` 捕获）。
3. 反射 emitter 用**自包含 fixture** 证明管道端到端（**不引用任何项目真实类型**：引用会增加耦合——真实头一改、测试即断；且真实类型多为私有成员，不符合"零侵入通道只服务全 public 类型"的规则。见 tasks Review Log Round 7）。
4. 回归：`python build.py --config Debug` 全量 + 现有 tester 通过（无运行时行为变化）。
5. 回滚：删掉 codegen 步骤与生成文件即可回到纯 MSVC 手动清单；CA_REFLECTION 路径未动，回滚无运行时副作用。

## Open Questions

1. **树边界**：预编译 sidecar（build.py 调）vs add_subdirectory(LLVM) —— 决定引擎是"复用"还是"自备" libclang。Phase 1 取前者（`-DCALIB_CLANG_DIR` 默认指 CACodeGen，不提交 100MB）。
2. **枚举范围**：C# 侧到底要不要构建期枚举值表（需扩 ParserLibrary 补 EnumDecl），还是只当 4 字节句柄（magic_enum 已护 runtime）？留给 C# 绑定阶段。
3. **ABI guard 形式**：构建期对比 codegen 元数据 vs `CATypeDescriptor<T>::member_count` 的 assert（**实现只断言 `fieldCount == member_count`；`name != nullptr` 是非空检查、不做名字内容比对；不含 offset**），落在生成代码里还是单独检查工具？→ 已实现（`static_assert_abi_consistency<T>()`）。
4. **白名单默认暴露什么**：只对 `CA_REFLECT`/`CA_REFLECTION` 标注的类型生成，还是自动反射 parser 所见 + 收窄？需定"默认暴露"避免误绑内部类型。Phase 1 取"标过才生成"。
5. **`CA_REFLECT` 标记宏语义**：options 语法（`serializable`/`ui`/`min=0`/`marshal(as=...)`）与谁消费（inspector widget vs C# marshal = 一套元数据两用）需作为**codegen 输入契约**进一步定（`AUDIT-4`：6b 只传输 options、不定义语义/消费方，`serializable` 是否门控 6.1 序列化 emitter 待定）；`CA_CODEGEN_REGISTER`（已实现，无 options）为最小形态，`CA_REFLECT` 在此扩展。**引擎自有类型从 `CA_REFLECTION` 迁移到 `CA_REFLECT`：已定论 = 不在本 change**（Phase-1 只证明管道，迁移作为后续独立 task）。
   > **AUDIT-4 已定案（2026-09-08，为 6.1 落地）**：`serializable` **是** 6.1 序列化 emitter 的门控。输出形态 = 从类型表按**声明顺序**生成 per-field 的 `serialize/deserialize`（`CodeGenSerializer<T>`，含 `static serialize(T const&, ByteBuffer&)` + `static deserialize(T&, ByteBuffer const&)`）；**并行**于既有 `serializer/deserializer`（后者仍走 `visit_members`、运行时承重、本 change 不改）；仅 `serializable` 标记的类型发射序列化代码，未标记只出反射。字段顺序 = `CATypeDescriptor`/类型表声明顺序（满足 6.1 field-ORDER oracle）。
