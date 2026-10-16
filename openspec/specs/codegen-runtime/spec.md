# codegen-runtime Specification

## Purpose
CodeGen 自有的运行时：读写自带游标（不再依赖旧运行时）、哈希不依赖旧哈希设施且与相等判断同源、定长数组按位置覆写而不是先清空、新哈希必须在既有调用点生效、哈希算法来源必须显式声明且与编译单元的可见性无关，以及「旧的三份头必须真正消失、旧实现不许改名留下」这条收尾判据。
## Requirements
### Requirement: 生成物的读写必须自带游标

CodeGen 生成的序列化代码 MUST NOT 依赖任何"把读位置藏在对象内部"的设施。读位置的 MUST 由调用者显式持有并在递归调用之间传递，使生成代码能在字节流的任意位置调用**另一个类型**的生成代码并继续读下去。

生成物 MUST NOT 引用 `cacore::serializer` / `cacore::deserializer`。这两个符号已随旧头删除（口径：除 `Tools/CodeGen/check_legacy_symbols.py` 的守卫符号表以外，源码树 0 处），所以这一条是**防回流**的守卫，不是"还有东西要迁"的待办。

#### Scenario: 容器里的结构体由它自己的生成代码读

- **WHEN** 一个容器的元素是另一个已用 CodeGen 标注的结构体，且该结构体内部含无序容器（`std::vector<Inner>`，`Inner` 内有 `unordered_map`）
- **THEN** 生成代码在读到该元素时把自己持有的读位置交给 `Inner` 的生成代码，读完后位置继续往后；同一内容、不同内层插入顺序的两个对象，写出的字节逐字节相同、读回的内容与写出内容一致，**不再以非零退出码拒绝**

#### Scenario: 落盘字节与冻结的期望字节逐字节一致

- **WHEN** 同一个对象用生成物（新读写）写一遍，再与测试里**冻结成字面量**的期望字节比对：`Test/CoreTests/CodeGenRuntimeTest.cpp` 的 `kOldSampleHex` 字面量与读它的那一行 `hexToBytes(kOldSampleHex)`
- **THEN** 两份字节逐字节相同 —— 旧运行时已删除，跑不出"与旧方案（`cacore::serializer` 逐成员写）对拍"，对照物因此改成入库的字面量；那些字面量是迁移期实测得到的（不是手算），改动它们等于改协议，MUST 单独说明理由

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

生成的特化 MUST 被引擎里既有的哈希调用点使用，而**不修改那些调用点**（`HashObj<...>` 的使用属于此类）。分派 MUST 走"显式来源优先"：先看生成物或手写特化，再走内置档（基本类型与枚举、指针与句柄、容器、包装类型），三者都不满足时 MUST 是编译错误 —— MUST NOT 用"平凡可拷贝就按整块字节"当兜底，也 MUST NOT 让 `std::hash` 作为隐式来源参与分派。既有特化的宿主类型**是不是**平凡可拷贝，不再影响任何一个类型的哈希口径。两个入口 `hasher::hash<T>` 与 `cacore::hash<T>` MUST 走同一条链。

#### Scenario: 平凡可拷贝的类型也走生成的特化

- **WHEN** 一个纯标量成员的结构体（平凡可拷贝）在**任意**编译单元里求哈希，且它已有生成的特化
- **THEN** 得到的是按字段算出的哈希，而不是整块字节的哈希；判据有两条：① 同一对象"按字段哈希"与"经既有入口哈希"两值相等；② **把该类型生成物的 `#include` 从这个单元去掉之后，这个单元 MUST 编不过** —— 不是"值悄悄变了却照样通过"

#### Scenario: 既有特化的行为不变

- **WHEN** 对 `HashObj<...>`、`NameHash`、`PathHash`、`ComPtr<ID3DBlob>` 四种既有特化各取一个值，经 `hasher::hash<T>` 与 `cacore::hash<T>` 两个入口求哈希
- **THEN** 结果与改动前一致，且两个入口两两相等。这四个宿主里前三个**同时**有 `custom_hash_trait` 与 `std::hash` 特化（`CACore/header/HashKeys.h` 里两处都写着），是"双来源"的活样本：本 change 之后它们 MUST 仍由那条手写特化决定分派，`std::hash` 特化只作为标准库设施留给别的调用方

### Requirement: 相等判断与哈希必须同源

对同一类型，"内容相等"的判据 MUST 与哈希使用的字段遍历一致：哈希按字段算的类型，其相等比较也 MUST 走生成特化的 `equal`，MUST NOT 退回按字节或按容器遍历顺序比较。

#### Scenario: 内容相同但内存表示不同

- **WHEN** 两个对象内容相同，但 padding 字节或无序容器的插入顺序不同
- **THEN** 相等判断为真**且**哈希相同；两者不一致即判为失败

### Requirement: 旧的三份头必须真正消失，且旧实现不许改名留下

本 change MUST 以三条**同时**成立作为完成条件：

