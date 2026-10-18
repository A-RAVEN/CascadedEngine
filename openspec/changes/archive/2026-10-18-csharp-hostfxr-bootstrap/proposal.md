## Why

C# 那条线现在**一步都没走**：仓库里 `.cs` 文件 0 个、`Experimental/DotNetHost/` 的两个文件被 `CMakeLists.txt:91` 的 `#add_subdirectory ("Experimental/DotNetHost")` 注释在构建外，`build.py` 也完全不管托管侧。而引擎侧的规矩已经定完（`csharp-bindable-surface` 已归档：7 个接口 60 条方法的投影结论、8 条不可绑、14 行投影矩阵）。

问题是：**没有 host，绑定生成器产出的东西既没有加载者也没有调用者**。所以这一步只做最小的一件事 —— 让"引擎把 CoreCLR 拉起来、调一个托管函数、拿回一个值"这条路真的跑通，并且失败时会响。绑定生成留给下一个 change。

## What Changes

- **新模块 `CSharpHost`（C++ 手写壳，普通引擎模块）**：按既有的模块规则注册（`CA_MODULE_INSTANCE` + `TryInit` / `TryLink` / `TryRelease`），内部用 `nethost` 定位 `hostfxr`、`hostfxr_initialize_for_runtime_config` 拉起运行时、拿到 `load_assembly_and_get_function_pointer` 委托、加载托管程序集、取 `Boot` 入口调一次。**不链接 CoreCLR**（只用运行时函数指针），所以换 .NET 版本不用重编它。
- **托管侧最小工程 `Scripting.Managed`（C# 手写壳）**：`Boot(IntPtr api)` / `Tick(double dt)` 两个 `[UnmanagedCallersOnly]` 入口、异常→错误码的唯一关口、日志桥、`abi_version` / `size` 自检。**不含任何生成的绑定**（本 change 一行绑定都没有）。
- **构建接入**：托管侧一个 `.csproj`，`build.py` 负责编它、把 `Scripting.Managed.dll` + `runtimeconfig.json` 落到 `out/build/<预设>/bin/`；缺 .NET SDK 时**构建失败并说清缺什么**，不许静默跳过。
- **判掉那个原型**：`Experimental/DotNetHost/private/CSharpHost.cpp`（2026-03-22，2.4 KB，已经写过 `load_hostfxr()`）要么被复用进 `CSharpHost` 模块，要么删掉 —— 依据写进 design，不许"留着不管"。
- **五条能失败的验收**：引擎调托管拿回值；删掉托管程序集 → 启动报错并点名缺哪个文件；`CASCADED_CSHARP_RUNTIMECONFIG` 指向不存在的路径 → 报错并列出找过的路径；配置里的 `TargetFramework` 在本机找不到匹配运行时 → 报错并列出本机实际有哪些运行时；托管方法抛异常 → 引擎侧收到错误码与文本（不许穿边界）。

## Capabilities

### New Capabilities

- `csharp-host`: 引擎把 CoreCLR 拉进本进程、加载托管程序集、双向调用的最小通道，以及它的失败语义（缺运行时 / 缺程序集 / ABI 不符都必须报错而不是静默降级）。

### Modified Capabilities

（无）—— 本 change 不改任何既有能力的要求。

## Non-goals

- **不生成任何绑定**：C++ shim、C# 包装、函数表、句柄表全部留给 `csharp-bindings-generator`。
- **不做热重载 / 线程拓扑 / 独立程序集加载上下文**：那是后续的计划步骤（第 5、6 步）。
- **不接 IMGUI、不接渲染**：本 change 的托管侧只做"被调一次并回一个值"，不碰引擎任何接口。
- **不用 NativeAOT**：先用 framework-dependent 的 JIT 形态跑通；AOT 发布形态另开 change（两者的加载方式不同，混在一起验收会互相掩盖）。
- **不改引擎既有接口、不改 `build.py` 的 codegen 流程**。

## Impact

- **新增**：`CSharpHost/`（模块源码 + 公开接口头）、`Scripting/Managed/`（`.csproj` + 手写壳）、`openspec/changes/csharp-hostfxr-bootstrap/` 工件。
- **修改**：`CMakeLists.txt`（加一个模块目标）、`build.py`（加托管侧编译与产物落地；缺 SDK 要失败）、`docs/TODO.md`（那条"判原型"的待办在这里结掉）。
- **删除（可能）**：`Experimental/DotNetHost/` —— 由 design 的原型判定结论决定。
- **风险**：`hostfxr` 依赖本机装的 .NET 运行时，别的机器可能没有（所以验收里"缺运行时必须报错并列出路径"是硬判据，不是可选项）；`TryInit` 阶段拉起 CLR 会带来启动耗时，需要实测并记录。
