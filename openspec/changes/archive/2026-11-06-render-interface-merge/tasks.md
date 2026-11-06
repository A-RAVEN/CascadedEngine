# Tasks: render-interface-merge

> 分工与纪律：`GPUBackendTester.exe` 的一切运行由主 agent 代跑（沿用已归档批次口径：执行者备好命令上报、取回读数入证据后再勾）；`python build.py` 只由主循环跑——依据是全局规则 AGENTS.md §9（跑整套、启动程序由主循环自己做）与 CLAUDE.md「构建必须前台跑」，这条归属**不是**归档批次写过的口径，不许再标"沿用归档"。CoreTests、`Tools/check_*.py`、grep 与只读核对由执行者自跑。每条"计数/文本"判据必须先见红态输出再修绿并留档；计数类断言一律基线 + 增量（进程级累加器）；**造红态不得靠删改生成物**——登记产物每建重发会自愈，生效规范 `openspec/specs/csharp-bindings/spec.md` 已有条文「造这条红态 MUST NOT 靠修改生成物（构建会重新生成覆盖掉）」。所有基线数字是 2026-11-05 只读调研实测，apply 开工第一步先复算基线并把命令与输出写进 `evidence/`。
>
> 基线（可复算）：`git grep -c "interfaceId ==" -- CSharpHost/private/CSharpHost.cpp` = **30**；`CSharpHost.cpp` **1026 行**；`Scripting/Managed/Host.cs` **1282 行**；`Scripting/Managed/EngineApi.cs` **47 行**；ABI **v8 / sizeof(EngineApi)==80**；`check_csharp_host_failures.py` 现期望文本含 **托管侧期望 8**；`GPUBackendTester --list` = **16** 用例（2026-11-05 复核实测：Main.cpp 的 `--list` 分支 16 条用例名打印、run-all 序列 16 条注册调用、`docs/testing.md` 三处写 16；CLAUDE.md 的"10 个"是 2026-10-25 旧口径不作数；apply 开工以重跑 `--list` 为准）；渲染探针 **4** 个（三角/常量色/结构化缓冲/纹理）互不消耗；审计 **12 接口 90 方法** 绿；`Tools/**/check_*.py` 文件数按重 glob 实测（上次口径 12 份，文档禁手抄）。

## 1. 生成器：带编号的标注与宿主登记表

- [x] 1.1 在 `Tools/CodeGen/` 新建夹具头（命名沿 `BindableShapes.*.h` 惯例，如 `BindableShapes.RenderRegistry.h`）：含两个带编号接口（`CA_BIND(8)`、`CA_BIND(9)` 夹具专用号段，配最小可投影方法）；`build.py` 的 `CODEGEN_BINDINGS` 增登记条目并让该夹具进入登记表发射输入（真实接口此步未加号，登记表发射对"进表接口缺编号"的非零拒绝只能靠夹具演练证明）。
- [x] 1.2 生成器扩展第一步——编号解析与拒绝：`--emit-binding` 从 `CA_BIND(n)` 宏文本读编号；登记表发射输入里接口无编号 ⇒ 非零退出点名接口；两接口同号 ⇒ 非零退出点名双方与编号。**验收（先红后绿）**：夹具把 `CA_BIND(8)` 改回 `CA_BIND()` 跑 `CodeGen.exe … --emit-binding` 输出点名 `IBindRegistry8` 的非零退出（原始输出存 `evidence/1.2-缺编号红.txt`）；夹具两接口都写 `CA_BIND(8)` ⇒ 点名两个接口的非零退出（存 `evidence/1.2-重号红.txt`）；还原后退出 0、登记表产物含两接口。另核 spec 的"不出半成品"判据：每次拒绝退出的那轮跑完后，登记表既有产物与跑前 `Get-FileHash` 逐字节一致（本轮没有写出或改写任何登记产物），前后哈希进 `evidence/1.2-半成品核对.txt`。
- [x] 1.3 登记表发射：产出 C++ 侧 `BindingRegistry.Bindings.h`（编号清单常量、`EngineApi` struct 定义与 `static_assert`）与 `BindingRegistry.Bindings.cpp`（`get_interface_api` 路由表：编号 → 该接口生成的 `<接口>_GetApi`、默认实例/目标类型表角色标志、宿主固定名实现函数的装配），C# 侧 `EngineApi.Bindings.cs`（逐字段对照 struct + `AbiVersion`/`Size`/编号常量）。字段顺序与宽度必须与现 v8 手写表逐字段一致（对照源：`CSharpHost/private/CSharpHost.cpp` 的 `struct EngineApi` 与 `Scripting/Managed/EngineApi.cs`，生成常量 `abi_version` 取 9）。**验收**：同一输入连跑两次全部登记产物 `Get-FileHash` 逐字节相同（读数留档）；`Tools/CodeGen/check_bindings.py` 新增编号期望对账段：期望表（8→夹具接口限定名等）与产物不符 ⇒ 非零并点名差异，先构造一次改号红态留档。
- [x] 1.4 `build.py` 期望产物前缀纳入登记产物：`BindingRegistry`、`EngineApi` 前缀进必须出现清单。**验收（红态不得靠删生成物——每建重发会自愈，生效规范已明文禁止）**：两种构造都跑一遍并留档——①临时给期望清单加一个生成器不会产出的假前缀（如 `IBindNoSuch`）再跑 `python build.py` ⇒ 构建失败并点名该缺失项（`evidence/1.4-假前缀红.txt`）；②临时把 `build.py` 的登记表发射调用注释掉（改的是源码侧发射调用，不是产物）再跑 ⇒ 同样点名缺失（`evidence/1.4-静默跳过红.txt`）；两处还原后构建绿。产物目录按所选预设记录在 evidence，本条不预设死目录名。