1. **生成物与生产代码零引用**：检查脚本对生成物（**构建目录** `out/build/<预设>/codegen/<模块>/`；源码树里的 `generated/` 目录已由 `improve-build-time` 删除）与引擎源码（后缀 `.h/.hpp/.cpp/.cxx`，排除 `openspec/`、`out/`、脚本自身）逐符号扫描，命中 0 行。符号表 MUST 固定为：`cacore::serializer`、`cacore::deserializer`、`cacore::batch_deserializer`、`cacore::aggregateHasher`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`、`careflection::containerStates`、`careflection::managed_wrapper_traits`、`CATypeDescriptor`、`CA_REFLECTION`。脚本 MUST 打印"扫描 N 个文件、命中 0 行"，且 **N MUST NOT 为 0**（扫到空集就是恒真；脚本在找不到任何生成物目录时 MUST 以退出码 2 报错，MUST NOT 把空集当成"0 命中"）。
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

### Requirement: 哈希算法来源必须显式声明，且与编译单元的可见性无关

对任何"可能被求哈希"的类型，MUST 在**每个**用到它的编译单元里都能看到一个**显式**的算法来源：生成物或手写的 `custom_hash_trait` 特化、紧随类型定义的一行按字节声明（`CA_HASH_BY_BYTES`）、或运行时内置的那几档（基本类型与枚举、指针与句柄、容器、包装类型）。三者都不满足时 MUST 是**编译期错误**（静态断言，错误信息里点名该类型与三条出路），MUST NOT 退回"平凡可拷贝就按整块字节"这类隐式兜底。`std::hash` MUST NOT 作为**隐式**来源参与分派：外部库类型要复用它，MUST 在模块的 Includes 头里写一行**显式委托**特化。按字节声明处 MUST 带一条 `std::has_unique_object_representations_v<T>` 断言 —— 有填充字节或含 `float`/`double` 成员的类型 MUST 走字段级（字段级会把 `-0.0` 归一成 `+0.0`、把 NaN 规范成一个稳定值，字节口径不会）。两个哈希入口 `hasher::hash<T>` 与 `cacore::hash<T>` MUST 走同一条分派链，对同一个对象 MUST 算出同一个值。MUST 提供一支盘点脚本，能逐类型打印"算法来源"，并在存在来源不明的键类型时以非零退出。

#### Scenario: 漏了生成物必须当场编译失败

- **WHEN** 取一个已在接入清单里的类型，在某个会用到它的编译单元里把该类型生成物的 `#include` 去掉，再构建
- **THEN** 该编译单元 MUST 以静态断言失败，错误信息点名该类型；MUST NOT 出现"构建照样通过、只有哈希值悄悄变了"。**改造前的读数**（红态）：同一处改动**能编过**，该类型在这个单元里退回整块字节，链接期之后整个二进制的口径被换掉 —— 先把这条读数贴出来再改

#### Scenario: 按字节算哈希必须显式声明

- **WHEN** 一个平凡可拷贝、无填充字节的键类型没有 `CA_HASH_BY_BYTES` 声明
- **THEN** 求它的哈希 MUST 是编译错误；补上那一行声明之后 MUST 编过，且经既有入口取到的哈希值与改造前**逐字相同**

#### Scenario: 有填充字节或浮点成员的类型不许声明按字节

- **WHEN** 对一个含 `float` / `double` 成员、或带填充字节的类型写 `CA_HASH_BY_BYTES`
- **THEN** 声明处那条 `std::has_unique_object_representations_v<T>` 断言 MUST 失败、构建 MUST 不通过（该类型只能走字段级）

#### Scenario: 外部库类型用一行委托声明

- **WHEN** 某个第三方类型自带 `std::hash` 并被当键用，而某个编译单元没 include 定义那个 `std::hash` 的头
- **THEN** 求哈希 MUST 是编译错误；在模块的 Includes 头里写一行委托特化之后，MUST 处处一致（现成范例：`D3D12RenderBackend/private/D3D12Includes.h` 的 `ComPtr<ID3DBlob>`、`VulkanRenderBackendNew/private/Utils/VulkanIncludes.h` 的 `TypedVKHashVal<T>`）

#### Scenario: 两个入口对同一个对象必须算出同一个值

- **WHEN** 对每个已标注类型，在同一个编译单元里分别用 `hasher::hash<T>` 与 `cacore::hash<T>` 对同一个对象求值；再把该类型生成物的 `#include` 从该单元去掉重来一次
- **THEN** 前一半两值 MUST 相等（贴读数）；后一半两个入口 MUST **一起**失败 —— MUST NOT 出现"一个入口编译失败、另一个入口照样算得出值"。**改造前的读数**（红态）：`cacore::hash<T>` 在结果类型是 `size_t` 且该类型有 `std::hash` 可用时走的是 `std::hash<T>`，与另一个入口不同链，所以"藏掉特化"之后它可能照样通过

#### Scenario: 算法来源可一次盘清

- **WHEN** 跑盘点脚本（`Tools/CodeGen/` 下的新脚本）
- **THEN** 它 MUST 打印"扫描 N 个文件、命中 M 个来源不明的键类型"以及一张逐类型表（类型名 + 算法来源：生成物 / 手写声明 / 内置档 / 无）；M MUST 为 0 时退出码 0，M 大于 0 时 MUST 非零退出并逐个点名（形状与 `Tools/CodeGen/check_legacy_symbols.py` 一致）

