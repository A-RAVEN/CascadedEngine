## ADDED Requirements

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
