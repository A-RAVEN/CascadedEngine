# Design: codegen-native-binding-prereqs

## Context

现状（全部为 2026-11-05 只读实测，出处为文件+逐字引文，归档 evidence 复核）：

- 发射器采集规则：`Tools/CodeGen/CodeGenMain.cpp` 的 `collectMethods` 对 `clang_CXXMethod_isPureVirtual(cursor) == 0u` 直接跳过——非纯虚与非虚公开方法**不存在进表路径**。
- 原生面的方法形状恰恰以非虚为主：`GPUGraph.h` 全文件 `virtual` 命中 0，13 个建图构件是 `inline GPUGraph& AddPass(RenderPass const&)` 这类返回自身引用的非虚成员；`CRenderBackend.h` 15 个 virtual 中 3 个带实现体；`ShaderStruct` 公开写入面是 `template<typename T> ShaderStruct& SetValue(...)`（模板+自返回）。
- 值类障碍：`ShaderResourceHandle.h` 注释逐字——"这两个句柄是**哈希键**（`std::unordered_map<BufferHandle, …>` 之类），而它们**不能进生成表**：/ 两个类各有一个 `castl::shared_ptr` 成员，那是工具明令拒绝的形状（托管指针成员，退出码 15）"。注意"没有 `CA_REFLECT`、也不该有"是转述非原文；且退出码 15 属类型发射器的拒绝路径——绑定发射器遇到该形状走自己的拒绝文本，红态以实际输出留档、不预设退出码。
- 序列化块：`Tools/CodeGen/README.md` 十三节逐字"统一规则：**元素里含 `string` / 容器 / 指针 ⇒ 一律降到序列化块**，并在生成期日志里逐条说明"，同文件限制清单逐字"不支持……通用序列化块"——**规则在生成器文档、生效规范尚无条文（现行要求是含容器成员一律拒绝）、实现缺席**。本批把它立为规范（specs 增量 MODIFIED"嵌套记录"+ADDED 序列化块），措辞不得称其为"生效规范的既有统一规则"。
- 编号现状：上一批（`render-interface-merge`，15/15 未归档）把编号做成了 `CA_BIND(编号)` 标注参数；用户 2026-11-05 裁定"这种要用户自己手写意义不明的 ID 的东西就是狗屎"（ROADMAP 存档原文，含 ID 前后两空格），方向改锁文件（已记"优先级最高"）。
- 分片文件已是现成形状：`out/build/x64-debug/codegen/bindings/render.registry.part` 每行 `编号⇥短名⇥完整限定名⇥头路径`——锁文件沿用这个行形状，不发明新格式。
- 牵动的既有锚：`check_bindings.py` 手抄 `EXPECTED_BINDING_NUMBERS`/`FIXTURE_BINDING_NUMBERS`；`CSharpHost/README.md` 编号表；测试 `Test/GPUBackendTester/Main.cpp` 里硬编码 `GetInterfaceInstance(3, nullptr)` 与"未知接口编号 9"负例（`GetInterfaceApiForTest(9u, ...)`）——编号的**值**都不许变。

约束：构建唯一合法路径 `python build.py --config Debug`（本批全程 Debug 预设）；数字必须可溯源，禁新造魔数；红线测试（16 用例+四探针）行为不许变。

## Goals / Non-Goals

**Goals:**

- 人零手写编号：`CA_BIND` 无参数成为唯一合法标注；锁文件承载编号历史；`--update-ids` 是唯一分配途径；换源后所有既有产物逐字节不变。
- 发射器具备直绑原生面所需的最小形态集：非虚/非纯虚采集、自返回 void 化、静态工厂、构造 `Create`、重载序数槽、资源引用值类、序列化块记录、② 类参数注入。
- 真实头四份的"投影快照清单"入库并对账——它是直绑批的开工地图，也是"能力补齐了"的判据载体。

**Non-Goals:**

- 见 proposal 的 Non-goals：不绑原生接口、不退适配层、不加原生方法、不做 span/容器出参/映射容器/模板单态化。

