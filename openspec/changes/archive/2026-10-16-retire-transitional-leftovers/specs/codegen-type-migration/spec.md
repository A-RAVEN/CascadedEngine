## REMOVED Requirements

### Requirement: 已有类型的迁移形态是双份描述
**Reason**: 该要求描述的是**过渡期**形态：头文件里只放新标记，老宏留在消费方 `.cpp` 里提供 `CATypeDescriptor` 供 ABI 守卫跨比对字段数。迁移已经结束 —— `CA_REFLECTION` 与 `CATypeDescriptor` 在源码树里各 **0 处**，`CACore/header/Reflection.h` 在文件系统上不存在（2026-10-15 实测），而该要求自己的收尾口径写的就是"白名单必须为 0 行"。把它留在规格里，下一个读的人会以为现在还需要"一个类型写两处"。
**Migration**: 没有待迁对象。ABI 守卫的现状由 `codegen-framework` 的「生成源码以显式源清单纳入构建」描述（守卫只断言生成描述符的类型名非空；字段数 / 字段名 / offset 由消费单元的断言文件对着**写死的期望值**比）；"旧符号零引用"由本规格的「不在构建内的模块不纳入本次迁移」之外的零引用判据与 `codegen-runtime` 的防回流要求共同覆盖。

## MODIFIED Requirements

### Requirement: 私有成员与指针成员两类形状的处置必须写在明处

- 含**非 public 成员**的类型：MUST 在类体里写一行 `CA_CODEGEN_FRIEND`（新系统的私有成员友元入口；旧系统里那个等价的宏 `CA_PRIVATE_REFLECTION` 已随旧头删除），否则工具继续拒绝；这条 MUST 写进 `Tools/CodeGen/README.md` 的限制一节。
- 含**裸指针 / 托管指针成员**的类型：生成期拒绝保持；MUST 在 README 里写明替代做法（把要落盘的部分提取成值语义结构，句柄写名字或索引，而不是写进程内地址）。

#### Scenario: 私有成员类型的放行路径可核

- **WHEN** 对一个私有成员类型加一行 `CA_CODEGEN_FRIEND` 后请求序列化
- **THEN** 出码成功；不加时仍然拒绝（退出码 5）。两个方向都要有读数

#### Scenario: 指针成员类型不许静默丢数据

- **WHEN** 对一个含 `castl::vector<int*>` 或 `shared_ptr` 成员的类型请求序列化
- **THEN** 工具非零退出并点名成员；MUST NOT 生成"写了但读不回来"的代码

### Requirement: 迁移必须逐类型可核，且不得留下静默不生效的钩子

每迁一个类型，MUST 交出三样：① 该类型进接入清单后三个发射器都为 0 的读数；② 生成物被**真实目标的编译单元** include 并断言的证据；③ 新旧两套写同一对象字节相同的对拍。**"不适用"不是逃生口**：说某类型不适用第③样，MUST 同时交出它的形状读数（同一类型在探针里被拒的退出码 + stderr 点名的那一行），否则按未完成算。

MUST NOT 出现"迁移完成、但生成的特化在实际调用点不生效"的状态（例如哈希分派顺序没改就宣称哈希已迁移）。判断"生效"的地方 MUST 是**既有调用点**，不是只在测试里直接调生成的特化：至少点名两个跨模块的真实调用点（例如 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 里的 `HashObj<CPipelineStateObject>`），在其所在编译单元里断言该类型的特化可见（`cacore::codegen::has_custom_hash_func<该类型, cacore::codegen::hasher<>>` 为真 —— 旧头里的 `cacore::has_custom_hash_func` 与 `cacore::aggregateHasher<>` 已随旧头删除，判据 MUST 写成今天存在的符号），并做"填充字节改成不同值后哈希不变"的断言；只在夹具上绿不算完成。

#### Scenario: 类型迁移的验收三件套

- **WHEN** 完成一个类型的迁移
- **THEN** 上面三样证据都在 change 的 tasks 记录里；缺任一条即未完成

#### Scenario: 调用点真的用上了新特化

- **WHEN** 某个已迁移类型在既有调用点被求哈希（例如 `HashObj<CPipelineStateObject>`）
- **THEN** 实际走的是生成的特化；判据是拿一个填充字节被故意改成不同值的对象求哈希，结果不变（按字段算）；若结果变了说明还在按整块字节算（红）
