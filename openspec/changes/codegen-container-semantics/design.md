## Context

新 CodeGen 的字段表目前对容器与包装器**没有表达能力**，全部记成 `TypeKind::Opaque`。写这份设计时实测到的事实：

- **分类没有规范化**：分类器按 clang 的类型种类 switch（`CXType_UInt` → `UInt` 等），最后一个 `default: return "TypeKind::Opaque"`。`unsigned int` 命中 `CXType_UInt`，而 `uint32_t` 取到的是 `CXType_Elaborated`（实测 119；不是初稿写的 `CXType_Typedef`）→ 落进 default。实测同一个结构体里：`unsigned int` → `UInt`、`uint32_t` → `Opaque`、`unsigned long long` → `UInt`、`uint64_t` → `Opaque`。
- **容器也是 Opaque**：实测 `castl::vector<unsigned>`、`castl::string`、`castl::unordered_map<unsigned, unsigned>`、`int*` 四行全是 `TypeKind::Opaque`。⚠ 初稿写"`TypeKind` 里本来就有 `Vector` / `Mat` / `String` / `Array` / `Map` / `Set`"，**这是错的**：实测枚举只有 `None` / `Bool` / `Int` / `UInt` / `Float` / `Double` / `Enum` / `Struct` / `String` / `Vector` / `Mat` / `Array` / `Opaque`，**没有 `Map` / `Set`**；而且 `Vector` 的注释是 glm 向量、`Array` 是固定 C 数组，与"标准容器"语义不同，不该借用。
- **容器在运行时一侧是有协议的**（旧设施 `Reflection.h` / `Serialization.h` / `Hasher.h`，2024 年入库）：`containerInfo<T>` 为"有 `size()` 且有 `value_type`"的类型（`is_size_container`）与 C 数组提供特化；写的时候先写元素个数、再按 `for(auto& item : object)` 逐个写（没有 foreach 就用下标）；读的时候按 `resize` + 下标 → `push_back` → `insert` 依次尝试。
- **无序容器的字节与哈希不确定**：哈希那条路是 `hash_one(元素个数)` 之后，若元素平凡可拷贝且有 `data()` 就整块算，否则**按遍历顺序逐个算** —— `unordered_map` / `unordered_set` 的遍历顺序与插入历史有关，于是"内容相等"的两个对象哈希不同，而生成的相等判断是内容比较。
- **容器里的指针静默丢数据**：指针在写侧是 `//ignore`、读侧置 `nullptr`，`vector<T*>` 会写出"个数 + 一串空元素"，不报错。
- **引擎里的容器用量**（grep 计数）：`vector` 609、`string` 294、`unordered_map` 187、`deque` 41、`array` 38、`set` 10、`map` 10、`list` 6、`unordered_set` 5；`castl` 里**没有** `multimap` / `multiset`。
- **`castl` 是开放集合**：`CAContainerBase.h` 里逐字是 `using namespace eastl;` 与 `using namespace std;`，所以 `castl::X` 对 std（或 eastl）的**每一个**名字都成立；`castl::unordered_map` / `unordered_multimap` 另有显式别名声明。结论：按名字硬列容器一定随 `USING_EASTL` 漂移，**并且"castl 不提供某某容器"这种断言不能靠翻 castl 头文件得出**（初稿的"castl 没有 multimap / multiset"就是这么错的，实测 `castl::multimap` / `castl::multiset` 都能编译、工具 exit 0）。

## Goals / Non-Goals

**Goals:**

- 字段表能如实说出"这个成员是容器、元素是什么、元素里有没有指针"。
- 无序容器（`unordered_map` / `unordered_set`）写出**稳定字节**、算出**稳定哈希**：同样内容 → 同样字节、同样哈希。
- 含指针 / 句柄元素的容器在构建期被**拒绝**，而不是静默丢数据。
- 磁盘格式（容器成员的形状）成为写进对外文档的契约。

**Non-Goals:**

- 不改旧运行时（`Reflection.h` / `Serialization.h` / `Hasher.h`）；生成代码仍调用它读写单个元素。
- 不做深拷贝指针：指针仍是"不序列化、读回空、哈希按地址"。
- 不支持 `multimap` / `multiset`、不支持指针作键或元素。
- 不追求"给所有容器加规则"：有序容器（`vector` / `array` / `string` / `deque` / `list` / `map` / `set`）遍历顺序本来就是确定的，不加特殊处理。

## Decisions

### D1 分类前先取规范化类型

在 switch 之前对 `CXType` 取规范类型，别名（`uint32_t` / `uint64_t` / `size_t` / `castl::string` / `castl::vector` …）因此能落到真实种类上。判据：同一个结构里 `unsigned int` 与 `uint32_t` 必须得到同一个 `TypeKind`。

### D2 容器按结构与运行时同口径识别

