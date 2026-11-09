# Proposal: csharp-native-direct-binding（原生接口直绑 + IScript*/CScript* 桥整体退役）

## Why

用户 2026-11-05 裁定（`docs/ROADMAP.md` 第 88 行逐字）："我对codegen的要求就是在不牺牲c++接口易用性的前提下直接生成C#binding，你现在写这些iscripting就是在妥协"，且"我的合并 interface 的要求就是合并 IScript 和原本的 CRenderBackend"——`IScript*`+`CScript*` 适配层整体退役、原生接口是唯一绑定来源，是结论不是待议事项。前置批 `codegen-native-binding-prereqs`（2026-11-08 归档于 `openspec/changes/archive/2026-11-08-codegen-native-binding-prereqs/`）已交付全部开工条件：原生形态的采集与投影能力（链式自返回/非纯虚/重载/构造工厂/资源引用值类/序列化块/参数注入）、编号锁文件、以及本批唯一开工输入 `evidence\原生头投影快照.txt`（4 头 22 类 126 方法：**110 采集 / 16 排除**——②裸指针 9、①模板 4、映射容器 3，与独立文本清单对账 0 差异）。触发条件（"本批归档后立即立项"）由 `docs/TODO.md` 2026-11-06 直绑批账写死，本 change 即该账的兑现。

## What Changes

- **原生接口直接进绑定面**：`CRenderBackend`、`GPUGraph`、`ShaderStruct`、`GPUTexture`、`WindowHandle` 五类挂无参 `CA_BIND()`（绑定面与排除面以快照 110/16 为准，逐条可指认；快照之外的方法集合变化必须同步重跑快照与独立对账）；`CRenderBackend::Release`/`Initialize` 维持审计判定的 ④ 语义越界不可绑（`CA_BIND_SKIP`，依据 `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`），不进本批绑定面；`ImageHandle`/`BufferHandle` 挂 `CA_BIND_VALUE` 作过界值类；宿主 `ExecuteGraph` 的 `TaskScheduler*` 走 `CA_BIND_INJECT`（前置批已交付该通道）。锁文件 host 段换面走 design D10 的原地置换（一次 change 内删桥 3–7 号槽、四类原生接口按 3/4/5/6 补入，host 段保持从 1 起连续不跳号），发号经 `--update-ids`，人不接触数字。
- **数据上传通道补齐**（快照排除面里唯一造成功能回退的形态）：`GPUGraph::ScheduleData` 系（`const void* + offset + size` 内存窗参数）现判 ② 排除不绑——但桥的字节数组通道（`IScriptRenderGraph::AddBuffer(name, descriptor, byte[])`，声明在 `Interface/RenderInterface/header/ScriptRenderBridge.h:87`，`private/ScriptRenderBridge.cpp:459-462` 内部 memcpy 后经原生 `ScheduleData` 提交，托管对应面为 `IScriptRenderGraph.Bindings.cs:316` 的 `AddBuffer(string, GPUBufferDescriptor, byte[])`；它是 `csharp-rendering` "字节复制…完整提交"条的行为基础）必须在原生面上等价存在，否则退役即砍功能。机制设计归 design（生成器新投影或托管侧糖），验收=真实像素用例在退役后仍两后端全过。
- **BREAKING——桥整体退役**：删 `Interface/RenderInterface/header/ScriptRenderBridge.h`（5 个 `IScript*` 接口及其 `CA_BIND` 标注）与 `private/ScriptRenderBridge.cpp`（1152 行）、**`Interface/RenderInterface/private/ScriptRenderBridge.h`（325 行私有头——5 个 `CScript*` 类声明与全部 28 个 `*ForTest` 钩子声明的实际所在；.cpp 实测 8 个钩子名、21 处提及、2 处定义）**、宿主 `kBindingPolicies[]` 桥 5 行、锁文件 host 段桥 5 行退役；宿主工厂 `m_renderAdapter = std::make_unique<graphics_backend::CScriptRenderBackend>(...)` 改为直接构造/传递原生对象；**三个桥私有头消费者（`CSharpHost/private/CSharpHost.cpp`、`Test/GPUBackendTester/Main.cpp`、`Test/CoreTests/ScriptRenderBridgeTest.cpp`）同批改除包含关系**；编号槽位的原地置换程序见 design D10。退役=删除，不留兼容层、不留空壳转发。
- **BREAKING——ABI 与登记表重建**：绑定面换源必然改函数表与 `EngineApi` 布局，ABI 版本自 v9 递增、函数表按新面重建；产物逐字节基线（96 份，前置批锚）**换锚**——桥 5 接口产物消失、原生类产物新增，换锚前后清单与依据入 evidence；`check_csharp_host_failures.py` 等托管侧期望计数按新面重算（判据只严不松：用例一条不少）。
- **既有 16 用例零回退**：四条渲染用例（`TestCSharpTriangle`、`TestCSharpConstantColor`、`TestCSharpStructuredBuffer`、`TestCSharpTextureSampling`，C# 脚本体在 `Test/GPUBackendTester/assets/` 对应 UserScript 探针处）的 C# 代码改写为原生直绑表面 + C# 糖层（链式、`using`/`Dispose`、字节数组上传糖），行为验收不变（真实像素、两后端、`--list` 仍 16 条）；两条窗口用例（`TestCSharpWindow`）本就不走桥，不动；`csharp-rendering` 三条**面主语**条文（"脚本窄渲染适配面必须形成真实建图与提交"、"适配器必须在进入后端前拒绝无效图与无效参数"、"着色器参数绑定必须按编译元数据在进入后端前校验"）按原生直绑 + 托管前置校验改写（验收行为与拒绝语义逐条保留），其余五条（字节复制生命周期与四条像素验收）不动。
- **C# 糖留在托管侧**（阶段 4 目标既有约定）：原生接口形状不为用户便利改动任何签名；便利由生成表面与 C# 糖提供。