## Decisions

### D1 编号进锁文件而不是"声明顺序自动编号"或"标注带号+工具提示"

声明顺序编号否决：重排头文件或 `#if` 少编一个接口会**悄悄移动编号**；编号出现在 README 编号表、测试构造（`GetInterfaceApiForTest(9u, ...)` 的"未知编号"负例）、历批日志与 evidence 文本里，值漂移没有运行期红可兜。"保留标注带号、撞号时提示下一个空号"否决：用户裁定原文就是不许人填号。锁文件方案：编号历史是**数据**（入库、diff 可评审、工具读写），标注只是**声明**（"这个类要绑"）；构建期核对而非分配，分配只发生在显式 `--update-ids`，从而"构建永不改锁、锁改必有痕"。

### D2 锁文件格式与两种模式

`Tools/CodeGen/interface-ids.lock`，行形状沿用 `*.registry.part`：`编号⇥短名⇥完整限定名⇥头路径`；文件按 `# scope host` / `# scope fixture` 分段；每段尾一条 `# high water <n>` 行记录该段历史最高号。核对模式（默认）四项检查——口径钉死为"段内"（上批判据本就是两张表：主表 `contiguous_from=1`、夹具表 `contiguous_from=8`，不引入全局唯一；宿主段与夹具段各自独立号空间，夹具段永不进宿主路由，"未知编号 9"负例语义不受影响）：段内编号唯一；段内从起点连续（host 从 1、fixture 从 8——沿用现值）；登记表输入的 `CA_BIND` 接口名集合与锁内该段名集合**完全相等**（多一行少一行都点名）；新出现的完整限定名必须先跑 `--update-ids`（构建模式绝不写锁）；任何编号 ≤ 该段 high water 且不在锁内 = 退役号，**MUST NOT 复用**（`--update-ids` 给新名发号时跳过）。`--update-ids` 模式：只加新行（号=max+1 并推 high water）、只删消失名行，既有行号与顺序不动；写完即退，不产绑定。初值逐字迁移现值：1–7 现七接口、8–9 现夹具对。备选"锁放构建目录不入库"否决——不入库就没有评审面也没有跨构建历史。

### D3 采集规则：公开方法全集 + 显式排除

`CA_BIND` 类采集"全部公开成员函数（非模板、非析构、非删除）"，虚与非虚一视同仁；线形（参数与返回）任一处落在未实现形状 ⇒ 要么被 `CA_BIND_SKIP(方法, 理由)` 排除、要么生成器非零点名（沿用生效规范"可投影但尚未实现必须显式排除"的语义）。对既有接口的兼容性由回归锚保证：五个 `IScript*` 与 `IWindow`/`IWindowSystem` 的公开方法集恰等于现表集合——新规则下产物必须逐字节不变（D6）。`protected`/`private` 不采（`ShaderStruct` 的 protected internal 不入表）。静态成员函数与"返回记录值的静态工厂"作为值记录构造通道采集（D5/D8）。

### D4 自引用返回与链式

返回类型为该类的 `T&` / `T const&` ⇒ C ABI 槽返回 `void`；托管表面方法返回**同一表面对象**（C# `return this;` 风格），链式 `graph.Rast(a).Rast(b)` 在 C# 侧保持可读。夹具判据：两连调用 ⇒ 原生实现计数 +2 且序参一致。红构：改造前该形态（非纯虚+自返回）生成器根本不出码。**与既有"丢返回值"判据的互斥必须显式解决**：`Tools/CodeGen/check_bindings.py` 现在把"方法有返回值而 thunk 不写回"判为缺陷（"真实接口桩数 0 / 丢返回值 0"）——自返回 void 化正是"有返回值却不写回"，若不改该判据，本条与它互斥、构建必红。改法：判据豁免"**返回值是该类自身引用**"这一类（识别依据：C++ 返回类型的 cursor 拼写为 `<类名>&`/`<类名> const&` 且形参表无其它出参），非自身引用的有返回值方法仍按"丢返回值"拒绝。**红绿**：豁免未实现时夹具红（判据点名链式方法为"丢返回值"），实现后 0 处——这条豁免本身要能在被改错时变红，不能只靠"链式跑通"间接证明。