## 2. 标注加号与原生头零改动

- [x] 2.1 给真实接口加编号：`WindowSystem/header/CAWindow/WindowSystem.h` 两接口标 `CA_BIND(1)`/`CA_BIND(2)`；`Interface/RenderInterface/header/ScriptRenderBridge.h` 五接口标 `CA_BIND(3)`…`CA_BIND(7)`；`CACore/header/CodeGenMeta.h` 的 `CA_BIND` 注释补"编号写在这里、进登记表必须带号"。既有夹具头（`Tools/CodeGen/BindableShapes*.h` 通配全量，按 design D2 豁免口径；2026-11-05 复核读数：带 `CA_BIND` 标注的夹具头 19 份、其中 17 份带无号标注，另有 4 份已在 `CODEGEN_BINDINGS` 走绑定发射——它们是否加号由实现按"进登记表才必须带号"的规则处理并留理由）维持无号现状。**验收**：一次 `python build.py` 绿；`check_bindings.py` 对真实产物编号对账 0 处差异；`grep 'CA_BIND(' Interface/RenderInterface/header/ScriptRenderBridge.h` 命中 5 行且编号为 3–7。
- [x] 2.2 原生头零改动核对（D8 判据①）：**比对基准必须是本 change 开工时的提交号**（先跑 `git rev-parse HEAD` 记入 `evidence/基线提交.txt`；拿它当 diff 基准，否则工作树干净时 diff 恒空、恒真）。`git diff --name-only <基线提交>` 里 MUST NOT 出现 `Interface/RenderInterface/header/` 下除 `ScriptRenderBridge.h` 外的任何头（点名清单：`CRenderBackend.h`、`GPUGraph.h`、`ShaderStruct.h`、`ShaderProvider.h`、`GPUBuffer.h`、`GPUTexture.h`、`GPUTiming.h`、`WindowHandle.h`、`ShaderResourceHandle.h`、`CCommandList.h`、`GPUFrame.h`、`LayoutBuilder.h`、`CNativeRenderPassInfo.h`），`Interface/RenderInterface/private/ScriptRenderBridge.*` 两文件零改动。**验收**：贴 `git diff --name-only <基线提交>` 全量输出并逐条说明归属；命中禁止清单即为越界，先还原。红态自证：临时触碰清单内任一头（如加一行注释）后 diff 必须点名它，截图/输出留档再还原。

## 3. 宿主与托管侧的手写层退役

