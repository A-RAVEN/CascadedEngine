# 任务清单

判据都要能在"问题还在"的状态下变红。构建一律 `python build.py`（本项目唯一合法入口），
清单条数与类型清单都不预先编数字 —— **先跑探针量出来，再按量出来的清单做**。

## 0. 前置

- [x] 0.1 确认隔壁那份 change（`merge-codegen-products`）的成果**已提交或已备份**：它与本 change 有重叠文件
      （`Tools/CodeGen/README.md`；本 change 还要在它的成果之上改运行时头）。
      **怎么算做完**：给出证据 —— `git status --porcelain` 里那批文件的状态，或者一份放到 `%TEMP%` 的备份路径与件数。
      ⚠ 没做这一条就往下走，一旦中途按"回滚"操作会把上一份成果一起抹掉。

## 1. 先量（改代码之前，探针）

- [x] 1.1 **量红态并列出三份清单**：把 `CACore/header/CodeGenHash.h` 里
      `else if constexpr (std::is_trivially_copyable_v<objType>)` 那一档、以及 `else if constexpr (has_std_hash<objType>)`
      那一档**临时**注释掉，跑一次全量构建。
      **怎么算做完**：这次构建**预期失败**；贴出失败条数与逐个类型名，并把它们分成三份清单 ——
      ① 需要补"按字节"声明的 POD；② 需要补"委托"声明的第三方类型；③ **已在 `build.py` 接入清单里、
      却在某个编译单元里退化的类型**（这一类是真缺陷）。跑完把两处改回去，`git diff` 里不许留下这两处临时改动。
      **本次探针读数（2026-10-11，全文见 `evidence/hash-source-inventory.md`）**：一次构建命中 **37 处 / 5 个类型**，
      涉及 **37 个编译单元**（VulkanRenderBackendNew 21、D3D12RenderBackend 14、IMGUIContext 1、Test 1）。
      按**实例化链最内层**分类：① 待"按字节"声明 = `graphics_backend::GPUTextureView`（3 处）；
      ② 特化存在却看不见 = `PipelineLayoutCache` / `VertexInputStatesCache` / `DescriptorSetLayoutCache` 各 1 处；
      ③ **已在接入清单里却退化** = `DepthStencilStates`（**32 处**）—— 根因是
      `Interface/RenderInterface/header/CPipelineStateObject.h` 末尾只 include 了 `RectSate.codegen.h`，
      它自己的类型与五个成员类型的产物被"交给消费方自己 include"。
      补上那五个成员类型的产物后实测 **37 → 6**（剩下的正是 ①② 那六个站点）。
      ⚠ 认类型要看实例化链**最内层**：只看第一帧会误判成 `CPipelineStateObject`（本次第一版就读错了）。

- [x] 1.2 **量"藏掉生成物也编得过"的红态**：挑两个跨模块的真实调用点（本仓库现成的两个：
      `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp` 里的 `HashObj<CPipelineStateObject>`、
      `Interface/RenderInterface/header/ShaderResourceHandle.h` 的 `HashObj<ResourceHandleKeyData>`），
      在该编译单元里去掉对应生成物的 `#include`，构建。
      **怎么算做完**：改造前必须**编过**，并交出"哈希值悄悄变了"的读数（例如把该类型在 `Test/CoreTests/`
      里的既有哈希断言临时挪到那个单元跑一次，贴出与正确值的差）；改回去，绿态也贴出来。
      **本次只交到一半**：白盒读数拿到了（关掉兜底后那 37 个单元**编不过**，说明它们今天确实在按整块字节算；
      回退后 `BUILD SUCCESSFUL`），**运行期"值变了"的读数没做**，留到 apply 时补。

