# 002 · 反射 CodeGen 化 —— 统一引擎 CodeGen 框架

> **状态**：探索中（未达 OpenSpec proposal 水位）
> **日期**：2026-09-06
> **关联**：[[001-csharp-scripting-framework.md]]（C# 绑定生成）、[[003-hash-equality-via-codegen.md]]（哈希/相等第 4 个 emitter）、`CACore/header/Reflection.h`、`CACore/header/Serialization.h`、`Test/VulkanRendererBackendTester/private/GLMReflectionCommon.h`
> **来源**：一次 `open/explore` 讨论的沉淀。本文件论证：反射、C# 绑定、序列化都是同一条 CodeGen 管线的不同出口，值得架成**一套规范框架**而非各自为战。

---

## 1. 现状：模板反射的固有上限（`CACore/header/Reflection.h`）

现有反射是"宏 + 模板"方案，硬伤全部可见：

- **侵入式 `friend`**（`:99`）：`CA_PRIVATE_REFLECTION(Type)` = `friend struct ::CATypeDescriptor<Type>;`，**必须写进每个类型**，侵入且恶心。
- **只能反映数据成员**：`CA_REFLECTION(Type, ...)`（`:82`）产出 `std::tuple<CATypeMemberDesc<decltype(declval<Type>().Member), offsetof(...), CA_STUCTNAME_TYPE(#Member)>>`。**没有方法、没有枚举、没有基类、没有 per-member 元数据**——恰恰是脚本 / UI / 序列化最需要的。
- **`offsetof` 潜在 UB**：对非标准布局类型 `offsetof` 属于条件支持；现代 C++ 应避免。
- **900 行结构化绑定大括号展开**（`:388-924`）：为"自动枚举聚合体"手写 `if constexpr (member_count == N){ auto&&[a1..aN]=object; ... }` 直到 **64 个成员**，靠 `any_type{}` 聚合探测（`:314-331`）数成员。这就是"晦涩、难调试"的元编程塔。
- 大量 SFINAE/concept 特征（`has_resize`/`is_bytebuffer`/`containerInfo` 等）全是编译期戏法。

**这不是作者的问题，是这个方案的上限。** 且它在关键路径上（`Serialization.h` 直接 `#include "Reflection.h"`，`GLMReflectionCommon.h` 反映 glm 类型）——换不起不代表它好。

## 2. 核心洞见：反射、绑定、序列化是同一条 CodeGen 管线的三个出口

关键洞察：**反射元数据、C# 绑定、序列化，都是同一个源头（带标注的 C++ 类型）同一次解析、产出不同工件。** 所以"一套规范的 CodeGen 框架"一次解决四个痛点：

```
带标注的 C++ 头（CA_TYPE / CA_PROPERTY / CA_METHOD / CA_SUPER + 自定义 attribute）
        │
        ▼
   parser（libclang ~ast-dump=json，或吸收现有 CA_PRIVATE_REFLECTION + 结构化绑定）
        │
        ▼
   [类型表（数据，不是元编程）]
        ├──▶ Gen.Reflection.cpp  ← 反射元数据（字段/方法/枚举/基类/attributes）
        ├──▶ Gen.Bindings.cpp    ← C-ABI thunk 表（[001] C# / 跨模块）
        ├──▶ Gen.Bindings.cs     ← C# 包装
        └──▶ Gen.Serialization   ← [反]序列化
```

每条要的都是同一份"这个类型长什么样"的事实；模板版只能拿到字段，CodeGen 版还拿得到**方法 / 枚举 / attribute**——所以可读性、表达力、可调试性都压过模板。

## 3. 生成产物长什么样（before / after）

**现在**（宏 + 模板）：
```cpp
template<> struct CATypeDescriptor<MVP> {           // 每个类型都要侵入式 friend + 成员清单宏
  using member_tuple_type = std::tuple<
      CATypeMemberDesc<decltype(declval<MVP>().model), offsetof(MVP,model), CA_STUCTNAME_TYPE("model")>,
      CATypeMemberDesc<...view...>, /* ... */ >;
  static constexpr size_t member_count = 3;
  static constexpr char const* name = "MVP";
};
```

**CodeGen 生成**（plain data，能读能断点）：
```cpp
template<> struct TypeDescriptor<MVP> {
  static constexpr const char* name = "MVP";
  static constexpr const MemberInfo* members() {
    static constexpr MemberInfo arr[] = {
      { "model", offsetof(MVP,model), TypeKind::Mat4,  AsFloat, kSerializable },
      { "view",  offsetof(MVP,view),  TypeKind::Mat4,  AsFloat, kSerializable },
      { "proj",  offsetof(MVP,proj),  TypeKind::Mat4,  AsFloat, kSerializable },
    };
    return arr;          // + methods() / enumInfo() / baseClass() / attributes()
  }
};
```

