## Why

"这个类型的哈希怎么算"目前取决于**每个编译单元自己看得见什么**。`CACore/header/CodeGenHash.h` 的分派链是：

- `if constexpr (has_custom_hash_func<objType, hasher>)` → 用生成物（或手写）给的字段级特化；
- `else if constexpr (std::is_trivially_copyable_v<objType>)` → **悄悄按整块字节算**；
- ……最后一条才是 `static_assert(sizeof(objType) == 0, "这个类型没有生成代码、没有自定义哈希特化…")`。

于是：只要某个编译单元没 include 那个类型的生成物，它就**静默**走第二条，别的单元走第一条。同一个函数名两份不同的体，链接期合并弱符号后整个二进制用哪一份由链接器决定 —— 而标准把这种情况定为"程序不合格但**不要求**编译器报错"，所以**没有任何诊断**。这件事已经出过一次：一条与改动无关的老判据由 1 掉成 0（读数在归档 change `2026-10-07-clear-codegen-parse-blockers` 的 design 里，那一份把它记作"漏 include 一份生成物 → 该类型哈希口径被换掉"）。

为什么现在做：三份产物刚合并成一份（`merge-codegen-products`），"漏哪一份"这个判断已经消失，正是重审分派链的时机；而这类分叉只在运行期以"哈希值不对/查不到"的形式现身，越晚越贵。

## What Changes

- **删掉"平凡可拷贝就按整块字节"这条默认兜底**：按字节从"默认"改成**要显式声明**——在类型旁边写一行 `CA_HASH_BY_BYTES(<类型>)`（这是"哪条路"的声明，不是算法实现）。声明处带一条 `static_assert(std::has_unique_object_representations_v<T>, …)`，把"有填充字节"和"含 `float`/`double`"（`-0.0` 与 `+0.0` 字节不同、NaN 有多种表示）的类型挡在门外，逼它们进清单走字段级（字段级那条路会把 `-0.0` 归一、NaN 规范成一个值）。
- **没有声明、也没有生成特化的类型 → 编译错误**，不再沉默。现成那句静态断言会补上第三条出路（"或者用 `CA_HASH_BY_BYTES` 声明它按字节算"），错误信息里带类型名。
- **`std::hash` 这条隐式兜底也改成显式**：外部库类型不能改它源码，就在**模块的 Includes 头**里写一行委托特化（仓库已有先例：`D3D12RenderBackend/private/D3D12Includes.h` 里的 `ComPtr<ID3DBlob>`、`VulkanRenderBackendNew/private/Utils/VulkanIncludes.h` 里的 `TypedVKHashVal<T>`），然后删掉 `CodeGenHash.h` 里那条隐式的 `std::hash` 档。理由：`std::hash` **只给哈希、不给相等**，容器还要相等比较，两个来源分离就会出现"相等说一样、哈希说不一样"；字段级特化是同一个生成物同时给出哈希与相等。
- **两个入口的优先级统一**：`CACore/header/HashKeys.h` 的 `cacore::hash<T>` 里那句 `if constexpr (has_std_hash<T> && std::is_same_v<result_type, size_t>) { return std::hash<T>{}(obj); }` 去掉，让它和 `hasher::hash<T>` 走同一条链。今天两个入口顺序不同，只是算出来的值恰好相同（靠约定与几条测试钉着）。
- **加一次"算法来源盘点"**：`Tools/CodeGen/` 下加一个脚本（形状照 `check_legacy_symbols.py`），扫出引擎里所有当哈希键用的类型，逐个输出它的**算法来源** —— 生成物 / 手写声明 / 内置档（基本类型、指针与句柄、容器、包装类型）/ **都没有**；最后一类就是漏的。同时加一条判据：同一个对象经两个入口算出的值必须相等。
- **先量后改**：动代码之前先跑探针（把上面两条隐式兜底临时收掉、构建一次），当场得到三份清单 —— ①需要补"按字节"声明的 POD；②需要补"委托"声明的第三方类型；③**已经在接入清单里、却在某个编译单元里退化的类型**（这一类是真缺陷，要补的是 include，不是声明）。三份清单贴进 change 的证据文件，再按清单落地。

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `codegen-runtime`：**新增**一条要求（哈希算法来源必须显式且处处一致：缺失即编译错误、两个入口同序、来源可盘点）。
- `codegen-runtime`：**修改**「新哈希必须在既有调用点生效」—— 它现在逐字写着"允许把'有自定义特化'的判据提到'平凡可拷贝'之前；做这件事的前提是：所有既有特化的宿主类型都不是平凡可拷贝"，而这条前提正是本 change 要推翻的东西；它点名的入口 `aggregateHasher::hash` 也已随旧设施退役，现行入口是 `hasher::hash<T>` 与 `HashObj<T>::GetHash()`。

## Non-goals

- **不改符号形态**：不把生成的特化从内联改成外联 `.cpp`，不改 `custom_hash_trait` / `has_custom_hash_func` 的语义（那条路要动跨 DLL 的符号可见性与热路径，另行评估）。
- **不做聚合头**：不把 26 个消费文件的 include 收成每模块一行（那是"让忘 include 不可能发生"，与本 change 的"忘了就报错"是两条路）。
- **不动序列化**：只管哈希那一侧的来源问题，`CodeGenSerialization.h` 一行不改。
- **不新增类型进接入清单**：本 change 只让"没有来源"变成编译错误、让已有的来源显式化；某个类型该不该进清单仍按 `codegen-type-migration` 的既有要求逐类型办。
- **不改哈希值**：凡是能编过的代码，改造前后哈希值必须逐字相同；本 change 只把"沉默"换成"响亮"。

## Impact

- **运行时**（3 个头）：`CACore/header/CodeGenHash.h`（删两条隐式兜底、补一句错误信息）、`CACore/header/CodeGenHashFwd.h`（新增 `CA_HASH_BY_BYTES` 声明宏）、`CACore/header/HashKeys.h`（去掉 `std::hash` 优先那一支）。
- **声明落点**：每个靠隐式兜底活着的类型各加一行 —— 自家类型写类型自己的头，第三方类型写模块的 Includes 头；条数由探针给出（本 proposal 不预告数字）。
- **工具与文档**：`Tools/CodeGen/` 新增盘点脚本 + `Tools/CodeGen/README.md` 新增一节（怎么用、退出码、示例与真实输出）。
- **测试**：`Test/CoreTests/` 加"两个入口一致"与"填充字节不进哈希"的逐类型判据；改造前后既有断言里的哈希字面量必须一个都不变。
- **既有读数**（本次实测，作为影响面的底数）：引擎侧源码里 `struct hash<` / `std::hash<` 命中 5 行（其中三行是 `PathHash` / `NameHash` / `HashObj` 的 `std::hash` 特化 —— 这三个类型**同时**有手写 `custom_hash_trait`，是"双来源"的活样本）；手写 `custom_hash_trait<` 特化 13 处；`has_custom_hash_func` 断言 25 处。
- **台账对照**：`docs/failed-approaches.md` 现有 25 条里没有"要求显式声明哈希来源"这一条；相邻的是第 3 条（"序列化/哈希也用字面量偏移取值 —— 拿字节当值"），本 change 与它的关系是**收紧**（把字节口径从默认改成要声明，并加 `has_unique_object_representations` 断言），不是重提。
