## Context

新 CodeGen 的字段表目前对容器与包装器**没有表达能力**，全部记成 `TypeKind::Opaque`。写这份设计时实测到的事实：

- **分类没有规范化**：分类器按 clang 的类型种类 switch（`CXType_UInt` → `UInt` 等），最后一个 `default: return "TypeKind::Opaque"`。`unsigned int` 命中 `CXType_UInt`，而 `uint32_t` 取到的是 `CXType_Elaborated`（实测 119；不是初稿写的 `CXType_Typedef`）→ 落进 default。实测同一个结构体里：`unsigned int` → `UInt`、`uint32_t` → `Opaque`、`unsigned long long` → `UInt`、`uint64_t` → `Opaque`。
- **容器也是 Opaque**：实测 `castl::vector<unsigned>`、`castl::string`、`castl::unordered_map<unsigned, unsigned>`、`int*` 四行全是 `TypeKind::Opaque`。⚠ 初稿写"`TypeKind` 里本来就有 `Vector` / `Mat` / `String` / `Array` / `Map` / `Set`"，**这是错的**：实测枚举只有 `None` / `Bool` / `Int` / `UInt` / `Float` / `Double` / `Enum` / `Struct` / `String` / `Vector` / `Mat` / `Array` / `Opaque`，**没有 `Map` / `Set`**；而且 `Vector` 的注释是 glm 向量、`Array` 是固定 C 数组，与"标准容器"语义不同，不该借用。
- **容器在运行时一侧是有协议的**（旧设施 `Reflection.h` / `Serialization.h` / `Hasher.h`，2024 年入库）：`containerInfo<T>` 为"有 `size()` 且有 `value_type`"的类型（`is_size_container`）与 C 数组提供特化；写的时候先写元素个数、再按 `for(auto& item : object)` 逐个写（没有 foreach 就用下标）。⚠ **读侧要分清两个类**（第二轮对抗验证实测）：生成代码用的是 `cacore::deserializer`，分支顺序是 `reserve` → `push_back` → `insert` → `resize` + 下标，而且**元素个数不为 0 时不清空目标容器**（旧元素会留下）；`clear()` 只写在 `cacore::batch_deserializer` 里（且只在个数为 0 时）。初稿把两者的分支顺序混成了一句话。
- **无序容器的字节与哈希不确定**：哈希那条路是 `hash_one(元素个数)` 之后，若元素平凡可拷贝且有 `data()` 就整块算，否则**按遍历顺序逐个算** —— `unordered_map` / `unordered_set` 的遍历顺序与插入历史有关，于是"内容相等"的两个对象哈希不同，而生成的相等判断是内容比较。
- **容器里的指针静默丢数据**：指针在写侧是 `//ignore`、读侧置 `nullptr`，`vector<T*>` 会写出"个数 + 一串空元素"，不报错。
- **引擎里的容器用量**（grep 计数）：`vector` 609、`string` 294、`unordered_map` 187、`deque` 41、`array` 38、`set` 10、`map` 10、`list` 6、`unordered_set` 5。**引擎源码里 `multimap` / `multiset` 用量为 0，但它们可达** —— `CAContainerBase.h` 的 `using namespace std;` / `eastl` 把名字全透出来，`castl::unordered_multimap` / `unordered_multiset` 另有显式别名；初稿写成"`castl` 里没有这两个容器"，是错的，第二轮的工件里已一并改掉。
- **`castl` 是开放集合**：`CAContainerBase.h` 里逐字是 `using namespace eastl;` 与 `using namespace std;`，所以 `castl::X` 对 std（或 eastl）的**每一个**名字都成立；`castl::unordered_map` / `unordered_multimap` 另有显式别名声明。结论：按名字硬列容器一定随 `USING_EASTL` 对不上，**并且"castl 不提供某某容器"这种断言不能靠翻 castl 头文件得出**（初稿的"castl 没有 multimap / multiset"就是这么错的，实测 `castl::multimap` / `castl::multiset` 都能编译、工具 exit 0）。

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

