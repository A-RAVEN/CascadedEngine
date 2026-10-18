## Context

现状（2026-10-18 实测）：

- 仓库里 `.cs` 文件 **0 个**；`build.py` 只管 MSVC + cmake，完全不管托管侧。
- `Experimental/DotNetHost/` 目录里是 **2 个文件**：`private/CSharpHost.cpp`（2.4 KB，2026-03-22，已经写过 `nethost` + `hostfxr` 的加载序列）与 `CMakeLists.txt`（34 行，含 `add_executable`、`nethost` 的 `IMPORTED` 目标、`POST_BUILD` 拷贝）。它**有构建规则**，只是根 `CMakeLists.txt` 第 91 行把 `add_subdirectory ("Experimental/DotNetHost")` 注释掉、从没被配置过；而它第 7 行把 nethost 路径写死在 `C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/8.0.4/...`，本机只有 8.0.23 / 9.0.12 / 10.0.2 三个 Host pack —— 这条写死的路径在本机是坏的。
- 本机 `C:\Program Files\dotnet\shared\Microsoft.NETCore.App` 下有 **8.0.6 / 8.0.23 / 9.0.12 / 10.0.2** 四个运行时；`hostfxr.dll` 只在 `C:\Program Files\dotnet\host\fxr\` 下的 **8.0.6 / 9.0.12 / 10.0.2** 三份里（8.0.23 有运行时、没有配套 hostfxr）。核对命令：`Get-ChildItem 'C:\Program Files\dotnet\host\fxr' -Directory | % { $_.Name + '=' + (Test-Path (Join-Path $_.FullName 'hostfxr.dll')) }`。
- 引擎的模块规矩已经成熟：DLL + 导出 `TryInit`/`TryLink`/`TryRelease`、`CA_MODULE_INSTANCE` 登记、`GetInstance<T>()` 取用、按依赖逆序销毁。新模块必须遵守，不另起一套。

## Goals / Non-Goals

**Goals**

- 引擎进程内能加载一个托管程序集、调一个函数、拿回一个值 —— 端到端可复现。
- 失败必须响：缺运行时 / 缺程序集 / `runtimeconfig` 不符 / 托管抛异常，四种都有明确报错。
- 托管侧的编译接进 `build.py`，缺 .NET SDK 时构建**失败并说清缺什么**。
- 把 `Experimental/DotNetHost` 的去留判掉（复用其序列并删目录，或明确保留理由）。

**Non-Goals**

- 不生成绑定（`csharp-bindings-generator`）；不做热重载 / 多脚本 / 线程拓扑 / AOT；不碰任何引擎接口；不改 codegen 流程。

## Decisions

### D1 原型处置：搬序列、删目录

`Experimental/DotNetHost` 的价值是**加载序列**（nethost → hostfxr → delegate），不是它的形态：入口两处 `assert` 在 Release 里等于没有；错路靠 `std::cerr` + 返回 `false` / `EXIT_FAILURE` 打到 stderr，没把原因与找过的路径交回调用方；而且**它的 `main` 只调 `load_hostfxr()` 就返回，`get_dotnet_load_assembly` 从未被调用** —— 也就是说"加载并调用托管方法"这半截它根本没跑过。所以：把加载序列搬进 `CSharpHost` 模块（定位改用 `nethost` 的 `get_hostfxr_path`，**不许沿用那条写死的 8.0.4 路径**），**删掉 `Experimental/DotNetHost/` 与 `CMakeLists.txt:91` 那行注释**，理由写进本 change 的提交信息。若执行时发现原型里有本设计没覆盖的东西（例如它处理过某个 hostfxr 版本差异），先把那一段搬过去再删，不许"先删后想"。

### D2 形态：framework-dependent 的 JIT，不是 AOT

`runtimeconfig.json` 指向 `Microsoft.NETCore.App`，由已装的共享运行时加载托管程序集。`TargetFramework` 写 **`net8.0`**（本机 SDK 9.0.310 / 10.0.102 都能编 `net8.0`，且 8.0.6 运行时存在），`rollForward` 写 **`LatestMinor`**（允许 8.0.x 内部前滚，不许跨大版本）。这两个值写进 `.csproj` 的 `<TargetFramework>` 与 `<RollForward>`，**在 design 里定死**，执行时不许临场改 —— 因为"`runtimeconfig` 与已装运行时不符"这条验收要能构造出来（见 D5 第 3 条）。

理由：本机有三套 hostfxr（8.0.6 / 9.0.12 / 10.0.2），AOT 发布形态（原生 dll、`LoadLibrary` + 导出函数）与 JIT 形态的加载方式完全不同，混在一个 change 里验收会互相掩盖。AOT 另开 change。

### D3 加载时机：`TryInit` 里加载，并记录耗时

最晚加载能省启动时间，但失败会拖到"第一次用"才暴露 —— 与本 change"失败必须响"的目标相悖。所以 `TryInit` 里加载、`CA_LOG` 记录耗时；`TryLink` 里把 `IEngineApi` 交给托管侧；`TryRelease` 里 `hostfxr_close` 并卸程序集。

### D4 边界只开两个入口，函数表只有一个函数

本 change 的托管侧只需要证明"双向可调"：

| 方向 | 形状 |
|---|---|
| 引擎 → C# | `[UnmanagedCallersOnly] static int Boot(IntPtr api)` 与 `static int Tick(double dt)`，由引擎取函数指针调用 |
| C# → C++ | `IEngineApi` v1：`uint32 abi_version; uint32 size; void (*log)(int level, char const* utf8, uint64 len);` + 一个 `echo(int32) -> int32` 用于验证回值 |

**不做**"把引擎接口指针交给托管侧"（那是绑定生成器的事）。

### D5 错误语义（四条硬判据）

0. **配置路径的注入点**：host 读环境变量 `CASCADED_CSHARP_RUNTIMECONFIG`；未设置时取 `<输出目录>/Scripting.Managed.runtimeconfig.json`。两条失败判据都挂在这个注入点上（设成不存在的路径 / 设成版本不符的配置），否则"配置里的路径"这句话无处落地。
1. 找不到 `hostfxr` → 报错并**列出找过的路径**（`nethost` 返回的路径 + 环境变量 `CASCADED_CSHARP_RUNTIMECONFIG` 指向的路径）。
2. 托管程序集缺失 / `runtimeconfig.json` 缺失 → 报错并点名缺哪个文件。
3. 配置里的 `TargetFramework` 在本机找不到匹配的运行时（例如把 `net8.0` 改成 `net7.0`）→ 报错并列出本机实际有哪些运行时版本。
4. `IEngineApi` 的 `abi_version` 或 `size` 不符 → 托管侧 `Boot` 返回错误码，引擎侧**拒绝继续**（不是带着错位继续跑）。
5. 托管方法抛异常 → 托管壳 try/catch，返回"错误码 + 一条文本"，引擎侧记日志；异常**不许穿边界**。

### D6 构建接入：默认开、缺 SDK 失败、显式开关才跳过

`build.py` 增加托管侧编译（`dotnet build` 一个 `.csproj`，产物落到 `out/build/<预设>/bin/`）。缺 .NET SDK 时**构建失败**并打印"需要什么、去哪装"。另给 `--without-managed` 显式开关：用了它才允许跳过，且必须打印"已跳过托管侧，C# 不可用"（显式降级，不静默）。

### D7 产物位置：与 `CSharpHost.dll` 同目录

`out/build/<预设>/bin/` 下放 `Scripting.Managed.dll` 与 `Scripting.Managed.runtimeconfig.json`。理由：`hostfxr_initialize_for_runtime_config` 按路径找程序集，同目录最省事；模块加载器本来就把 `bin/` 当工作目录。

## Risks / Trade-offs

- **[别的机器没有 .NET 运行时]** → 验收第 1、2 条就是为它写的（报错 + 路径清单）；D6 的"缺 SDK 构建失败"保证"看起来构建成功其实没有托管侧"不会发生。
- **[`TryInit` 拉 CLR 拖慢启动]** → 实测并记录耗时；如果超过可接受范围，改成首次 `Tick` 懒加载（**必须先有读数再改**，不预先优化）。
- **[hostfxr 的 API 稳定性]** → 只用 `initialize_for_runtime_config` + `get_runtime_delegate` + `close` 三个入口（官方 host 文档里的最小集）；版本差异靠"缺符号就报错"暴露，不猜。
- **[托管侧 GC 与引擎线程混用]** → 本 change 只允许主线程调用（`Boot`/`Tick` 都在引擎主循环里），第 5 步才谈线程拓扑。
- **[删掉 `Experimental/DotNetHost` 后有人以为丢了东西]** → 提交信息里写明"逻辑搬到了 `CSharpHost/`，见本 change 的 design D1"。

## Migration Plan

1. 先判原型（读全文件、结论落 design 或直接按 D1 搬）；
2. 建模块骨架 → 3. 建托管工程 → 4. 接 `build.py` → 5. 跑四条失败判据 → 6. 跑端到端；
7. 删原型目录与 `CMakeLists.txt:91` 的注释；8. 末条：构建 + `CoreTests` + 双后端确认没碰引擎行为。

回滚：删模块目录 + 撤 `build.py` 与 `CMakeLists.txt` 的两处改动即可（不涉及引擎既有接口）。

## Open Questions

- 托管壳要不要现在就按 AOT 兼容写（不用 `Assembly.Load`、只用 `[UnmanagedCallersOnly]`）？倾向：**是**，因为 AOT 迁移时不必重写入口。
- 程序集名是否固定为 `Scripting.Managed`？倾向：是，等绑定生成器落地后再按模块拆多个程序集。
