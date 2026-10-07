## ADDED Requirements

### Requirement: 被解析门拒收的键类型必须迁进接入清单

因为"解析环境过不了保真门"而只能手写 `cacore::codegen::custom_hash_trait` 的键类型，MUST 在把解析链上的诊断清零之后迁进 `build.py` 的接入清单（`CODEGEN_ANNOTATED`）；MUST NOT 通过放松保真门来达成 —— 不得允许主头自身带错误级诊断、不得为任何头开诊断白名单、不得抬高 `too many errors emitted` 那个上限、也不得往推导环境里塞 `-ferror-limit` 之类抬高上限或把错误降级的开关。本次纳入范围（2026-10-05 实测，四个都在同一条解析链上）：`graphics_backend::ViewRectData`（`Interface/RenderInterface/header/GPUGraph.h`）、`graphics_backend::GPUPipelineStateKey`、`graphics_backend::VertexInputBindingData`、`graphics_backend::VertexInputBindingData::InputElementDesc`（后三个在 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`，最后一个是嵌套类型）。迁一个类型的前提是它先被标注（`CA_REFLECT(serializable)`，嵌套类型写在类体内）—— 工具只对带标注的类型出码：文件里一条标注都没有时它打印 `no annotated types (empty whitelist)` 并 exit 0 **且零产物**，文件里有别的标注而目标类型没标时是退出码 4，两种都不产出该类型的产物，所以"exit 0"本身不能当通过判据。每个类型 MUST 交出五样：① 类型上的标注（写在类型定义处，MUST NOT 写在被解析头之外的 `.cpp` 里）；② 接入清单里的行，且构建期三个发射器退出码 0 **并写出 3 份产物**（"退出码 0 且零产物"是标注丢了，不算通过）；③ 生成物被**真实消费它的编译单元** include 并断言的证据（字段表 + `has_custom_hash_func` 断言，落点必须是该类型真正被用的那个模块，不是随便一个断言文件）；④ 手写 `custom_hash_trait` 特化被删除，且删除前后**经既有入口**（`cacore::hash<T>` 或 `HashObj<T>::GetHash()`）对同一对象取到的哈希值逐个相等；⑤ 填充字节不变性：同一个对象造两份、只把填充字节改成不同值，经既有入口取到的哈希必须相等，而"整块按字节"口径下两个哈希必须不同（反向对照，证明这条判据不是恒真）。含浮点成员或定长数组成员的类型 MUST 另外交出"值会变"的读数与处置理由：手写特化对浮点走"平凡可拷贝 → 按字节"、**不归一化**，而生成代码会先把 `-0.0` 归一成 `+0.0`、把 NaN 归一成一个稳定值；手写特化对定长数组成员走"整块按字节"，生成代码走"先写个数、再逐个元素"。MUST NOT 拿 `cacore::dispatch_branch_unchanged<T>()` 当"哈希已生效"的判据 —— 它对"既平凡可拷贝、又带自定义特化"的类型（`ViewRectData` 就是）恒为假，那正是它的语义。

#### Scenario: 四个类型的三个发射器都是 0

- **WHEN** 跑 `python build.py --config Debug`
- **THEN** 这四行在 codegen 段各自 `--emit-all` 退出码 0，产物落在构建目录的 `codegen/<归属目录>/` 下；
  **改动之前实测为红**：四个类型都以 exit 6 被拒（`ViewRectData` 是主头自身 5 条诊断，
  另外三个所在的头是链上错误打满 20 条上限）

#### Scenario: 解析链不再被保真门拒收

- **WHEN** 用推导环境对 `Interface/RenderInterface/header/GPUGraph.h` 与
  `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 跑工具
- **THEN** 退出码不再是 6；链上不再出现 `too many errors emitted, stopping now`；
  `GPUGraph.h` 的"主头自身错误级诊断"计数为 0（**改动之前实测为 5**）

#### Scenario: 哈希值逐个不变

- **WHEN** 对这四个类型的同一个实例，分别用迁移前的手写特化与迁移后的生成物求哈希
- **THEN** 值相等；期望值 MUST 先在迁移前实测得到、再写进断言，不许手算；
  反向对照 MUST 存在：取一个含 `-0.0f` 浮点成员的对象，两个口径算出的哈希值不同 ——
  这条对照用来证明守卫真的会红，而不是永远绿

#### Scenario: 不许拿放松判据换取通过

- **WHEN** 复核本次改动
- **THEN** `Tools/CodeGen/CodeGenMain.cpp` 里那句
  `bool untrustworthy() const { return !fatalMsg.empty() || inHeader > 0; }` 一个字符未改；
  接入清单与工具里都没有为任何头开诊断白名单的分支
