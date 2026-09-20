# engine-codegen-framework

## Why

引擎当前靠"宏 + 模板元编程"做反射（`CACore/header/Reflection.h`），能力被结构性锁死：**只能反映数据成员**（无 methods / 枚举 / 基类 / per-member attribute），**纯编译期**（一个构建期外部工具根本无法读取其输出——`CATypeDescriptor` 只管 `type/member_tuple_type/member_count/name`，唯一的 `log_reflection` 以 `return;` 死掉 `:122-133`），且手上大量晦涩的 900 行结构化绑定展开。与此同时我们正计划接入 **C# 脚本运行时**（Phase 2-6），它需要一条**从带标注的 C++ 类型自动生成 C# 绑定**的管道，还要求这部分绑定是**可读、可调试**的生成代码，而不是模板塔。

一次解析、四条出路的 **统一 CodeGen 框架**能同时解决 4 个痛点：**反射（002）、C# 绑定（001）、序列化、哈希+相等（003）**。它们要的是同一份"这个类型长什么样"的事实；模板版只能拿到字段，CodeGen 路线的**扩展位**还包括**方法 / 枚举 / 基类 / attribute**——所以可读性、表达力、可调试性都压过模板。这就是本 change 要架的底盘。⚠ **本 change 只交付"字段"这一档**（方法与枚举/基类的提取**不在本 change 实现范围**，见下方 Non-goals 的"本次不注册任何项目类型"与 `specs/` 的收窄条款）；上面那句讲的是**路线的上限**，不是本次的交付。

## What Changes

