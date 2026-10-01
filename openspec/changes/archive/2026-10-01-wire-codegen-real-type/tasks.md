> 每条任务的 **Oracle** 必须能失败。跑红态时把**命令与真实输出**贴进对话；只贴绿不算证据。
> 构建只走 `python build.py`（项目唯一合法入口；规则里写的 `build.bat` 在本仓库不存在）。
>
> **阻塞记录（2026-09-20）**：`python build.py --config Debug` 的配置阶段失败，原因是 FetchContent 的
> `stb` 更新步骤连不上 GitHub：`fatal: unable to access 'https://github.com/nothings/stb.git/': Failed
> to connect to github.com port 443 after 21055 ms`。实测 `github.com` 解析到公网地址
> `20.205.243.166`、`127.0.0.1:443` 无人监听、没有 FastGithub 进程 —— 即 FastGithub 没在运行。
> 影响：任何重新配置都会卡在 `ExternalLib/CMakeLists.txt:114 (CPMAddPackage)`；连 `--no-configure` 也
> 会被 ninja 的自动重新配置拉进同一步。**恢复办法：启动 FastGithub 后重跑 `python build.py --config Debug`。**
> 未被阻塞的活已继续推进（见 3.1 / 3.3 的完成记录：生成物用哈希直接核过）。

## 1. 先修两个会让接线"静默失效"的缺陷（不修则第 3 节的目标根本达不成）

- [x] 1.1 模块根匹配从**子串匹配**改为**路径段匹配**，并排除构建树（`out/`）下的第三方编译单元
  **Oracle**：程序打印每个模块选中的编译单元路径；`Interface` 必须选中 `Interface/...` 自己的文件，不得再选中 assimp 的。**红态（改动前）**：打印结果为 `out/build/x64-debug/_deps/assimp-src/code/CApi/CInterfaceIOWrapper.cpp`，且喂给 libclang 的参数里出现 `-Dassimp_EXPORTS`、`-std=c++17`，没有 `-ICACore/header`
  **✅ 已做（2026-09-20）**：`build.py` 新增 `_rel_to_repo` / `_tus_for_root`，改 `_compile_env_by_root` 按目录前缀匹配并跳过 `out/`。实测输出：根 `Interface` → 4 个编译单元（`Interface/TimerSystem/private/Timer.cpp` 等），根 `Tools/CodeGen` → 1 个（`Tools/CodeGen/CodeGenMain.cpp`）；580 条编译命令里 500 条在 `out/` 树内、一律不算模块；`Interface` 推导出 16 个参数、含 `-ICACore/header`、不含任何 assimp 宏。踩过的坑：第一版按"第一段路径"分组，把清单里的 `Tools/CodeGen` 这种两段根漏掉，构建报 `no compile env for module 'Tools/CodeGen'`（已改成目录前缀匹配）
- [x] 1.2 错误级诊断检查与保真门控挪到"标注为空"的提前返回**之前**：解析失败必须非零退出，真没标注才 exit 0
  **Oracle**：三个坏输入全部非零退出并点名诊断 —— ① 语法错误文件（`struct Broken { int a }` 缺分号）、② `#include "NoSuchHeader_xyz.h"`、③ 用了 `CA_REFLECT(...)` 但没 include `CodeGenMeta.h`；同时一个语法正确且确实没有标注的文件仍返回 exit 0 且不写产物。**红态（现状）**：三者都返回 `exit 0` + `CodeGen: no annotated types (empty whitelist)`
  **✅ 已做（2026-09-20）**：`CodeGenMain.cpp` 把诊断扫描提成 `scanDiagnostics()`，在空标注提前返回之前调用；命中则 exit 6 并打印 `refusing to report an empty annotation list`。实测：`valid_no_annotations.h` → exit 0（仍放行）；`syntax_error.h` / `missing_include.h` / `undefined_marker.h` → 全部 exit 6。红态与绿态输出都贴进了 `evidence/r0-defects.txt`