规范化后如果是记录类型，查它有没有 `size()` 与 `value_type`：两个都有就是容器（与运行时 `is_size_container` 同口径）；C 数组按 extent 认。**不按名字列白名单**，因为 `castl` 经 `using namespace std` / `eastl` 透出全部名字（见 Context）。⚠ 元素描述取**直接元素类型**：运行时的 `containerInfo` 对多维 C 数组用 `remove_all_extents` 取到最内层类型、长度只取第一维（实测 `int[2][3]` 报元素 `int`、长度 2），而写出的字节是分层的（先 2、再每个 `int[3]` 各写 3 与 3 个 int）。照抄运行时会让元素描述与真实字节不符，所以要按维度逐层描述，并在 README 的磁盘契约里写明多维数组的嵌套形状。判据：同一个 TU 里断言"工具认出的容器集合"与"运行时 `containerStates<T>::is_container_with_size`"一致。

### D3 字段表补一层元素描述（不动 `TypeKind`）

`CodeGenFieldInfo` 增两个指针成员：`key`（仅 `map` 用）与 `element`（`vector` / `array` / `string` / `set` / `map` 的值），各自指向一份同构描述（递归，嵌套容器因此天然表达得出来）；`FieldFlags` 增四位：`Container`、`Unordered`、`HasOpaqueElement`（元素里含裸指针 / 托管指针 / 句柄 / 包装器时置位）、`Wrapper`。**不新增也不改写 `TypeKind`**：实测枚举里没有 `Map` / `Set`，而既有的 `Vector`（glm 向量）与 `Array`（固定 C 数组）语义不同；容器一律由 `Container` 标志 + 元素描述表达，`Array` 仍只表示 C 数组（C 数组同时置 `Container`）。

### D4 无序容器写之前摊平成数组并排序（**逐层**）

生成代码里对无序容器成员：先摊平成元素数组（`map` → `pair<K,V>` 数组，把 `pair<const K,V>` 的 const 去掉；`set` → 元素数组），按**参与排序的那一侧**排序（`map` 按键、`set` 按元素本身；`map` 的值不参与排序），然后按数组那条路写（先写个数、再逐个写）。读回来先读成数组、再逐个 `insert`。

⚠ **"逐层"是初稿漏掉的**：初稿只处理"直接是容器类型的成员"，而元素本身是无序容器时（`vector<Inner>`，`Inner` 里有 `unordered_map`）生成代码会把整个元素交给旧运行时按遍历顺序写。实测：两个只有内层插入顺序不同的对象写出 112 字节、首个不同字节在第 20 字节、哈希 `c188763afb62ef98` 与 `1020e97f4c8baf58` 不同，而生成的相等判断说"内容相等"。**定下来的做法**：元素类型的读写**递归摊平** —— 元素类型自己也有生成序列化器时就调用它，而不是 `s.serialize(item)`。加标志位拒绝只是兜底，不作为本 change 的实现。

**重复键容器不支持**：`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset` 里同键项之间没有规范顺序，本 change 不规定二级排序键，直接拒绝（见 D6）。

### D5 哈希走同一套规范顺序，并写清这条不变量的范围

无序容器的哈希也按排序后的顺序累加，保证"**顺序**不影响哈希"。但排序不解决元素内部的字节问题：实测 `unordered_map<uint32_t, float>` 里 `+0.0f` 与 `-0.0f` 的两个对象，生成的相等判断为真而哈希不同（`1a2742e842fb0203` 与 `1a27c2e842fbdb83`）—— 因为浮点规范化只写在生成代码的**顶层字段**那一层，容器元素仍交给旧运行时整块按字节算。**定下来的做法**：把浮点规范化**递归套到元素**上，元素按值参与哈希，不再走整块字节那条快路径；代价是容器元素的哈希变慢，换来"内容相等 ⇒ 哈希相同"这条不变量真正成立。

### D6 退出码：四条拒绝路径