规范化后如果是记录类型，查它有没有 `size()` 与 `value_type`：两个都有就是容器（与运行时 `is_size_container` 同口径）；C 数组按 extent 认。**不按名字列白名单**，因为 `castl` 经 `using namespace std` / `eastl` 透出全部名字（见 Context）。

⚠ **实现时改了判据（2026-10-02 实测）**：原本打算用 libclang 游标去探"有没有 `size()` 与 `value_type`"，**实测探不到** —— `std::vector<unsigned>` 的规范化类型确实是 `CXType_Record`（105），但 `clang_getTypeDeclaration` 对类模板特化不暴露成员（访问器加递归后 `hasSize` 仍是 0）。改用**规范化后的基名**列表（`vector` / `array` / `string` / `basic_string` / `deque` / `list` / `map` / `multimap` / `set` / `multiset` / `unordered_*`）：规范化拼写一定是 `std::…` / `eastl::…`，不会是 `castl::…` 别名，所以这条判据不受别名影响；与运行时口径的一致性由 2.5 的编译期对照断言（表的 `Container` 标志 ⇔ `containerStates<T>::is_container_with_size`）保证，而不是靠工具侧的探针。⚠ 元素描述取**直接元素类型**：运行时的 `containerInfo` 对多维 C 数组用 `remove_all_extents` 取到最内层类型、长度只取第一维（实测 `int[2][3]` 报元素 `int`、长度 2），而写出的字节是分层的（先 2、再每个 `int[3]` 各写 3 与 3 个 int）。照抄运行时会让元素描述与真实字节不符，所以要按维度逐层描述，并在 README 的磁盘契约里写明多维数组的嵌套形状。判据：同一个 TU 里断言"工具认出的容器集合"与"运行时 `containerStates<T>::is_container_with_size`"一致。

### D3 字段表补一层元素描述（不动 `TypeKind`）

`CodeGenFieldInfo` 增两个指针成员：`key`（仅 `map` 用）与 `element`（`vector` / `array` / `string` / `set` / `map` 的值），各自指向一份同构描述（递归，嵌套容器因此天然表达得出来）；`FieldFlags` 增四位：`Container`、`Unordered`、`HasOpaqueElement`（**只记裸指针 / 托管指针 / 句柄**）、`Wrapper`（包装器单独一位）。⚠ **包装器不算"不透明"**（第二轮对抗验证）：`NameHash` / `PathHash` / `HashObj` 这类包装器旧运行时用 `managed_wrapper_traits` 正常解包读写，把它们算进 `HasOpaqueElement` 再接到拒绝路径，会把真正要写盘的 `ShaderLibrary`（值类型链上有 `vector<NameHash>`）整份拒掉；所以 `Wrapper` 只作记录，**不触发任何拒绝**。**不新增也不改写 `TypeKind`**：实测枚举里没有 `Map` / `Set`，而既有的 `Vector`（glm 向量）与 `Array`（固定 C 数组）语义不同；容器一律由 `Container` 标志 + 元素描述表达，`Array` 仍只表示 C 数组（C 数组同时置 `Container`）。

⚠ **按位存储的容器要单独拒绝**（第二轮对抗验证）：`castl::vector<bool>` 满足"有 `size()` 且有 `value_type`"这条结构判据（所以"与运行时口径一致"的断言会通过），但旧运行时的写与哈希用的是 `for (auto& item : object)`，按位代理绑定不上引用 —— 消费方一实例化就 `error C2440`。识别判据要补一条"元素可取 `data()` 或可寻址"，不满足者进拒绝路径（本 change 里用 D6 的 ⑤）。

### D4 无序容器写之前摊平成数组并排序（**逐层**）

生成代码里对无序容器成员：先摊平成元素数组（`map` → `pair<K,V>` 数组，把 `pair<const K,V>` 的 const 去掉；`set` → 元素数组），按**参与排序的那一侧**排序（`map` 按键、`set` 按元素本身；`map` 的值不参与排序），然后按数组那条路写（先写个数、再逐个写）。读回来先读成数组、再逐个 `insert`。