- [x] 3.1 `CSharpHost.cpp` 改造：删手写 `struct EngineApi` 与 `static_assert(sizeof(EngineApi) == 80u` 抄件，改为包含 `BindingRegistry.Bindings.h`；删逐接口 `interfaceId ==` 手写分支与 `s_interfaceId*` 手写常量，取表路由与布局查询分派（复核实测 30 处中 14 处属 `get_binding_layout_type/field` 两个函数的逐接口分派）都进生成登记表路由；保留 `log`/`echo` 实现与"后端默认实例工厂（模块链接期用 `GetInstance<graphics_backend::CRenderBackend>()` + `CThreadManager`）"，工厂按登记表约定以固定名字暴露给装配代码。`s_engineApiVersion` 改取生成常量（9）。**验收（先红后绿，红态改宿主源码、不碰生成物——每建重发会自愈，生效规范明文禁止拿生成物造红）**：红——临时注释掉手写工厂行（逐字锚点：`m_renderAdapter = std::make_unique<graphics_backend::CScriptRenderBackend>(backend, threads);`）再构建跑任一渲染探针：绑定失败文本点名后端接口未登记、四个渲染探针全部明确失败、窗口路径既有用例与计数不变（日志存 `evidence/3.1-工厂缺失红.txt`）——这一手同时坐实增量规范 Scenario"缺少渲染依赖必须明确失败"。目标类型表（含 `IScriptTexture`）本无工厂可删，其"单表缺失点名接口"的红在构建期由 1.2/1.4 的缺编号/假前缀演练覆盖，不在运行期构造。绿——还原后 `git grep -c "interfaceId ==" -- CSharpHost/private/CSharpHost.cpp` 读数 **0**（基线 30；若实现认定布局查询分派属宿主行为不可纳入登记表，必须停工上报冲突由主裁，不许静默改口径为 ≤14），四探针全绿。
- [x] 3.2 托管侧改造：删 `Scripting/Managed/EngineApi.cs`（手写镜像退役，生成 `EngineApi.Bindings.cs` 经 csproj 现有 `*.Bindings.cs` 通配进编译）；`Host.cs` 里 `ExpectedAbiVersion`/`sizeof` 期望/五个 `Script*InterfaceId` 字段读取全部改走生成常量与生成 struct；`BindRenderInterfaces` 改为遍历登记表编号 3–7 的绑定循环，`BindTargetTable` 语义并入循环；`WindowApiTableVersion`、`RenderApiTableVersion` 两个手写表版本抄件改引生成常量（D3 的单点化落点就在这一条）。**可失败判据（复核后重定口径）**：①退役判据数的是**手写常量声明行**而不是"InterfaceId 字样"——`git grep -c 'private const uint ExpectedAbiVersion\|const int WindowApiTableVersion\|const int RenderApiTableVersion' -- Scripting/Managed/Host.cs` 基线实测 **3**（apply 先复算记入 evidence），改造后 MUST 为 **0**；红构造=手工恢复其中任一常量并把数值写成与生成值不一致 ⇒ `Boot` 版本/大小自检拒绝启动（abi-mismatch 既有场景可读）。②单表失败文本仍点名具体接口：后端表缺登记的红由 3.1 坐实；托管循环层再复现一次"循环遇缺表 ⇒ 文本点名接口"留档 `evidence/3.2-托管循环点名红.txt`。③成功日志行文本"五个函数表已绑定"**保持原文但本批不冒充可失败判据**——2026-11-05 复核实测全仓无任何脚本或测试读取该行（只有 `Host.cs` 自己打印与归档 evidence 里的历史输出），它的约束力=一致性约束（改动即违反 D6），运行成功与否由四探针结果判定。窗口 `EnsureBound` 的两表绑定序列不动（D6）。
- [x] 3.3 ABI 不符场景重算（判据强度不变、数字以实跑为准）：跑 `Tools/check_csharp_host_failures.py` 的 abi-mismatch 场景，**期望文本不写预测值**——先实跑贴原始输出（"开关打印版本 + 1"的既有构造不变），再按实跑读数同步脚本期望；脚本自身断言只看它实际解析的两侧版本读数文本，禁止引入没有读取方的新断言。**验收**：脚本 5 条场景全绿、退出 0，且把脚本期望改回任一过时版本号能复红（留档一次红态输出）。

## 4. 判据家族与对外文档

