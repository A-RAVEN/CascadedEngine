## Context

代码生成工具（libclang sidecar，`Tools/CodeGen/`）已在 `engine-codegen-framework` 里做完并归档。它现在的接入状态，以及写这份设计时**实测**到的现状：

- `build.py` 的执行顺序是 `configure()` → `codegen()` → `build()`，即生成发生在编译之前；产物写 `out/build/<配置>/codegen/<类型>.<种类>.h`，再与入库的 `Tools/CodeGen/generated/` **逐字节对拍**，任一不一致即构建失败。
- 接入清单只有测试类型：`CODEGEN_ANNOTATED = [("Tools/CodeGen/TestEmit.h", "EmitSample", "Tools/CodeGen")]`，`CODEGEN_GOLDEN_DIR` 写死为 `Tools/CodeGen/generated`。
- 全仓没有任何引擎目标 include 过生成物；唯一消费者是 `Test/CoreTests/EmitReflectionTest.cpp`，它 include 的是入库的那份黄金样本。也就是说**删掉生成物不会有任何构建反应**。
- 实测（`out/build/x64-debug/compile_commands.json`，580 条编译命令）：
  - 按模块取"一条代表编译单元"的 include 目录会漏。**修正后的实测**（根匹配修好之后重测，见任务 2.1）：`Interface` 单条最多 12 个 / 并集 13 个、`Test` 单条最多 21 个 / 并集 22 个；`CACore` 6/6、`VulkanRenderBackendNew` 16/16 相同。（初稿写的"`Interface` 代表 8 个"是**错误的根匹配**下的读数——那次匹配到的根本是 assimp 的编译单元，见下面那条。）
  - 宏集合在一个模块内是**一致的**，只有 `Test` 有两种（其中一种带 Jolt 系列宏）。
  - `Interface`（目标 `Rendering`）与 `ThreadManager` 使用 CMake 预编译头：`/FI` 与 `/Yu` 都指向 `out/build/<配置>/.../cmake_pch.hxx`；当前转换器把 `/FI`、`/Yu`、`/Yc` 全部丢掉。
  - `/external:` 只出现 `anglebrackets` 与 `W0`（不带 include 目录），没有响应文件（`@rsp`），所以第三方头文件全部走普通 `/I`，无需手写任何头文件清单。
  - ⚠ **早先那条"真实头文件能解析到 AST"的证据无效，已当场推翻**：exit 0「no annotated types」**不区分**"解析成功但没有标注"与"解析失败"。实测三种坏输入全部返回 exit 0：① 文件里有语法错误（`struct Broken { int a }` 缺分号）；② `#include "NoSuchHeader_xyz.h"` 找不到；③ 标记宏未定义（没有 include `CodeGenMeta.h`）。
  - 实测**环境推导的模块根匹配是子串匹配**，会串到第三方：模块根 `Interface` 命中的编译单元是 `out/build/x64-debug/_deps/assimp-src/code/CApi/CInterfaceIOWrapper.cpp`（assimp 的），于是喂给 libclang 的是 assimp 的 `-std=c++17` + assimp 宏 + assimp include 目录，且**没有 `CACore/header`**。在这套错误环境下，一个真实加了标注的头文件仍然返回 exit 0「no annotated types」—— 典型的假绿。
  - 实测**诊断门控只在标注非空时才跑**：同一个坏文件，只要文件里还有一个能识别到的标注，就返回 exit 6（stderr 写明 "N error-level diagnostic(s) inside the parsed header itself"）；一旦标注为空，就直接走"empty whitelist → exit 0"提前返回，诊断从不检查。
  - 两处缺陷叠加的后果：**接一个真实类型时，环境一旦串错，工具会报告"没有标注"并成功退出，构建照常通过、生成物却不存在** —— 这正是本 change 必须先修掉的前提。
- 实测类型支持边界：`cacore::Rect<float>`（模板）在"标记在 `template` 之前"和"标记在 `template` 与 `struct` 之间"两种写法下都返回 **exit 4（未标注）**，模板不在本次范围。

