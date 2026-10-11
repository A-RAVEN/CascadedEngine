# 任务清单

判据都要能在"问题还在"的状态下变红。构建一律 `python build.py`（本项目唯一合法入口），
清单条数与类型清单都不预先编数字 —— **先跑探针量出来，再按量出来的清单做**。

## 0. 前置

- [ ] 0.1 确认隔壁那份 change（`merge-codegen-products`）的成果**已提交或已备份**：它与本 change 有重叠文件
      （`Tools/CodeGen/README.md`；本 change 还要在它的成果之上改运行时头）。
      **怎么算做完**：给出证据 —— `git status --porcelain` 里那批文件的状态，或者一份放到 `%TEMP%` 的备份路径与件数。
      ⚠ 没做这一条就往下走，一旦中途按"回滚"操作会把上一份成果一起抹掉。

## 1. 先量（改代码之前，探针）

- [ ] 1.1 **量红态并列出三份清单**：把 `CACore/header/CodeGenHash.h` 里
      `else if constexpr (std::is_trivially_copyable_v<objType>)` 那一档、以及 `else if constexpr (has_std_hash<objType>)`
      那一档**临时**注释掉，跑一次全量构建。
      **怎么算做完**：这次构建**预期失败**；贴出失败条数与逐个类型名，并把它们分成三份清单 ——
      ① 需要补"按字节"声明的 POD；② 需要补"委托"声明的第三方类型；③ **已在 `build.py` 接入清单里、
      却在某个编译单元里退化的类型**（这一类是真缺陷）。跑完把两处改回去，`git diff` 里不许留下这两处临时改动。

- [ ] 1.2 **量"藏掉生成物也编得过"的红态**：挑两个跨模块的真实调用点（本仓库现成的两个：
      `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp` 里的 `HashObj<CPipelineStateObject>`、
      `Interface/RenderInterface/header/ShaderResourceHandle.h` 的 `HashObj<ResourceHandleKeyData>`），
      在该编译单元里去掉对应生成物的 `#include`，构建。
      **怎么算做完**：改造前必须**编过**，并交出"哈希值悄悄变了"的读数（例如把该类型在 `Test/CoreTests/`
      里的既有哈希断言临时挪到那个单元跑一次，贴出与正确值的差）；改回去，绿态也贴出来。

- [ ] 1.3 **量"两个入口不同链"的红态**：对 `cacore::PathHash` / `cacore::NameHash` /
      `cacore::HashObj<...>` 这三个"同时有手写特化与 `std::hash` 特化"的类型（都在 `CACore/header/HashKeys.h`），
      藏掉手写特化后看 `cacore::hash<T>` 是否照样算得出值。
      **怎么算做完**：贴出两个入口各自的读数（一个编不过 / 一个照样出值），说明它们今天不同链。

- [ ] 1.4 **把 1.1–1.3 的清单与读数写进 `evidence/hash-source-inventory.md`**：
      三份清单、三组红态读数、所用命令与原始输出、以及"这次要加几行声明"的合计数。
      **怎么算做完**：文件在仓库里，每条读数都带命令与输出。

## 2. 运行时（改分派链）

- [ ] 2.1 删 `CACore/header/CodeGenHash.h` 里"平凡可拷贝就按整块字节"那一档（`std::is_trivially_copyable_v` 那条），
      并把最后那条静态断言的错误信息补上第三条出路（"或者用 `CA_HASH_BY_BYTES` 声明它按字节算"）。
      **怎么算做完**：贴出改动前后的分派链逐字对照；并给出 1.1 清单①里的任一类型在改后**编不过**的原始输出。

- [ ] 2.2 在 `CACore/header/CodeGenHashFwd.h` 加按字节声明宏（名暂定 `CA_HASH_BY_BYTES(<类型>)`）：
      展开成一个 `cacore::codegen::custom_hash_trait<该类型>` 特化（函数体调 `hasher.hash_raw(...)`），
      并带一条 `static_assert(std::has_unique_object_representations_v<T>, …)`。
      **怎么算做完**：贴出宏的定义原文；单跑一个类型证明"没声明编不过、加一行声明就编过、哈希值与此前一致"。

- [ ] 2.3 删 `CACore/header/CodeGenHash.h` 里隐式的 `std::hash` 那一档（`has_std_hash` 那条）。
      **怎么算做完**：贴出改后分派链的逐档清单（只剩：显式特化 / 按字节声明 / 指针与句柄 / 基本类型与枚举 /
      容器 / 包装类型 / 静态断言），并给出清单②里任一个第三方类型在改后**编不过**的原始输出。

- [ ] 2.4 去掉 `CACore/header/HashKeys.h` 的 `cacore::hash<T>` 里
      `has_std_hash<T> && std::is_same_v<result_type, size_t>` 那一支，让它与 `hasher::hash<T>` 走同一条链。
      ⚠ **顺序**：先做完 3.4 的委托声明再删这一支（两条在同一轮内落地，中间不提交），否则会改哈希值。
      **怎么算做完**：贴出两个入口改后的分派链对照；并给出"同一对象两个入口相等"的读数。

- [ ] 2.5 **机制自检（红绿成对）**：任取一个已声明的类型，把那一行声明注释掉 → 编译必须失败并点名该类型；改回 → 编过。
      **怎么算做完**：两组原始输出都贴出来。

- [ ] 2.6 **机制自检（断言真的挡得住）**：对一个含 `float` / `double` 成员（或带填充字节）的类型写
      `CA_HASH_BY_BYTES` → 声明处那条 `has_unique_object_representations` 断言必须失败。
      **怎么算做完**：贴出编译输出；随后把这个声明删掉并说明该类型的正确处置（写按字段的手写特化，或迁进接入清单）。