- [x] 4.1 `check_csharp_host_readme.py`/`check_bindable_audit.py` 同步与 `SurfaceSignatureCheck.cs` 实测定性：README 的 v8=80 字样、编号表来源说明改后 readme 判据退出 0；审计 12 接口 90 方法口径不因编号变化改变，`check_bindable_audit.py` 应零改动仍绿。**`SurfaceSignatureCheck.cs`（复核纠正）**：2026-11-05 复核实测该文件内没有任何"61"计数或对应断言——此前工件里"基线 61"的说法出自仓库级 `git grep -c 'Func<\|Action<'` 的观测口径、不是该文件的判据，本批不把它冒充可失败判据；执行时先读该文件弄清它实际判什么（它怎么进编译、失败以什么形式暴露），改造后跑它的实际机制保持绿；`Func<`/`Action<` 行数只作观测写进 evidence。**验收**：判据实跑退出 0 且读数贴档；审计脚本 `git diff` 为空（若不为空必须解释）。
- [x] 4.2 文档同批更新：`CSharpHost/README.md`（ABI v9/80 字节、"编号写在 `CA_BIND` 标注上"、登记表机制与函数表来源）、`Tools/CodeGen/README.md`（第十四节补登记产物形状与退出码、参数表如需新开关）、`docs/testing.md`（用例/钩子计数按 `--list` 与实测重刷）。**验收**：`python Tools/check_csharp_host_readme.py` 与 `python Tools/CodeGen/verify_readme.py`（该仓库既有的 README 判据）退出 0；`docs/testing.md` 的清单条目与 `GPUBackendTester.exe --list` 实跑输出逐条一致（主 agent 跑，贴输出）。
- [x] 4.3 全量实测（主 agent 执行、子 agent 读日志）：`python build.py --config Debug` 绿 → `GPUBackendTester.exe` 不带 `--test` 全量 16 用例（以开工重跑 `--list` 为准；两后端各一次，`--headless` 按既有口径）→ 四探针 + 窗口回调全部既有条目读数与归档批次基线**基线+增量**一致 → CoreTests 全绿 → `Tools/**/check_*.py` 重 glob 全家族逐个退出 0——**唯一记名例外**：`Tools/CodeGen/check_hash_sources.py` 在基线提交 `1301768` 即红（worktree 快照实跑 EXIT=1、输出与工作树逐字同：`来源不明: remove_cvref_t（出现在 Test/CoreTests/GeneratedHashTest.cpp）`；扫描器、例外清单、触发文件三者对本批 `git diff` 零改动，非本批引入），其修复另立跨 change 账（`docs/TODO.md`），本批 evidence 留基线红与排除证据两段；除该记名例外外全家族 MUST 逐个退出 0。**验收**：每层留日志路径与关键计数；任何一条计数钩子的绝对值断言必须写成基线+增量。

## 5. 演练、收尾与编译验证

- [x] 5.1 "新增接口成本下降"演练（D8 判据③）：在 1.1 夹具上走通"新编号接口进登记表"全流程后，贴出本次演练的手写改动文件清单：新增一个被绑定接口的手写改动面=①带号标注（夹具头）②宿主工厂/角色登记③审计表行（或夹具豁免审计的说明行）④`check_bindings.py` 的编号期望对账行，**共四处**（复核纠正：原写三处漏了④）；除这四处外 MUST NOT 需要改 `EngineApi`（已生成）、`Host.cs` 绑定序列、`CSharpHost.cpp` 路由——若需要改，即合并未达标，回到 1–3 修正。**验收**：演练清单与结论写入 `evidence/5.1-演练.md`，演练残留（夹具号段变更等）还原后全量判据复绿。
- [x] 5.2 `openspec validate render-interface-merge --strict` 与 `openspec validate --all` 退出 0；清理 `evidence/` 之外的一切临时产物（工作区还原为仅本 change 文件 + 既有未跟踪目录）。
- [x] 5.3 最终编译验证：`python build.py --config Debug`（项目唯一合法构建路径；**不带 `--config` 的默认预设是 RelWithDebInfo（复核纠正），本批全程统一 `--config Debug`**——启用 validation 层、产物目录 `out/build/x64-debug`，与 1.4/4.3 及各日志口径一致；等价于模板要求的 build.bat 步骤）跑至成功；失败则分析修复直到成功，最后一次全量实测读数与 4.3 对齐复核。

## 修订记录

> 2026-11-05 对抗验证轮（攻击 5 面 40 条主张 → 独立复核 40/40 → 裁决：blocking 8 / misleading 9 / false-positive 4 / 矛盾对 2）。逐条改前/改后全文见 `evidence/对抗验证处置.md`；本节只记口径变更条目。