⚠ **"逐层"是初稿漏掉的**：初稿只处理"直接是容器类型的成员"，而元素本身是无序容器时（`vector<Inner>`，`Inner` 里有 `unordered_map`）生成代码会把整个元素交给旧运行时按遍历顺序写。实测：两个只有内层插入顺序不同的对象写出 112 字节、首个不同字节在第 20 字节、哈希 `c188763afb62ef98` 与 `1020e97f4c8baf58` 不同，而生成的相等判断说"内容相等"。

**定下来的做法（第二轮对抗验证后改写过一次）**：**按元素描述递归摊平**，不依赖"元素类型有没有生成序列化器" —— 容器类型永远拿不到生成序列化器，初稿那句"元素类型自己也有生成序列化器时就调用它"对 `vector<unordered_map<...>>`、`unordered_map<u, unordered_map<...>>`、`unordered_map m[2]` 这三种形状根本落不下来（实测三者同内容不同插入顺序时仍然 bytesEqual=0，firstDiff 分别是 16 / 20 / 16）。只有确实递归不了的形状才进拒绝路径。

⚠ **做完时按本表最后一行括号里的第二选项办（2026-10-02 实测）**：递归摊平覆盖了"元素本身是容器"和"C 数组的元素是容器"这两类（`cgWriteContainer` / `cgReadContainer` 递归 + C 数组按"先 extent、再逐个元素"），但**结构体元素**这一类做不到 —— 递归摊平要求生成代码能进到那个结构体的字段里，而它拿不到旧运行时的游标（`serializer` 的缓冲区、`deserializer` 的偏移都在 `private:` 段且无取用方法）。所以这一类按本表写的"**或加一位标志并入拒绝**"办：`--emit-serialization` 与 `--emit-hash` 遇到"容器的元素是内含无序容器的结构体"时**非零退出（11）**并点名成员、元素类型与那个无序容器；工具在扫描到的头文件里递归查元素的字段（深度上限 3）来判断，元素类型不在本次扫描头文件里时看不到字段、按"未发现"放行 —— 这是本 change 的已知边界。

**重复键容器不支持**：`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset` 里同键项之间没有规范顺序，本 change 不规定二级排序键，直接拒绝（见 D6）。

### D5 哈希走同一套规范顺序，并写清这条不变量的范围

无序容器的哈希也按排序后的顺序累加，保证"**顺序**不影响哈希"。但排序不解决元素内部的字节问题：实测 `unordered_map<uint32_t, float>` 里 `+0.0f` 与 `-0.0f` 的两个对象，生成的相等判断为真而哈希不同（`1a2742e842fb0203` 与 `1a27c2e842fbdb83`）—— 因为浮点规范化只写在生成代码的**顶层字段**那一层，容器元素仍交给旧运行时整块按字节算。

⚠ **"把规范化递归到元素"这句话本身不够**（第二轮对抗验证实测）：旧运行时的哈希对**平凡可拷贝**的类型先走 `is_trivially_copyable_v` → 整块 `hash_one`，根本不会调到元素自己的哈希特化 —— 探针里 `h.hash(element)` 与显式 `custom_hash_trait<Element>::hash()` 给出相反结论；只要生成代码写成 `h.hash(item)`，声明的做法就静默失效。所以定下来的是：**生成代码对元素显式调用元素类型的哈希特化**（拿不到特化的元素类型进拒绝清单），而不是把元素丢给 `h.hash(item)`。另外**有序容器里的浮点**（如引擎里真实存在的 `castl::vector<castl::vector<float>>`）也违反这条不变量（实测同内容不同哈希）——**定下来的做法是一并纳入**：所有容器一视同仁，按元素显式调用元素类型的哈希特化，不留例外（spec 里有对应场景）。

**读进非空目标的语义也定死了**：生成代码 MUST 先 `clear()` 再读（前提是容器有 `clear()`，没有的进拒绝路径），使读回来的内容只由载荷决定。理由是实测生成代码走的 `cacore::deserializer` 不清空目标容器，而"还原"只有在清空之后才成立。

`build.py` 的口径同样定死：每条清单条目固定跑三个发射器，所以**进清单的类型必须三个发射器都能出**，只能出反射的类型不进清单。

