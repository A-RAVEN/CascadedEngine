## ADDED Requirements

### Requirement: 字段种类必须按规范化类型判定

工具的字段分类 MUST 先取类型的规范化形式再判定种类。类型别名 MUST NOT 因为"拼写不是内建名"而落进 `TypeKind::Opaque`。

#### Scenario: 别名与内建写法得到同一个种类

- **WHEN** 同一个结构体里同时有 `unsigned int` 与 `uint32_t` 两个成员（实测现状：`unsigned int` → `UInt`，`uint32_t` → `Opaque`）
- **THEN** 两个成员的种类都是 `TypeKind::UInt`；`unsigned long long` 与 `uint64_t` 同理都是 `UInt`

#### Scenario: 内建类型与枚举不受影响

- **WHEN** 结构体里有 `int` / `float` / `double` / `bool` / 作用域枚举成员
- **THEN** 种类与改动前一致（`Int` / `Float` / `Double` / `Bool` / `Enum`），既有生成物逐字节不变

### Requirement: 容器必须被识别，并在字段表里携带元素描述

工具 MUST 按**结构**识别容器（与运行时同口径：有 `size()` 且有 `value_type` 的记录类型、以及 C 数组），MUST NOT 依赖 `std::vector` / `eastl::vector` 这类名字。字段表 MUST 给出容器成员的**元素描述**（元素种类、元素类型名、是否嵌套容器、元素里是否含指针 / 句柄 / 包装器），并标出容器是否无序。

#### Scenario: 容器不再被记成 Opaque

- **WHEN** 结构体里有 `castl::vector<unsigned>`、`castl::string`、`castl::unordered_map<unsigned, unsigned>`、`castl::set<int>` 成员（实测现状：四者都是 `TypeKind::Opaque`）
- **THEN** 这四个成员都带 `Container` 标志（`TypeKind` 不改：枚举里没有 `Map` / `Set`，`Vector` / `Array` 已被 glm 向量与 C 数组占用），且 `vector` 的元素描述给出元素种类 `UInt`；`unordered_map` 另带 `Unordered` 标志并给出 `key` / `element` 两层描述

#### Scenario: 嵌套容器能看出层级

- **WHEN** 成员类型是 `castl::vector<castl::vector<int>>`
- **THEN** 字段表里该成员带 `Container` 标志，其元素描述本身也是一个容器（同样带 `Container`，元素是 `Int`），**不借用 `TypeKind::Vector`**（那个值已被 glm 向量占用，与标准容器无关）

#### Scenario: 识别口径与运行时一致

- **WHEN** 同一个编译单元里既用工具产出的表判断"是不是容器"，又用运行时的 `containerStates<T>::is_container_with_size` 判断
- **THEN** 两侧对同一批类型给出相同结论（编译期断言通过）

#### Scenario: 元素里含指针或托管指针时被标出

- **WHEN** 成员类型是 `castl::vector<int*>`、`castl::unordered_map<int, void*>`，或 `castl::vector<castl::shared_ptr<T>>`（引擎里真实存在这种成员）
- **THEN** 该成员都带 `Container` 标志，且元素描述里标明元素含裸指针或托管指针（`HasOpaqueElement` 置位），供拒绝路径使用

#### Scenario: 多维 C 数组按维度逐层描述

- **WHEN** 成员类型是 `int a[2][3]`
- **THEN** 表的元素描述给出的是**直接元素类型**（`int[3]`）而不是最内层的 `int`，并且 README 的磁盘契约写明"先写第一维、再逐个写内层数组"（运行时 `containerInfo` 取的是最内层类型，照抄会让描述与真实字节不符）

### Requirement: 无序容器的序列化与哈希必须走规范顺序

对无序容器（`unordered_map` / `unordered_set`），生成的代码 MUST 在写出与哈希之前把内容摊平成**按规范顺序排好的元素数组**（`map` 按 key、`set` 按元素本身），再按数组的形状写出 / 累加哈希。读回来 MUST 以逐个插入的方式还原，且与写入顺序无关。有序容器（`vector` / `array` / `string` / `deque` / `list` / `map` / `set`）的行为 MUST NOT 改变。

#### Scenario: 插入顺序不影响写出的字节

- **WHEN** 两个 `castl::unordered_map<uint32_t, uint32_t>` 内容相同、但插入顺序不同，各自序列化
- **THEN** 两份产物逐字节相同

#### Scenario: 插入顺序不影响哈希

- **WHEN** 上述两个内容相同、插入顺序不同的对象各自算哈希
- **THEN** 两个哈希相同（与生成的相等判断"内容比较"保持一致）

#### Scenario: 内容不同则字节与哈希都不同

