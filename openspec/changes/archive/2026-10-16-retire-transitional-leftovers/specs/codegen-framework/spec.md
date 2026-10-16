## REMOVED Requirements

### Requirement: 保留 CA_REFLECTION 运行时语义
**Reason**: 旧反射路径已随 `CACore/header/Reflection.h` / `Serialization.h` / `Hasher.h` 删除（`retire-legacy-serde-hash`，2026-10-05 归档）：那三个路径在文件系统上不存在，`CA_REFLECTION` 与 `visit_members` 在源码树里各 **0 处**（2026-10-15 实测）。这条要求写的是"生成元数据 SHALL 是**加法**，不替换、不改变现有反射路径"，与现状正好相反 —— 反射现在只有 CodeGen 一条路，没有"现有路径"可保留。
**Migration**: 无。没有任何代码或调用方按这条要求行事；它想覆盖的"构建期零运行时依赖"由本规格里既有的构建期要求（生成物是纯构建期产物、引擎产物不含 `libclang.dll` 依赖）继续覆盖。

## MODIFIED Requirements

### Requirement: 生成源码以显式源清单纳入构建
系统 SHALL 用**显式方式**纳入生成的源文件（非 `file(GLOB_RECURSE)`）：生成的**头文件**经显式 include 路径 + `#include` 纳入；生成的 **.cpp**（若存在）经显式 `target_sources` 纳入（禁 `file(GLOB_RECURSE *.cpp)`）。生成的反射代码 SHALL 与 **ABI 守卫**做构建期校验，而守卫的**实际范围** MUST 写在对外文档里：它**只**断言生成描述符的类型名非空（"生成头到底有没有被包含进来"），**不比名字内容、不比字段数、不比 offset**。字段数 / 字段名 / offset 的漂移由**消费单元的断言文件**对着**写死的期望值**比对（`Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、两个 `ShaderLibraryCodeGenAssertions.cpp`），生成表与期望值对不上时那些断言编译不过。旧的一致校验对象（`CATypeDescriptor<T>` 与它的 `member_count`）已随旧头删除，MUST NOT 再作为**校验对象**出现在任何要求或实现里；`Tools/CodeGen/check_legacy_symbols.py` 的守卫符号表与解释这段历史的注释句除外 —— 它们列出这些名字是守卫的职责。

#### Scenario: 缺失生成文件被构建捕获

- **WHEN** 生成源文件缺失
- **THEN** 构建以失败而非静默通过：缺失生成头 → 经显式 `#include` 报编译错误；缺失生成 .cpp → 经显式 `target_sources` 报构建错误。**该方向已验**（4.2：删生成头 → `C1083`）。

#### Scenario: 与注解对不上时消费单元先红

- **WHEN** 生成表与源码注解**对不上**（字段增删、改名、偏移变化）
- **THEN** 消费单元的断言文件编译失败（`error C2338`）并点名对不上的那一条；`static_assert_abi_consistency<T>()` 只负责"生成头没被包含进来"这一种情况，MUST NOT 被当成字段级的漂移守卫。

### Requirement: 无序容器的序列化与哈希必须走规范顺序
对无序容器（`unordered_map` / `unordered_set`），生成的代码 MUST 在写出与哈希之前把内容摊平成**按规范顺序排好的元素数组**（`map` 按 key、`set` 按元素本身），再按数组的形状写出 / 累加哈希。读回来 MUST 以逐个插入的方式还原，且与写入顺序无关。有序容器（`vector` / `array` / `string` / `deque` / `list` / `map` / `set`）**不排序**：写出的字节与改动前逐字节相同（任务 3.5 对拍）。有序容器与无序容器一样，**元素按值参与哈希**（元素用自己的哈希特化，浮点先规范化 —— 见下面「元素里的浮点也要按值参与哈希」），所以有序容器里含浮点时，哈希值与改动前**允许不同**：这是"所有容器一视同仁、不留例外"定下来的口径，不是行为回退。容器的元素是**结构体**（逐层展开后含无序容器）时，生成代码 MUST 把当前读位置交给该元素类型自己的生成代码并继续往后读 —— MUST NOT 以非零退出码拒绝这个形状（旧运行时拿不到读写游标时的拒绝路径已随旧头删除，见 `codegen-runtime` 的「生成物的读写必须自带游标」）。

#### Scenario: 插入顺序不影响写出的字节

- **WHEN** 两个 `castl::unordered_map<uint32_t, uint32_t>` 内容相同、但插入顺序不同，各自序列化
- **THEN** 两份产物逐字节相同