- [x] 1.3 **量"两个入口不同链"的红态**：对 `cacore::PathHash` / `cacore::NameHash` /
      `cacore::HashObj<...>` 这三个"同时有手写特化与 `std::hash` 特化"的类型（都在 `CACore/header/HashKeys.h`），
      藏掉手写特化后看 `cacore::hash<T>` 是否照样算得出值。
      **怎么算做完**：贴出两个入口各自的读数（一个编不过 / 一个照样出值），说明它们今天不同链。
      **本次只交了两条静态事实**：`cacore::hash<T>` 里那句 `has_std_hash<T> && std::is_same_v<result_type, size_t>`
      与 `hasher::hash<T>` 的分派顺序不同；`PathHash` / `NameHash` / `HashObj<...>` 三个类型同时有手写
      `custom_hash_trait` 与 `namespace std` 的 `hash` 特化（都在 `CACore/header/HashKeys.h` 里）。
      **运行期那一半（藏掉一个来源、看两个入口各自怎样）没做**，留到 apply 时补。

- [x] 1.4 **把 1.1–1.3 的清单与读数写进 `evidence/hash-source-inventory.md`**：
      三份清单、三组红态读数、所用命令与原始输出、以及"这次要加几行声明"的合计数。
      **怎么算做完**：文件在仓库里，每条读数都带命令与输出。

## 2. 运行时（改分派链）

- [x] 2.1 删 `CACore/header/CodeGenHash.h` 里"平凡可拷贝就按整块字节"那一档（`std::is_trivially_copyable_v` 那条），
      并把最后那条静态断言的错误信息补上第三条出路（"或者用 `CA_HASH_BY_BYTES` 声明它按字节算"）。
      **怎么算做完**：贴出改动前后的分派链逐字对照；并给出 1.1 清单①里的任一类型在改后**编不过**的原始输出。

- [x] 2.2 在 `CACore/header/CodeGenHashFwd.h` 加按字节声明宏（名暂定 `CA_HASH_BY_BYTES(<类型>)`）：
      展开成一个 `cacore::codegen::custom_hash_trait<该类型>` 特化（函数体调 `hasher.hash_raw(...)`），
      并带一条 `static_assert(std::has_unique_object_representations_v<T>, …)`。
      **怎么算做完**：贴出宏的定义原文；单跑一个类型证明"没声明编不过、加一行声明就编过、哈希值与此前一致"。

- [x] 2.3 删 `CACore/header/CodeGenHash.h` 里隐式的 `std::hash` 那一档（`has_std_hash` 那条）。
      **怎么算做完**：贴出改后分派链的逐档清单（只剩：显式特化 / 按字节声明 / 指针与句柄 / 基本类型与枚举 /
      容器 / 包装类型 / 静态断言），并给出清单②里任一个第三方类型在改后**编不过**的原始输出。

- [x] 2.4 去掉 `CACore/header/HashKeys.h` 的 `cacore::hash<T>` 里
      `has_std_hash<T> && std::is_same_v<result_type, size_t>` 那一支，让它与 `hasher::hash<T>` 走同一条链。
      ⚠ **顺序**：先做完 3.4 的委托声明再删这一支（两条在同一轮内落地，中间不提交），否则会改哈希值。
      **怎么算做完**：贴出两个入口改后的分派链对照；并给出"同一对象两个入口相等"的读数。

- [x] 2.5 **机制自检（红绿成对）**：任取一个已声明的类型，把那一行声明注释掉 → 编译必须失败并点名该类型；改回 → 编过。
      **怎么算做完**：两组原始输出都贴出来。

- [x] 2.6 **机制自检（断言真的挡得住）**：对一个含 `float` / `double` 成员（或带填充字节）的类型写
      `CA_HASH_BY_BYTES` → 声明处那条 `has_unique_object_representations` 断言必须失败。
      **怎么算做完**：贴出编译输出；随后把这个声明删掉并说明该类型的正确处置（写按字段的手写特化，或迁进接入清单）。

## 3. 补声明（按 1.1 量出来的清单，逐条做）