- 头部分工行：改前"（沿用已归档批次口径）：`python build.py` 与 `GPUBackendTester.exe` 只由主 agent 跑"→ 改后 tester 归属沿用归档、构建归属标注真实依据（AGENTS.md §9 + CLAUDE.md"构建必须前台跑"），并新增"造红态不得靠删改生成物"总纪律（依据：裁决 misleading9、矛盾对 2；生效规范 `csharp-bindings` 条文）。
- 基线行：改前"`--list` = **10** 用例"→ 改后 **16** + 复核来源 + "以开工重跑为准"（裁决 misleading2，八条复核合并；CLAUDE.md 的 10 是 2026-10-25 旧口径）。
- 1.2：补"拒绝退出那轮不得写出/改写登记产物（跑前后哈希一致）"（裁决 misleading4：spec 的"不出半成品"无判据）。
- 1.4：改前"人为删除产物目录 `out/build/x64-debug/…` 下任一登记产物再跑构建"→ 改后"假前缀/注释发射调用两种源码侧构造 + 不预设死目录名"（裁决 blocking1：原构造每建重发自愈恒绿）。
- 2.1：改前"夹具头（`BindableShapes.h` 等三个…）"→ 改后按 D2 通配全量豁免 + 复核读数（19/17/4）+ "以重 glob 为准"（裁决 misleading3）。
- 2.2：补"diff 基准 = 开工记录的基线提交号"与红态自证（裁决 blocking5：无基准恒空恒真）。
- 3.1：改前"注释掉登记表中 `IScriptTexture` 的工厂登记"→ 改后"注释宿主手写工厂行 `m_renderAdapter = std::make_unique<graphics_backend::CScriptRenderBackend>(backend, threads);`，同时坐实 Scenario'缺少渲染依赖必须明确失败'；目标类型表缺表红移到构建期演练"（裁决 blocking2：目标类型表本无工厂，点名了不存在的机制 + 矛盾对 2）；绿判据保持 0 命中并把"布局查询 14 处不可纳入时必须停工上报、不许静默改 ≤14"写死（裁决 blocking7）。
- 3.2：改前"grep 'InterfaceId' 手写编号字面量 0 处"→ 改后数手写常量声明行（基线 3 → 0）+ 红构造；"五个函数表已绑定"由伪文本判据改为一致性约束并写明"全仓无读取方"实测；表版本抄件退役并入本条（裁决 blocking3、blocking4、misleading7）。
- 3.3：改前"预期为'引擎 10，托管侧期望 9'"→ 改后不写预测值、先实跑贴档再改期望（裁决 misleading5）。
- 4.1：改前"SurfaceSignatureCheck 计数基线 61 按实测更新"→ 改后如实定性"61 是仓库级观测口径、该文件无此断言"，改为读它实际机制（裁决 blocking6）。
- 4.3 / 5.3：10→16 用例；`python build.py` 补 `--config Debug` 并写明默认预设是 RelWithDebInfo（裁决 blocking8、misleading2）。
- 5.1：改前"三处"→ 改后"四处（含 `check_bindings.py` 编号期望行）"（裁决 misleading8）。
- design.md 同步五处：D5"19 个字段"→14（含逐成员算式）并删预测读数（裁决 misleading1、5）；D6 日志行定性一致性约束（blocking3）；D8 三条判据随 tasks 更新（blocking5、misleading2、8）；D9 红绿矩阵重写三条（blocking1、2 + 矛盾对 1、2）；D3 补表版本单点化的任务落点（misleading7）。
- specs/csharp-host 增量：改前"19 个字段的顺序与宽度"→ 改后"14 个字段"（裁决 misleading1）。
- 驳回 4 条（false-positive，理由以复核文件为准）：复核-0009（工厂登记是宿主源码、红态可构造——本条与 blocking2 不冲突，被驳回的是"构造不出"的泛断言）、复核-0016（readme 判据由 4.1 同步并验收退出 0，不恒退 2）、复核-0017（还原闭环已在头部纪律与 5.1 存在）、复核-0018（零改动清单只限渲染头，窗口头/CodeGenMeta 加号不在清单内、design 迁移步骤已明写）。
- （apply 中修订，2026-11-05）4.3 判据句：改前"`Tools/**/check_*.py` 重 glob 全家族逐个退出 0"→ 改后增补"唯一记名例外 `Tools/CodeGen/check_hash_sources.py`"及其基线红证据（worktree 实跑基线 `1301768` EXIT=1、输出与工作树逐字同；扫描器/例外清单/触发文件本批 `git diff` 零改动）与跨 change 账去处（`docs/TODO.md`）。依据：主循环实测排除（非"既有问题"口头豁免），判据其余部分不放松。

