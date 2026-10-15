## Context

**现状（逐字引文，都在 `CACore/header/CodeGenHash.h` 里）**：`hasher::hash<Obj>` 的分派链是

- `if constexpr (has_custom_hash_func<objType, hasher>)` → `custom_hash_trait<objType>::hash(object, *this)`；
- `else if constexpr (std::is_trivially_copyable_v<objType>)` → `hash_raw(&object, sizeof(objType))`；
- ……最后一条才是 `static_assert(sizeof(objType) == 0, "这个类型没有生成代码、没有自定义哈希特化、也不是容器 / 包装 / 标量。")`。

第二条是**默认放行**：一个编译单元只要没 include 某个类型的生成物，`has_custom_hash_func<T, hasher>` 就是假（主模板在 `CACore/header/CodeGenHashFwd.h` 里是 `struct custom_hash_trait {};`），而只要这个类型平凡可拷贝，它就**悄悄**按整块字节算。同一个 `hasher::hash<T>` 于是在两个编译单元里是两份不同的体：链接期合并弱符号，整个二进制用哪一份由链接器决定；按标准这类情况"不要求诊断"，所以**没有链接错误、没有编译警告**。归档 change `2026-10-07-clear-codegen-parse-blockers` 记过一起实例（一条与改动无关的老判据由 1 掉成 0，design 里记作"漏 include 一份生成物 → 该类型哈希口径被换掉"）。

**第二个隐式来源**：`CACore/header/CodeGenHash.h` 里还有一档 `else if constexpr (has_std_hash<objType>)`（用 `castl::hash<T>` 的结果按字节算）。`std::hash` 在某个单元可不可用，同样取决于"那个单元有没有 include 定义它的头"。

**两个入口顺序不同**：`CACore/header/HashKeys.h` 的 `cacore::hash<T>` 里写着 `if constexpr (has_std_hash<T> && std::is_same_v<result_type, size_t>) { return std::hash<T>{}(obj); }` —— 结果类型是 `size_t` 时**它优先 `std::hash`**，而 `hasher::hash<T>` 永远先看生成特化。同一段代码的注释声称"两者对同一个宿主算出的值相同 —— 两条路的第一个分支都是"生成物给的特化""，这个"相同"是靠约定与几条测试维持的，不是靠顺序；而且对"同时有生成特化与 `std::hash`"的类型并不自动成立。

**已经存在的"双来源"活样本**：`CACore/header/HashKeys.h` 里 `PathHash` / `NameHash` / `HashObj<...>` 三个类型**同时**有手写 `custom_hash_trait` 特化与 `namespace std { struct hash<...> }` 特化。

**本 change 的底线**：把"沉默"换成"响亮"，**不改任何能编过的代码的哈希值**。

**台账对照**：`docs/failed-approaches.md` 现有 25 条里没有这一条；最近的是第 3 条「序列化/哈希也用字面量偏移取值 —— 拿字节当值」。**这次哪里不同**：第 3 条否的是"用字节当值"这条**实现手段**，本 change 恰恰是把它从"默认"收紧成"要显式声明、且要满足 `has_unique_object_representations`"，方向与台账一致而不是重提。

## Goals / Non-Goals

**Goals:**

- "一个类型的哈希怎么算"只由**显式来源**决定，与"某个编译单元恰好看得见什么"无关；来源缺失时是**编译期错误**，错误信息点名类型与三条出路。
- 按字节算哈希从"默认"变成"要写一行声明"，并且声明处挡住"有填充字节 / 含浮点成员"这两类（它们只能走字段级）。
- `std::hash` 从"隐式来源"变成"显式委托"：外部库类型在模块的 Includes 头里写一行，处处可见。
- 两个入口同链、同值。
- 给出一次性的**来源盘点**能力（脚本 + 对外文档），能在 CI 里当门。

**Non-Goals:**

- 不改符号形态（不把特化挪进外联 `.cpp`）：那要动跨 DLL 符号可见性与哈希热路径，另案评估。
- 不做每模块聚合头（那解决的是"忘 include 不可能发生"，与本 change 的"忘了就报错"是两条路）。
- 不动序列化侧（`CodeGenSerialization.h` 一行不改）。
- 不替任何类型决定"该不该进接入清单"。
- 不删 `PathHash` / `NameHash` / `HashObj` 的 `std::hash` 特化（它们是标准库设施，别的调用方要用），只让它们不再是**分派来源**。

## Decisions

### D1 删掉"平凡可拷贝就按字节"这一档，而不是给它加白名单

**备选（否决）**：保留那一档，维护一张"允许按字节"的白名单。否决理由：白名单是把"默认放行"挪个地方写，漏一个类型仍然沉默；而删掉之后唯一的出路是显式声明，漏了就**编译不过**。