### D5 重载序数槽

同名不同签名（不含隐式转换歧义，那种维持 ③ 类拒绝，现有拒绝夹具 `IBindRowOverloadReject` 行为不变）⇒ 第 1 个保持现有槽名（既有产物逐字节锚不受影响），第 2 个起 `_<序数>` 后缀；C# 出同名重载。夹具判据：同名两签都进表、extern 名不重复、分别调用互不串。

### D6 回归锚=产物逐字节

本批开始前（apply 第一步）对 `out/build/x64-debug/codegen/bindings/`（含 `fixture/` 子目录）现有全部产物取 `Get-FileHash` 存 `evidence/基线产物哈希.txt`，**同时记录基线工作区快照**：`git rev-parse HEAD` 加 `git status --porcelain` 的输出——底座批改动未提交，提交号单独摆会假装"那个提交能复现这批产物"（实际复现不出，哈希输入来自工作区未提交态）；两样并记才自洽。基线集合先剔除无源分片：`*.registry.part` 与产物清单里指向已删除头的条目（如底座批演练残留 `drill.registry.part`）在取基线前清掉并留一行记录，防止"指向不存在输入的旧文件"被当成"既有产物"锁进锚。锁文件换源与采集扩围全部落地后重跑构建，基线内产物哈希 MUST 逐一相同（新增夹具产物除外）。任何一字节差异 = 换源改动越界，先回修再推进。这条同时兜住编号值、字段线形、`EngineApi` 表、`Tables` 数组，也承担 D2 说的"换号/调号"检测。对账用**仓库常驻脚本** `Tools/CodeGen/check_binding_baseline.py`（输入=基线文件+产物目录；缺失/多余/不一致非零且点名；自检：篡改基线一行 ⇒ 退出 2"判据自身失效"路径可触发），不许把对账写成一次性口头比较。

### D7 资源引用值类投影

新形状（夹具先行，真实 `ImageHandle`/`BufferHandle` 的直绑在下一批按快照执行）：线形四段 `(uint32_t kind, uint64_t handle, uint8_t const* name, uint64_t nameLen)`——全部由四类线形组合，不新增 C ABI 线形类别。托管侧出静态工厂：`FromTexture(uint handle)` / `FromWindow(uint handle)` / `FromName(string)`；`kind` 投影自值类的类型枚举。C++ 侧 thunk 组装栈上值类：按纹理/窗口构造走既有句柄表反查（复用"shared_ptr 入参重建真实对象"规范条文，同一对象性可证）；按名构造走 `NameHash` 原生构造通道（复用"名称与路径由原生构造语义重建"场景）。无效句柄 ⇒ 错误码 + 实现调用计数 0（红）。值类自身的成员方法**不采集**（只作参数/返回的过界值），避免把哈希键类拖进表面层。

### D8 序列化块记录