## Goals / Non-Goals

**Goals:**

- 用一个**真实的引擎内部类型**把"标注 → 生成 → 入库 → 真实目标编译期消费 → 对不上即失败"整条链跑通。
- 让"删掉/改坏生成物"从**无反应**变成**构建失败**（这是当前最大的空档）。
- 把编译环境推导的两处实测缺口补掉，并把"宏集合不唯一时拒绝猜"写成可失败的规则。

**Non-Goals:**

- 不改运行时反射体系；不把手写的 `CA_REFLECTION(...)` 换成生成物；两套描述符不合并。
- 不做枚举 / 函数 / 基类提取；不支持成员私有类型（工具 exit 5 拒绝）。
- 不支持模板类型（实测 exit 4）。
- 只接这一个类型；不做"自动爬取全仓标注"。

## Decisions

### D1 接哪个类型：`graphics_backend::GPUBufferDescriptor`

- 依据：定义在 `Interface/RenderInterface/header/GPUBuffer.h`，两个 public 成员 `count` / `stride`（都是 `uint64_t`），非模板；全仓 **45 个文件**在用，D3D12 与 Vulkan 两个后端的图执行器都依赖它；它所在模块的头文件已实测能被工具解析。
- 否决的备选：`cacore::Rect<T>`（模板，实测 exit 4）；`VulkanResourceState`（成员是 `vk::` 类型，序列化 trait 未知，不适合第一次接线）；`VulkanLinearMemoryManager` 里的 `StagingAllocation` / `Page`（嵌套在类内，不是文件/命名空间级类型，工具的绑定规则覆盖不到）。

### D2 标注通道与写法：`#include "CodeGenMeta.h"` + 类型前 `CA_REFLECT(serializable)`

- 工具的判定依据是**预处理器的真实宏展开**，所以标注必须是已被定义的宏 —— 头文件里要能看到 `CodeGenMeta.h`。`GPUBuffer.h` 现有的 `Common.h` 就已经在用不带前缀的 CACore 头（`<Platform.h>` / `<DebugUtils.h>` / `<uenum.h>`），说明 `CACore/header` 本来就在这条 include 链上，加这一行不需要新 include 路径。
- 选 `CA_REFLECT` 而不是另两条通道：`CA_REFLECTION` 是引擎自己的运行时反射宏、走文本通道，且对序列化/哈希发射器返回 exit 8（本 change 要让生成物供这两者使用）；`CA_CODEGEN_REGISTER` 要求从命令行传限定名，且同样需要 `CodeGenMeta.h`，没有额外好处。
- 标记紧贴类型之前（工具按"标记之后第一个文件/命名空间级类型"绑定），中间不夹别的声明。

### D3 生成物位置与消费点

- 生成物**入库**到 `Interface/RenderInterface/generated/GPUBufferDescriptor.{Reflection,Serialization,Hash}.h`；`build.py` 仍把本次产物写 build 目录，再与入库这份逐字节对拍 —— 保持"构建不改源码树"的现有性质。
- 消费点是**新增**的编译单元 `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp`：`Rendering` 目标用 `file(GLOB_RECURSE "*.cpp")` 收源文件，加文件后重新配置即入编译；该目标恰好使用预编译头，正是本 change 要修的那类环境。
- `Interface/RenderInterface/CMakeLists.txt` 加一行 `target_include_directories(${PROJECT_NAME} PRIVATE generated)`，让消费单元用 `#include "GPUBufferDescriptor.Reflection.h"` 这种稳定写法。
- 清单增加第 4 列"生成物入库目录"（`CODEGEN_GOLDEN_DIR` 保留给现有测试条目，或者把两者统一：每条自带入库目录，取不到时报错）。

### D4 消费单元里断言什么