### D6 退出码：五条拒绝路径

① **参与排序的一侧**没有全序（`map` 的键、`set` 的元素；`map` 的值不参与排序，**不检查**）。⚠ 判据必须接受 `operator<=>`：引擎里全部真实键类型（`NameHash` / `PathHash` / `CAHash::result_type`）**只有 `<=>`、没有 `operator<`**（实测这些头文件里 `operator<(` 零命中），按 `<` 判定会把它们全拒；含 NaN 的浮点键**判不出全序**（两个 NaN 互为"等价"但 `==` 为假，实测排序后写出字节仍不同）——**定下来的做法是拒绝**（spec 里有对应场景），不做"按位型规范化再定序"那套。② 容器元素含**裸指针 / 托管指针（`castl::shared_ptr` / `castl::unique_ptr`）/ 句柄**且请求序列化 —— 旧运行时对裸指针与托管指针都是"不写、读回空"，实测引擎里就有 `vector<shared_ptr<D3D2ShaderStruct>>`、`unordered_map<NameHash, vector<shared_ptr<...>>>`、`unordered_set<shared_ptr<CWindowContext>>` 这样的真实成员；③ **重复键容器**（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset`）—— 判据按**规范化后的类型名**（实测 `set` 与 `multiset` 的运行时特征完全相同，结构上分不开，只能看名字；名字取规范化后的 `std::` / `eastl::` 形式，因此不受 `USING_EASTL` 影响）；④ 容器的键或元素**不可默认构造**（旧运行时的 pair 支持靠 `is_tuple_like`，它要求 `std::tuple_element_t<0, T>{}` 合法，实测不可默认构造的元素会撞 `static assertion failed: 'object incompatible for reflection'`）；⑤ **按位存储的容器**（`castl::vector<bool>` 一类，见 D3）。

⚠ **包装器不在任何一条拒绝清单里**（见 D3）：`vector<NameHash>`、`unordered_map<NameHash, X>` 必须放行。

五条各有独立退出码，`Tools/CodeGen/README.md` 的退出码表同步。

⚠ **清单口径**（第二轮对抗验证）：`build.py` 每条清单条目**固定跑三个发射器**，任一次非零退出即整体构建失败，没有"只要反射"这种表达方式。因此规则是：**进清单的类型必须三个发射器都能出**；只能出反射的类型不进清单（工具本身仍允许对它单独请求反射，那是命令行行为）。spec 的"仅请求反射时哪些豁免"必须与这条口径一致。

### D7 包装器只记录、不新造语义

元数据里标出"这是包装器"及其被包装类型（`FieldFlags` 增一位 `Wrapper`）；读写沿用旧运行时的 `managed_wrapper_traits`（解出被包装数据来写），智能指针沿用"不写、读回空、哈希按地址"。本 change 不新增包装器语义。

### D8 与旧运行时的边界（写清楚）

生成代码仍把**单个元素**的读写交给旧运行时；**容器这一层（识别、元素描述、规范顺序、拒绝）属于新系统**。若日后新系统自建运行时，D3 的表就是生成读写代码的输入（这条岔路仍未定）。

### D10 生成物的落点与 include 约定（2026-10-01 按用户裁定）

用户的要求是"使用者用起来尽量简单，头文件不要配复杂的生成路径"。定下来的约定：

1. **接入清单收回三列**：`(头文件, 类型名, 所属目录)`；生成物的入库目录**由约定推出**，不再逐条指定 —— 就是 `<所属目录>/generated/`。
2. **所属目录可以是任意目录前缀**，按需选粒度：夹具用 `Tools/CodeGen`（→ `Tools/CodeGen/generated/`），真实类型用 `Interface/RenderInterface`（→ `Interface/RenderInterface/generated/`，即当前位置，不用搬家）。
3. **消费方只写文件名**：模块把 `<所属目录>/generated` 加一次到自己的 include 路径（模块的 CMakeLists 里一行 `target_include_directories(${PROJECT_NAME} PRIVATE generated)`），之后新增类型**不需要再动 CMake**；消费代码写 `#include "GPUBufferDescriptor.Reflection.h"`，**不带任何相对路径**（现在的消费单元写的是 `"../generated/…"`，这次一并改掉）。
4. **`build.py` 增加 `--commit-generated` 开关**：加上它才把生成结果复制进 `<所属目录>/generated/`；**不加时构建绝不改源码树**（保持现有性质），只做逐字节对拍、不一致即失败并提示加这个开关。
5. 保留逐字节对拍这道闸：生成物仍在源码树里、仍入库，所以"改了类型忘记重新生成"照样在构建期报出来。

