## Why

可绑定面的规矩已经定完了：`openspec/specs/csharp-bindable-surface/spec.md` 给出三类边界形状、四类不可投影、异常不跨边界等**要求**；**14 行投影矩阵**在 `Tools/CodeGen/README.md` 的「十三、可绑定面」节（第 987–1000 行是它的数据行）；`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md` 里是 7 个接口 **60 条**方法的逐条结论：**52 条可绑、8 条不可绑**（其中 6 条是"所有权不明裸指针"、2 条是"语义越界"）。规矩有了，但**一行绑定代码都不存在** —— 而这一层手写不出来：52 条方法 × 两侧（C++ shim + C# 包装）× 句柄表 × 布局清单，手写必然漂移。

所以这一步把规矩变成生成器：给 CodeGen 加第 4 个发射器，按投影矩阵出两侧绑定；对 6 条形状不合法的 MUST **报错并给建议**，对 2 条"语义越界"的 MUST 用**带理由的显式排除**处理（只打印、退出码 0），对"排除但没写理由"的 MUST 报错 —— 三者都不许静默跳过。

## What Changes

- **CodeGen 第 4 个发射器**（`--emit-binding`，与现有三个发射器并列）：输入是"被标注为可绑定的接口"（新增标注，与 `CA_REFLECT` 平行），输出两份产物：
  - `<接口>.Bindings.cpp`：每个可绑方法一个 `extern "C"` thunk、一张版本化函数表结构体（`abi_version` + `size`）、以及"取表函数"；
  - `<接口>.Bindings.cs`：C# 包装类（方法 → 函数指针字段）、blittable 句柄结构体、**布局清单**（每个过界结构体的 size + 每个字段 offset，供托管侧启动自检）。
- **句柄表与配对释放**：生成器产出句柄分配/释放的样板（`shared_ptr` / `unique_ptr` 返回值 → 句柄；`release` 配对；代际号防旧句柄命中新对象）。**手写部分只有"把实例登记进来"那一行**。
- **四类不可投影的报错**：接口方法命中"模板 / 所有权不明裸指针 / 依赖隐式转换的重载 / 语义越界"之一 → 生成器**非零退出**并点名"哪个接口的哪个方法、属于哪一类、建议怎么改"，绝不生成看着能编译、调用即崩的绑定。
- **conformance 测试**：投影矩阵每类形状一个夹具，断言产出的 C ABI 签名逐字符合矩阵（与现有 `TestShapes.h` 的做法一致）。
- **与 host 联动的一条端到端**：`csharp-hostfxr-bootstrap` 的托管壳里加一行 —— 调 `IWindow::GetWindowSize` 拿回真实的窗口尺寸并打日志。这条把"生成物 + 函数表 + 句柄 + 托管壳"四样串起来。

## Capabilities

### New Capabilities

- `csharp-bindings`: 从被标注的接口生成"C++ shim + C# 包装 + 版本化函数表 + 句柄表 + 布局清单"的能力，以及它对不可投影形状的拒绝语义。

### Modified Capabilities

（无）—— 本 change 不改既有能力的要求；`csharp-bindable-surface` 的矩阵原样消费，不改一个字。

## Non-goals

- **不改任何接口签名**：审计表里 6 条"② 所有权不明裸指针"的改法（`ExecuteGraph` 拆两步、`GetWindowHandle` 收窗口 id、`NewWindow` 返 id、`WBatch`/`IOBatch` 句柄化、删自引用）都属于**接口改造**，另开 change；本 change 对它们只做"报错 + 给建议"。
- **不实现 `...Span` 快路重载**：审计出来的"需快路"清单是空的（60 条里没有每帧容器调用），没有消费者就不做。
- **不覆盖 `GPUGraph` / `IMGUIContext` / `IOBatch` / `WBatch` / `IShaderCompiler`**：它们还没审计（`csharp-bindable-surface` 的已知限制里写着），生成器对未标注接口一律不处理。
- **不做 NativeAOT、不做热重载、不做多实例脚本**。
- **不改 `build.py` 的既有 codegen 流程**：新增的是独立开关与独立产物目录。

## Impact

- **新增**：`Tools/CodeGen/` 里的发射器与测试；生成物两类文件（`.Bindings.cpp` / `.Bindings.cs`）与产物目录；`openspec/changes/csharp-bindings-generator/` 工件。
- **修改**：`Tools/CodeGen/README.md`（新增发射器的用法与退出码、句柄位数等配置键）、`build.py`（新增绑定的接入清单与产物落地，缺 .NET 时绑定这一档可选跳过但必须打印"跳过了什么"）、`Scripting/Managed/`（托管壳里加一行端到端调用）、`Test/GPUBackendTester` 的 `TestSimpleTriangle`（加一行打印窗口尺寸，供端到端逐字比对）。
- **依赖**：需要 `csharp-hostfxr-bootstrap` 的 `CSharpHost` 模块才能做端到端那一条；生成器本身与 conformance 测试不依赖 host。
- **风险**：生成物与手写壳的边界容易越走越糊（design 里要写死"哪些是生成的、哪些必须手写"）；布局清单会随结构体演进而漂移（靠生成期重新出清单 + 托管侧启动自检兜住，而不是靠人记）。
