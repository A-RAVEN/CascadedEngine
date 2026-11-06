# Design: render-interface-merge

## Context

C++ 侧现在是两层并行的渲染接口面，用户 2026-11-04 已把合并方向写进 `docs/ROADMAP.md`（逐字）："现在 C++ 侧是两层并行的接口面（原生 `CRenderBackend`/`GPUGraph` 与手写的 `IScriptRender*` ScriptBridge 适配层），目标是合并为一致的 C++ 接口面，且合并后必须保证 C++ 端的易用性不下降。"

以下数字全部是 2026-11-05 只读调研实测（命令与读数在归档 evidence 里复核）：

| 事实 | 读数 | 来源 |
|---|---|---|
| 原生接口规模 | `CRenderBackend` 15 个 virtual（12 纯虚 + 3 个带基类默认实现）；`GPUGraph` 23 个 public 函数 | `Interface/RenderInterface/header/CRenderBackend.h`（134 行）、`GPUGraph.h`（885 行） |
| 脚本接口规模 | 五个 `IScript*` 共 28 个纯虚 + 5 个虚析构 | `Interface/RenderInterface/header/ScriptRenderBridge.h`（144 行） |
| 适配层手写规模 | 实现 1152 行 + 私有头 325 行；`Fail(` 96 处、`dynamic_cast` 6 处、`ForTest` 钩子 28 个 | `Interface/RenderInterface/private/ScriptRenderBridge.cpp/.h` |
| 已经生成的部分 | `--emit-binding` 从 `CA_BIND` 标注发射 thunk、版本化函数表（生成表版本 2u）、句柄表、托管表面、布局清单断言；真实接口桩数 0 | `Tools/CodeGen/README.md` 第十四节、`check_bindings.py` 实跑 |
| 仍在手写的机械层 | ① 两侧逐字镜像的 `EngineApi`（v8、80 字节，C++ 在 `CSharpHost/private/CSharpHost.cpp` 手抄 struct + `static_assert(sizeof(EngineApi) == 80u`，C# 在 `Scripting/Managed/EngineApi.cs` 手抄 struct）；② 宿主逐接口手写分派 `interfaceId == s_interfaceId` 30 处（`CSharpHost.cpp` 全文件 1026 行）；③ 托管侧 `BindRenderInterfaces`/`BindTargetTable` 手写逐表序列（`Host.cs` 1282 行）；④ 编号常量两侧手抄（`s_interfaceIdScript*` 与 `Script*InterfaceId` 字段） | 同上 |
| 消费方 | `CRenderBackend.h` 被 10 个真实 include 点消费（两个后端、适配层、IMGUIContext、CSharpHost、GPUBackendTester、CoreTests 等），调用点密集且用原生形状（`std::span`、`castl::shared_ptr`、裸 `TaskScheduler*`、链式引用返回） | `grep CRenderBackend\.h` 实测 |

关键形状事实（决定合并边界）：

- 脚本面与原生面是**两套词汇表**，不是同一套方法的两种写法：`IScriptRenderGraph.AddTrianglePass` 在适配层里展开为 `RenderPass::New(...).SetShaderInfo(...).Batch(...)` + `m_Graph->AddPass(pass)` 的组合调用；原生有、脚本面无对应物的能力实测为 0 命中词：Compute/Dispatch/Readback/Timing/Depth/ViewPort/Scissor/DrawIndexed/PipelineState/Subpass（这些是后续批次的活，见 Non-goals）。
- 绑定发射器只收纯虚方法（`Tools/CodeGen/CodeGenMain.cpp`：`clang_CXXMethod_isPureVirtual(cursor) == 0u` 即跳过），而原生面大量是带实现体虚函数、模板重载（`GPUGraph&` 链式构件 13 个）、不可投影形状（`std::span<uint8_t>`、`std::unique_ptr<IReadbackToken>`、裸 `TaskScheduler*`）。
- 审计表（`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`）已把 `CRenderBackend::ExecuteGraph`、`Release` 等登记为**不可绑**并给出改法；四类形状规则与"语义越界"清单在 `openspec/specs/csharp-bindable-surface/spec.md`（11 个 Requirement）。

## Goals / Non-Goals

**Goals:**