**顺带一起做的"去路径"清理**（同一批完成，免得留两套写法）：消费单元把 `#include "../generated/GPUBufferDescriptor.Reflection.h"` 改成 `#include "GPUBufferDescriptor.Reflection.h"`；`build.py` 的清单与对拍逻辑从四列收回三列；`Tools/CodeGen/README.md` 的接线章节按新约定重写 —— 使用者只要三步：加标注、清单加一行、跑一次带 `--commit-generated` 的构建。

## Risks / Trade-offs

- [字段表结构变更会让现有生成物字节改变] → **两个条目的六份入库产物**都要重新生成并入库，这是预期内的变更；改分类那一步（D1）单独先做，按条目核对（`EmitSample` 三份不变，真实类型那份的 `uint64_t` 会从 `Opaque` 变 `UInt`）。
- [摊平排序引入额外拷贝] → 先做副本版，测试里量一次大容器（例如 10 万个键）的开销；超标再改成只排下标。
- [工具的容器口径与运行时对不上] → D2 的判据要求同一个 TU 里对拍两侧结论。
- [新拒绝路径可能把本来能用的类型挡在外面] → 拒绝只针对"会静默出错"的组合（指针键、指针元素、无序容器无比较键）；有序容器与含普通元素的容器不受影响。
- [退出码增加，文档与自检脚本要同步] → `verify_readme.py` 会逐条跑文档里的命令，新增退出码必须在同一次提交里写进 README。

## Migration Plan

0. **顺序依赖已满足**：`wire-codegen-real-type` 已做完并归档（`openspec/changes/archive/2026-10-01-wire-codegen-real-type`）—— 四列清单、按模块并集推 include、宏不唯一即拒绝、预编译头补 `-include`、真实类型 `GPUBufferDescriptor` 都在位。反过来做会让那边"既有黄金样本逐字节不变"的要求自相矛盾，所以这一步的顺序不能反。
1. **只做 D1**（分类规范化），然后**按条目核对入库产物**：`EmitSample` 三份应当逐字节不变（成员是 `int` / `float` / `bool` / `double`）；真实类型那份 `Interface/RenderInterface/generated/GPUBufferDescriptor.Reflection.h` **会变**（两个 `uint64_t` 从 `Opaque` 变 `UInt`），必须重新生成并入库之后构建才会回到绿 —— 第二轮对抗验证发现初稿只提了 `EmitSample`，照它执行第一步就会撞上构建失败，且没有任何任务负责刷新那份文件。
2. **做 D2 / D3**（容器识别 + 元素描述），此时表结构变了 → 重新生成并入库黄金样本；补分类与元素描述的编译期断言。
3. **做 D4 / D5 / D6**（规范顺序、哈希一致、三条拒绝路径），每条配改之前实测。
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

## 对抗验证记录（第二轮，2026-10-01）

上一轮是在 `wire-codegen-real-type` **做完之前**做的；这一轮针对"做完之后的真实代码 + 四份工件"再跑一次五路对抗验证，改掉的仍是**错事实与漏覆盖**：