差别不在"好看"，在**表达力**与**可调试性**：一条线性数组、每个字段都是 `{name, offset, type, kind, flags}` 数据；出错是看得见的生成代码，而不是元编程塔里的一层；drift 由"重新生成"变 build error。

## 4. 相比模板方案的表达力增益

- **方法/属性**：`methods()` → 脚本调用、事件、UI action。
- **枚举**：`enumInfo()` → editor 下拉、序列化。
- **基类/继承**：`baseClass()` → is-a 查询、多态、CRTP 框架。
- **per-member attribute**：`editable`、`serializable`、`min/max`、`gui_label` → 驱动 inspector widget + C# 绑定 marshal 提示。
- **robust 类型**：非聚合体、private 字段、带默认成员初始化器、`std::array`、有基类——模板版做不到或做错，CodeGen 版照常。
- **无需侵入 `friend`**：parser 读类，不必每个类型塞 `CA_PRIVATE_REFLECTION`（顶多留一个轻量 `CA_REGISTER(Foo)`）。

## 5. 与 C# 绑定合流（[001]）

- 现有 `CA_PRIVATE_REFLECTION`（ShaderResourceHandle.h:116/175 的 ImageHandle/BufferHandle）就是种子的雏形——CodeGen 反射**吸收它**，而非再造一个。
- 同一个**标记宏 `CA_REFLECT(...)` 的文本 options**（`serializable`/`ui`/`min=0`/`marshal(as=...)`），既告诉"inspector 里显示成什么 widget"，也告诉 C# 绑定器"这个字段怎么 marshal"——**一套元数据，两个消费端**（对应 [001] §5.4 绑定器需要 attribute 覆盖默认映射）。options 走标记宏**文本参数**（工具 grep 读取），**不经 libclang 自定义命名空间 attribute**（那层弱，见 §7.1）。
- **同一个 parser + 类型表**，一个 emitter 出 Gen.Bindings（[001]，C ABI + C#），一个 emitter 出 Gen.Reflection（本文件），一个出序列化。

## 6. 诚实的代价

- 要建 **parser + 接入构建**（libclang `-ast-dump=json`，或读现有反射），构建期多一步、需 clang（Unreal/Godot/Flax 都付）。
- 失去"任何类型无标注也能猜"——但模板版其实只对聚合/元组有效且一堆限制，故非真损失。换成"**自动反射 parser 所见 + `CA_REGISTER` 白名单收窄 + attribute 覆盖默认**"（与 [001] §5.3 同一哲学）。

## 7. 未决问题（open questions）

1. **parser 元数据来源**（**2026-09-06 已核实，见下方「决策门结论」**——结论 = 用 **libclang**（复用 CACodeGen 的 CLANG_LIB），引擎保持纯 MSVC，parser 作独立构建期 sidecar；**不用字面 `[[careflection::...]]`/`[[clang::annotate]]`**（已核实：MSVC C5030 警告 + libclang 自定义命名空间 attribute 弱 + `[[clang::annotate]]` AST 可见性 bug）；改用**标记宏 `CA_REFLECT(...)` + libclang AST 爬 class 体**；CA_REFLECTION **保留为外部库非侵入通道**，不配当 extraction。) **2026-09-06 追加**：标注形态明确为「标记宏做 discovery + AST 做 extraction」，`CA_REFLECTION` 降级为外部库逃生舱（详见 [engine-codegen-framework] design D2）。
2. **是否保留运行时反射 vs 纯编译期**：现有是编译期 `constexpr`；CodeGen 产物可同时供编译期（constexpr 数据）与运行期（字符串表）用——是否两者都给？（inspector/脚本要运行期字符串表，序列化要编译期。）定：生成产物应同时暴露 constexpr 数组 + 可选的运行期 `name→TypeDescriptor*` 注册表（后者复用现有 `IModuleManager`/工厂的"按名解析"）。
3. **序列化是否也迁移到同管线**：`Serialization.h` 现基于模板反射；是否把 [反]序列化做成第三个 emitter（从同一类型表），与反射/绑定同步生成？收益是三者永不错位。
4. **`CA_REGISTER` 白名单 vs 全自动**：自动反射所有 parser 所见 vs 只对 `CA_REGISTER` 的类型生成——需要定"默认暴露什么"，避免误绑内部类型（与 [001] 绑定面同问）。
5. **`offsetof` 替换**：CodeGen 虽非关键，但生成时可用 `&Type::member` 成员指针 + lambda 访问器替代裸 `offsetof` 与裸指针强转，避免非标准布局 UB。是否采用（成员指针方案对 private 成员、虚继承更稳，但生成器得多发一个访问器）。

## 7.1. 决策门结论（2026-09-06 workflow 调查核实，含 file:line）

> 本组工作的**两个前置决策门已过**，结论如下（依据见 [[004-codegen-proposal-decomposition.md]] §6 + 下方 file:line，全部经对抗回验 CONFIRMED）。