- 能力声明单一来源：接口与编号只在带 `CA_BIND` 标注的接口头声明一次；跨边界函数表（`EngineApi`）与宿主登记表由同一次生成发射到两侧，手写镜像 struct 与手写 `interfaceId` 分支退役。
- 新增一个被绑定接口/编号时，需要手改的文件集合从"标注头 + CSharpHost + EngineApi.cs + Host.cs + CSharpHost README"缩到"标注头 + 宿主工厂注册 + 审计表行"（可 grep 核验，见 tasks）。
- C++ 端易用性不下降，用可失败判据钉死：原生头文件本 change 零改动；GPUBackendTester 不带 `--test` 的全量用例与 IMGUIContext 调用点零修改保持绿。
- ABI 升到 v9 后，版本/大小不符仍拒绝并报数（失败语义保持，判据数字重算不放松）。

**Non-Goals:**

- 不新增渲染能力（多阶段图、计算、读回、GPU 查询各自另批）。
- 不生成适配器方法本体，不改 `dynamic_cast` 身份校验、两级拒绝校验与 `Fail` 错误文本——它们是已归档规范的内容本身。
- 不改 `CRenderBackend`/`GPUGraph`/`ShaderStruct`/`ImageHandle` 等原生头的任何签名。
- 窗口两张表（编号 1、2）的托管绑定序列维持现状，不进本批改造（其判据与用例量大，路由统一即可，见 D6）。

## Decisions

### D1 合并口径：合并"声明→登记→绑定"的机械链，不做字面类合并

把 `CRenderBackend`/`GPUGraph` 与 `IScript*` 合成同一个 C++ 类被否决，依据是上面"关键形状事实"：原生方法集大量不满足 C ABI 四类形状（非纯虚、模板、`std::span`、裸指针、链式引用返回），强行合一要么让原生接口退化成绑得出但难用的形状（违反"易用性不下降"），要么让发射器支持一批形状特例（范围失控）。替代口径：**带标注的接口头是唯一的能力声明点**，原生面保持富接口原样，脚本面是它的投影声明，两者之间的机械层（表、路由、编号、绑定序列）全部从声明生成。语义分层保留、机械层合一，这是本 design 对 ROADMAP 那句"合并为一致的 C++ 接口面"的落地解释；若用户裁定要字面合一，本 change 应改立项而非带病执行。

### D2 接口编号写在标注上：`CA_BIND(编号)`

编号是跨边界契约，必须单一来源且可审计。选择在标注宏上带显式数字参数（`CA_BIND(3)` 起），生成器从宏文本读取（与 `CA_REFLECT(...)` 同一收集机制，`CACore/header/CodeGenMeta.h` 的宏体本来就是 `static_assert(true, "CA_BIND(" #__VA_ARGS__ ")")`，加参数零编译影响）。

- 备选"按声明顺序自动编号"否决：头文件重排或 `#if` 条件编译会悄悄改号，而编号改了托管不会红（审计脚本自己都写明："脚本按**纯虚方法名**对账，**不看签名**"）。
- 备选"外挂清单文件"否决：又造一个与头分离的事实源，正是本 change 要消灭的东西。
- 规则：编号重复 ⇒ 生成器非零退出并点名两个接口；已登记编号不许改号、不许改序，只许在现有最大号之后追加新号；`CA_BIND()` 不带号且该接口进入登记表 ⇒ 生成器非零退出点名"缺编号"。既有夹具头（`Tools/CodeGen/BindableShapes*.h`）不进真实登记表，按现状豁免。

### D3 生成产物形状：一份 C++ 登记表 + 一份 C# 函数表对照

在 `--emit-binding` 现有产物之外，新增发射（产物落 `codegen/bindings/`，同不进源码树）：

- `BindingRegistry.Bindings.h/.cpp`：编号清单常量、`get_interface_api` 路由表（编号 → 该接口生成物 `<接口>_GetApi` + 角色标志：默认实例 / 目标类型表）、`EngineApi` C++ struct 定义与其装配代码。装配代码填的 `log`/`echo`/布局查询函数指针指向宿主固定名字的实现（这些是宿主行为不是接口契约，实现留手写，分支消失）。
- `EngineApi.Bindings.cs`：与 C++ struct 同一次产出、逐字段对照的托管 struct（含 `AbiVersion`/`Size` 常量与七个编号字段），经 `Scripting.Managed.csproj` 现有 `*.Bindings.cs` 通配符进编译。手写 `Scripting/Managed/EngineApi.cs` 删除。
- 函数表版本号（现写死 `2u` 的生成接口表）与 `EngineApi` 版本号（v8→v9）都由生成常量单点定义，两侧 `static_assert` / 运行时自检对生成值断言（消费侧落点见 tasks 3.2：`Host.cs` 的 `WindowApiTableVersion`、`RenderApiTableVersion` 两个手写抄件改引生成常量）。