成员含 `castl::vector<T>`（T 为标量记录/枚举/标量）或文本的输入记录：生成器出托管镜像类型（字段级、可赋值构造）+ **两侧同源生成的序列化器**。块头三项：`uint32_t formatVersion = 1u`（本批首次定义格式，取值 1 随格式文档走）、类型完整限定名的 `NameHash`、**字段布局哈希**（各字段"名+类型+偏移"序列的哈希，与布局清单同源）——只带前两项时"字段改名而类型序不变"字节完全相同、检不出来，第三项就是堵这个。过界 = 托管序列化 → `byte[]` → C ABI `uint8_t const* + count` → C++ thunk 反序列化到栈对象 → 调原生（签名不动）。私有字段记录须带 `CA_REFLECT`/`CA_CODEGEN_FRIEND` 标注（只加标注，不改字段布局与 C++ 写法）。**本批实现子集的深度界**：一层 `castl::vector<标量|枚举|纯标量记录>`——界不是规范上限而是"直绑首批见证形状"的覆盖界：`RenderPass` 的三个容器成员（`castl::vector<AttachmentConfig> m_AttachmentConfigs;`、`castl::vector<DrawCallBatch> m_DrawCallBatches;`、`castl::vector<ImageHandle> m_Attachments;`）全是一层 vector<记录>，更深容器在四份真实头里 0 实例；`vector<vector<T>>`、元素含 `castl::string`、映射容器按"可投影但未实现"生成期拒绝并在排除清单点名+给恢复路径（真实实例出现时立扩形状批），**不是永久禁令**。出参方向（非 const 引用/指针要写回托管的容器）MUST NOT 走序列化块——本批只定义输入方向，生成期拒绝并在清单留名。红态一律在**测试内构造**（CoreTests 手摆字节流：version 字节 +1 / 类型名哈希改错 / 字段布局哈希与接收方不符 ⇒ 反序列化拒绝且实现计数 0）；MUST NOT 靠改生成物或"篡改镜像后重生成"造红（生成物每建重发会自愈，且两侧同源重生成永远一致——那是抓不到的假绿构造）。真实头见证（逐字、归属点名）：`RenderPass` 的工厂是 `static RenderPass New(ImageHandle const& color`（4 个重载）；`AttachmentConfig` 的工厂是 `static AttachmentConfig Create(` / `static AttachmentConfig Clear(` / `ClearDepthStencil(`（没有 New，先前草稿写错）；`DrawCallBatch::New()`；`HashObj` 成员在 `PipelineDescData`（`cacore::HashObj<CPipelineStateObject> m_PipelineStates;`）不在 `RenderPass`——见证表按真实归属书写。

### D9 参数注入 `CA_BIND_INJECT(方法, 参数)`

只允许**参数位置**的裸指针；生成器把该参数从 C ABI 与托管表面移除，thunk 从固定名钩子取指针：`extern "C" void* CAInject_<类短名>_<方法名>_<参数名>()`。本批钩子实现放夹具（返回哨兵指针，原生实现断言收到它）；真实实现（默认调度器）属直绑批的宿主接线，本批不做。校验：参数名不存在或参数非指针 ⇒ 生成期非零点名；钩子声明进分片、由登记表装配核对（有注入没钩子声明 ⇒ 拒绝）。"② 所有权不明裸指针"的规范定义同步修订（specs 增量）：参数位置经注入合法化后不再是"不可绑"，返回位置维持。

### D10 真实头快照判据（能力齐不齐的证据）

`--parse-report <头>` 模式：只解析、不产物，打印"采集清单（方法/线形形状）+ 排除清单（方法/原因）"。对四份真实头各跑一次，快照逐行入库 `openspec/changes/codegen-native-binding-prereqs/evidence/原生头投影快照.txt`（工件内引用一律写到这个全路径，design/tasks 同一文件名字义）。**对账不许只比"快照 vs 快照生成器"**——同出一个生成器的两侧漏采同一方法时永远相等、判据恒真。交叉核对独立于 libclang 路径：判据脚本（仓库常驻，如 `Tools/CodeGen/check_native_snapshot.py`，命名落地时定）自己用**剥注释后的文本解析**（与 `check_bindable_audit.py` 同族手法：取类体、按分号切声明、摘方法名）从同一头文件生成第二份公开方法清单，两清单条数与逐条名对账，差异非零点名。红态两例：①给 `GPUGraph.h` 临时加一个公开方法（主 agent 代跑产品变异、当场还原）⇒ 独立清单 +1 而快照未变 ⇒ 点名；②手工删快照一行 ⇒ 点名。**验收还含"清单不漏非虚"锚**：独立文本清单的方法数 ≥ 头内已知构件数（GPUGraph 13 链式 + 10 访问器）——漏采方向被兜住。快照是"直绑首批要绑哪些方法、哪些必须 `CA_BIND_SKIP`"的唯一依据，下一批开工不得凭记忆。