#### Scenario: 插入顺序不影响哈希

- **WHEN** 上述两个内容相同、插入顺序不同的对象各自算哈希
- **THEN** 两个哈希相同（与生成的相等判断"内容比较"保持一致）

#### Scenario: 内容不同则字节与哈希都不同

- **WHEN** 两个 `unordered_map` 内容不同（多一对、少一对、或值不同）各写一次
- **THEN** 字节不同且哈希不同 —— 这是防止"顺序无关"判据恒真的对照组

#### Scenario: 元素是内含无序容器的结构体时逐层交给元素自己

- **WHEN** 容器的元素是**结构体**，而该结构体（逐层展开后）含无序容器，例如 `struct Inner { castl::unordered_map<unsigned, unsigned> m; }; struct Outer { castl::vector<Inner> items; };`
- **THEN** 生成代码在读到该元素时把自己持有的读位置交给 `Inner` 的生成代码，读完后位置继续往后；同一内容、不同内层插入顺序的两个对象写出的字节逐字节相同、读回内容与写出内容一致；MUST NOT 输出"看着能编译、字节却随插入历史变化"的代码，MUST NOT 以非零退出码拒绝这一形状
- **对照组**：元素是结构体但内部只有标量与**有序**容器时（如 `struct Plain { unsigned a; castl::map<unsigned,int> ordered; };`）MUST 同样出码（exit 0）
- **已知边界**：元素类型若不在本次扫描的头文件里，工具看不到它的字段、无法判定，此时按"未发现"放行

#### Scenario: 元素本身就是容器时同样稳定

- **WHEN** 无序容器是**元素**或 **map 的值**，或 C 数组的元素本身是无序容器：`castl::vector<castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned, castl::unordered_map<unsigned,unsigned>>`、`castl::unordered_map<unsigned,unsigned> m[2]`，各取两个只有内层插入顺序不同的对象
- **THEN** 三者的产物逐字节相同、哈希相同。实测现状：三者首个不同字节分别在 16 / 20 / 16（初稿那句"调用元素类型自己的生成序列化器"对这些形状落不下来）

#### Scenario: 元素里的浮点也要按值参与哈希

- **WHEN** 两个 `castl::unordered_map<uint32_t, float>` 内容只差在 `+0.0f` 与 `-0.0f`
- **THEN** 生成的相等判断为真时两个哈希必须相同。实测现状：相等判断为真（`==` 成立）而哈希不同（`1a2742e842fb0203` 与 `1a27c2e842fbdb83`），因为浮点规范化只写在顶层字段那一层；本要求把规范化**递归到元素**（元素按值参与哈希），不再整块按字节算

#### Scenario: 有序容器里的浮点同样按值参与哈希（**已定死，不列为例外**）

- **WHEN** 有序容器含浮点且两个对象只差 `+0.0f` / `-0.0f`，例如引擎里真实存在的 `castl::vector<castl::vector<float>>`
- **THEN** 相等判断为真时哈希必须相同 —— 所有容器（有序无序一视同仁）都按元素显式调用元素类型的哈希特化。实测现状：`vector<float>` 同内容不同哈希（`5f242d39c2422be4` 与 `5f23ad39c2415264`）

#### Scenario: 读回来的内容与写入一致（空目标）

- **WHEN** 把排序后写出的字节读回一个**空**容器
- **THEN** 读回的容器与写出的容器内容相等（顺序无关的比较）

#### Scenario: 读进非空目标时先清空（**已定死，不留选择**）

- **WHEN** 把同样的字节读进一个**已经有元素**的容器
- **THEN** 生成代码 MUST 先清空该容器再读，使读回来的内容**只由载荷决定**。依据（新运行时）：`CACore/header/CodeGenSerialization.h` 的容器读取路径按"容器有没有 `clear()`"分派 —— 有 `clear()` 的先清空（`if constexpr (hasClear<C>::value) c.clear();`），没有 `clear()` 的定长数组先校验载荷长度与 extent 一致、再按位置逐个覆写。旧运行时"不清空目标容器"的那条依据已随 `cacore::deserializer` 删除。

#### Scenario: 有序容器的产物不变

- **WHEN** 序列化 `castl::vector<int>` / `castl::map<int, int>` / `castl::string`
- **THEN** 写出的形状与改动前一致（元素个数 + 按容器自身顺序逐个写），既有黄金样本不需要改动