### D4 实例与工厂留在宿主，路由交给生成表

后端默认实例必须由模块管理器里的真实 `CRenderBackend` + `CThreadManager` 在模块链接阶段创建（现规范条文，`openspec/specs/csharp-host/spec.md`："宿主 SHALL 在模块链接阶段使用模块管理器中的真实 `CRenderBackend` 与 `CThreadManager` 创建脚本渲染适配器"），这个**工厂**留手写；目标类型表允许空实例取表的语义不变。改造点仅是：宿主把"编号 → 工厂/是否目标类型表"作为小登记数据交给生成路由，取代 `CSharpHost.cpp` 里 30 处手写 `interfaceId ==` 分支的取表/填实例逻辑。判据：`interfaceId ==` 手写分派命中数 30 → 0（grep 可核，口径见 tasks）。

### D5 ABI v8 → v9：字节布局不变，版本语义变"表体来自生成"

`EngineApi` 现有 14 个字段的顺序与宽度不动（两侧手写 struct 逐成员实测：`abi_version`/`size` 两个 `uint32_t` + `log`/`echo`/`get_interface_api`/布局查询 2 个共 5 个函数指针 + 窗口 2 个与渲染 5 个共 7 个编号 `uint32_t`；正文 76 字节按 8 字节对齐到 80，由生成侧 `static_assert` 继续钉住），版本号升到 9，语义是"此表由登记表生成，手写不再拥有它"。连锁重算（全部是数字口径更新，不是放松）：`s_engineApiVersion`/`ExpectedAbiVersion` 8→9；`check_csharp_host_failures.py` 的期望随 abi-mismatch 场景**实跑输出**同步（"开关打印版本 + 1"的构造不变，具体读数不在设计里预测，见 tasks 3.3）；`CSharpHost/README.md` 的 v8/80 字样与编号表同步。

### D6 托管绑定序列：五个渲染表进生成清单循环，窗口两表不动

`Host.cs` 的 `BindRenderInterfaces` 改为遍历生成编号清单（3–7）的绑定循环，产物仍逐表调各 `<接口>Bindings.Bind`。保留失败语义判据：单表失败时错误文本必须点名是哪个接口（后端表由 tasks 3.1 的手写工厂删除坐实；目标类型表无工厂可删，其缺表红在构建期由 1.2/1.4 覆盖）。成功日志行"五个函数表已绑定"保持原文，定性为**一致性约束**——2026-11-05 复核实测全仓无脚本读取该行，不冒充可失败判据，运行成败由四探针判定（tasks 3.2）。窗口两张表（1、2）的取表路由进生成登记表，但 `EnsureBound` 的窗口绑定序列保持手写——范围收窄，避免动窗口批次的大量既有判据。

### D7 适配器方法本体、钩子与校验全部保持手写

`ScriptRenderBridge.cpp` 的语义翻译（`AddTrianglePass` 组合建 pass、`CreateTexture` 的"创建+上传图+Finalize"组合）、6 处 `dynamic_cast` 身份校验、96 处 `Fail` 错误文本、28 个 `ForTest` 钩子（注释明写"不进任何绑定接口"）原样保留。本 change 对这三个文件（公开头仅加 `CA_BIND(编号)` 参数）之外的适配实现不做任何重写。

### D8 "C++ 端易用性不下降"的可失败判据

1. 原生头零改动：`CRenderBackend.h`、`GPUGraph.h`、`ShaderStruct.h`、`ShaderProvider.h`、`GPUBuffer.h`、`GPUTexture.h`、`GPUTiming.h`、`WindowHandle.h`、`ShaderResourceHandle.h`、`CCommandList.h` 在 apply 的 `git diff` 里不出现（diff 基准必须是 apply 开工时记录的基线提交号——`git diff --name-only` 不带基准在工作树干净时恒空、恒真，复核纠正；点名清单写进 tasks；`ScriptRenderBridge.h` 只允许标注参数变化）。
2. 调用方零修改：`IMGUIContext/*`、`Test/GPUBackendTester/Main.cpp`、两个后端目录不改一行，`python build.py --config Debug` 全绿 + tester 不带 `--test` 全量 16 用例绿（2026-11-05 复核实测：`--list` 分支 16 条、run-all 16 条注册、`docs/testing.md` 三处 16；CLAUDE.md 的"10 个"是 2026-10-25 旧口径）。
3. C++ 书写者新增一次接口绑定的手写改动面不扩大：对照现状（声明 + 表两侧手抄 + 宿主分支 + 托管序列 + README），合并后为带号标注 + 宿主工厂/角色登记 + 审计表行 + `check_bindings.py` 编号期望行**四处**（复核纠正：原写三处漏了第四处），其余由生成承担——用"新增一个夹具接口编号"演练时点名的手写文件集合验证（见 tasks 5.1）。

