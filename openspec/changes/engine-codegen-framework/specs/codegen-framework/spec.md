# codegen-framework

## ADDED Requirements

### Requirement: 统一 CodeGen 流水线从 annotated 头产出可读生成代码
系统 SHALL 提供一条统一 CodeGen 流水线：解析带 annotation 的 C++ 头文件，产出**线性类型表**（`{name, offset, type, kind, flags}` 数据，而非元编程），并由一个 emitter 生成可读的反射代码（`TypeDescriptor<T>` 线性数组）。同一张类型表 SHALL 可供后续序列化 / 哈希 emitter 复用。

#### Scenario: 解析一个 annotated 类型并生成线性描述
- **WHEN** 一个用 CA_* 标注的 C++ 类型（如含 `name/offset/type` 的字段）被 pipelined 解析
- **THEN** 生成的反射代码是线性数组，每个字段暴露 `name`, `offset`, `type`, `kind`, `flags`，且对同一注解展开两次产物逐字节一致

#### Scenario: 反射只能覆盖字段之外也要能从表拿到
- **WHEN** 被解析类型包含方法 / 枚举 / 基类（libclang 可提取）
- **THEN** 类型表可暴露 `methods()`/`enumInfo()`/`baseClass()` 元数据（如实现支持），而非仅有字段

### Requirement: 反射标注用标记宏自动爬取 + 未迁移/外部走非侵入通道
系统 SHALL 提供 attribute 风格的**标记宏** `CA_REFLECT(...)`（可带 `serializable`/`ui`/`min=0` 等 options），由 libclang 从 **class 体 AST 自动提取**成员名/type/offset/方法/枚举/基类（作者**不手写成员清单**）；未迁移引擎类型与外部库类型 SHALL 仍可用 `CA_REFLECTION(Type, 成员...)` **非侵入式**声明（命名空间作用域、不改类型源码），两者喂**同一张类型表**、同一批 emitter。系统 SHALL NOT 依赖字面 `[[careflection::...]]` 或 `[[clang::annotate]]` attribute 作元数据载体（MSVC C5030 警告 / libclang 读自定义命名空间 attribute 弱 / `[[clang::annotate]]` AST 可见性 bug）。**本 change 以示例类型证明管道；引擎自有类型从 CA_REFLECTION 迁移到 CA_REFLECT 属后续 task，不在此 SHALL**。

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
  > 注：`test_output/` 被 git-ignore、VUID baseline 不可提交（TODO/004 §3.4），故本条取**自洽可证伪断言**；若需严格"无新增 VUID vs baseline"diff，须前置"提交基线" task（可选强化，非本条验收门槛）。——RC-H 调和

### Requirement: 生成源码以显式源清单纳入构建
系统 SHALL 用**显式方式**纳入生成的源文件（非 `file(GLOB_RECURSE)`）：生成的**头文件**经显式 include 路径 + `#include` 纳入；生成的 **.cpp**（若存在）经显式 `target_sources` 纳入（禁 `file(GLOB_RECURSE *.cpp)`）。生成的反射代码 SHALL 与现有 `CATypeDescriptor<T>` 做构建期一致性校验。——RC-F 调和（诚实前提：本 change 生成物为头文件）

#### Scenario: 缺失生成文件被构建捕获
- **WHEN** 生成源文件缺失 / 与注解漂移
- **THEN** 构建以失败而非静默通过：缺失生成头 → 经显式 `#include` 报编译错误；缺失生成 .cpp → 经显式 `target_sources` 报构建错误；与注解漂移 → 经 `member_count`/offset ABI 校验（ABI guard）失败；均非静默。

### Requirement: build.py 接入 codegen 步骤
`build.py` SHALL 在 `patch_ninja()`（`build.py:230`）与 `build()`（`:231`）之间调用 codegen sidecar，且该步骤在 MSVC 环境下运行（继承 vcvars 与 sanitized PATH）。

#### Scenario: 构建顺序正确
- **WHEN** 执行 `python build.py`（无 `--no-configure`）
- **THEN** 日志显示 codegen 步骤在 configure/patch_ninja 之后、engine 编译之前执行，且未拆分出别的无关构建动作