| 这一轮发现 | 实测证据 | 现在怎么写 |
|---|---|---|
| 第 1 步的"既有产物不变"已经过时：真实类型的入库反射头里两个 `uint64_t` 会从 `Opaque` 变 `UInt`，构建必然报错，而没有任何任务负责刷新它 | 入库件逐字 `{ "count", "uint64_t", …, TypeKind::Opaque, … }`；`build.py` 对拍不等即 `CODEGEN FAILED` + `sys.exit(1)` | tasks 1.2 与迁移计划第 1 步改成"真实类型那份必须重新生成入库"，并先贴出那条 `differs byte-for-byte` |
| 新夹具挂在模块根 `Tools/CodeGen` 会 exit 6：该根实测 0 个 include 目录，夹具一 include `CAUnorderedMap.h` 就找不到 `sha256.h` | `env 'Tools/CodeGen': 1 compile unit(s), 0 include dir(s)`；`unresolved include (fatal): … 'sha256.h' file not found` | tasks 2.3 写死"夹具的模块根必须是引擎模块（实测 `Interface` 可用）+ include 集合限定" |
| "构建打印产物路径"与"入库文件不存在就判失败"互相矛盾；2.4 又写死"恰好三行 ` M`" | `build.py`：`if not golden.exists(): failures.append("no golden at …")` | tasks 2.3/2.4 改成"先把产物入库再跑构建"，判据改成列出全部预期文件、不再写死行数 |
| "逐层摊平"挂在"元素类型有没有生成序列化器"上，而容器类型永远没有生成序列化器 | `vector<unordered_map>` firstDiff=16、`unordered_map<u,unordered_map>` firstDiff=20、`unordered_map m[2]` firstDiff=16 | D4 改成**按元素描述递归摊平** |
| "把浮点规范化递归到元素"交付不了那条不变量：平凡可拷贝的元素先走整块字节哈希，根本不会调到元素特化；有序容器里的浮点同样违反 | 探针：`h.hash(element)` 与显式 `custom_hash_trait` 结论相反；`vector<float>` 同内容不同哈希 | D5 改成**生成代码对元素显式调用元素类型的哈希特化**，并把有序容器含浮点列为例外或一并纳入 |
| 包装器被算进 `HasOpaqueElement`、又接到拒绝路径，会把真正写盘的 `ShaderLibrary` 整份拒掉 | `ShaderLibrary : TResource<ShaderLibrary>`，值类型链上有 `vector<NameHash>`；`managed_wrapper_traits<NameHash>` 本来就能读写 | D3/D6 把包装器从那一位里摘出去：`Wrapper` 只记录、**不触发拒绝**，并加放行夹具 |
| "全序"没写判据，而引擎真实键类型只有 `operator<=>` | `operator<(` 在 `CACore` / `Interface` 头文件里零命中；`operator<=>` 见 `NameHash` / `PathHash` / `CAHash::result_type` | D6 ① 写明判据必须接受 `<=>`；含 NaN 的键另行处理 |
| 按位存储容器（`castl::vector<bool>`）满足结构判据却过不了旧运行时的 `for (auto& item : object)` | 实测 MSVC `error C2440`；工具仍 exit 0 出码 | D3/D6 新增第 ⑤ 条拒绝路径 |
| spec 与 tasks 对"反射豁免哪几条"互补（②/④ 互换）；且清单会让三个发射器全跑 | spec「①②③ 不受影响」vs tasks 4.2「①③④ 不受影响」；`build.py` 每条固定跑三个发射器、任一非零即 `sys.exit(1)` | D6 末尾写明**清单里的类型必须三个发射器都能出**；4.2 统一口径 |
| **读侧两个类混成一句**：生成代码走 `cacore::deserializer`（`reserve`→`push_back`→`insert`→`resize`），且**个数不为 0 时不清空目标** | 探针：读进 3 元素目标后 size=5；个数=0 的载荷读进 1 元素目标后 size=1；`clear` 只在 `batch_deserializer` | Context 与 tasks 3.4 按实际分支改写，读回场景补"非空目标"一条 |

⚠ **已知边界（任务 4.2）**：**接入清单没有 per-entry 发射器选择** —— `build.py` 对每条清单条目固定跑三个发射器，任一次非零退出即整体构建失败，所以**只能出反射、出不了序列化或哈希的类型不能进清单**（例如含指针元素的类型：`--emit-reflection` 成功、`--emit-serialization` 退出码 15）。要让这类类型也用上生成物，只能直接调用工具，不能走清单。