- 手写 `CA_REFLECTION(graphics_backend::GPUBufferDescriptor, count, stride);`（与仓库既有写法一致：`Interface/RenderInterface/header/ShaderBindingBuilder.h`、`Interface/RenderInterface/header/CPipelineStateObject.h` 就在头文件里写 `CA_REFLECTION`）。
- 断言 `static_assert((cacore::codegen::static_assert_abi_consistency<graphics_backend::GPUBufferDescriptor>(), true), "...")`（`Test/CoreTests/EmitReflectionTest.cpp` 已有同样写法）。这条断言比的是生成表的字段数与手写成员列表的数量，**只有生成头被 include 才有意义** —— 正是"生成物被消费"的可失败判据。
- 另外直接断言生成表的内容（`fieldCount == 2`、两个成员名是 `count` / `stride`），把"生成物被替换成别的类型"这类错误也变成编译错误。

### D5 对不上的两道闸（互补，不重复）

1. `build.py` 的逐字节对拍：抓"改了类型但没重新生成"（生成结果与入库版本不同 → 构建失败）。
2. 消费单元的编译期断言：抓"生成物被删、被换、字段数不符"（现在删掉它没有任何反应）。

### D6 编译环境怎么收集（不手写任何头文件清单）

- include 目录：取**该模块全部编译单元的并集**（去重、保持首次出现顺序）。实测收益：`Interface` 8 → 13、`Test` 21 → 22。并集只增加搜索路径，不制造宏。
- 宏：**必须来自单一条编译单元**，并且**当同一模块内出现多套宏时拒绝猜**（报错要求清单里点名编译单元）—— 实测 `Test` 就有两套（一套带 Jolt 宏）。这条规则把"悄悄用错宏解析出错误布局"变成一条能失败的错误。
- 预编译头：把该编译单元 `/FI` / `/Yu` 指向的文件用 `-include <绝对路径>` 补进 clang 参数。实测该文件内容只是 `#include ".../private/pch.h"`（外加 `#pragma system_header`），对当前探测头文件的解析结果无影响（补与不补都是 exit 0），所以这是一条保真修正，不是行为变化。
- 不做的方案：为每个头文件反查"哪条编译单元包含它"。需要额外的依赖扫描（MSVC `/showIncludes` 输出不入库、ninja 的 depfile 不是逐 include 清单），代价高于收益；并集 + 保真门控（exit 6）+ 逐字节对拍已覆盖本 change 的范围。若日后真出现"同模块内 include 目录冲突"的失败，再回来做这条。

### D7 模块根匹配改为按路径段，并排除构建树

- 现状是子串匹配（`if root in file`），实测把 `Interface` 匹配到 assimp 的 `CInterfaceIOWrapper.cpp`。改为：把编译单元路径规范化为相对仓库根的路径，**要求第一段等于模块根**（路径段比较，不是子串），并**排除 `out/` 下的第三方构建树**。
- 这条修好之前，任何模块的环境都可能是别人的 —— 而由于 D8 的缺陷，错了也不会报错。

### D8 解析失败不得被报告成"没有标注"

- "清单为空 → exit 0 且不产出"是既定语义（空清单是合法配置），保留。但**只有在文件确实解析成功时**才允许这样收场：把错误级诊断检查与保真门控挪到"标注为空"的提前返回**之前**。
- 判定依据用属性而不是计数：clang 报告的错误级诊断条数 > 0，或存在未解析的 include → 非零退出并点名文件与诊断；0 条错误诊断且真的没有标注 → exit 0。
- 这样 `build.py` 的接入步骤才能信任"工具成功 = 真的没标注"。

### D9 新旧两套接口的边界（写清楚，避免张冠李戴）

仓库里有两套设施：**旧的**宏反射 + 运行时序列化/哈希（`Reflection.h` 2024-04-23、`Serialization.h` 2024-01-16、`Hasher.h` 2024-04-24，宏 `CA_REFLECTION`，16 个引擎文件在用），和**新的** CodeGen（`CodeGenMeta.h` / `CodeGenTypeRegistry.h` 2026-09-12 起、`Tools/CodeGen/`、生成的 `<类型>.{Reflection,Serialization,Hash}.h`，宏 `CA_REFLECT` / `CA_CODEGEN_REGISTER`，命名空间 `cacore::codegen`）。