## 3. 补声明（按 1.1 量出来的清单，逐条做）

- [ ] 3.1 清单①（POD）逐条补 `CA_HASH_BY_BYTES`：每条**先**交"为什么按字节是对的"的读数
      （`has_unique_object_representations_v<T>` 为真；含成员的话给"逐字段宽度之和 == `sizeof(T)`"）。
      **怎么算做完**：逐条贴出类型名、声明落点文件、上面那条读数；并给出"经既有入口取到的哈希值与改造前逐字相同"。

- [ ] 3.2 清单②（第三方库类型）在**模块的 Includes 头**里补一行委托特化
      （范例：`D3D12RenderBackend/private/D3D12Includes.h` 里的 `ComPtr<ID3DBlob>`、
      `VulkanRenderBackendNew/private/Utils/VulkanIncludes.h` 里的 `TypedVKHashVal<T>`）。
      **怎么算做完**：逐条贴出类型名、落点文件与那一行原文；并给出"该模块的编译单元都看得见"的证据（构建通过 + 该类型的哈希读数不变）。

- [ ] 3.3 清单③（**已在接入清单里却在某个单元退化**的类型）逐条给成对读数：
      藏掉那个单元的生成物 `#include` → 编译失败并点名；补回 → 编过。
      **怎么算做完**：每个类型一组红绿读数；并在 `evidence/hash-source-inventory.md` 里记下"这是真缺陷，补的是 include，不是声明"。

- [ ] 3.4 按 1.3 的结果，给"会因 2.4 改值"的类型补委托特化（把 `std::hash` 的结果接进统一入口），保证值不变。
      **怎么算做完**：逐个贴出"改造前 `cacore::hash<T>` 的值 / 改造后的值"两组读数，必须逐字相同。

## 4. 盘点脚本与对外文档

- [ ] 4.1 在 `Tools/CodeGen/` 下新增盘点脚本：扫出引擎里当键用的类型（`HashObj<X>` / `cacore::hash<X>` /
      `unordered_map<X,` / `unordered_set<X,` / `HashDictionary<X,` 这些写法），逐个打印**算法来源**
      （生成物 / 手写声明 / 内置档 / 无），有来源不明的类型时非零退出。
      **怎么算做完**：贴出它自己那行读数（"扫描 N 个文件、命中 M 个来源不明的键类型"）与逐类型表；
      现状 M 必须为 0 —— 不为 0 就说明还有来源不明的键类型，先回到 3.x 补完再跑。

- [ ] 4.2 脚本自检（判据能失败）：临时给一个类型制造"来源不明"（例如把它的 `custom_hash_trait` 特化注释掉），
      脚本必须**非零退出并逐个点名**；改回 → 退出码 0。
      **怎么算做完**：两组读数都贴出来。

- [ ] 4.3 `Tools/CodeGen/README.md` 新增一节写这支脚本：它做什么 / 不做什么（写明"按写法扫、扫不到宏包一层与别名"的边界）、
      参数、退出码、**可直接复制运行**的示例与当场跑过的真实输出。
      **怎么算做完**：`python Tools/CodeGen/verify_readme.py` → `0 mismatch(es)`；命令原样跑一遍并贴输出。

- [ ] 4.4 把盘点表当天的读数与"逐条对账"（清单条目数 = 实际新增声明行数 + 明确写出不用补的理由）贴进
      `evidence/hash-source-inventory.md`。

## 5. 判据与收尾

- [ ] 5.1 `python build.py --config Debug` → `BUILD SUCCESSFUL`，codegen 段 76 行逐条 exit 0。
      **怎么算做完**：贴出 codegen 合计读数与构建结论。

- [ ] 5.2 `out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED` **12** 行、`FAILED` 0；
      两个后端 tester 各跑一次（`--headless 10`）→ 各自 `test_output/result.json` 里 **8 条 status 都是 pass**。
      **怎么算做完**：三组读数都贴出来（两次 `result.json` 各留一份快照）。

- [ ] 5.3 **值不变（本 change 的底线）**：`Test/CoreTests/` 里既有的哈希字面量（例如 `GeneratedHashTest.cpp` 的四个读数）
      与改造前**逐字相同**。
      **怎么算做完**：贴出改造前 / 改造后两组读数；任何一处不同都要停下查根因，不许改判据。

- [ ] 5.4 **漏了就编不过（在真实调用点上验）**：至少两个跨模块调用点，藏掉生成物 → 编译失败并点名类型；补回 → 编过。
      **怎么算做完**：两组红绿读数；与 1.2 的"改造前能编过"读数并排对照。

- [ ] 5.5 **两个入口同值**：对**全部**已标注类型，在同一个编译单元里分别用 `hasher::hash<T>` 与 `cacore::hash<T>`
      对同一个对象求值，两值必须相等；并且藏掉该类型生成物后，两个入口必须**一起**失败。
      **怎么算做完**：贴出逐类型读数与那条成对判据的输出。

- [ ] 5.6 `openspec validate require-explicit-hash-source` → **valid**；工件点名的文件都存在。
      **怎么算做完**：贴出 validate 输出与文件存在性检查。

- [ ] 5.7 **末条：编译验证与修复** —— 再跑一次 `python build.py --config Debug`；若失败，分析并修复到
      `BUILD SUCCESSFUL`（修根因，不许把判据改松）。
      **怎么算做完**：最后一次构建输出为 `BUILD SUCCESSFUL`。