- **WHEN** 两个 `unordered_map` 内容不同（多一对、少一对、或值不同）各写一次
- **THEN** 字节不同且哈希不同 —— 这是防止"顺序无关"判据恒真的对照组

#### Scenario: 嵌套的无序容器同样稳定

- **WHEN** 两个对象的**内层**无序容器插入顺序不同（例如 `struct Inner { castl::unordered_map<unsigned, unsigned> m; }; struct Outer { castl::vector<Inner> items; };`，只让 `items[0].m` 的插入顺序不同）
- **THEN** 两份产物逐字节相同且哈希相同；生成代码按**元素描述递归摊平**（不依赖"元素类型有没有生成序列化器" —— 容器类型永远拿不到生成序列化器）。实测现状：112 字节、首个不同字节在第 20 字节、哈希 `c188763afb62ef98` 与 `1020e97f4c8baf58` —— 这条是初稿漏掉的层级

#### Scenario: 元素本身就是容器时同样稳定

- **WHEN** 无序容器是**元素**或 **map 的值**，或 C 数组的元素本身是无序容器：`castl::vector<castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned, castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned,unsigned> m[2]`，各取两个只有内层插入顺序不同的对象
- **THEN** 三者的产物逐字节相同、哈希相同。实测现状：三者首个不同字节分别在 16 / 20 / 16（初稿那句"调用元素类型自己的生成序列化器"对这些形状落不下来）

#### Scenario: 元素里的浮点也要按值参与哈希

- **WHEN** 两个 `castl::unordered_map<uint32_t, float>` 内容只差在 `+0.0f` 与 `-0.0f`
- **THEN** 生成的相等判断为真时两个哈希必须相同。实测现状：相等判断为真（`==` 成立）而哈希不同（`1a2742e842fb0203` 与 `1a27c2e842fbdb83`），因为浮点规范化只写在顶层字段那一层；本 change 把规范化**递归到元素**（元素按值参与哈希），不再整块按字节算

#### Scenario: 有序容器里的浮点同样按值参与哈希（**已定死，不列为例外**）

- **WHEN** 有序容器含浮点且两个对象只差 `+0.0f` / `-0.0f`，例如引擎里真实存在的 `castl::vector<castl::vector<float>>`
- **THEN** 相等判断为真时哈希必须相同 —— 所有容器（有序无序一视同仁）都按元素显式调用元素类型的哈希特化，不因为"这个 change 只谈无序容器"就把它留成例外。实测现状：`vector<float>` 同内容不同哈希（`5f242d39c2422be4` 与 `5f23ad39c2415264`）

#### Scenario: 读回来的内容与写入一致（空目标）

- **WHEN** 把排序后写出的字节读回一个**空**容器
- **THEN** 读回的容器与写出的容器内容相等（顺序无关的比较）

#### Scenario: 读进非空目标时先清空（**已定死，不留选择**）

- **WHEN** 把同样的字节读进一个**已经有元素**的容器
- **THEN** 生成代码 MUST 先清空该容器再读（可用的前提是 `containerStates<T>::has_clear`；没有 `clear()` 的容器进拒绝路径），使读回来的内容**只由载荷决定**。依据：实测生成代码走的是 `cacore::deserializer`，它**不清空**目标容器（读进 3 元素目标后 size=5；元素个数为 0 的载荷读进 1 元素目标后 size 仍是 1，`clear` 只写在 `cacore::batch_deserializer` 里），而"还原"这个说法只有在清空之后才成立

#### Scenario: 有序容器的产物不变

- **WHEN** 序列化 `castl::vector<int>` / `castl::map<int, int>` / `castl::string`
- **THEN** 写出的形状与改动前一致（元素个数 + 按容器自身顺序逐个写），既有黄金样本不需要改动

### Requirement: 会静默出错的容器组合必须被拒绝

