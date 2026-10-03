## ADDED Requirements

### Requirement: 生成物的读写必须自带游标

CodeGen 生成的序列化代码 MUST NOT 依赖任何"把读位置藏在对象内部"的设施。读位置的 MUST 由调用者显式持有并在递归调用之间传递，使生成代码能在字节流的任意位置调用**另一个类型**的生成代码并继续读下去。

生成物 MUST NOT 引用 `cacore::serializer` / `cacore::deserializer`。

#### Scenario: 容器里的结构体由它自己的生成代码读

- **WHEN** 一个容器的元素是另一个已用 CodeGen 标注的结构体，且该结构体内部含无序容器（`std::vector<Inner>`，`Inner` 内有 `unordered_map`）
- **THEN** 生成代码在读到该元素时把自己持有的读位置交给 `Inner` 的生成代码，读完后位置继续往后；同一内容、不同内层插入顺序的两个对象，写出的字节逐字节相同、读回的内容与写出内容一致，**不再以非零退出码拒绝**

#### Scenario: 落盘字节与旧方案逐字节一致

- **WHEN** 同一个对象分别用生成物（新读写）与旧运行时（`cacore::serializer` 逐成员写）各写一遍
- **THEN** 两份字节逐字节相同 —— 这是迁移期防止格式对不上的对照，也是本条的判据（新读写在实现前该判据为红）

#### Scenario: 截断的载荷不会读越界

- **WHEN** 读一个被截断的载荷
- **THEN** 读对象在越界前失败（不读长度之外的内存），且失败可被调用方观察到

### Requirement: 生成物的哈希不依赖旧哈希设施

生成的哈希特化 MUST 由 CodeGen 自己的哈希对象驱动；生成物 MUST NOT 引用 `cacore::custom_hash_trait`、`cacore::has_custom_hash_func`、`careflection::containerStates` 这些写在旧头里的符号。

容器判据、特化检测与容器元素的哈希规则 MUST 由新运行时提供，且与字段表里的容器标志同口径。

#### Scenario: 生成物不再引用旧符号

- **WHEN** 对仓库里的生成物跑 `git grep`，逐个搜 `cacore::serializer`、`cacore::deserializer`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`、`careflection::containerStates`
- **THEN** 生成物里零命中（带文件限定）；同一判据在本 change 开始时为红（现状每个符号都有命中）

### Requirement: 定长数组按位置覆写而不是先清空

容器的读取路径 MUST NOT 假定容器一定有 `clear()`。对定长数组（`castl::array` 等没有 `clear()` 的类型）MUST 按位置逐个覆写，并在写入载荷时带上长度、读取时校验长度与数组 extent 一致。

#### Scenario: 含定长数组的真实类型可以出码并被消费

- **WHEN** 对一个含 `castl::array<T, N>` 成员的已标注类型请求序列化，并在真实目标的编译单元里实例化生成的读路径
- **THEN** 编译通过、往返一致；不再需要"没有 `clear()` 就拒绝"这条路径

### Requirement: 新哈希必须在既有调用点生效

生成的特化 MUST 被引擎里既有的哈希调用点使用，而**不修改那些调用点**（`HashObj<...>` 的 60 处使用属于此类）。

为此允许把旧哈希分派里"有自定义特化"的判据提到"平凡可拷贝"之前；做这件事的前提是：所有既有特化的宿主类型都不是平凡可拷贝，因此既有哈希值不变。

#### Scenario: 平凡可拷贝的类型也走生成的特化

- **WHEN** 一个纯标量成员的结构体（平凡可拷贝）经旧入口 `aggregateHasher::hash` 求哈希，且它已有生成的特化
- **THEN** 得到的是按字段算出的哈希，而不是整块字节的哈希。判据：同一对象"按字段哈希"与"旧入口哈希"两值相等 —— 在本条实现之前两者不等（红）

#### Scenario: 既有特化的行为不变

- **WHEN** 对 `HashObj<...>`、`NameHash`、`PathHash`、`ComPtr<ID3DBlob>` 四种既有特化各取一个值求哈希
- **THEN** 结果与改动前一致（它们的宿主类型都不是平凡可拷贝，见 design 的 D3 核对表）

### Requirement: 相等判断与哈希必须同源

对同一类型，"内容相等"的判据 MUST 与哈希使用的字段遍历一致：哈希按字段算的类型，其相等比较也 MUST 走生成特化的 `equal`，MUST NOT 退回按字节或按容器遍历顺序比较。

#### Scenario: 内容相同但内存表示不同

- **WHEN** 两个对象内容相同，但 padding 字节或无序容器的插入顺序不同
- **THEN** 相等判断为真**且**哈希相同；两者不一致即判为失败

### Requirement: 旧的三份头可以删除

本 change 的完成条件是：`Reflection.h`、`Serialization.h`、`Hasher.h` 不再被任何生成物或引擎代码引用，可以从仓库删除而全量构建通过。注意"可以删除"MUST 以全仓符号搜索为依据，MUST NOT 以"迁移清单看起来做完了"为依据。

#### Scenario: 删除后构建仍然通过

- **WHEN** 三份头被删除（或移出 include 路径）后跑全量构建与已有测试
- **THEN** 构建成功、测试全绿。判据在本 change 开始时为红（三份头仍被大量引用）
