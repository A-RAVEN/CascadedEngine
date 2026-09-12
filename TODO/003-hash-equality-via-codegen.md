# 003 · 哈希与相等 CodeGen 化 —— 统一 CodeGen 框架的第 4 个 emitter

> **状态**：探索中（未达 OpenSpec proposal 水位）
> **日期**：2026-09-06
> **关联**：[[001-csharp-scripting-framework.md]]（绑定）、[[002-reflection-via-codegen.md]]（反射）、`CACore/header/Hasher.h`、`CACore/header/CACore/CAHash.h`（komihash）
> **来源**：一次 `open/explore` 讨论。论证：哈希与相等本质上是在「类型表」上按字段序遍历的两个函数，因此和反射/绑定/序列化一样可由 CodeGen 从同一张类型表生成，是统一框架的第 4 个 emitter。

---

## 1. 两大痛 → 代码出处

现有哈希在 `CACore/header/Hasher.h`，痛点能精确映射：

**痛 1：结构体当哈希键 → 编译报错。**
- `aggregateHasher::hash`（:80-126）处理 `class` 的兜底是 `visit_members`（:120-125），硬断言见 ``Reflection.h:350`：`static_assert(has_type_desc<objType> || is_structured_binding_capable<objType> || managed_wrapper...)`。**只要类型不是 `CA_REFLECTION` 标注、不是聚合体/元组 → 编译失败**。
- `hash_one` 还要求 `is_trivially_copyable`（:65），带 `std::vector` 等成员的非平凡类型再叠加一层失败。
- → 反射/结构化绑定 CodeGen 后，这个断言自动被满足，任何反射类型都能当键。

**痛 2：碰撞 / 把不同值当同一个值。** 两层：
- **`HashObj::operator==` 比的是哈希而非值**（:258-261）：`return m_HashValue == b.m_HashValue;`；`operator<=>` 在 `SHA256` 模式直接比 SHA 哈希（:244-247）。→ **相等=哈希相等**，哈希一碰撞即误判相等。
- **`is_trivially_copyable` 路径对对象内存 raw-bytes 哈希**（:86-90，`hash_one→hash_raw(&object, sizeof)`）：
  - 含 **padding 字节** → 同逻辑值两次构造 padding 不同 → 哈希不同 → 插进去的键找不到；
  - 含**浮点值表示** → `-0.0`/`+0.0` 语义相等但二进制不同；
  - **布局/编译器相关** → 换个编译选项哈希就变。
- → 这正是"明明同一个值却找不到，或不同值却被当成同一个"且极难 debug 的根因。

## 2. 核心洞见：哈希与相等 = 类型表上同一字段遍历的两个函数

类型表（统一框架核心）已有每个字段的 `name/offset/type/kind/attributes`。**哈希**与**相等**本质都是在同一批字段上按序遍历的两个输出。所以从同一张表，再发一个 `Gen.Hash+Equality`：

```
带标注 C++ 类型
   │
   └─▶ 类型表（字段 name/type/kind/attributes）
         ├─▶ Gen.Reflection        (002)
         ├─▶ Gen.Bindings          (001)
         ├─▶ Gen.Serialization
         └─▶ Gen.Hash + Gen.Equality   ← 本文件