① **参与排序的一侧**没有全序（`map` 的键、`set` 的元素；`map` 的值不参与排序，**不检查** —— 初稿把"键或元素"一起要求全序，会把 `ShaderLibrary` 那类"键有序、值没有比较运算符"的类型整份误拒）；② 容器元素含**裸指针 / 托管指针（`castl::shared_ptr` / `castl::unique_ptr`）/ 句柄**且请求序列化 —— 旧运行时对裸指针与托管指针都是"不写、读回空"，实测引擎里就有 `vector<shared_ptr<D3D2ShaderStruct>>`、`unordered_map<NameHash, vector<shared_ptr<...>>>`、`unordered_set<shared_ptr<CWindowContext>>` 这样的真实成员；③ **重复键容器**（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset`）—— 判据按**规范化后的类型名**（实测 `set` 与 `multiset` 的运行时特征完全相同，结构上分不开，只能看名字；名字取规范化后的 `std::` / `eastl::` 形式，因此不受 `USING_EASTL` 影响）；④ 容器的键或元素**不可默认构造**（旧运行时的 pair 支持靠 `is_tuple_like`，它要求 `std::tuple_element_t<0, T>{}` 合法，实测不可默认构造的元素会撞 `static assertion failed: 'object incompatible for reflection'`）。

四条各有独立退出码，`Tools/CodeGen/README.md` 的退出码表同步。

### D7 包装器只记录、不新造语义

元数据里标出"这是包装器"及其被包装类型（`FieldFlags` 增一位 `Wrapper`）；读写沿用旧运行时的 `managed_wrapper_traits`（解出被包装数据来写），智能指针沿用"不写、读回空、哈希按地址"。本 change 不新增包装器语义。

### D8 与旧运行时的边界（写清楚）

生成代码仍把**单个元素**的读写交给旧运行时；**容器这一层（识别、元素描述、规范顺序、拒绝）属于新系统**。若日后新系统自建运行时，D3 的表就是生成读写代码的输入（这条岔路仍未定）。

## Risks / Trade-offs

- [字段表结构变更会让现有生成物字节改变] → 既有 `Tools/CodeGen/generated/` 三个黄金样本要重新生成并入库，这是**预期内的变更**；改分类那一步（D1）单独先做并确认 `EmitSample`（`int` / `float` / `bool` / `double`）的产物逐字节不变，把"表结构变更"与"分类修正"分开验。
- [摊平排序引入额外拷贝] → 先做副本版，测试里量一次大容器（例如 10 万个键）的开销；超标再改成只排下标。
- [工具的容器口径与运行时漂移] → D2 的判据要求同一个 TU 里对拍两侧结论。
- [新拒绝路径可能把本来能用的类型挡在外面] → 拒绝只针对"会静默出错"的组合（指针键、指针元素、无序容器无比较键）；有序容器与含普通元素的容器不受影响。
- [退出码增加，文档与自检脚本要同步] → `verify_readme.py` 会逐条跑文档里的命令，新增退出码必须在同一次提交里写进 README。

## Migration Plan

0. **顺序依赖（两个 change 的先后必须写死）**：本 change 会改字段表结构、并让 `Tools/CodeGen/generated/` 三份黄金样本发生变更；而 `wire-codegen-real-type` 的第 1 步要求「既有黄金样本逐字节不变」。所以**先落 `wire-codegen-real-type`，再落本 change**；反过来做会让那条要求自相矛盾。
1. **只做 D1**（分类规范化），跑一次构建与既有测试，确认 `EmitSample` 三个黄金样本逐字节不变（它们都是 `int` / `float` / `bool` / `double`，规范化后种类相同）。
2. **做 D2 / D3**（容器识别 + 元素描述），此时表结构变了 → 重新生成并入库黄金样本；补分类与元素描述的编译期断言。
3. **做 D4 / D5 / D6**（规范顺序、哈希一致、三条拒绝路径），每条配红态实测。
4. **D7 与文档**：包装器标记、README 的容器规则与磁盘格式契约。
5. 回滚：四步各自独立成提交，任何一步出问题都能单独回退；D1 之前的产物与行为是回退点。

## Open Questions

- 新 CodeGen 最终是长期复用旧运行时，还是自建一套？（本 change 不改这个决定）
- 大容器要不要改成只排下标？以实测开销为准。
- 递归摊平后，容器元素的哈希不再走整块字节，慢多少？在 3.x 的性能记录里量一次。

## 对抗验证记录（2026-09-20）

这一版是 workflow 五路对抗验证后的修订，改掉的都是**提案里的错事实与漏覆盖**，逐条有实测：

| 初稿错在哪 | 实测证据 | 现在怎么写 |
|---|---|---|
| 声称 `TypeKind` 里本来就有 `Map` / `Set` | 枚举里没有这两个值；既有 `Vector` 是 glm 向量、`Array` 是 C 数组 | 不动枚举，容器用 `Container` 标志 + 元素描述（D3） |
| 声称 `castl` 不提供 `multimap` / `multiset` | `castl::multimap` / `castl::multiset` 都能编译、工具 exit 0；根因是 `CAContainerBase.h` 的 `using namespace std;` | 改成按规范化类型名拒绝重复键容器（D6） |
| 根因写成 `CXType_Typedef` | 实测 `uint32_t` 是 `CXType_Elaborated`（119） | Context 已改 |
| 只摊平"直接是容器的成员" | 两个只有内层 `unordered_map` 插入顺序不同的对象：112 字节、首差偏移 20、哈希不同 | D4 写明逐层摊平（或加一位标志并入拒绝） |
| 承诺"内容相等 ⇒ 哈希相同"没写范围 | `unordered_map<uint32_t,float>` 的 `+0.0f` / `-0.0f`：内容相等、哈希不同 | D5 二选一：递归规范化，或限缩并写明 |
| 拒绝规则只提裸指针 / 句柄 | 旧运行时还拒绝**托管指针**；引擎里有 `vector<shared_ptr<...>>` 等真实成员 | D6 ② 扩到托管指针 |
| 要求 `map` 的值也有全序 | 引擎里 `unordered_map<NameHash, ShaderStructData>` 的值没有比较运算符，字面实现会整份误拒 | D6 ① 只要求参与排序的一侧 |
| 元素类型取最内层（照抄运行时） | `int[2][3]` 运行时元素类型是 `int`、长度 2，实际字节分层 | D2 取直接元素类型并写明多维形状 |
| 漏掉"元素必须可默认构造" | 不可默认构造的元素会撞 `static assertion failed: 'object incompatible for reflection'` | D6 ④ 新增一条拒绝路径 |
