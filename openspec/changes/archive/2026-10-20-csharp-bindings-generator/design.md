## Context

规矩已经定完，数据也在手边：

- `openspec/specs/csharp-bindable-surface/spec.md`：三类边界形状、四类不可投影、快路约定、异常不跨边界。
- `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`：**7 个接口、60 条方法**逐条投影结论 —— **52 条可绑**、**8 条不可绑**（② 所有权不明裸指针 6 条、④ 语义越界 2 条）。
- CodeGen 现状：一个类型一份产物（反射 / 序列化 / 哈希三节）、接入清单 `build.py` 的 `CODEGEN_ANNOTATED`（76 行）、夹具 `Tools/CodeGen/TestShapes.h`、`verify_readme.py` 会跑 README 里每条命令、`check_bindable_audit.py` 对账接口方法与审计表。

## Goals / Non-Goals

**Goals**

- 给 CodeGen 加第 4 个发射器：从"被标注的接口"同时产出 C++ shim 与 C# 包装，形状**逐条**服从投影矩阵。
- 句柄表、布局清单、配对释放这些样板由生成器出，手写侧只留"把实例登记进来"那一行。
- 8 条不可绑的方法在生成期**报错并给建议**（退出码与诊断可核），绝不静默跳过。
- 每个矩阵形状有 conformance 夹具；一条端到端把生成物跑起来（经 `CSharpHost` 调 `IWindow::GetWindowSize`）。

**Non-Goals**

- 不改任何接口签名（6 条需要改形状的"② 类"另开 change）。
- 不实现 `...Span` 快路重载（审计出来的需快路清单是空的）。
- 不覆盖未审计的接口（`GPUGraph` / `IMGUIContext` / `IOBatch` / `WBatch` / `IShaderCompiler`）。
- 不做 AOT、热重载、多脚本程序集。

## Decisions

### D1 标注挂在**接口类**上，不挂在方法上；本次只标 `IWindow`

新增 `CA_BIND(...)`（与 `CA_REFLECT` 平行），标在接口类上；生成器只处理被标注接口的纯虚方法。**没标注的接口一律不处理、也不报错**（不猜）。这样"哪些接口要给 C# 用"是一个显式决定，而不是"扫到就生成"。

**本次标注清单（写死，执行时不许自己扩）**：

| 接口 | 纯虚方法 | 本次加 `CA_BIND` | 说明 |
|---|---|---|---|
| `IWindow` | 23 | **是** | 22 条可绑 + 1 条（`GetWindowSystem`）用 `CA_BIND_SKIP` 排除，等接口形状改造后再绑 |
| `IWindowSystem` / `CRenderBackend` / `IReadbackToken` / `IShaderCompilerManager` / `IOManager` / `IResource` | 37 | **否** | 留给后续 change（`IWindowSystem` 有 10 个回调，风险另评） |

覆盖口径：审计表 52 条可绑里本次覆盖 **22 条**，其余 **30 条**由后续 change 承接（名称待定；本表就是条数契约，4.1 要核）。判据：生成器日志的"生成清单"必须逐字等于本表（多一个或少一个接口都非零退出）。

### D2 语义越界只能由标注表达，机器判不出来

审计表里 8 条不可绑里**没有一条能靠形状自动判**：`CRenderBackend::Release`（无参）形状完全合法；而 `IOManager::Initialize` 的裸指针参数虽然会被"② 所有权不明裸指针"命中，同样形状的 `IWindow::GetNativeWindowHandle`（返回 `void*`）、`IWindowSystem::GetSystemNativeHandle`（返回 `void*`）、`IShaderCompilerManager::AquireShaderCompiler`（返回 `IShaderCompiler*`）、`ReturnShaderCompiler`（`IShaderCompiler*` 参数）审计结论却都是**可绑** —— 所以"裸指针一律拒绝"会把 4 条可绑方法误判成报错。结论：**② 类也不能自动判**，只能逐方法写 `CA_BIND_SKIP(方法名, "理由")`，理由进生成日志与产物注释；**没有理由的排除一律报错**（防"随手关掉"）。生成器自动判的只有 ① 模板方法与 ③ 依赖隐式转换的重载。

