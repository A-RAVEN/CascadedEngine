# codegen-type-migration Specification

## Purpose
把引擎已有类型从老宏迁到 CodeGen 的过程规范：嵌套类型必须能被标注、私有成员与指针成员两类形状的处置必须写在明处、迁移必须逐类型可核且不得留下静默不生效的钩子、不在构建内的模块不纳入迁移范围、D3D12 模块的推导环境必须能解析它自己的头，以及被解析门拒收的键类型如何迁进接入清单。
## Requirements
### Requirement: 嵌套类型必须能被标注

引擎里存在被老宏标注的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）。工具 MUST 支持标注这类类型；工具对类型名的解析口径（按最后一个非限定名，还是接受全名）MUST 在 `Tools/CodeGen/README.md` 里写明，并与命令行、接入清单的写法一致。

#### Scenario: 嵌套类型能出码

- **WHEN** 对一个嵌套类型请求三个发射器
- **THEN** 三个都成功（现状是"类型未标注"，退出码 4 —— 判据在改动前为红）

#### Scenario: 文档与工具口径一致

- **WHEN** 按 README 里写的类型名写法（全名或短名）逐条跑文档里的命令
- **THEN** 全部成功；`verify_readme.py` 报 0 处不符

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

