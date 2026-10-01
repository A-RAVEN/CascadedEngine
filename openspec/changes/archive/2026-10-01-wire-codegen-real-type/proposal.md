## Why

代码生成工具已经做完并归档，但接入清单里只有一个自带的测试类型（`Tools/CodeGen/TestEmit.h` 里的 `codegen_demo::EmitSample`）：**生成物没有任何引擎目标消费**，所以"标注 → 生成 → 编译期消费 → 漂移即失败"这条链从来没有在真实类型上跑过。挑一个真实的引擎内部类型接一次，把这条链跑通，并给每一环留下**能失败**的判据。

## What Changes

- 选 `graphics_backend::GPUBufferDescriptor`（定义在 `Interface/RenderInterface/header/GPUBuffer.h`，两个 public 成员 `count` / `stride`，全仓 **45 个文件**在用，D3D12 与 Vulkan 两个后端都依赖它）作为第一个真实类型。类型本身不改语义、不改布局。
- 在这个真实头文件上加标注（`CA_REFLECT` 还是 `CA_CODEGEN_REGISTER` 由 design 定）。
- 生成物**入库到模块目录**（`Interface/RenderInterface/generated/GPUBufferDescriptor.{Reflection,Serialization,Hash}.h`）。`build.py` 把本次生成的产物与入库那份**逐字节对拍**，不一致即构建失败。
- 接入清单增加**生成物入库目录**一列 —— 现在黄金样本目录写死在 `Tools/CodeGen/generated`，接第二个类型就必须按模块存放。
- **真实目标消费生成物**：在一个已经包含该类型的真实编译单元里 include 生成头，并用 `careflection::CATypeDescriptor` 那侧的手写成员列表与生成表做编译期对齐断言。删掉、改坏或漏生成这个文件，**该目标编译失败**（现在删掉它不会有任何反应 —— 这正是本 change 要消掉的空档）。
- 编译环境推导修正（两处**实测**缺口）：① 现在按模块取"一条代表编译单元"的编译环境，实测 `Interface` 会漏 5 个 include 目录、`Test` 漏 1 个 → 改成取该模块全部编译单元的 **include 目录并集**；② `Interface` 与 `ThreadManager` 使用 CMake 预编译头（`/FI` + `/Yu` 指向 `cmake_pch.hxx`），现在的转换器把这两个开关**丢掉** → 补 `-include`。另外：宏只能来自单一条编译单元，**同一模块内出现多套宏时拒绝构建**并报出来（实测 `Test` 有两套）。
- 明确两套描述符的分工：生成物提供字段表 / 序列化 / 哈希；引擎运行时反射继续用手写 `CA_REFLECTION(...)`；两者用编译期断言对齐，**本 change 不合并它们**。
- **修掉两个会让接线静默失效的缺陷**（写这份 proposal 时实测出来的，不修则本 change 的目标无法达成）：
  - **模块根匹配是子串匹配**：实测模块根 `Interface` 命中的编译单元是 `out/build/x64-debug/_deps/assimp-src/code/CApi/CInterfaceIOWrapper.cpp`（assimp 的），于是喂给 libclang 的是 assimp 的 `-std=c++17` + assimp 宏与 include 目录，且没有 `CACore/header`。改为按**路径段**匹配并排除 `out/` 构建树。
  - **解析失败被报告成"没有标注"**：实测语法错误文件、找不到的 `#include`、标记宏未定义三种情况**都返回 exit 0**「no annotated types」，因为诊断与保真门控只在标注非空时才跑。改为：诊断与保真门控在"标注为空"的提前返回之前执行 —— 真没标注才 exit 0。

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `codegen-framework`：新增三条要求 —— ① **生成物必须被真实引擎目标消费**（不再只是与黄金样本对拍）；② **解析一个头文件所用的编译环境必须来自该模块自己的编译单元**（模块根按路径段匹配，include 目录取并集，保留预编译头的强制包含，宏不唯一时拒绝猜）；③ **解析失败不得被报告成"没有标注"**（诊断与保真门控必须先于空清单的提前返回）。

## Non-goals

- 不改运行时反射体系，不把手写的 `CA_REFLECTION(...)` 换成生成物；两套描述符不合并。
- 不做枚举 / 函数 / 基类提取；不支持成员私有的类型（工具会以 exit 5 拒绝）。
- **不支持模板类型**：实测 `cacore::Rect<float>` 在两种标注写法下都返回 exit 4（未标注），模板不在本次范围。
- 只接这一个类型，不追求覆盖所有模块；不把"自动爬取全仓标注"作为目标。

## Impact

- `Interface/RenderInterface/header/GPUBuffer.h`：加标注（不改语义与布局）
- 新增 `Interface/RenderInterface/generated/`（入库的生成代码）
- `build.py`：清单结构（多一列入库目录）、按头文件取编译环境、把预编译头补进 clang 参数
- 消费方：`D3D12RenderBackend` 里一个已包含该类型的编译单元（具体文件由 design 定），需要 include 路径与编译期断言
- `Tools/CodeGen/README.md`：第六节"构建期怎么用它"要按新的清单结构与入库位置改写