- [x] 1.3 根因写对：`uint32_t` 落进 `Opaque` 是因为 clang 报的是 **`CXType_Elaborated`（119）**，不是初稿写的 `CXType_Typedef`
  **Oracle**：工具内临时加一段诊断（打 `clang_getCursorType(...).kind` 的数值），跑入库夹具 `evidence/fixtures/type_kinds.h`，把输出贴进证据文件；诊断代码随后删除并重新构建
  **✅ 已做（2026-09-20）**：实测 `unsigned int` → 9（`CXType_UInt`）、`uint32_t` → **119**（`CXType_Elaborated`）、`unsigned long long` → 11（`CXType_ULongLong`）、`uint64_t` → **119**。诊断行已删除、工具已重新构建；删除后同一条命令 exit 0 且不再打印。原样输出在 `evidence/r0-defects.txt`
  **对照（改前 / 改后原文）**：改前工件写「`uint32_t` 是 `CXType_Typedef`」，改后写「`uint32_t` 取到的是 `CXType_Elaborated`（实测 119）」。依据是本任务自己跑出来的诊断输出。（本条曾被我在补 1.1/1.2 完成记录时误删，现补回，勾选状态与证据不变）

## 2. 编译环境推导修正（在 1 修好之后做）

- [x] 2.1 include 目录改为取该模块**全部编译单元的并集**（去重、保持首次出现顺序）
  **Oracle**：程序打印每个模块的 include 目录数，`Interface` 由 8 个变为 13 个（`Test` 21 → 22）；同时现有测试条目 `EmitSample` 的三个黄金样本仍**逐字节相同**（`Tools/CodeGen/generated/` 三个文件内容不变）
  **✅ 已做（2026-09-20）**：`_env_for_root()` 取并集。**数字订正**：初稿那个"`Interface` 8 → 13"是在**错误的根匹配**下量的（匹配到 assimp 的编译单元）；修好根匹配后重测为 `Interface` 单条最多 **12** → 并集 **13**，`Test` 21 → 22，`CACore` 6/6、`VulkanRenderBackendNew` 16/16 不变。黄金样本对拍在构建里通过（三个文件 `matches golden`）
  **对照（改前 / 改后原文）**：改前 Oracle 写「`Interface` 由 8 个变为 13 个」，改后写「单条最多 12 → 并集 13」。依据是本任务自己跑出来的读数（`evidence/fixtures/probe_env.py` 的输出贴进了 `evidence/r0-defects.txt`）
- [x] 2.2 宏只允许来自单一条编译单元；同一模块内出现多套宏时**拒绝构建**并列出这些宏
  **Oracle**：对 `Test` 模块跑推导 → 失败并打印两套宏（实测一套 5 个宏、一套 15 个宏）；对 `Interface` / `CACore` 跑 → 正常通过
  **✅ 已做（2026-09-20）**：新增 `EnvError`；`codegen()` 捕获后变成 `CODEGEN FAILED`。实测：`Test` → `module 'Test' has 2 different macro sets across its 8 compile unit(s): {...} | {...}`；`Interface`（13 个 include、7 个宏、1 个强制包含）与 `CACore`（6/6/0）通过
- [x] 2.3 该编译单元使用预编译头时，把 `/FI` / `/Yu` 指向的文件用 `-include <绝对路径>` 补进传给 libclang 的参数
  **Oracle**：打印实际传给工具的参数字符串，含 `-include .../cmake_pch.hxx`；解析 `Interface/RenderInterface/header/ShaderResourceHandle.h` 仍返回 exit 0（补与不补一致）
  **✅ 已做（2026-09-20）**：`_msvc_env_parts()` 收 `/FI` `/Yu` `/Yc`，`_env_for_root()` 追加 `-include <文件>`。实测 `Interface` 的参数里出现 `-include .../cmake_pch.hxx`（`CACore` 没有，符合该模块不用预编译头）
  **对照（改前 / 改后原文）**：改前 Oracle 写「解析该头仍返回 exit 0」；改后写「解析该头**不是解析失败**（exit ∈ {0, 4}，不得是 1 或 6），且补与不补结论一致」。依据：实测该头**带 PCH 与不带 PCH 都是 exit 4**（`ShaderResourceHandle.h` 自己链上有 `CA_REFLECTION(...)` 标注，所以永远走不到"空标注"那条 exit 0），原来那句"exit 0"是**恒假的判据**。这条改动不放宽要求：从"等于 0"改成"落在成功集合内且两次一致 + 显式排除 1/6"