### D3 一接口一份产物，放独立目录

`<接口>.Bindings.cpp` 与 `<接口>.Bindings.cs`，落 `out/build/<预设>/codegen/bindings/`。与"一个类型一份产物"同构（现有流程已经会清目录、会打印接入清单），不新造一套产物管理。

### D4 函数表：全局一张、版本化、**本 change 只做"相等"校验**

```c
struct IWindowApi_v1 { uint32_t abi_version; uint32_t size; /* 方法指针 */ };
// 调用方把自己的版本与缓冲区大小传进来；引擎只写 min(调用方 size, 真实表大小) 这一段，并回填真实大小
extern "C" uint32_t IWindow_GetApi(uint32_t version, uint32_t size, struct IWindowApi_v1* out);
```
句柄作首参（`uint64_t self`）。**本 change 只验收"版本相等 + 大小相等"**（与已落地的 `Scripting/Managed/Host.cs` 口径一致）；"旧侧按自己较小的 size 也能通过"的追加兼容**不在本 change**——在只有出参的签名下它不成立（引擎不知道调用方缓冲区多大，写下去就越界）；真要追加兼容，得先把调用方的 size 传进来，那是后续 change 的事。

### D5 句柄表与布局清单都是生成物；布局清单要给编译期断言

- 句柄表：句柄是一个 `uint64_t`，**低 48 位放序号、高 16 位放代际号**。这两个位数是**配置项**（生成器参数 `--handle-index-bits`，默认 **48**，合法范围 **1 –（64 − 代际位数）**；代际号位数用 `--handle-generation-bits`，默认 **16**），默认值与范围写进 `Tools/CodeGen/README.md` 的参数表；理由：序号位给同时存活的句柄（默认 2^48 ≈ 2.8×10^14 个），代际位给"同一序号回绕多少次才复用"（默认 65536 次），取舍由使用方按自己的对象数量调。`release` 后代际号进位。**不写死 32 与 56 这两个界**（仓库里没有它们的出处）。
- 布局清单：每个过界结构体生成两份**同一数据的常量**（C++ 一份、C# 一份：size + 每字段 offset），托管侧 `Boot` 时逐项比对，不符就点名"哪个类型哪个字段"并拒绝启动。**并且 C++ 侧必须同时产出 `static_assert(sizeof(T) == <n>)` 与 `static_assert(offsetof(T, <字段>) == <n>)`** —— 只有两张同源常量相比时，生成器把布局算错会让两侧一起错、自检照样绿；静态断言把"算错布局"提前到**编译期**变红。

### D6 生成期拒绝：只有 ① 与 ③ 自动判，② 与 ④ 必须带理由排除

命中"模板方法 / 依赖隐式转换的重载"（① ③）→ 生成器**非零退出**，逐条打印"接口::方法 + 类别 + 建议"。**②（所有权不明裸指针）与 ④（语义越界）MUST NOT 自动判** —— 同样裸指针的 4 条方法（`IWindow::GetNativeWindowHandle` / `IWindowSystem::GetSystemNativeHandle` / `AcquireShaderCompiler` / `ReturnShaderCompiler`）在审计表里结论是"可绑"，自动判会把它们误杀；这两类只能逐方法写 `CA_BIND_SKIP(方法, "理由")`，带理由只打印、退出码 0。**"排除但没写理由"是唯一一条独立的非零退出**。

### D7 生成器启动时先核对审计表（子进程调用，不是 import）

CodeGen 是 C++（`Tools/CodeGen/CodeGenMain.cpp`），`check_bindable_audit.py` 是 Python —— 跨语言没有 `import`。所以：给 `check_bindable_audit.py` 加 `--json`，生成器以**子进程**方式调用 `python Tools/CodeGen/check_bindable_audit.py --json`，按退出码与 JSON 决定是否继续；子进程启动失败（找不到 python）也 MUST 非零退出并说明。这样"审计表"与"生成器"不会各自漂移，且不复制解析代码。

