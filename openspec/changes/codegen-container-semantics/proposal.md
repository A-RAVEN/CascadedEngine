## Why

新的 CodeGen **对容器和包装器没有任何规范**：字段表里凡是工具认不出的类型一律记成 `TypeKind::Opaque`，于是容器、指针、智能指针、连 `uint32_t` / `uint64_t` 这种到处在用的别名都挤在同一个桶里。实测（同一个结构体里挨着写）：

| 成员写法 | 字段表记成 |
|---|---|
| `unsigned int` | `TypeKind::UInt` |
| `uint32_t` | `TypeKind::Opaque` |
| `unsigned long long` | `TypeKind::UInt` |
| `uint64_t` | `TypeKind::Opaque` |
| `castl::vector<unsigned>` | `TypeKind::Opaque` |
| `castl::unordered_map<K,V>` | `TypeKind::Opaque` |
| `int*` | `TypeKind::Opaque` |

根因是分类前**没取规范化类型**：`unsigned int` 落到 `CXType_UInt`，而 `uint32_t` 取到的是 `CXType_Elaborated`（实测 119），直接落进 `default: Opaque`。

这件事现在就要修，因为接真实类型时"能不能序列化"要靠这张表判断：

- **容器里的指针会静默丢数据**：指针在写侧被忽略、读侧置空，`vector<T*>` 会写出"元素个数 + 一串空元素"，不报错。
- **无序容器的字节不确定**：`unordered_map` / `unordered_set` 的遍历顺序与插入历史有关，同样内容会写出不同字节，和"同样输入 → 同样字节"冲突；哈希也按遍历顺序累加，而相等判断是内容比较，于是"内容相等却哈希不同"。

## What Changes

- **分类改为按规范化类型判定**（取 clang 的 canonical type 再 switch），并在规范化后识别容器。
- **容器按结构与运行时同口径识别**：运行时认"有 `size()` 且有 `value_type`"的类型就是容器，工具也用这个判据（查这两个成员），不硬列 `std::vector` / `eastl::vector` 名字 —— `castl::vector` 本身就是它们的别名，`USING_EASTL` 一改名字就飘。
- **字段表补"元素"这一层**：加一位 `Container` 标志，加两份元素描述（`key` / `element`：元素种类、元素类型名、是否嵌套容器、元素里是否含指针 / 托管指针 / 句柄 / 包装器），`unordered_*` 另标"顺序不确定"。**`TypeKind` 枚举保持不动** —— 实测它只有 `None` / `Bool` / `Int` / `UInt` / `Float` / `Double` / `Enum` / `Struct` / `String` / `Vector` / `Mat` / `Array` / `Opaque`，**没有 `Map` / `Set`**（初稿写成"枚举值本来就有"，是错的）；既有的 `Vector`（注释写 glm 向量）与 `Array`（固定 C 数组）语义不同，不该借用，所以容器种类用标志位加元素描述表达。
- **无序容器按规范顺序写**：写与哈希之前摊平成"元素数组"（`map` 按 key 排、`set` 按元素本身排），再按数组序列化；读回来逐个 `insert`。摊平与排序发生在**生成代码**里，旧运行时不动。**嵌套要逐层摊平** —— 元素本身是无序容器时同样处理；只做成员这一层不够，实测两个只有内层 `unordered_map` 插入顺序不同的对象写出 112 字节、首个不同字节在第 20 字节、哈希也不同。
- **哈希的承诺写清范围**：排序只解决顺序。元素的浮点仍走旧运行时的整块字节哈希，实测 `unordered_map<uint32_t, float>` 里 `+0.0f` 与 `-0.0f` 内容相等（`==` 为真）却哈希不同。**做法已定：把浮点规范化递归到元素**（元素按值参与哈希），让"内容相等 ⇒ 哈希相同"这条不变量真正成立，而不是把承诺限缩掉。
- **定死拒绝规则**：**参与排序的那一侧**没有全序（`map` 的键、`set` 的元素；`map` 的值不参与排序，不检查）、**重复键容器**（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset` —— 实测 `castl` 经 `using namespace std` / `eastl` 把这些名字透出来，它们可达；且 `set` 与 `multiset` 的运行时特征完全相同，只能按规范化后的类型名判定）→ 拒绝出码并说明原因；含**裸指针 / 托管指针（`shared_ptr` / `unique_ptr`）/ 句柄**元素的容器出序列化产物同样拒绝，而不是静默写出空元素。初稿写的"`castl` 不提供 multimap / multiset"是错的，已改。
- **包装器只做记录与继承**：元数据里标出包装器及其被包装类型，读写沿用旧运行时的 `managed_wrapper_traits`（解出被包装数据来写）、智能指针沿用"不写、读回空、哈希按地址"，本 change 不新造语义。
- **格式契约写进对外文档**：map / set 成员在磁盘上的形状就是"键值对数组（或元素数组）的规范顺序"，这是可跨语言解析的契约，必须写进 `Tools/CodeGen/README.md`。

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `codegen-framework`：新增三条要求 —— ① 字段种类必须按**规范化类型**判定（别名不得落进 `Opaque`）；② 容器必须被识别并携带**元素描述**，识别口径与运行时一致；③ **无序容器的序列化与哈希必须走规范顺序**（摊平成数组后排序），键或元素无全序时拒绝出码。

## Non-goals

- **不改旧运行时**：`Reflection.h` / `Serialization.h` / `Hasher.h` 是旧的反射与序列化设施（2024 年入库），本 change 一行不改；生成代码仍调用它们读写单个元素。是否让新 CodeGen 自建运行时，是另一个尚未决定的问题。
- 不支持 `multimap` / `multiset`（`castl` 里没有这两个容器）；不支持指针作键或元素（无全序）。
- 不做"深拷贝指针指向对象"（指针仍按浅语义：不序列化、哈希按地址）。
- 不给 `deque` / `list` 之类的有序容器加特殊规则（遍历顺序本来就是确定的）。
- 不改运行时反射 `CA_REFLECTION` 一侧的任何东西。

## Impact

- `Tools/CodeGen/CodeGenMain.cpp`：类型分类改规范化；容器识别与元素描述；三个发射器对无序容器的规范顺序与拒绝路径
- `CACore/header/CodeGenMeta.h`：`CodeGenFieldInfo` 增加元素描述与标志位（新系统一侧）
- `Test/CoreTests/`：分类与元素描述的编译期断言、插入顺序无关性测试
- `Tools/CodeGen/README.md`：容器与包装器规则、新的拒绝路径与退出码、磁盘格式契约
- `build.py`：接入清单与生成步骤预计不需要改（发射器数量不变），仅当新增拒绝路径需要新退出码时同步