### D9 红绿矩阵（每条都要求缺陷态先红）

- 缺编号 / 编号重复 / 改既有编号 ⇒ CodeGen 非零退出并点名（D2 的三条规则各一条红态）。
- 删除宿主手写的**后端适配器工厂**登记（目标类型表本无工厂，"注释掉某表工厂"是点名了不存在的机制，复核纠正）⇒ 绑定失败文本点名后端接口未登记、四个渲染探针明确失败、窗口路径不变；目标类型表的缺表红由构建期编号/前缀演练覆盖。
- 登记产物缺失闸门 ⇒ 用两种构造之一做红：给期望清单临时加一个不会产出的假前缀，或临时注释 `build.py` 的登记表发射调用（源码侧）——**不得靠删生成物**：登记产物每建重发会自愈，且生效规范 `csharp-bindings` 明文"造这条红态 MUST NOT 靠修改生成物"（复核纠正原"删一份注册输入"的写法：删输入连期望一起删，闸门恒绿）。
- 托管期望版本落后一档（abi-mismatch 场景）⇒ 拒绝并报数（既有场景，期望随实跑重算，不写预测值）。
- 全绿基线：四探针 + 窗口回调 + CoreTests + tester 全量 16 用例 + `check_*.py` 家族 + `openspec validate --all`。

### D10 文档与判据同步（同一次提交内）

`CSharpHost/README.md`（编号表来源改"写在 `CA_BIND` 标注上"、v9/80 字节、函数表生成说明）、`Tools/CodeGen/README.md`（登记产物进第十四节与参数表）、`docs/testing.md`（钩子与用例数按 `--list` 实测重刷）、`Tools/check_csharp_host_failures.py`（期望重算）、`SurfaceSignatureCheck.cs` 的计数基线按实测更新并标注口径。`check_bindable_audit.py` 的 `INTERFACES` 名单与审计表行仍是硬门，编号不改变审计口径（仍 12 接口 90 方法，除非同批新增了审计行）。

## Risks / Trade-offs

- [生成器要解析 `CA_BIND(n)` 的数字参数，改 clang 游标文本收集路径] → 先做夹具：`Tools/CodeGen/BindableShapes*.h` 加带号接口，红（缺号拒绝）先于绿；正式头最后才加号。
- [版本升级连锁碰 `check_csharp_host_failures.py` 等判据，容易"改期望迁就实现"] → 期望重算必须以实跑输出为准并贴原始输出，禁止手算；判据强度（不符 ⇒ 拒绝 + 报数）不变。
- [登记表把窗口两接口也纳入路由，误改窗口行为] → D6 明确窗口绑定序列不动；窗口全部既有用例（`TestCSharpWindow`、`--list` 里的窗口项）作为回归锚。
- ["后续批次成本下降"只是承诺] → 用 D8.3 的夹具接口 8 演练把它变成可数的判据，不靠叙述。
- [生成的 `EngineApi.Bindings.cs` 与手写 `Host.cs` 之间仍可能漂移] → `Host.cs` 里对 `AbiVersion`/`Size`/七个编号的读取全部改走生成常量；grep 判据：`Host.cs` 内 `InterfaceId` 字面量 0 处（以实测基线定起点）。

## Migration Plan

1. 生成器扩展 + 夹具红绿（D9 前三条在夹具层先跑）。
2. 标注加号（窗口头 + 渲染头共 7 个接口一次加完），生成登记表与 `EngineApi` 对照。
3. `CSharpHost.cpp` 手写路由退役、工厂登记改为数据；托管 `EngineApi.cs` 删除、`Host.cs` 绑定循环改造。
4. 判据与文档同批重算（D10），全量实跑（D9 全绿基线）。
5. 回滚策略：本 change 不搬数据、不改行为语义，单批 revert 即回到 v8 手抄形态；无中间态依赖。

## Open Questions

- 窗口 `EnsureBound` 绑定序列是否也并入生成循环：本批按 D6 不动；若用户裁定"要全动"，改 spec 增量再扩。
- `--emit-binding` 与新的登记表发射是"一次调用两件事"还是拆独立入口 `--emit-registry`：实现细节，倾向同一次调用（保证两侧产物永远同批），apply 时由实现者定并留理由。
