## ADDED Requirements

### Requirement: 已有类型的迁移形态是双份描述

把一个已有类型从老宏迁到 CodeGen 时，**过渡期** MUST 采用"头文件里只放新标记、老宏留在消费方 `.cpp`"的形态：新标记供工具解析并出码，老宏提供 `CATypeDescriptor` 供 ABI 守卫跨比对字段数。**这是过渡形态，不是终态** —— 见下面的收尾口径。

同一个文件里 MUST NOT 同时出现老宏与新标记 —— 实测那样工具会走只支持反射的文本通道，序列化与哈希一律非零退出。

**收尾口径（2026-10-04 修正）**：原来打算给"消费方 `.cpp` 里为 ABI 守卫保留的老宏"留一份逐行白名单（`命中行数 == 白名单行数`）。实测这条留不住：那个宏（`CA_REFLECTION`）与它建出来的 `CATypeDescriptor` 都定义在 `CACore/header/Reflection.h` 里，而本 change 要求该头从文件系统上消失；头一删，消费方那几行就编译不过。所以终态口径是：**白名单必须为 0 行**，消费单元的 ABI 断言改成"与写死的期望值比"（字段数、字段名、每个字段的 `offsetof` 逐个断言），`CodeGenMeta.h` 的 `static_assert_abi_consistency<T>()` 里"与老描述符比字段数"的那一支 MUST 删除。收尾时必须同步改基线 `openspec/specs/codegen-framework/spec.md` 里对应的要求，不能只在 change 工件里说。

#### Scenario: 终态不许留豁免

- **WHEN** 在旧头删除之后，任何引擎源码或生成物里再出现老宏 / `CATypeDescriptor` / `has_type_desc`
- **THEN** 零引用判据红（白名单为 0 行，命中一行即红），必须先把那处迁掉

#### Scenario: 迁移后 ABI 守卫仍然生效

- **WHEN** 把生成表里的字段数改成与写死的期望值不一致（或漏掉某个字段）
- **THEN** 构建失败（守卫挡住）；守卫 MUST NOT 因为"头里没有老宏"而静默失效

#### Scenario: 同文件并存被拒绝

- **WHEN** 同一个头文件里既有老宏又有新标记，对该类型请求序列化
- **THEN** 工具非零退出并说明它走的是只支持反射的通道（实测退出码 8）—— 这是"必须分文件"的判据

### Requirement: 嵌套类型必须能被标注

引擎里存在被老宏标注的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）。工具 MUST 支持标注这类类型；工具对类型名的解析口径（按最后一个非限定名，还是接受全名）MUST 在 `Tools/CodeGen/README.md` 里写明，并与命令行、接入清单的写法一致。

#### Scenario: 嵌套类型能出码

- **WHEN** 对一个嵌套类型请求三个发射器
- **THEN** 三个都成功（现状是"类型未标注"，退出码 4 —— 判据在改动前为红）

#### Scenario: 文档与工具口径一致

- **WHEN** 按 README 里写的类型名写法（全名或短名）逐条跑文档里的命令
- **THEN** 全部成功；`verify_readme.py` 报 0 处不符

### Requirement: 私有成员与指针成员两类形状的处置必须写在明处

- 含**非 public 成员**的类型：MUST 提供与老系统 `CA_PRIVATE_REFLECTION` 等价的 friend 入口（类型自己加一行），否则工具继续拒绝；这条 MUST 写进 `Tools/CodeGen/README.md` 的限制一节。
- 含**裸指针 / 托管指针成员**的类型：生成期拒绝保持；MUST 在 README 里写明替代做法（把要落盘的部分提取成值语义结构，句柄写名字或索引，而不是写进程内地址）。

#### Scenario: 私有成员类型的放行路径可核

- **WHEN** 对一个私有成员类型加一行 friend 入口后请求序列化
- **THEN** 出码成功；不加时仍然拒绝（退出码 5）。两个方向都要有读数

#### Scenario: 指针成员类型不许静默丢数据

- **WHEN** 对一个含 `castl::vector<int*>` 或 `shared_ptr` 成员的类型请求序列化
- **THEN** 工具非零退出并点名成员；MUST NOT 生成"写了但读不回来"的代码

### Requirement: 迁移必须逐类型可核，且不得留下静默不生效的钩子

每迁一个类型，MUST 交出三样：① 该类型进接入清单后三个发射器都为 0 的读数；② 生成物被**真实目标的编译单元** include 并断言的证据；③ 新旧两套写同一对象字节相同的对拍。**"不适用"不是逃生口**：说某类型不适用第③样，MUST 同时交出它的形状读数（同一类型在探针里被拒的退出码 + stderr 点名的那一行），否则按未完成算。

MUST NOT 出现"迁移完成、但生成的特化在实际调用点不生效"的状态（例如哈希分派顺序没改就宣称哈希已迁移）。判断"生效"的地方 MUST 是**既有调用点**，不是只在测试里直接调生成的特化：至少点名两个跨模块的真实调用点（例如 `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 里的 `HashObj<CPipelineStateObject>`），在其所在编译单元里断言该类型的特化可见（`cacore::has_custom_hash_func<该类型, cacore::aggregateHasher<>>` 为真），并做"填充字节改成不同值后哈希不变"的断言；只在夹具上绿不算完成。

#### Scenario: 类型迁移的验收三件套

- **WHEN** 完成一个类型的迁移
- **THEN** 上面三样证据都在 change 的 tasks 记录里；缺任一条即未完成

#### Scenario: 调用点真的用上了新特化

- **WHEN** 某个已迁移类型在既有调用点被求哈希（例如 `HashObj<CPipelineStateObject>`）
- **THEN** 实际走的是生成的特化；判据是拿一个填充字节被故意改成不同值的对象求哈希，结果不变（按字段算）；若结果变了说明还在按整块字节算（红）

### Requirement: 不在构建内的模块不纳入本次迁移

模块根在 `compile_commands.json` 里没有编译单元的（实测：`CAResource`、旧 `VulkanRenderBackend`、`Test/VulkanRendererBackendTester`）MUST NOT 计入本 change 的迁移范围：既无法测量，也没有真实编译单元能消费生成物。

#### Scenario: 范围有据可查

- **WHEN** 复核迁移范围
- **THEN** 每个纳入范围的模块根都能给出"该根有 N 个编译单元"的读数；0 个的模块 MUST 在 design 或 tasks 里被显式排除

### Requirement: D3D12 模块的推导环境必须能解析它自己的头

`build.py` 为 `D3D12RenderBackend` 推导出的编译环境 MUST 能解析该模块自己的头（现状实测：`D3D12RenderBackend/private/D3D12Includes.h` 里 `directx/d3dx12.h` 找不到，退出码 6）。补齐后 MUST 有"该模块的头能被工具解析"的读数。

#### Scenario: 解析该模块的头不再失败

- **WHEN** 用推导环境对 `D3D12RenderBackend/private/D3D12Includes.h`（该文件内部没有老宏标注，所以会真正走 libclang 解析与诊断检查）请求反射
- **THEN** 退出码 0；**改动之前实测为 6**，stderr 逐字含 `unresolved include (fatal): D3D12RenderBackend/private/D3D12Includes.h:11:10: fatal error: 'directx/d3dx12.h' file not found`
- **反例（原判据写错的地方）**：不要拿 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 当这条的红态 —— 它自己在文件末尾带老宏标注，工具会走只支持反射的文本通道并直接 `exit 0`，解析是否可信根本没被检查（已实测）