## Risks / Trade-offs

- [采集扩围可能误伤既有接口（多出方法或改了线形）] → D6 逐字节锚第一时间暴露；红绿顺序：先锁文件（D6 通过）再逐能力扩围，每步重验哈希。
- [序列化块两侧格式漂移] → 单一来源：块格式由生成器同一模板出 C++ 与 C# 两份代码；`formatVersion`+类型名哈希做运行时对账；夹具的篡改红是常驻判据。
- [资源引用投影与"哈希键类不能进生成表"的旧结论看似矛盾] → 本批**不**把值类本身登记为可反射类型，只定义它的过界线形与工厂——与"退出码 15 拒的是把带 shared_ptr 成员的类型当记录镜像"不冲突；快照里明写边界，防止误扩。
- [真实头解析踩到宏/模板复杂度（`GPUGraph.h` 885 行含模板与 friend）] → 快照模式允许对解析不了的成员直接进"排除+原因"清单（人工可复核），不追求全采集；解析崩溃类缺陷以夹具隔离。
- [编号值被测试硬编码依赖（`GetInterfaceInstance(3, ...)`、"未知编号 9"）] → D2 初值原号迁入 + D6 哈希锚；这两处测试文本不改。
- [与未归档底座批的 spec 冲突（它刚把"编号写在标注上"写进 delta）] → 顺序已钉死：底座先归档、本批后实施（tasks 0.1 硬门 + Migration 前置段）；本批 specs 对它写出的三条 Requirement 做显式替换，不留两批并存矛盾。

## Migration Plan

前置（0 组，先于一切）：确认底座批 `render-interface-merge` 已归档（tasks 0.1 硬门，用户已授权归档顺序则记录凭证；未归档不开工）——本批 specs 的三处 MODIFIED 都以归档后的主规范文本为基线，`csharp-bindings` 的编号条、`csharp-host` 的登记表条、`csharp-bindable-surface` 的嵌套记录条都是"改底座批刚写进去的句子"，两批未落定先后就是拿未生效文本当已生效基线。

1. 取基线哈希 + 工作区快照（含剔除无源分片，D6）→ 2. 锁文件 + 生成器读锁与 `--update-ids`（过渡态：带号与无号都接受、带号须与锁一致；此步产物逐字节锚必须已过）→ 3. 真实头 + 夹具标注去号、`check_bindings.py` 改读锁 → 4. 启用"带号即拒"并做红绿（红态用临时夹具头单因触发）→ D6 哈希复验 → 5. 采集扩围（D3/D4/D5，夹具红绿；**新夹具头接进 `build.py` 的 `CODEGEN_BINDINGS` 期望前缀清单与 CoreTests include 单列一步**）→ 6. 资源引用（D7）→ 7. 序列化块（D8）→ 8. 注入（D9）→ 9. 真实头快照 + 独立计数对账（D10）→ 10. 文档/README/能力边界清单同步 → 11. 全量实测（16 用例、四探针、CoreTests、check 家族、失败检查 5/0）→ 12. 直绑批跨 change 账落 `docs/TODO.md`（触发条件：本批归档后立即立项、快照为开工输入）。回滚：整批 revert 即回 `CA_BIND(n)` 形态；锁文件与标注同时消失，无数据迁移。

**采集扩围与底座批编号迁移分先后**：D3 的采集改造依赖 D6 逐字节锚，而 D6 基线要在标注去号前取——所以顺序固定为"取基线（带号态）→ 锁文件+去号迁移+产物复验 → 采集扩围"，两段各自重验哈希，禁止合并成一次大改（合并后哈希红无法归因）。

## Open Questions

（本节无悬案：批次顺序已成 tasks 0.1 硬门；序列化块深度界是"首批见证覆盖界"非规范上限，超出即生成期拒绝并留恢复路径，真实实例出现时立扩形状批——不留在开放问题里等实现者临场拍板。）