```

**一个 emitter 同时修掉两个痛**：
1. 任何反射到的类型自动当键 → 不再编译报错（`hashable` 概念自动满足）。
2. 哈希 = 值语义成员遍历；相等 = 同一遍历 → **由构造保证哈希与相等一致**、且 **padding 无关 / 布局无关 / 浮点规范化**。消碰撞 bug 的根本不是"换个 combine"，而是"让相等和哈希描述**同一组语义字段**"，永不错位。

## 3. 生成产物（before / after）

**现在**：`is_trivially_copyable` → `hash_one(sizeof bytes)`；否则 `visit_members` 反射/结构化绑定开路，且 `HashObj` 缺值相等。

**CodeGen 生成**（插入现有 `custom_hash_trait` 预留槽，Hasher.h:20-30 / :91-94）：
```cpp
// 生成的 MyType_Hash.cpp
template<> struct careflection::custom_hash_trait<MyReflectedStruct> {
  static constexpr void hash(MyReflectedStruct const& o, auto& hasher) {
    // 字段顺序 + 值语义 + padding/布局无关
    hasher.hash_semantic(o.id);                 // 整数按值
    hasher.hash_semantic(o.weight);             // float 归一(-0.0==0.0, NaN→常量)
    hasher.hash_semantic(o.vertices);           // 容器: 长度 + 逐元素
    hasher.hash_semantic(o.mode);               // enum → 规范底层值
  }
};
// 生成的 operator== 走完全一样的遍历 + 同一归一 → 哈希与相等天然一致
```

## 4. 修掉两个痛的机制

- **一致性递归**：每个字段调「该字段类型的语义哈希」，而非对字段 raw-bytes——否则字段是嵌套 POD 时又把 padding bug 带回来。生成器为每个反射类型发一个 first-class 语义 `hash()`，combine 只是按字段序拼接、全链路禁 raw-bytes 兜底（除非显式 `[careflection(hash=raw)]`）。
- **值语义归一**：整数按值、浮点先归一（-0.0==0.0、NaN→固定常量）、enum 取规范底层值、bool 编 0/1、容器 = 长度 + 逐元素、指针按 pointee 语义（shared_ptr 哈希其指向内容；raw 指针才哈希地址）。
- **相等与哈希同源**：生成的 `operator==`/`operator<=>` 与哈希用同一字段遍历 + 同归一 → -0.0==0.0、NaN==NaN 一致，杜绝"相等=哈希相等"的隐患。

## 5. 接入槽位（改动小）

引擎无需大改：`aggregateHasher::hash` 的类类型路径已经会走 `custom_hash_trait<objType>::hash`（:91-94）。生成器只需：为每个反射类型**特化该 trait + 生成值相等的 `operator==`/`operator<=>`**。默认为生成的键使用 `FullCompare`（值相等），取代 `SHA256`（哈希相等）——单点撤掉那个隐患。

## 6. 诚实的代价 / 注意

- **hot POD**：成员语义哈希通常略慢于 raw-bytes；对确定无 padding/无 float/无指针的 POD，留 `[careflection(hash=raw)]` 逃生舱，默认走正确的成员语义路径。
- **跨进程/持久化**：要稳定跨构建 & 跨运行，需在成员语义摘要外用强 finalizer（SHA256）；进程内 `unordered_map` 用 FNV/DIY 成员组合足够。
- **constexpr**：生成哈希可折叠为编译期常量（匹配现有 `constexpr_fnvla_64_Hash` 默认）。
- **AOT / 热重载无关**：哈希是纯 C++，不碰 C# AOT 约束。

## 7. 未决问题（open questions）

1. **哪些字段可哈希 / 需跳过**：不可哈希字段（opaque 句柄、私有状态、指针含地址语义）需 `[careflection(hash=skip)]` 显式标记；默认策略（hash 所有可见字段 vs 显式允许）需定。
2. **归一化范围**：float 归一（-0.0/NaN）是否默认全开，还是仅对"语义 float"字段？（影响热路径小结构体。）
3. **是否替换现有 `HashObj` 语义**：默认把 `HashObj` 从"相等=哈希相等"迁移到"值相等（FullCompare）"是否波及现有依赖 `SHA256` 语义的代码（引擎里已有 `HashObj` 用于管线状态/viewport 键）。
4. **与 `is_trivially_copyable` 快路径的取舍**：生成哈希是否默认禁用 raw-bytes 路径，还是保留一个"确认无 padding 才 raw"的启发式；如何在不破坏现有性能假设前提下迁移。

## 8. 升级建议

- **当前未达 proposal 水位**（parser 元数据来源未定 + `CA_PRIVATE_REFLECTION` 未核实，见 [[002]] §8.1；归一化/替换 `HashObj` 语义待定）。
- 与 [[001]]/[[002]] 同前置。作为**同一份 "engine CodeGen 框架" proposal**的一个 facet（第 4 个 emitter：Gen.Hash+Equality），共享 parser + 类型表 + 构建接入。
- 拆分与 task 标准（含"过大/不可测"修正、伪前提）见 [[004-codegen-proposal-decomposition.md]]。