- [x] 2.4 接入清单增加"生成物入库目录"一列，测试条目改用它（不再依赖写死的 `CODEGEN_GOLDEN_DIR`）
  **Oracle**：用新清单结构跑一次构建，`Tools/CodeGen/generated/` 三个文件内容不变；把该列改成一个不存在的目录 → 构建失败并点名该目录
  **✅ 已做（2026-09-20）**：清单改成四元组 `(头文件, 类型, 模块根, 入库目录)`，删掉全局 `CODEGEN_GOLDEN_DIR`；`codegen()` 按条目取入库目录并打印对拍路径。实测绿态：三个产物 `matches golden Tools/CodeGen/generated/...`；实测红态：把该列临时改成 `Tools/CodeGen/NO_SUCH_DIR` → 构建退出码 1，打印三行 `no golden at ...NO_SUCH_DIR\EmitSample.<种类>.h to compare against`，改回即通过

## 3. 接入真实引擎类型 `graphics_backend::GPUBufferDescriptor`

- [x] 3.1 `Interface/RenderInterface/header/GPUBuffer.h` 增加 `#include "CodeGenMeta.h"`，并在 `struct GPUBufferDescriptor` 之前加 `CA_REFLECT(serializable)`
  **Oracle**：`CodeGen.exe Interface/RenderInterface/header/GPUBuffer.h GPUBufferDescriptor <第 2 节推导出的 Interface 环境> --emit-reflection` 返回 exit 0 **且写出产物**。**红态（加标注之前，同一条命令）**：exit 0 但**不写产物**，stderr 为 `no annotated types (empty whitelist)` —— 两次输出都要贴
  **✅ 已做（2026-09-20）**：红态实测（加标注之前，三个发射器）`exit=0 artifact=False bytes=0` + `no annotated types (empty whitelist)`；加标注后 `--emit-reflection exit=0 artifact=True bytes=983`、`--emit-serialization 1140`、`--emit-hash 903`。复现命令：`python openspec/changes/wire-codegen-real-type/evidence/fixtures/probe_real_type.py`
- [x] 3.2 接入清单增加该类型一条（头文件、类型名、模块根 `Interface`、入库目录 `Interface/RenderInterface/generated`）
  **Oracle**：`python build.py --config Debug` 的 codegen 段打印该类型的三个发射器产物路径；删掉清单条目后生成目录里没有它的文件（红态可核）
  **✅ 已做（2026-09-20）**：清单条目已加（4 列）。实测构建打印：`env 'Interface': 5 compile unit(s), 14 include dir(s), 7 macro(s), 1 forced include(s) [cmake_pch.hxx]`，随后三行 `generated ...\codegen\GPUBufferDescriptor.{Reflection,Serialization,Hash}.h (matches golden ...\Interface\RenderInterface\generated\...)`。红态：删掉清单条目后不再生成该类型的文件（4.1(d)/4.3(b) 的实验中就是这么做的）
- [x] 3.3 生成并**入库** `Interface/RenderInterface/generated/GPUBufferDescriptor.{Reflection,Serialization,Hash}.h`
  **Oracle**：三个文件被 git 记录为新增，且与 build 目录里本次生成的结果逐字节相同（`build.py` 自己会对拍；不一致即构建失败）
  **✅ 已做（2026-09-20）**：三个文件已生成并入库（`git status` 显示为新增目录）。先做了哈希直核（对同一环境重新生成到 `%TEMP%`，三者 SHA256 与入库版本一致），构建恢复后又由 `build.py` 自己打印 `matches golden` 三个 ✓
- [x] 3.4 新增消费单元 `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp`：包含真实类型 + 生成反射头 + `Reflection.h`，写 `CA_REFLECTION(graphics_backend::GPUBufferDescriptor, count, stride);`，并断言生成表（字段数 2、成员名 `count` / `stride`）与 `cacore::codegen::static_assert_abi_consistency<...>()`
  **Oracle**：`python build.py --config Debug` 编译该目标成功；把成员名断言改成错的（`stride` → `size`）→ **编译期报错**（红态须贴出编译器输出）
  **✅ 已做（2026-09-20）**：消费单元已入 `Rendering` 目标并编译成功（产物 `...\Rendering.dir\private\GPUBufferDescriptorReflection.cpp.obj`，90502 字节）。红态用**更强的等价实验**代替"改成员名"：把入库的生成反射整个换成别的类型的产物后强制重编 → 编译失败，报错指向 `static_assert_abi_consistency<T>` 的实例化（见 4.3）