## Capabilities

### New Capabilities

- `csharp-native-render-binding`: 原生渲染接口作为唯一绑定来源的绑定面治理——快照即绑定面/排除面的入库依据、五类原生的进表集合（含 3.1 对账裁定的 WindowHandle）、`CRenderBackend::Release`/`Initialize` 的 ④ 不可绑维持、数据上传通道在原生面的等价形态、桥退役后不得残留任何 `IScript`/`CScript` 符号。

### Modified Capabilities

- `csharp-rendering`: "脚本窄渲染适配面…真实建图与提交"、"适配器…拒绝无效图"、"着色器参数绑定…进入后端前校验"三条的绑定面主语由 `IScript*` 桥改为原生直绑 + 托管前置校验（拒绝行为与像素验收语义逐条保留）；其余五条不动。
- `csharp-host`: `kBindingPolicies[]` 桥条目退役、引擎侧工厂不再构造桥；ABI 版本递增与函数表重建的宿主侧语义；`Scripting/Managed` 装配面随桥类型消失的连带更新（条文若引用桥符号则改，未引用不动）。
- `csharp-bindings`: 锁文件 host 段对退役名与新进表名的登记规则落到本批真实动作（换号/删号按 design D10 原地置换，一次 change 内完成、段内连续不跳号；产物逐字节对账换锚承接）；"被审计接口逐字节一致"类锚的换锚程序。
- `csharp-bindable-surface`: 新增三类跨界形状的投影规则——参数位置内存窗（`const void*` + 相邻长度参数 ⇒ 托管 `byte[]`）、返回位置指向可序列化记录的非拥有裸指针（两段式：托管只读副本/`null`）、模板方法的显式实例化声明（`CA_BIND_INSTANTIATE`，不改原生签名）；所有权不明裸指针与映射容器的不可绑面维持——返回位置两段式是"裸指针不可绑"明文裁出的唯一例外，参数位置未经 `CA_BIND_INJECT` 声明的裸指针不在例外内，仍不可绑。

## Impact

- 产品：`Interface/RenderInterface/`（四原生头挂标注、桥 .h/.cpp 删除）、`CSharpHost/private/CSharpHost.cpp`（策略表、工厂锚、桥包含关系）、`Scripting/Managed/Host.cs` 与用户脚本面（`TestCSharp*` 四条渲染用例改写）、`Tools/CodeGen/CodeGenMain.cpp` 与 `check_bindings.py`（新投影/新判据）、`Tools/CodeGen/interface-ids.lock`（host 段换面经 `--update-ids`）、`build.py`（绑定输入清单换头）、`Test/CoreTests/`（桥测试文件退役、原生面断言新增）、`Tools/check_csharp_host_failures.py` 与 `check_csharp_*` 判据族（期望计数重算）。
- 基线锚：`evidence\基线产物哈希.txt` 换锚（前置批锚记录本批换锚前后对照，不删旧账）。
- 回归口径：`python build.py --config Debug`、两后端 `--list` 16 条全过、CoreTests 全绿、`openspec validate --all` 全绿；四渲染探针基线+增量在新面复跑。
- 规范依据（逐字已核实存在）：直绑与合并接口两条裁定同在 `docs/ROADMAP.md` 第 88 行；废除手写编号的裁定在第 90 行；触发账在 `docs/TODO.md`；快照 160 行在归档 `evidence\`。