### D8 conformance 夹具：**矩阵 14 行，一行一个夹具**

夹具必须与 `Tools/CodeGen/README.md` 第十三节的 14 行矩阵**逐行对应**（不是"数量相等"）：定宽标量（含 `bool`/`enum` 合一）/ blittable 结构体 / `string const&` / `string_view` 返回 / `vector<T>`（T blittable）/ 输出 `vector` / `vector<T>`（T **非** blittable → 序列化块）/ `map`·`set`（摊平或序列化块）/ 智能指针返回 / 智能指针**参数** / `weak_ptr`（应拒绝）/ 出参 `T&` / `function` 回调 / 抛异常。做法照 `TestShapes.h` 的既有模式（夹具头 + 期望签名 + 比对脚本）。**判据是"矩阵每一行都能指到一个夹具"，而不是"两个数字相等"** —— 后者在缺 3 行、多 3 个重复夹具时会假绿。

### D9 手写与生成的边界（写死，不许事后协商）

| 生成（不许手改） | 手写 |
|---|---|
| 函数表结构体、thunk、`GetApi`、句柄表样板、C# 包装类、布局清单、`release` 函数 | "把实例登记进来"的一行（`Register<接口>(this, "名字")`）、托管壳的 `Boot`/`Tick`/日志桥、错误码 → 文本 |

**可失败判据**（不许用"代码评审时对表"）：生成物目录里 MUST NOT 出现手写登记行（脚本扫 `Register<` 命中数 = 0）；手写文件（托管壳、`IWindow` 实现）里 MUST NOT 出现生成的段标记（脚本扫生成段开头的注释标记命中数 = 0）。两条脚本读数都进 4.1。

## Risks / Trade-offs

- **[生成物与手写边界糊掉]** → D9 的两条脚本判据（生成物里 0 处 `Register<`、手写文件里 0 处生成段标记），不是"评审时看一眼"。
- **[布局清单随结构体演进漂移]** → 清单每次生成重建（不是人维护），并且两侧各出 `static_assert`：结构体改字段而生成没跟上 ⇒ **编译期**就红，不必等到启动自检。
- **[`CA_BIND` 普及后生成物爆炸]** → 一接口一份、只处理被标注接口；未标注不处理（D1），本次只标 `IWindow`。
- **[端到端怎么验]** → **必须经 `CSharpHost` 调真接口**：只测"生成的 C++ shim 对着假实现能跑"不算测过（C# 侧一行没跑、两侧布局也没比）。所以本 change 含四件事：`EngineApi` 末尾加取接口表的函数指针、`s_engineApiVersion` 1→2、`Host.cs` 同步、tester 里登记窗口；判据是"两侧 `GetWindowSize` 读数逐字一致"+"旧壳配新表必须报版本不符（退出码 3）"。
- **[生成器把审计表的建议抄错]** → 生成期的诊断直接引用审计表那一行的建议列（子进程 `--json` 解析，不手抄）。

## Migration Plan

1. 夹具与矩阵 conformance（先有判据）→ 2. 发射器骨架（只出签名、不出实现）→ 3. 句柄表与布局清单 → 4. 4 条不可绑的报错 → 5. 端到端一条 → 6. README 与退出码 → 7. 末条：构建 + 全部测试 + 双后端。

回滚：删发射器与产物目录、撤 `build.py` 的绑定量与 README 一节；引擎侧零改动。

## Open Questions

- C# 侧包装用 `[LibraryImport]` 还是纯函数指针（`delegate* unmanaged`）？倾向：**纯函数指针**（表本来就是指针，`LibraryImport` 是给具名导出用的）。
- 一接口一份产物是否够用（`IWindowSystem` 的 **10** 个回调会不会让单文件大到难读）？倾向：先按接口分；**拆分阈值不预设** —— 等第一个大接口的产物落地、量出行数后再决定，并把阈值写进 README 的配置键表（不拍脑袋写一个数字）。