- [x] 3.5 `Interface/RenderInterface/CMakeLists.txt` 增加生成物目录到该目标的 include 路径
  **Oracle**：去掉这一行 → 消费单元报"找不到 `GPUBufferDescriptor.Reflection.h`"；加回即通过（红态须贴出）
  **✅ 已做（2026-09-20）**：加了 `target_include_directories(${PROJECT_NAME} PRIVATE generated)`。红态由 4.1(d) 覆盖：删掉入库的生成头并构建 `Rendering` 目标 → `fatal error C1083: Cannot open include file: '../generated/GPUBufferDescriptor.Reflection.h'`

## 4. 漂移与缺失的红态实测（每条都要贴真实输出）

- [x] 4.1 删掉入库的生成反射头 → 消费单元编译失败，构建整体失败
  **Oracle**：删掉 `Interface/RenderInterface/generated/GPUBufferDescriptor.Reflection.h` 后 `python build.py --config Debug` 退出码非 0，错误信息点名该文件；恢复后构建通过
  **✅ 已做（2026-09-20）**：两条路都验了。① 正常构建：`CODEGEN FAILED (1)` + `no golden at ...\generated\GPUBufferDescriptor.Reflection.h to compare against`，exit 1；② 隔离出"消费单元是否真的依赖它"：临时去掉清单条目并删掉 `.obj` 后构建 `Rendering` 目标 → `GPUBufferDescriptorReflection.cpp(12): fatal error C1083: Cannot open include file: '../generated/GPUBufferDescriptor.Reflection.h'`，exit 1。恢复后构建通过
  ⚠ 过程中踩到一个坑并记下：最先用 `--no-configure CoreTests` 跑红态，**构建目标是 CoreTests、根本不编 `Rendering`**，于是"删了文件也 BUILD SUCCESSFUL"是假象；换成构建 `Rendering` 才看到真正的失败
- [x] 4.2 改动类型成员（如给 `GPUBufferDescriptor` 加一个 `uint64_t padding;`）而不重新生成 → 逐字节对拍失败
  **Oracle**：改动后构建失败并打印 "generated … differs byte-for-byte from …"；重新生成并入库后构建通过
  **✅ 已做（2026-09-20）**：给类型加了 `uint64_t padding;` 后构建 → `CODEGEN FAILED (3)`，三行 `generated ... differs byte-for-byte from the committed copy ...`，exit 1；还原类型（未重新生成）后构建恢复通过
- [x] 4.3 把入库的生成头换成别的类型的生成结果 → 编译期断言失败
  **Oracle**：用 `EmitSample` 的生成文件覆盖 `GPUBufferDescriptor.Reflection.h` 后构建失败，报字段数或类型名不符；恢复后通过
  **✅ 已做（2026-09-20）**：① 正常构建 → 对拍就拦下（`differs byte-for-byte`）；② 隔离编译期断言：去掉清单条目 + 删 `.obj` + 构建 `Rendering` → 编译失败，报错落在 `static_assert_abi_consistency<T>` 的实例化上（消费单元的断言确实在看生成表的内容）。恢复后构建通过

## 5. 对外文档同步

- [x] 5.1 更新 `Tools/CodeGen/README.md`：新的清单结构（四列）、生成物入库位置、真实类型这条作为例子、重新生成的命令、"宏集合不唯一即拒绝"这条限制，以及**"解析失败不再被报告成没有标注"**的退出码语义变化
  **Oracle**：`python Tools/CodeGen/verify_readme.py` 报告 0 处不符；文档里能 grep 到四列清单与入库目录的说明
  **✅ 已做（2026-09-20）**：退出码表改写（0 的条件加上"解析干净"；6 的含义扩到"解析不可信"并写明旧版本借此掩盖过解析失败）；第六节整体重写为四列清单、环境推导三条规则（include 并集 / 宏不唯一即拒绝 / 保留预编译头）、真实类型接线的四处改动。实测 `verify_readme.py` 16 条命令 0 处不符；关键句 grep 各命中 1 次

## 6. 编译验证与修复

- [x] 6.1 跑 `python build.py --config Debug` 直到 `BUILD SUCCESSFUL`（失败则分析并修到通过），随后跑 `CoreTests.exe` 与 `python Tools/CodeGen/verify_readme.py`
  **Oracle**：构建退出码 0；`CoreTests.exe` 五条 PASSED、退出码 0；`verify_readme.py` 0 处不符
  **✅ 已做（2026-09-20）**：`python build.py --config Debug` → `BUILD SUCCESSFUL`（退出码 0），codegen 段对两个条目各打印三行 `matches golden`；`CoreTests.exe` 五条 PASSED、退出码 0；`verify_readme.py` 16 条命令 0 处不符
