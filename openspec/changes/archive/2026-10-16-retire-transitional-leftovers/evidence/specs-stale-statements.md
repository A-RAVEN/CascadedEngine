# 三份 delta 里的旧设施符号：逐行分类（`retire-transitional-leftovers` 任务 7.1）

判据面只取本 change 改动的三份 delta（`openspec/changes/retire-transitional-leftovers/specs/`）——
主规格侧的同一份清单属于 4.5（要等归档合并后才成立，现在跑必然是红的）。

```powershell
$sym = 'CA_REFLECTION|CATypeDescriptor|cacore::serializer|cacore::deserializer|visit_members|aggregateHasher'
git grep -n --untracked -E $sym -- 'openspec/changes/retire-transitional-leftovers/specs/'
```

**命中行数 = 8**，下表 **8 行**——行数相等，判据通过。只允许两类：**防回流守卫** 与 **历史理由**。

| # | 文件:行 | 命中原文（逐字，截断） | 分类 | 判它属于这一类的依据 |
|---|---|---|---|---|
| 1 | `codegen-framework/spec.md:3` | `### Requirement: 保留 CA_REFLECTION 运行时语义` | 历史理由 | 它在 `## REMOVED Requirements` 段里，是"要删哪一条"的指针；归档后整条消失 |
| 2 | `codegen-framework/spec.md:4` | `**Reason**: 旧反射路径已随 … 那三个路径在文件系统上不存在，`CA_REFLECTION` 与 `visit_members` 在源码树里各 **0 处**…` | 历史理由 | 过去时陈述 + 实测读数，说明**为什么删**；不是要求别人照做的事 |
| 3 | `codegen-framework/spec.md:10` | `旧的一致校验对象（`CATypeDescriptor<T>` 与它的 `member_count`）已随旧头删除，MUST NOT 再作为**校验对象**出现在任何要求或实现里；…守卫符号表与解释这段历史的注释句除外` | 防回流守卫 | 一句 `MUST NOT`：禁止把旧校验对象请回来，并点名唯一允许的例外 |
| 4 | `codegen-framework/spec.md:70` | `…旧运行时"不清空目标容器"的那条依据已随 `cacore::deserializer` 删除。` | 历史理由 | 过去时：解释这条场景的**依据**换了，现状由前一句（`hasClear` 分支）给出 |
| 5 | `codegen-runtime/spec.md:7` | `生成物 MUST NOT 引用 `cacore::serializer` / `cacore::deserializer`。…这一条是**防回流**的守卫` | 防回流守卫 | 规范句 `MUST NOT`，且明写自己的职责是防回流 |
| 6 | `codegen-runtime/spec.md:17` | `旧运行时已删除，跑不出"与旧方案（`cacore::serializer` 逐成员写）对拍"，对照物因此改成入库的字面量` | 历史理由 | 过去时：解释对照物为什么换成冻结字面量 |
| 7 | `codegen-type-migration/spec.md:4` | `**Reason**: 该要求描述的是**过渡期**形态：…老宏留在消费方 `.cpp` 里提供 `CATypeDescriptor`…迁移已经结束` | 历史理由 | 它在 `## REMOVED Requirements` 段里，说明**为什么删这条** |
| 8 | `codegen-type-migration/spec.md:28` | `…旧头里的 `cacore::has_custom_hash_func` 与 `cacore::aggregateHasher<>` 已随旧头删除，判据 MUST 写成今天存在的符号` | 防回流守卫 | 一句 `MUST`：判据只许用今天存在的符号，禁止再引旧符号当判据 |

**分布**：防回流守卫 **3** 条（#3 / #5 / #8），历史理由 **5** 条（#1 / #2 / #4 / #6 / #7），**没有第三类**。

**没有"现在时"的旧设施陈述**：8 条里每一条要么是 `MUST` / `MUST NOT` 的防回流约束，要么是解释历史的过去时句子；
没有任何一条把旧设施说成"还在用"。
