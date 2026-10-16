# 归档后照单复核（`retire-transitional-leftovers` 任务 4.5）

归档那一步由用户点头，不在本 change 内执行。归档之后，下面这几条要在**主规格**上成立 —— 那时 delta
已经合并进 `openspec/specs/`，本 change 想收回的过渡期陈述才算真的收回去了。

**⚠ 归档之前这张单子必然是红的**：本 change 改的是 delta，主规格要等归档才被覆盖。别拿"现在还红"
当成本 change 没做干净的证据。

| # | 判据（命令） | 归档后的期望 |
|---|---|---|
| 1 | `git grep -n -F '保留 CA_REFLECTION 运行时语义' -- openspec/specs/` | **0 处**（该要求整条删除） |
| 2 | `git grep -n -F 'MUST 采用"头文件里只放新标记、老宏留在消费方' -- openspec/specs/`（或直接搜 `双份描述`） | **0 处**（该要求整条删除） |
| 3 | `git grep -n -F 'cacore::aggregateHasher' -- openspec/specs/` | **0 处**（判据换成 `cacore::codegen::hasher<>`） |
| 4 | `git grep -n -F 'CA_PRIVATE_REFLECTION' -- openspec/specs/` | **0 处**（换成 `CA_CODEGEN_FRIEND`） |
| 5 | `git grep -n -F 'CA_REFLECTION' -- openspec/specs/` | 只允许出现在**防回流守卫**里：`codegen-runtime/spec.md` 里那条守卫要求的符号表（它必须列这个名字，否则守卫失效） |
| 6 | `git grep -n -F 'CATypeDescriptor' -- openspec/specs/` | 只允许出现在同一处守卫符号表 |
| 7 | `git grep -n -F '元素是内含无序容器的结构体时必须拒绝' -- openspec/specs/` | **0 处**（那条场景已被"逐层交给元素自己"替换） |
| 8 | `git grep -n -F 'Update Purpose after archive' -- openspec/specs/` | **0 处**（六份 Purpose 已在 tasks 4.1 里就地补上） |
| 9 | `openspec validate --all`（或逐份） | 全部 valid |

第 5、6 条的口径与 7.1 的分类表一致：**守卫符号表是唯一允许的命中**，它的职责就是列出这些名字；
除它以外的任何"现在时"陈述都算没清干净。