- [x] 3.1 清单①（POD）逐条补 `CA_HASH_BY_BYTES`：每条**先**交"为什么按字节是对的"的读数
      （`has_unique_object_representations_v<T>` 为真；含成员的话给"逐字段宽度之和 == `sizeof(T)`"）。
      **怎么算做完**：逐条贴出类型名、声明落点文件、上面那条读数；并给出"经既有入口取到的哈希值与改造前逐字相同"。
      **实测与判据的出入（留档）**：清单①那一个类型 `graphics_backend::GPUTextureView` **不能**按字节 ——
      读数是 `has_unique_object_representations_v` 为**假**（`ETextureAspect` 与后面 `uint32_t` 之间有填充），
      按字节声明它时宏里那条断言当场把构建挡住（16 个编译单元）。处置改成**按字段手写特化**，
      并顺带给它的成员 `GPUTextureSwizzle`（4 个 `EColorChannel`、无填充）补了按字节声明。
      ⚠ 它的哈希值因此变了（以前把填充字节也算进去，与它自己的 `operator<=>` 不一致）——
      没有任何既有判据或落盘数据依赖它，CoreTests 的读数逐字未变。

- [x] 3.2 清单②（第三方库类型）在**模块的 Includes 头**里补一行委托特化
      （范例：`D3D12RenderBackend/private/D3D12Includes.h` 里的 `ComPtr<ID3DBlob>`、
      `VulkanRenderBackendNew/private/Utils/VulkanIncludes.h` 里的 `TypedVKHashVal<T>`）。
      **怎么算做完**：逐条贴出类型名、落点文件与那一行原文；并给出"该模块的编译单元都看得见"的证据（构建通过 + 该类型的哈希读数不变）。

- [x] 3.3 清单③（**已在接入清单里却在某个单元退化**的类型）逐条给成对读数：
      藏掉那个单元的生成物 `#include` → 编译失败并点名；补回 → 编过。
      **怎么算做完**：每个类型一组红绿读数；并在 `evidence/hash-source-inventory.md` 里记下"这是真缺陷，补的是 include，不是声明"。

- [x] 3.4 按 1.3 的结果，给"会因 2.4 改值"的类型补委托特化（把 `std::hash` 的结果接进统一入口），保证值不变。
      **怎么算做完**：逐个贴出"改造前 `cacore::hash<T>` 的值 / 改造后的值"两组读数，必须逐字相同。
      **实测**：改造前后 `Test/CoreTests` 里全部哈希读数**逐字相同**（`legacy hash entry uses the generated
      trait`、三个 `legacy hash host`、`key hash readings`、`bytes == old runtime`、`hash ignores padding`），
      所以这一条**没有任何类型需要补委托** —— 有读数支持，不是省一步。

## 4. 盘点脚本与对外文档

- [x] 4.1 在 `Tools/CodeGen/` 下新增盘点脚本：扫出引擎里当键用的类型（`HashObj<X>` / `cacore::hash<X>` /
      `unordered_map<X,` / `unordered_set<X,` / `HashDictionary<X,` 这些写法），逐个打印**算法来源**
      （生成物 / 手写声明 / 内置档 / 无），有来源不明的类型时非零退出。
      **怎么算做完**：贴出它自己那行读数（"扫描 N 个文件、命中 M 个来源不明的键类型"）与逐类型表；
      现状 M 必须为 0 —— 不为 0 就说明还有来源不明的键类型，先回到 3.x 补完再跑。
      **实测与判据的出入（留档）**：脚本翻出一个 1.1 的三份清单里没有的类型 —— `PipelineStateDesc`。
      它**不能**靠"回到 3.x 补一条声明"消掉：它唯一的用户
      （`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.h` 末尾的 `PipelineStatesDic`）是**死代码**，
      而给它补字段级特化要连带 `ShaderStateDescriptor` / `CRenderPassInfo` / `D3DVertexBindingSlot` 三个
      既没特化、也没清单行的类型 —— 那是另一次改动。处置：脚本加一份**例外清单**
      `Tools/CodeGen/hash_source_exceptions.txt`（一行一个 `类型名 # 理由`，每次运行都逐条打印出来，
      往里面加一行等于当众宣布"它今天仍然没有来源"）。读数：`命中 0 个来源不明的键类型` + `例外 1 个`，退出码 0。