- **引入统一 CodeGen 框架**：一条流水线 =（加标注的 C++ 头 → **libclang parser** → **线性类型表** `{name,offset,type,kind,flags}` → 多个 **emitter**）。不再是"为反射/绑定/序列化/哈希各写一套宏"。
- **标注形态 = 标记宏 + libclang AST 自动爬**：用 attribute 风格的**标记宏** `CA_REFLECT(...)`（可带 `serializable`/`ui` 等选项）标在类型前；**成员名 / type / offset**由 libclang 从 **class 体 AST 自动提取**——作者不再手写成员清单，这正是 libclang CodeGen 相对 CA_REFLECTION 的核心收益。⚠ **本 change 只提取数据字段**：方法 / 枚举 / 基类**不在实现范围**（工具侧无对应 cursor 处理，见 `tasks.md` §9.8）。
- **不用字面 `[[careflection::...]]` attribute**：MSVC 会报 C5030 警告（[developercommunity](https://developercommunity.visualstudio.com/t/c-warning-c5030-generated-for-attribute-within-a-n/138429)），且 libclang C API 读自定义命名空间 attribute 弱（[llvm#87813](https://github.com/llvm/llvm-project/issues/87813) 连 `[[clang::annotate]]` 都常不在 ClassDecl AST 出现）→ 改用**可 grep、MSVC 无副作用的标记宏**做同一件事（Unreal `UCLASS()/UPROPERTY()` 同款思路）。⚠ **本行两个 URL 的实际所用工具无落盘证据**，也未列入 Round 6 的 URL 复查表 —— 引用时按"未经独立复查"对待。
- **双模式喂同一类型表**：① 引擎自有类型（新）= `CA_REFLECT(...)` + libclang AST 爬 class 体 → 完整元数据；② **未迁移引擎 + 外部库（过渡）** = 保留 `CA_REFLECTION(Type, 成员...)`，它在命名空间作用域声明、不改类型源码，工具 grep 它 → 字段-only 表。两者进**同一张表、同一批 emitter**。已实现的 `CA_CODEGEN_REGISTER("QualifiedName")` 是标记宏的最小前置形态。**Phase-1 以示例类型证明管道；引擎自有类型从 CA_REFLECTION → CA_REFLECT 的真实迁移属后续 task（不在此 change）。**
- **parser 元数据来源 = libclang**（复用 `CACodeGen` 已跑通的 `ExternalLibraries/LLVM` 里 vendored 的 `libclang.dll` 18.1.8 + `CLANG_LIB` CMake target），而非重造。
- **引擎保持纯 MSVC 编译**。CLI 格式的 clang/clang-cl 并未安装（VS LLVM 工具集缺），`CMakePresets` 的 clang 预设只是意图；CodeGen 作为**独立构建期 sidecar 工具**由 `build.py` 调用（位置：configure 步骤内 `patch_ninja()` 之后、引擎编译之前），**引擎永不链接 libclang**。
- **Phase 1 范围**：建共享 core（parser + 类型表 + 标记宏/白名单 + build.py hook + ABI guard）+ **反射 emitter**（先证明价值，产出可读的 `TypeDescriptor<T>` 线性数组）。**序列化 + 哈希/相等作为同一 proposal 内的后续 task**（共享同 core），不单独开 change。
- **CA_REFLECTION runtime 承重保留，且兼作外部库非侵入通道**：它跑在序列化 `Serialization.h:45-50/234-239` + 非 trivially-copyable 键的每帧哈希 `GPUGraph.h:167,224` 上，Phase 1 不改其运行时路径，只新增生成元数据 + 把它当成外部类型的反射输入。

## Capabilities

### New Capabilities
- `codegen-framework`: 从带 annotation 的 C++ 类型，经 libclang 解析 + 线性类型表，生成可读代码框架（反射为 Phase 1 出口，序列化/哈希为后续 task）的共享管道与构建接入。

### Modified Capabilities
<!-- 无：反射/哈希是 CACore 内部实现细节，不是已跟踪的 OpenSpec 能力；本 change 不改变任何既有 spec 的 requirement。 -->

## Impact

- **代码**：`CACore/header/Reflection.h`、`CACore/header/Hasher.h`（现有反射/哈希实现的对照，本 change 不改其运行时行为）；`build.py`（新增 codegen 步骤）；`CMake/ExternalProjects.cmake`（现有 `link_llvm` 为死代码，本 change 不复用，从 CACodeGen 侧车复用 libclang）。
- **依赖**：新增构建期依赖 = libclang（复用 `E:/Projects/CACodeGen/ExternalLibraries/LLVM`，非 vcpkg、非新增下载）；**引擎运行时依赖不变**。
- **新工具树**：一个独立 CodeGen 可执行工具（sidecar，链接 CLANG_LIB），由 `build.py` 调用；不进入引擎 CMake 的编译图。
- **不改变**：任何已有 render/backend 运行时行为；测试入口。

## Non-goals

- **不替换 CA_REFLECTION**：它 runtime 承重，本 change 只叠加生成元数据。
- **不用 clang-cl 编译引擎**、**不让引擎链接 libclang**、Phase 1 **不 `add_subdirectory` LLVM 进引擎 CMake**（侧车复用）。
- **不启用字面 `[[careflection::...]]` attribute**：不因 MSVC C5030 警告 + libclang C API 读自定义命名空间 attribute 弱（见 What Changes，已核实），改用**标记宏 + libclang AST 爬 class 体**，取得标一下自动反射的体验又避开两坑；`[[clang::annotate]]` 因 AST 可见性 bug（llvm#87813）同样不用。
- **不建 C# 运行时 / CoreCLR / AOT / 绑定生成**：这是 Phase 2-6（`csharp-hostfxr-bootstrap`、`csharp-bindings-generator` 等），各自独立 proposal，见 `TODO/004 §5`，不在本 change。
- **不做序列化/哈希语义迁移**的运行时替换：只加生成器，不改现有 `HashObj` 行为。
- **本次不注册任何项目类型**（2026-09-08 追加）：不在引擎自有头文件里添加 CodeGen 标注宏。`ImageHandle`/`BufferHandle` 上此前加的两行 `CA_CODEGEN_REGISTER` 将**移除**，`Interface/RenderInterface/header/ShaderResourceHandle.h` 还原为未改动。全部验证改用 `Tools/CodeGen/` 下的**自包含 fixture**，**不引用任何项目头文件**——理由是 CodeGen 尚未正式接入项目，让验证引用真实类型会**增加耦合**（真实头一改、测试即断），且真实类型多为**私有成员**，不符合"零侵入通道只服务全 public 类型"的规则。
  **代价（如实记录，2026-09-13 按树更正）**：`build.py` 的 per-module 编译环境喂入机制在本 change 内**没有真实类型作演练对象**（只对自包含 fixture 演练）；而**对真实类型的接线**仍**随正式接入的后续 change 处理**。⚠ **原文把 `static_assert_abi_consistency` 的实例化也算进"未达成"，这与树不符**：该 guard 在本 change 内**已实例化**（`Test/CoreTests/EmitReflectionTest.cpp` 里 `static_assert_abi_consistency<codegen_demo::EmitSample>()`），且"漂移 → 构建失败"这条路径**已实测跑通**（见 `tasks.md` 的 9.16：改注解成员数 → `error C2338: … ABI drift …`）。
  ✅ **2026-09-13 已使本条与树相符**（原为「与树不符」，处置已落盘）：只删那 2 行注册宏**并不够** —— 同一头文件里**本来就有** `CA_REFLECTION(graphics_backend::ImageHandle, …)`，工具的白名单照样命中，实测两个类型仍各产出一份反射头（1282 B / 1124 B）。真正的处置是改 `build.py` 的清单：现清单只指自包含 fixture，逐字为 `CODEGEN_ANNOTATED = [("Tools/CodeGen/TestEmit.h", "EmitSample", "Tools/CodeGen")]`，实测两个项目类型**不再被生成**。该口径由**执行者裁定（用户未确认）**，记录见 `tasks.md` §9.15。
  **该清单的口径**（2026-09-13 由执行者裁定，选「指向自包含 fixture」）：它仍是构建期 codegen 的作用对象，故 9.9 / 9.13 的异常输入验证有对象可验；清空方案会把 `build.py` 里"从 `compile_commands.json` 取模块编译环境喂给 sidecar"这段管道变成无演练对象。**"是否注册项目类型"这条范围决策不因清单口径而改变**。
