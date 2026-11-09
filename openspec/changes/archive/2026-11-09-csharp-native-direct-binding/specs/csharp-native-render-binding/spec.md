# csharp-native-render-binding 增量

## ADDED Requirements

### Requirement: 原生渲染接口是唯一绑定来源，绑定面以重跑快照为准
进入 C# 的渲染绑定面 MUST 由原生接口本体承载：`CRenderBackend`、`GPUGraph`、`ShaderStruct`、`GPUTexture`、`WindowHandle` MUST 挂无参数 `CA_BIND()` 进登记表；`ImageHandle`/`BufferHandle` MUST 挂 `CA_BIND_VALUE` 作过界值类；`ExecuteGraph` 的调度器裸指针参数 MUST 经 `CA_BIND_INJECT` 由宿主钩子提供，公开表面 MUST NOT 出现 `TaskScheduler*`。锁文件 host 段换面 MUST 按 design D10 两段程序：2.3 只经 `--update-ids` 把五个原生接口追加在桥行之后（8–12，零手工数字编辑），5.1 与桥删除同一执行轮删桥 3–7 行并把原生 8–12 重编号 3–7——任何构建可见状态 host 段从 1 起连续不跳号、不重号，MUST NOT 出现构建可见的空槽。绑定面与排除面 MUST 以字节窗投影（design D2）落地后**重跑的 `--parse-report` 快照**为准，并 MUST 通过与独立文本清单的逐条对账（0 差异）——MUST NOT 以手抄清单或前批快照定面。
#### Scenario: 绑定面与重跑快照逐条一致
- **WHEN** 四类原生挂标注后重跑快照与独立对账
- **THEN** 对账 MUST 为 0 差异；任何方法进表/被排除与两侧清单不符 MUST 非零点名具体方法；`GPUGraph` 链式构件 MUST 出现在采集侧且托管表面可链

#### Scenario: 退役后桥符号残留为零
- **WHEN** 桥退役完成后全仓搜索 `IScriptRender`、`IScriptShaderStruct`、`IScriptTexture`、`CScript`（宿主脚本入口接口 `IScriptHost` 明确不在退役面，命中它不算残留；`openspec/changes/archive/` 与 `docs/` 历史叙述行除外）
- **THEN** MUST 为 0 命中；任何残留 MUST 列出文件与行交主循环核，收口前 MUST 逐条清零

### Requirement: 数据上传通道在原生面等价存在
`GPUGraph::ScheduleData` 系 `const void* + 相邻 uint64_t 长度` 的内存窗参数 MUST 经字节窗投影绑定：托管表面收 `byte[]`，thunk 以固定内存传指针与长度，其余参数（偏移、复制标志）原样过界；配对不唯一或长度参数缺失 MUST 生成期点名而非猜测。退役桥后 C# MUST 仍能完成"字节上传→采样/呈现→真实像素验收"，与桥时代行为逐用例对齐（`csharp-rendering` 字节复制与像素验收条不降级）。
#### Scenario: 上传像素验收两后端全过
- **WHEN** 改写后的 C# 用例经原生面上传缓冲/纹理字节并提交
- **THEN** 两后端 MUST 通过既有真实像素断言；上传实现收到与 `byte[]` 逐字节一致的内容

#### Scenario: 配对缺失生成期点名
- **WHEN** 演练：把夹具内存窗方法的长度参数改名/删掉使其无法唯一配对
- **THEN** 生成器 MUST 非零点名该方法并说明配对判据；MUST NOT 产出一个把裸指针暴露给托管的半成品表面

### Requirement: 桥方法退役对账表逐条可溯源
退役前 MUST 产出桥 28 个纯虚方法（`ScriptRenderBridge.h` 共 33 个 `virtual` 行、5 个虚析构按可绑审计口径不计入）到原生面等价物或"无消费可删"声明的逐条对账表（消费依据=托管侧实测引用点），入本 change `evidence/`。任一桥方法既无原生等价物、又被脚本或测试消费时 MUST 判功能回退，禁止收口。提交前置校验防线（无效图/参数不完整/缓冲未登记/纹理采样器未绑/越界与未知字段拒绝）MUST 迁到托管糖层并保持"拒绝时原生执行调用计数增量 0、点名具体字段/缓冲/纹理名"的既有语义；元数据查询 MUST 经可序列化记录的两段式投影供托管校验使用，返回空指针 MUST 映射为托管 `null`。
#### Scenario: 对账表逐条核销
- **WHEN** 收口前逐行核对账表
- **THEN** 每行 MUST 指认原生等价物（接口::方法级）或"0 消费"证据；任何"未迁移且被消费"行 MUST 让对应收口失败

#### Scenario: 迁移后的拒绝防线仍可红
- **WHEN** 托管层提交一张含未 `SetBuffer` 元数据字段的参数结构的图
- **THEN** 托管前置校验 MUST 在进入函数表调用前拒绝并点名字段与缓冲名，原生执行入口计数增量 MUST 为 0；撤掉该校验实现后同一用例 MUST 变红（证明判据非虚设）