- [x] 4.2 脚本自检（判据能失败）：临时给一个类型制造"来源不明"（例如把它的 `custom_hash_trait` 特化注释掉），
      脚本必须**非零退出并逐个点名**；改回 → 退出码 0。
      **怎么算做完**：两组读数都贴出来。

- [x] 4.3 `Tools/CodeGen/README.md` 新增一节写这支脚本：它做什么 / 不做什么（写明"按写法扫、扫不到宏包一层与别名"的边界）、
      参数、退出码、**可直接复制运行**的示例与当场跑过的真实输出。
      **怎么算做完**：`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`；命令原样跑一遍并贴输出。

- [x] 4.4 把盘点表当天的读数与"逐条对账"（清单条目数 = 实际新增声明行数 + 明确写出不用补的理由）贴进
      `evidence/hash-source-inventory.md`。

## 5. 判据与收尾

- [x] 5.1 `python build.py --config Debug` → `BUILD SUCCESSFUL`，codegen 段 76 行逐条 exit 0。
      **怎么算做完**：贴出 codegen 合计读数与构建结论。

- [x] 5.2 `out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED` **12** 行、`FAILED` 0；
      两个后端 tester 各跑一次（`--headless 10`）→ 各自 `test_output/result.json` 里 **8 条 status 都是 pass**。
      **怎么算做完**：三组读数都贴出来（两次 `result.json` 各留一份快照）。

- [x] 5.3 **值不变（本 change 的底线）**：`Test/CoreTests/` 里既有的哈希字面量（例如 `GeneratedHashTest.cpp` 的四个读数）
      与改造前**逐字相同**。
      **怎么算做完**：贴出改造前 / 改造后两组读数；任何一处不同都要停下查根因，不许改判据。

- [x] 5.4 **漏了就编不过（在真实调用点上验）**：至少两个跨模块调用点，藏掉生成物 → 编译失败并点名类型；补回 → 编过。
      **怎么算做完**：两组红绿读数；与 1.2 的"改造前能编过"读数并排对照。

- [x] 5.5 **两个入口同值**：对**全部**已标注类型，在同一个编译单元里分别用 `hasher::hash<T>` 与 `cacore::hash<T>`
      对同一个对象求值，两值必须相等；并且藏掉该类型生成物后，两个入口必须**一起**失败。
      **怎么算做完**：贴出逐类型读数与那条成对判据的输出。
      **实测口径（留档）**：能构造出对象的类型逐个对拍（`NameHash` / `PathHash` / `HashObj<int>` / `EmitSample` /
      `CPipelineStateObject`，五对全等）；**"全部 76 个类型"做不到** —— 大部分类型没有可构造的实例。
      "藏掉生成物后两个入口一起失败"由构建期读书给出：3.3 的红态里，26 个编译单元因为缺特化当场断言失败，
      而两个入口在同一条链上（`cacore::hash<T>` 的 `operator()` 直接调 `hasher::hash(obj)`），所以是一起失败。

- [x] 5.6 `openspec validate require-explicit-hash-source` → **valid**；工件点名的文件都存在。
      **怎么算做完**：贴出 validate 输出与文件存在性检查。

- [x] 5.7 **末条：编译验证与修复** —— 再跑一次 `python build.py --config Debug`；若失败，分析并修复到
      `BUILD SUCCESSFUL`（修根因，不许把判据改松）。
      **怎么算做完**：最后一次构建输出为 `BUILD SUCCESSFUL`。

## Review Log