**门 1：CA_REFLECTION 真实形状 = 字段 only + 纯编译期，不够当完整 parser，且不能替换。**
- `CATypeDescriptor<T>` 主模板只存 `type/member_tuple_type/member_count/name`（`Reflection.h:58-65`）；`CATypeMemberDesc` 每字段只存 `{type, CAStructName<char...> 名字, offsetof}`（`:67-73`）。**无 methods / enumerators / base / access / per-member attribute**（`CA_REFLECTION` :82-90 只发 member 清单；`CA_PRIVATE_REFLECTION` :99 只是 `friend`）。
- **构建期工具读不到它**：全仓库无 runtime `name→TypeDescriptor` 注册表（grep registry 为空）；**唯一 `log_reflection` 以 `return;` 死掉**（`:122-133`，body 在 `:125` 提前 return），member 名只作为编译期 char-pack 类型存在（`:111-119`）。→ 构建期外部工具**无法读取** CA_REFLECTION 输出。
- **但它 runtime 承重，必须保留**：`visit_members_type_desc` 用 `((uint8_t*)&obj)+offset`（`:383`）跑在**序列化**（`Serialization.h:45-50/234-239`）与**每帧哈希**（非 trivially-copyable 的 `HashObj<VertexInputsDescriptor>` `GPUGraph.h:167,224`、`HashObj<GPUShaderBindingKey>` `GPUGraphExecutor.h:36,79`、`HashObj<DescriptorSetDesc>` `ShaderBindingHolder.h:33` 走 `visit_members` 热路径）。→ **parser 是加法，不是替换**。

**门 2：parser 元数据来源 = libclang（复用 CACodeGen 的 CLANG_LIB），引擎保持纯 MSVC，parser 作独立构建期 sidecar。**
- **libclang 可用且被用过**：`CACodeGen/ExternalLibraries/LLVM/bin/x64/libclang.dll`（**18.1.8，100,587,008B**）+ `lib/x64/libclang.lib` + `clang-c/` 头；`CLANG_LIB` 是 CMake imported target（`LLVM/CMakeLists.txt:12-13`）；`ParserLibrary`（CAParserLibrary SHARED，链 CLANG_LIB）真跑过（`clangLog.txt` 是真实 depfile）。所有 clang-c 锚点验证到位：`clang_visitChildren:3865`、`clang_getCursorKind:2302`、`CXCursor_CXXBaseSpecifier=44`、`clang_Type_getOffsetOf:3614`、`CXCursor_EnumDecl=5`、`clang_getEnumConstantDeclValue:3035`、`clang_Cursor_hasAttrs:2347`。
- **引擎保持纯 MSVC**：`build.py:95-96`（CC/CXX=cl）、`:149`（`-DCMAKE_CXX_COMPILER=cl`）；clang-cl **未安装**（`VC/Tools/Llvm` 只有 clang-format/clang-tidy，无编译器）→ `CMakePresets` 的 `x64-debug-clang` 预设只是意图。parser **不进引擎 CMake 图**，引擎**永不链接 libclang**。
- **PARSER 唯一缺口**：`ParserLibrary` 目前 **skip 枚举与函数**（`parser.cpp:189-190 EnumDecl empty break`、`:191-192 FunctionDecl`；无 EnumConstantDecl 处理）。C# 若需构建期枚举值表须小改扩展；若只当 4 字节句柄则 magic_enum 已护 runtime（`uenum.h:4`）。
- **`[[clang::annotate]]` 延期**：依赖"MSVC 对未知 attribute namespace 报 C5030"——但该行为**在仓库内无依据**（TODO 原本引用的两个 anchor 经对抗回验 REFUTED：`TODO/002:94` 是元数据来源开放问题、`TODO/004:55` 是 Phase-1 组件清单，均非 C5030 内容）→ 需**真编译验证**而非假设，故 Phase 1 不启用。用**可 grep 的 CA_* 宏**只做 **discovery**（哪些类型跨界），libclang 做 **extraction**。

## 8. 升级建议

- **当前未达 proposal 水位**（决策门已过，但 Phase 1 的具体 task 列表需按 [[004]] §3 "合格 task 标准" 重拆，且若干未决点见 `#7` + [[004]] §7）。
- **下一步 = `/opsx:propose engine-codegen-framework`**（唯一 codegen proposal，见 [[004]] §5：共享 core [parser + 类型表 + CA_* attribute + build.py hook + ABI guard + 反射 emitter]，先证明反射；序列化/哈希作为同 proposal 内 task）。
- 若推进：[001]（绑定点）+ 本 [002]（反射点）+（可能第三个 emitter）序列化，应作为**同一份 "engine CodeGen 框架" proposal 的 3 个 facet**，而非 3 份独立 change——它们共享 parser + 类型表 + 构建接入。
- 本组工作**如何拆成符合 OpenSpec 规范、单 agent 可完成、可测试的 proposal**（含"合格 task 标准"与"事实陷阱"），见 [[004-codegen-proposal-decomposition.md]]。