**代价**：能编过的代码集合变小（这正是目的）。哪些类型受影响，由 D6 的探针一次量出来，**不预先编数字**。

### D2 按字节的声明复用手写特化机制，不新造概念

声明写成一行宏（名暂定 `CA_HASH_BY_BYTES(<类型>)`），展开成一个 `cacore::codegen::custom_hash_trait<该类型>` 特化，函数体调 `hasher.hash_raw(...)`。

**为什么这样**：① `has_custom_hash_func` 的语义不变（"这个类型有没有可用特化"照旧），`CACore/header/HashKeys.h` 里按这个概念算分支的地方（`constexpr bool custom = codegen::has_custom_hash_func<Host, Hasher>;`）与 `cacore::dispatch_branch_unchanged<T>()` 都不受影响；② 仓库里已有现成写法（`PathHash` / `NameHash` 的手写特化就是 `hash_raw`），使用者不用学第二套。

**备选（否决）**：新造 `bytes_hashable<T>` 概念 + 在分派链里加一档。否决理由：两套"声明哈希来源"的机制并存，读者要同时记住"哪些档看哪套"，而这个 change 的全部价值就是"来源只有一个清单"。

### D3 声明处带 `std::has_unique_object_representations_v<T>` 断言

它挡住两类：**有填充字节的类型**（填充参与哈希 —— 与"内容相同、填充不同"的相等语义直接冲突）与**含 `float` / `double` 的类型**（`-0.0` 与 `+0.0` 字节不同、NaN 有多种表示；字段级那条路会归一，字节口径不会）。

⚠ **它会暴露几个"现在能编、其实算错"的类型**：这类发现按"发现"处置 —— 要么给它写按字段的手写特化，要么把它迁进接入清单，**不许**为了让它编过去而放松这条断言。数量由探针给出。

### D4 `std::hash` 只当标准库设施，来源必须显式委托

外部库类型不能改它源码，所以委托声明写在**模块的 Includes 头**里：`D3D12RenderBackend/private/D3D12Includes.h`（已有 `custom_hash_trait<ComPtr<ID3DBlob>>`）与 `VulkanRenderBackendNew/private/Utils/VulkanIncludes.h`（已有 `custom_hash_trait<TypedVKHashVal<T>>`）就是现成范例。然后删掉 `CodeGenHash.h` 里那条隐式的 `std::hash` 档。

**理由**：`std::hash` **只给哈希、不给相等**，容器还要相等比较；"哈希来源"与"相等来源"分离就会出现"相等说一样、哈希说不一样"。字段级特化是同一个生成物同时给出哈希与相等（`CodeGenEqual<T>`），这是本项目的既定口径（基线 `codegen-runtime` 的「相等判断与哈希必须同源」）。

### D5 两个入口同链，且以"值逐字不变"为底线

去掉 `cacore::hash<T>` 里 `has_std_hash<T> && size_t` 那一支，让它与 `hasher::hash<T>` 走同一条链。

⚠ **这条有改值风险**：今天靠 `std::hash` 走 `cacore::hash` 的类型，去掉之后会改走 codegen 链，值可能变。所以落地顺序是：**先盘点**"哪些类型今天在 `cacore::hash` 下走 `std::hash`、而 codegen 链会给不同的值"（探针的一部分），给它们写**显式委托特化**（把 `std::hash` 的结果接进统一入口），再删那一支。底线判据：改造前后既有断言里的哈希字面量**逐字相同**。

### D6 先量后改：一次探针给出三份清单

动代码之前先把 D1/D4 的两条隐式兜底**临时收掉**、构建一次，当场得到：

| 清单 | 处置 |
|---|---|
| ① 需要补"按字节"声明的 POD | 补一行（先逐条确认无填充、无浮点） |
| ② 需要补"委托"声明的第三方类型 | 在模块 Includes 头里补一行 |
| ③ **已在接入清单里、却在某个编译单元里退化的类型** | **真缺陷**：补的是 include，不是声明 |

三份清单、逐条命令与真实输出都贴进 change 的证据文件；清单是"这次要加几行声明"的唯一来源。

**探针已跑（2026-10-11，读数在 `evidence/hash-source-inventory.md`）**，结果与写这份设计时的预期不同，写在这里备查：

| 类别 | 实测 | 处置 |
|---|---|---|
| ① 待"按字节"声明 | `graphics_backend::GPUTextureView`（3 个站点） | 按 3.1 补声明 |
| ② 特化存在却看不见 | `PipelineLayoutCache` / `VertexInputStatesCache` / `DescriptorSetLayoutCache`（各 1 个站点） | 它们的特化写在 `VulkanRenderBackendNew/private/PipelineStates/` 下、使用点看不见 → 按 3.2 补可见性 |
| ③ 已在接入清单里却退化 | `DepthStencilStates`（**32 个站点 / 37 个编译单元**） | 根因是 `CPipelineStateObject.h` 只 include 了 `RectSate.codegen.h`、其余"交给消费方"；补上五个成员类型的产物后 **37 → 6** → 按 3.3 办 |

