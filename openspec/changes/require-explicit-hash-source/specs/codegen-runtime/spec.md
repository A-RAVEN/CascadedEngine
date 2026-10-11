## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: 新哈希必须在既有调用点生效

生成的特化 MUST 被引擎里既有的哈希调用点使用，而**不修改那些调用点**（`HashObj<...>` 的使用属于此类）。分派 MUST 走"显式来源优先"：先看生成物或手写特化，再走内置档（基本类型与枚举、指针与句柄、容器、包装类型），三者都不满足时 MUST 是编译错误 —— MUST NOT 用"平凡可拷贝就按整块字节"当兜底，也 MUST NOT 让 `std::hash` 作为隐式来源参与分派。既有特化的宿主类型**是不是**平凡可拷贝，不再影响任何一个类型的哈希口径。两个入口 `hasher::hash<T>` 与 `cacore::hash<T>` MUST 走同一条链。

#### Scenario: 平凡可拷贝的类型也走生成的特化

- **WHEN** 一个纯标量成员的结构体（平凡可拷贝）在**任意**编译单元里求哈希，且它已有生成的特化
- **THEN** 得到的是按字段算出的哈希，而不是整块字节的哈希；判据有两条：① 同一对象"按字段哈希"与"经既有入口哈希"两值相等；② **把该类型生成物的 `#include` 从这个单元去掉之后，这个单元 MUST 编不过** —— 不是"值悄悄变了却照样通过"

#### Scenario: 既有特化的行为不变

- **WHEN** 对 `HashObj<...>`、`NameHash`、`PathHash`、`ComPtr<ID3DBlob>` 四种既有特化各取一个值，经 `hasher::hash<T>` 与 `cacore::hash<T>` 两个入口求哈希
- **THEN** 结果与改动前一致，且两个入口两两相等。这四个宿主里前三个**同时**有 `custom_hash_trait` 与 `std::hash` 特化（`CACore/header/HashKeys.h` 里两处都写着），是"双来源"的活样本：本 change 之后它们 MUST 仍由那条手写特化决定分派，`std::hash` 特化只作为标准库设施留给别的调用方