下列组合 MUST 让工具以非零退出码拒绝出码，并在 stderr 点名成员与原因，MUST NOT 静默产出会丢数据的代码：① **参与排序的那一侧**没有全序（`map` 的键、`set` 的元素；`map` 的值不参与排序，不检查；判据须接受 `operator<=>`，含 NaN 的键判不出全序）；② 容器元素含**裸指针 / 托管指针（`shared_ptr` / `unique_ptr`）/ 句柄**且请求的是序列化产物；③ **重复键容器**（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset` —— `castl` 经 `using namespace std` / `eastl` 透出这些名字，它们是可达的）；④ 容器的键或元素**不可默认构造**（旧运行时的 pair 支持要求元素可默认构造）；⑤ **按位存储的容器**（`castl::vector<bool>` 一类：满足结构判据，却过不了旧运行时的逐元素循环）。

**包装器不在拒绝清单里**（`NameHash` / `PathHash` / `HashObj` 只记录，见字段表要求）。仅请求反射产物时，①②③④⑤ 都不受影响。⚠ **接入清单口径**：`build.py` 对每条清单条目固定跑三个发射器、任一次非零即整体构建失败，所以**进清单的类型必须三个发射器都能出**；只能出反射的类型不进清单（工具本身仍允许对它单独请求反射）。

#### Scenario: 指针作元素时序列化被拒绝

- **WHEN** 对含 `castl::vector<int*>` 或 `castl::vector<castl::shared_ptr<T>>` 成员的已标注类型请求 `--emit-serialization`
- **THEN** 工具非零退出，点名该成员路径与"元素含指针 / 托管指针，写出会丢数据"；同一类型的 `--emit-reflection` 仍然成功

#### Scenario: 指针作键时被拒绝

- **WHEN** 对含 `castl::unordered_map<int*, int>` 成员的已标注类型请求序列化或哈希
- **THEN** 工具非零退出并说明"键没有全序，写出的字节不可能稳定"

#### Scenario: 重复键容器被拒绝，且判据可按名判定

- **WHEN** 成员类型是 `std::multiset<int>` 或 `castl::multimap<int, int>`（实测 `castl::multimap` 可用，且 `set` 与 `multiset` 的运行时特征完全相同、结构上分不开）
- **THEN** 工具非零退出并说明"重复键容器没有规范顺序、本版本不支持"；判据取**规范化后的类型名**（`std::` / `eastl::` 形式），因此不受 `USING_EASTL` 开关影响

#### Scenario: 元素不可默认构造时被拒绝

- **WHEN** 容器的元素类型没有默认构造函数（如 `struct NoDef { int v; explicit NoDef(int); };` 放进 `castl::vector`）
- **THEN** 工具非零退出并说明原因；实测现状是照样出码，然后在消费方编译时才撞 `static assertion failed: 'object incompatible for reflection'`（写侧）与 `'Wrong'`（读侧）

#### Scenario: 包装器元素必须放行，不得被当成不透明

- **WHEN** 容器的元素或键是包装器（`cacore::NameHash` / `cacore::PathHash` / `cacore::HashObj`），例如引擎里真实存在的 `castl::unordered_map<cacore::NameHash, ShaderStructData>` 与 `castl::vector<cacore::NameHash>`
- **THEN** 类型带 `Wrapper` 记录但**不触发任何拒绝**，序列化产物照常生成（旧运行时用 `managed_wrapper_traits` 解包读写）；只有裸指针 / 托管指针 / 句柄才触发 ②

#### Scenario: 排序的比较判据必须接受 `operator<=>`

- **WHEN** 键或元素类型只提供了 `operator<=>`（引擎真实键类型 `NameHash` / `PathHash` / `CAHash::result_type` 都是这种）
- **THEN** 工具必须认定它有全序（C++20 里 `a < b` 会重写为 `(a <=> b) < 0`）；实测现状是这些头文件里 `operator<(` 零命中，按 `<` 判定会把引擎唯一的落盘资源类型整份误拒

#### Scenario: 全序判不出来的键必须拒绝

- **WHEN** 键是浮点且含 NaN（两个 NaN 互为"等价"但 `==` 为假，排序给不出稳定次序）
- **THEN** 工具拒绝出码并点名该键，不得按"能编译过 `a < b`"就放行；实测现状：两个 NaN 键的 `unordered_map` 摊平并 `std::sort` 后写出字节仍不同（首个不同字节在第 8）

#### Scenario: 按位存储的容器被拒绝

- **WHEN** 容器是按位存储的（`castl::vector<bool>` 一类）
- **THEN** 工具非零退出并点名该成员 —— 它满足"有 `size()` 且有 `value_type`"这条结构判据，但旧运行时的 `for (auto& item : object)` 绑不上按位代理，消费方一实例化就编译失败（实测 MSVC `error C2440`）；实测现状是工具 exit 0 照常出码

### Requirement: 容器的磁盘形状是对外契约

`Tools/CodeGen/README.md` MUST 写明容器成员在磁盘上的形状（先写元素个数、再按规范顺序逐个写元素；`map` 写的是键值对数组）以及排序规则与拒绝条件，供外部使用者按此解析生成的数据。

#### Scenario: 文档写明容器格式且示例可跑

- **WHEN** 运行 `python Tools/CodeGen/verify_readme.py`
- **THEN** 文档里新增的容器示例命令逐条执行且退出码与文档一致（0 处不符）；用 grep 能在 README 里找到"无序容器按排序后的数组写出"这条规则
