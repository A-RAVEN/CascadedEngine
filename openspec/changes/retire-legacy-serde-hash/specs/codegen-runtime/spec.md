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

### Requirement: 旧的三份头必须真正消失，且旧实现不许改名留下

本 change MUST 以三条**同时**成立作为完成条件：

1. **生成物与生产代码零引用**：检查脚本对生成物目录（`Tools/CodeGen/generated/`、`Interface/RenderInterface/generated/`、`CACore/generated/`）与引擎源码（后缀 `.h/.hpp/.cpp/.cxx`，排除 `openspec/`、`out/`、脚本自身）逐符号扫描，命中 0 行。符号表 MUST 固定为：`cacore::serializer`、`cacore::deserializer`、`cacore::batch_deserializer`、`cacore::aggregateHasher`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`、`careflection::containerStates`、`careflection::managed_wrapper_traits`、`CATypeDescriptor`、`CA_REFLECTION`。脚本 MUST 打印"扫描 N 个文件、命中 0 行"，且 N 与开工时那次红态运行的 N 相同。
2. **三份旧头在文件系统上不存在**：用文件系统枚举确认 `CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h` 三个路径都不存在。**改名、`git rm --cached`、搬到新头、移出 include 路径都不算**；随后 `python build.py --config Debug` → `BUILD SUCCESSFUL`，`CoreTests.exe` 退出码 0 且用例名逐条列出、总数不少于开工前。
3. **旧实现指纹零命中**：`visit_members`、`CATypeMemberDesc`、`containerInfo`、`SubmitAndWait`、`Object must be trivially copyable`、`hash_one`、`managed_pointer_traits` 命中 0 行；并用 `git log --diff-filter=R --name-status` 确认三份旧头是删除而不是改名 / 搬迁。

**唯一豁免**：消费方 `.cpp` 里为 ABI 守卫保留的老宏。豁免 MUST 写成逐行白名单（现状只有 `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp` 一处），判据是 `命中行数 == 白名单行数`，多一行即红；白名单每行要写明它守的是哪个生成表。

`CA_REFLECTION_TEMPLATE`（`CACore/header/uenum.h` 的类模板描述符）单独处理：先给一条任务尝试迁移；实测不可行时允许它在 `uenum.h` 里保留，但 MUST 交出实测证据，且非模板形态的 `CA_REFLECTION` 仍须为零。

#### Scenario: 三条判据在本 change 开始时都为红

- **WHEN** 对开工时的代码跑这三条检查
- **THEN** 三条都失败，并各自给出可复现的数字：符号扫描的命中行数与扫描文件数、三份旧头的存在、指纹扫描的命中行数

#### Scenario: 改名、移出 include 路径、删测试都不能通过

- **WHEN** 只把旧机制改名（符号换名字但实现不变）、把三份头移出 include 路径、或删掉依赖旧宏的测试用例
- **THEN** 三条判据里至少一条变红：指纹扫描命中、文件系统枚举仍能找到三份头、或 `CoreTests` 用例数少于开工前