两条给后续任务的教训（都留在证据文件里）：**认类型必须看实例化链的最内层**（第一帧会误判成 `CPipelineStateObject`）；
**"补 include 的位置"这条本次没有得到结论**（两种放法在原样状态下读数相同），不许当结论写进实现。

### D7 盘点脚本（辅助判据，不是主保证）

`Tools/CodeGen/` 下加一支脚本，扫出引擎里当键用的类型并逐个输出算法来源（生成物 / 手写声明 / 内置档 / **无**），无来源时非零退出；形状照 `Tools/CodeGen/check_legacy_symbols.py`，并配一节对外文档（用法、退出码、示例与真实输出）。

**边界（必须写在文档里）**：它是**按写法扫**的近似 —— 扫 `HashObj<X>` / `cacore::hash<X>` / `unordered_map<X,` / `unordered_set<X,` / `HashDictionary<X,` 这些形态，模板参数名、宏包一层、别名都会漏。真正的保证来自编译器（D1）；脚本只用来"一次看清现状"。

### D8 被本改动弄假的基线条文同改，范围写死

- `openspec/specs/codegen-runtime/spec.md` 的「新哈希必须在既有调用点生效」整条改掉：它逐字写着"做这件事的前提是：所有既有特化的宿主类型都不是平凡可拷贝"（本 change 推翻的正是这条前提），且点名的入口 `aggregateHasher::hash` 已随旧设施退役。
- **不改** `openspec/specs/codegen-type-migration/spec.md` 里那条虽也提到 `cacore::aggregateHasher<>`、但与本改动无关的条文 —— 只在本 change 的报告里点出来（范围写死，避免顺手扫荡）。

## Risks / Trade-offs

- [需要补声明的类型可能不少] → 先量后改（D6）；若清单长到不可接受，降级方案是**分两批**落地（第一批只收"清单里那些类型 + 第三方类型"，第二批收剩下的 POD），降级只影响批次，**不影响"漏了就编不过"这条性质**，且必须在变更说明里写清第二批还剩什么。
- [`has_unique_object_representations` 挡下的类型里可能有"现在其实算错"的] → 按发现处置（写按字段的手写特化 / 迁进清单），MUST NOT 放松断言；数量大时单列任务在本 change 内解决，不许静默放过。
- [两个入口同链改值] → D5 的先盘点 + 委托特化，判据是"哈希字面量逐字相同"。
- [编译期报错影响 26 个消费文件与若干 Includes 头] → 探针先给出精确清单；每一步都跑 `python build.py --config Debug`。
- [盘点脚本误报 / 漏报] → 定位成辅助判据（D7），主保证是编译器；文档里写明扫不到的形态。

## Migration Plan

1. **量**：跑探针（临时收掉两条隐式兜底 + 临时去掉 `cacore::hash` 的 `std::hash` 优先支），拿三份清单 + 三组红态读数，写进 `evidence/`。恢复代码。
2. **改运行时**：删 `CodeGenHash.h` 的两条隐式兜底；在 `CodeGenHashFwd.h` 加 `CA_HASH_BY_BYTES`；补错误信息里的第三条出路。
3. **补声明**：按清单① 补按字节声明（逐条先给"无填充、无浮点"的读数）；按清单② 在模块 Includes 头里补委托；按清单③ 给消费单元补 include。
4. **同链**：按 D5 的先盘点结果补委托，再去掉 `cacore::hash` 那一支。
5. **判据**：全量构建 + `CoreTests` + 两个后端 tester + 既有哈希字面量逐字不变 + "藏掉生成物必编不过"的红绿对拍。
6. **工具与文档**：盘点脚本 + `Tools/CodeGen/README.md` 新一节（含当场跑过的真实输出）。
7. **回滚**：本 change 全是源码改动、没有数据迁移；回滚 = 把第 2–4 步的差异退回去（改动前后都有构建与哈希读数，可逐条对照）。前提是执行前工作区已提交/已备份。

## Open Questions

- `has_unique_object_representations` 这条断言是否作为**硬要求**（探针读数出来后再定：若它挡下的类型多到要单独立 change，是改成"报告 + 显式豁免"，还是留硬要求分批做）。
- 盘点脚本扫不到的形态（宏包一层、别名、`castl::shared_dic` 之类）要不要在本次一并覆盖 —— 按实测点名列，不预先承诺。
- `castl::hash<T>` 那条（`has_std_hash` 依赖的概念）要不要一起收窄 —— 待探针给出"有多少类型只靠它活着"的读数。
