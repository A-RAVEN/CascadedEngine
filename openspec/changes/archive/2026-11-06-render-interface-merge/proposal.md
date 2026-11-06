# Proposal: render-interface-merge（合并 C++ 端渲染接口）

## Why

C#→GPU 绑定已跑通五个批次，但每个新能力都要在"两层并行的接口面"（原生 `CRenderBackend`/`GPUGraph` 与脚本窄接口 `IScriptRender*`）之外，再手写四类机械胶水：两侧逐字镜像的 `EngineApi` 函数表、宿主里 30 处 `interfaceId ==` 手写分派（`CSharpHost/private/CSharpHost.cpp`，1026 行）、托管侧手写的取表/绑定序列、两侧各自手抄的尺寸断言。ROADMAP 上记录的排期方向（用户 2026-11-04 原话转述，`docs/ROADMAP.md`）："现在 C++ 侧是两层并行的接口面……目标是合并为一致的 C++ 接口面，且合并后必须保证 C++ 端的易用性不下降"。剩余批次（多阶段图/计算/读回/GPU 查询）若在这套形态上继续，每批都要再手搓一遍；合并机制先行，后续批次才退化成"登记一次、实现一处"。

## What Changes

先钉死本提案对"合并"的口径（依据见 design）：**两层接口面在语义上保留（原生富接口给引擎/C++ 用、脚本窄接口给托管用），但把"能力声明 → 宿主登记 → 双侧绑定"这条链上的全部机械重复合并成单一来源** ——能力只在带 `CA_BIND` 标注的接口头声明一次，其余机械层全部由生成器发射；原生接口一个签名都不动，C++ 端现有调用点零改动，这就是"易用性不下降"的保证方式。

- 生成范围扩大（`--emit-binding` 已有：thunk、版本化函数表、句柄表、托管表面）：新增发射**宿主登记表**（接口编号清单、取表路由、默认实例登记）与**两侧同一次产出的 `EngineApi` 对照**（C++ 结构体 + C# struct）。**BREAKING**：`EngineApi` ABI v8 → v9，此后新增接口编号不再手改两侧表体。
- 手写分派退役：`CSharpHost.cpp` 里 `interfaceId == s_interfaceId...` 的逐接口手写分支改为查生成的登记表；`Host.cs` 里 `BindRenderInterfaces`/`BindTargetTable` 的逐个手写序列改为按生成清单循环绑定。
- 两侧尺寸/版本断言改为对生成值断言（`static_assert` 与托管自检的数字不再人抄）。
- 脚本适配器（`Interface/RenderInterface/private/ScriptRenderBridge.*` 的 `CScript*` 类）的**方法本体保持手写**——语义翻译与两级校验是需求本身，不可机械化；本批合并的是它的"外围机械层"（登记、路由、取表），并给它挂上生成的登记描述符，`CSharpHost::Init` 不再手工登记。
- 同步文档与判据：`CSharpHost/README.md`、`Tools/CodeGen/README.md`、`Tools/check_csharp_host_failures.py` 的 ABI 期望（8→9）与版本不符文案重算、`docs/testing.md` 数字重刷。

## Capabilities

### New Capabilities

（无——登记表发射并入 `csharp-bindings` 能力，宿主侧改动落在 `csharp-host` 能力。）

### Modified Capabilities

- `csharp-bindings`: 生成器的产物清单扩为"每接口绑定产物 + 跨接口的宿主登记表与 `EngineApi` 两侧对照"；新增"接口编号由标注声明分配、重跑生成保持稳定、只增不改序"的要求。
- `csharp-host`: 现要求"宿主必须登记渲染适配接口并同步手写 ABI"（`openspec/specs/csharp-host/spec.md`，明文要求手写 `EngineApi` 只在末尾追加编号 3–7）被替换——ABI 契约从"两侧手抄 + 人肉同步"改为"单源生成 + 对生成值断言"；版本/大小不符时报错拒绝的失败语义保持并加强。

## Impact

- 生成器与构建：`Tools/CodeGen/CodeGenMain.cpp`、`Tools/CodeGen/README.md`、`build.py` 的绑定步骤与 `CODEGEN_BINDINGS` 清单。
- 宿主：`CSharpHost/private/CSharpHost.cpp`（手写分派与手写 `EngineApi` 退役）、`CSharpHost/README.md`、`CSharpHost/CMakeLists.txt`（生成头包含）。
- 托管：`Scripting/Managed/EngineApi.cs`（改为生成产物的包含/删除手写体）、`Host.cs`（绑定序列改按清单；1282 行基数）。
- 判据：`Tools/check_csharp_host_failures.py`（期望 8→9）、`Tools/CodeGen/check_bindings.py`（登记表产物纳入对账）、`Tools/check_csharp_host_readme.py`、`Tools/CodeGen/check_bindable_audit.py` 口径不变但审计表行仍是硬门。
- 行为不变量（全部必须保持绿）：四个渲染探针 + 窗口回调全部现有断言、`GPUBackendTester` 不带 `--test` 全量用例、CoreTests、`python Tools/check_*.py` 全家族（现 12 份，文档需重 glob 不手抄）。
- 实测基线数（退役的起点，命令可复算）：手写 `interfaceId ==` 分派 30 处、`CSharpHost.cpp` 1026 行、`ScriptRenderBridge.cpp` 1152 行、`Host.cs` 1282 行、审计面 12 接口 90 方法、ABI v8 / 80 字节。

## Non-goals

- 不新增任何渲染能力（多阶段图、计算、读回、GPU 查询各自另立批次，骑在本 change 的机制上）。
- 不生成适配器方法本体，不把 `CScript*` 的校验逻辑改成声明式。
- 不改 `CRenderBackend`/`GPUGraph` 的任何签名，不做"把脚本窄接口折叠进原生 vtable"的字面合并（形状与可用性理由见 design）。
- 不动托管侧 `UserScript.cs` 的脚本语义与探针行为（仅 ABI 取表处变化）。
- 不做热路径零拷贝快路（审计表"需快路"仍为空，不在本批范围）。