- **新 CodeGen 尚未正式接入引擎**：接入清单里只有它自带的测试类型，没有任何引擎类型被标注，也没有任何引擎目标 include 生成物 —— 本 change 要改变的正是这一点。
- **两套之间现在只有两处桥**：① 生成的序列化/哈希代码 include 旧的 `Serialization.h` / `Hasher.h`，把逐成员读写委托给旧的 `cacore::serializer` / `deserializer` / `hasher`；② `CodeGenMeta.h` 的 `static_assert_abi_consistency<T>()` 拿新表跟旧的 `careflection::CATypeDescriptor<T>` 比字段数。
- **因此"指针不写、读回来是空指针、哈希按指针地址、容器按元素个数逐个写"这些语义属于旧运行时**，不是新系统的规范。本 change 只做接入，不改这些语义；若日后要让新系统自建运行时，那是另一个 change（**这个岔路至今未定**）。
- **写文档与任务时要说清是哪一侧**：本 change 里凡是引用这些语义的地方，都写成"复用旧运行时的语义"，不得写成"CodeGen 的规范"。

## Risks / Trade-offs

- [给公共接口头 `GPUBuffer.h` 增加 `#include "CodeGenMeta.h"` 会扩大所有消费者的编译依赖] → `CodeGenMeta.h` 是 header-only、无第三方依赖；只在需要标注的头文件里加；改动同一次提交内更新 `Tools/CodeGen/README.md`。
- [生成物入库意味着"改类型必须重新生成"] → 这正是目的；README 写明重新生成的命令，构建失败信息里点名文件与命令。
- [断言写在 `.cpp` 里，运行时反射只在该编译单元可见] → 本 change 只做"对齐断言"，不承担"给全引擎提供运行时反射"的职责；若日后要让 `CATypeDescriptor` 由生成物供给，另开 change。
- [宏集合不唯一时拒绝构建会让某些模块暂时接不进来] → 先只接 `Interface`（实测单套宏）；`Test` 那类模块要接时按报错点名编译单元即可。
- [并集 include 目录可能引入头文件遮蔽] → 由工具已有的保真门控（exit 6）与逐字节对拍兜底；真出问题时按 D6 的备选方案收敛。

## Migration Plan

1. 先改 `build.py` 的清单结构与环境推导，跑一次现有测试条目，确认 `EmitSample` 的三个黄金样本仍逐字节相同（不许在这一步改变任何已有产物）。
2. 给 `GPUBuffer.h` 加标注、加消费单元与 CMake include 路径，生成并入库三个文件。
3. 用真实构建（`python build.py --config Debug`）验收；随后做两次**改之前实测**：① 改类型不改生成物 → 构建失败；② 删掉入库的生成物 → 消费单元编译失败。
4. 回滚策略：本 change 不改运行时行为，任何一步出问题都可用 `git revert` 单个提交回退；标注与消费单元都是新增加代码，不牵动既有调用点。

## Open Questions

- 与 `codegen-container-semantics` 的先后：那个 change 会改 `CodeGenMeta.h` 的字段表结构（加容器标志与元素描述）并因此让 `Tools/CodeGen/generated/` 的三份黄金样本变更；而本 change 的第 1 步要求这三份在改动前后逐字节不变 —— 所以**本 change 必须先做完**。
- 第二个类型接哪个？是否要优先接入"会被序列化到磁盘"的类型（那时生成物才真正被使用，而不只是被断言）。
- 运行时反射最终要不要由生成物提供（现在两套描述符靠断言对齐）？留给后续 change 决定。
- 工具的模板支持要不要做？（实测模板返回 exit 4；`cacore::Rect<T>` 这类类型目前接不进来。）
