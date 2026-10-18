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

### D1 标注挂在**接口类**上，不挂在方法上

新增 `CA_BIND(...)`（与 `CA_REFLECT` 平行），标在接口类上；生成器只处理被标注接口的纯虚方法。**没标注的接口一律不处理、也不报错**（不猜）。这样"哪些接口要给 C# 用"是一个显式决定，而不是"扫到就生成"。

### D2 语义越界只能由标注表达，机器判不出来

审计表里 8 条不可绑里**没有一条能靠形状自动判**：`CRenderBackend::Release`（无参）形状完全合法；而 `IOManager::Initialize` 的裸指针参数虽然会被"② 所有权不明裸指针"命中，同样形状的 `IWindow::GetNativeWindowHandle`（返回 `void*`）、`IWindowSystem::GetSystemNativeHandle`（返回 `void*`）、`IShaderCompilerManager::AquireShaderCompiler`（返回 `IShaderCompiler*`）、`ReturnShaderCompiler`（`IShaderCompiler*` 参数）审计结论却都是**可绑** —— 所以"裸指针一律拒绝"会把 4 条可绑方法误判成报错。结论：**② 类也不能自动判**，只能逐方法写 `CA_BIND_SKIP(方法名, "理由")`，理由进生成日志与产物注释；**没有理由的排除一律报错**（防"随手关掉"）。生成器自动判的只有 ① 模板方法与 ③ 依赖隐式转换的重载。

### D3 一接口一份产物，放独立目录

`<接口>.Bindings.cpp` 与 `<接口>.Bindings.cs`，落 `out/build/<预设>/codegen/bindings/`。与"一个类型一份产物"同构（现有流程已经会清目录、会打印接入清单），不新造一套产物管理。

### D4 函数表：全局一张、版本化、只追加

```c
struct IWindowApi_v1 { uint32_t abi_version; uint32_t size; /* 方法指针 */ };
extern "C" uint32_t IWindow_GetApi(struct IWindowApi_v1* out);  // 填充并返回 size；版本不符返回 0
```
句柄作首参（`uint64_t self`）。**表只许末尾追加**（新版本 v2 在 v1 后面加字段，`size` 变大，旧托管侧按 v1 的 size 校验通过；新托管侧遇到旧引擎则拒绝）。

### D5 句柄表与布局清单都是生成物

- 句柄表：句柄是一个 `uint64_t`，**低 48 位放序号、高 16 位放代际号**。这两个位数是**配置项**（生成器参数 `--handle-index-bits`，默认 48、范围 32–56；代际号占剩下的位数），默认值写进 `Tools/CodeGen/README.md` 的配置键表；理由：低 48 位给同时存活的句柄（2^48 ≈ 2.8×10^14 个），高 16 位给代际号（同一序号可回绕 65536 次才复用），二者取舍由使用方按自己的对象数量调。`release` 后代际号进位。
- 布局清单：每个过界结构体生成两份**同一数据的常量**（C++ 一份、C# 一份：size + 每字段 offset），托管侧 `Boot` 时逐项比对，不符就点名"哪个类型哪个字段"并拒绝启动。

### D6 生成期拒绝（四类），退出码与诊断可核

命中"模板方法 / 所有权不明裸指针 / 依赖隐式转换的重载 / 未写理由的排除"→ 生成器**非零退出**（沿用现有退出码体系新增一档），逐条打印"接口::方法 + 属于哪一类 + 建议改法（引用审计表里那一条的建议）"。**语义越界**那 2 条因为有 `CA_BIND_SKIP` 带理由，属于合法排除，打印但不报错。

### D7 生成器启动时先核对审计表（子进程调用，不是 import）

CodeGen 是 C++（`Tools/CodeGen/CodeGenMain.cpp`），`check_bindable_audit.py` 是 Python —— 跨语言没有 `import`。所以：给 `check_bindable_audit.py` 加 `--json`，生成器以**子进程**方式调用 `python Tools/CodeGen/check_bindable_audit.py --json`，按退出码与 JSON 决定是否继续；子进程启动失败（找不到 python）也 MUST 非零退出并说明。这样"审计表"与"生成器"不会各自漂移，且不复制解析代码。

### D8 conformance 夹具：**矩阵 14 行，一行一个夹具**

夹具必须与 `Tools/CodeGen/README.md` 第十三节的 14 行矩阵**逐行对应**（不是"数量相等"）：定宽标量（含 `bool`/`enum` 合一）/ blittable 结构体 / `string const&` / `string_view` 返回 / `vector<T>`（T blittable）/ 输出 `vector` / `vector<T>`（T **非** blittable → 序列化块）/ `map`·`set`（摊平或序列化块）/ 智能指针返回 / 智能指针**参数** / `weak_ptr`（应拒绝）/ 出参 `T&` / `function` 回调 / 抛异常。做法照 `TestShapes.h` 的既有模式（夹具头 + 期望签名 + 比对脚本）。**判据是"矩阵每一行都能指到一个夹具"，而不是"两个数字相等"** —— 后者在缺 3 行、多 3 个重复夹具时会假绿。

### D9 手写与生成的边界（写死，不许事后协商）

| 生成（不许手改） | 手写 |
|---|---|
| 函数表结构体、thunk、`GetApi`、句柄表样板、C# 包装类、布局清单、`release` 函数 | "把实例登记进来"的一行（`Register<接口>(this, "名字")`）、托管壳的 `Boot`/`Tick`/日志桥、错误码 → 文本 |

## Risks / Trade-offs

- **[生成物与手写边界糊掉]** → D9 的表是**唯一裁决**；代码评审时逐文件对这张表。
- **[布局清单随结构体演进漂移]** → 清单每次生成重建（不是人维护），托管侧启动自检兜底；结构体改字段而清单没变 ⇒ 自检当场红。
- **[`CA_BIND` 普及后生成物爆炸]** → 一接口一份、只处理被标注接口；未标注不处理（D1）。
- **[端到端依赖 `CSharpHost`]** → D8 的夹具不依赖 host；只有一条端到端依赖 `csharp-hostfxr-bootstrap`（若它没做完，端到端那条 task 记成"被依赖挡住"而不是删掉）。
- **[生成器把审计表的建议抄错]** → 生成期的诊断直接引用审计表那一行的建议列（脚本解析，不手抄）。

## Migration Plan

1. 夹具与矩阵 conformance（先有判据）→ 2. 发射器骨架（只出签名、不出实现）→ 3. 句柄表与布局清单 → 4. 4 条不可绑的报错 → 5. 端到端一条 → 6. README 与退出码 → 7. 末条：构建 + 全部测试 + 双后端。

回滚：删发射器与产物目录、撤 `build.py` 的绑定量与 README 一节；引擎侧零改动。

## Open Questions

- C# 侧包装用 `[LibraryImport]` 还是纯函数指针（`delegate* unmanaged`）？倾向：**纯函数指针**（表本来就是指针，`LibraryImport` 是给具名导出用的）。
- 一接口一份产物是否够用（`IWindowSystem` 的 **10** 个回调会不会让单文件大到难读）？倾向：先按接口分；**拆分阈值不预设** —— 等第一个大接口的产物落地、量出行数后再决定，并把阈值写进 README 的配置键表（不拍脑袋写一个数字）。