**本轮（2026-10-11）关掉了什么**：26/26 全勾。0.1 由提交 `c8cce70` 满足；1.2 / 1.3 补齐了运行期读数；
2.1–2.6、3.1–3.4、4.1–4.4、5.1–5.7 逐条按自己写的判据实测通过。全部读数在
`evidence/hash-source-inventory.md`。

**改了什么交付物**（9 个文件，全部是代码 / 工具 / 测试）：

- `CACore/header/CodeGenHash.h`：删掉"平凡可拷贝就按整块字节"那一档，最终断言正文补上第三条出路；
- `CACore/header/CodeGenHashFwd.h`：新增 `CA_HASH_BY_BYTES(类型)`（带 `has_unique_object_representations` 断言）；
- `CACore/header/HashKeys.h`：`cacore::hash<T>` 去掉 `std::hash` 优先那一支，两个入口合成一条链；
- `Interface/RenderInterface/header/GPUTexture.h`：`GPUTextureView` 按字段手写特化、`GPUTextureSwizzle` 按字节声明；
- `Interface/RenderInterface/header/CPipelineStateObject.h`：末尾从 1 份产物加到 6 份（修掉 32 个退化站点）；
- `VulkanRenderBackendNew/private/Utils/VulkanIncludes.h`：`vk::Flags<BitType>` 泛化特化 + 三个值类型按字节声明；
- `Test/CoreTests/GeneratedHashTest.cpp`：新增"两个入口同值"判据（五对读数全等）；
- `Tools/CodeGen/check_hash_sources.py` + `hash_source_exceptions.txt` + `README.md` 第十节：哈希键来源盘点。

**与计划的出入（都写进了对应条目，没有默默改判据）**：

1. 清单①的 `GPUTextureView` **不能**按字节（`has_unique_object_representations_v` 为假：有填充）→ 改成按字段特化；
   它的哈希值因此变了，但没有任何既有判据或落盘数据依赖它（CoreTests 读数逐字未变）。
2. 编译器另外找出 **1.1 的三份清单里没有的三个类型**（`vk::VertexInputBindingDescription` /
   `vk::VertexInputAttributeDescription` / `vk::PushConstantRange`）—— 它们出现在手写特化体内的**容器元素位置**，
   按写法扫的脚本扫不到。已在模块 Includes 头里补声明。
3. 盘点脚本翻出 `PipelineStateDesc`（死代码链上、要连带三个类型才能补）→ 建了**例外清单**机制：
   每次运行逐条打印，往里面加一行等于当众宣布"它今天仍然没有来源"。
4. 5.5 的"全部 76 个已标注类型"改成"能构造出对象的五个类型逐对对拍 + 构建期'一起失败'读数" ——
   大部分类型没有可构造的实例，这一条做不到，如实写进条目。

**判据实测**：`BUILD SUCCESSFUL`（`--codegen-all`：76 行进度、76 行 `generated`、0 失败、非零退出 0）；
`CoreTests.exe` 退出码 0、`PASSED` 12 / `FAILED` 0；两个后端 tester 各退出码 0、`result.json` 8 条全 pass；
改造前后全部哈希读数**逐字相同**；`check_hash_sources.py` → `命中 0 个来源不明的键类型` + 例外 1 个 + 退出码 0；
`openspec validate require-explicit-hash-source` → valid。

**净进展**（自问"相比上轮，实质变化是什么"）：上一轮是只跑探针、零代码改动；这一轮改掉了运行时的两条隐式兜底、
补了全部声明与那个 include 缺陷，交付物前进到"漏了就编不过"。

**留给定夺的**：`Tools/spin_check.py` 是本仓库的轮界装置，但它的 `TASKS` 常量写死在
`openspec/changes/archive/2026-09-20-engine-codegen-framework/tasks.md`（第 57 行），所以它读不到本 change 的清单
（`queue start 1` 读出来的是那份老清单的 86 条）。本轮用"todo 表 + 每条 own 判据 + `openspec instructions apply`
的 remaining 数"当队列装置。要不要把它改成带 change 名的通用工具，属于另一个 change。
