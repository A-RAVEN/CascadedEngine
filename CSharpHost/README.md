# CSharpHost：把托管运行时（C#）接进引擎的宿主模块

**它做什么**：在一个普通引擎模块的初始化阶段，把 .NET 运行时（CoreCLR）拉进本进程、加载 `Scripting.Managed.dll`、拿到托管侧 `Boot` / `Tick` 两个入口，并每帧调一次 `Tick`；同时把一张函数表交给托管侧，让它能回调引擎的日志、`echo`，以及按"接口编号 + 实例名"取引擎接口的函数表。

函数表（`EngineApi`）当前是 **v5**、56 字节，字段顺序就是 ABI、只许在末尾追加：

| 字段 | 从哪一版起 | 作用 |
|---|---|---|
| `abi_version` / `size` | v1 | 两侧握手：版本与大小都必须相等 |
| `log` / `echo` | v1 | 引擎回调（日志、echo 自检） |
| `get_interface_api` | v2（v4 加实例名参数） | 按 (接口编号, 实例名) 取该接口的函数表与实例；`size == 0` 是"只问大小"的探针调用 |
| `window_interface_id` / `window_system_interface_id` | v3 | 接口编号由引擎侧单向下发，托管侧不手抄 |
| `get_binding_layout_type` / `get_binding_layout_field` | v5 | 布局清单的引擎侧读数（真实 `sizeof` / `offsetof`）：托管 `Host.Boot` 逐项比对后才允许绑定 |

接口编号表（编号由引擎下发，这里只是给它一个可读的名字）：

| 编号 | 接口 | 实例名（本仓库现有的那份） |
|---|---|---|
| 1 | `cawindow::IWindow` | `Window` |
| 2 | `cawindow::IWindowSystem` | `WindowSystem_GLFW` |

同一个接口可以登记多份实现，靠实例名区分；实例名传 `nullptr` = 该接口**默认那一份**（最先登记的）。登记与查找都走 `IScriptHost::SetInterfaceInstance` / `GetInterfaceInstance`，两边必须用同一对 (编号, 实例名)。

**它不做什么**：

- **不生成任何绑定**：绑定产物由 `Tools/CodeGen`（`--emit-binding`）生成、由本模块 `#include` 进同一个翻译单元；哪个引擎接口能调、用什么形状过界，属于 `openspec/specs/csharp-bindable-surface/spec.md` 的规矩。
- **不做 AOT**：现在是 framework-dependent 的 JIT 形态（`runtimeconfig.json` 指向 `Microsoft.NETCore.App`）。
- **不建线程、不做热重载、不支持多脚本程序集**：`Boot` / `Tick` 都在引擎主线程上被调用。

## 脚本层：`Scripting/Managed/UserScript.cs`

- **写在哪**：`Scripting/Managed/UserScript.cs`（与宿主同程序集，编译期一起编进去）。使用者改这个文件、重新构建即生效，**不必碰 `Host.cs`**。
- **入口签名**：`public static void OnEngineReady(Scripting.Generated.Bindings.IWindowSystem windows)` —— 一个静态类、一个静态方法。
- **什么时候被调**：宿主 `EnsureBound()` 把**编号 1、2 两张函数表都绑定成功之后**调一次；**整个进程生命周期内只调一次**（重绑不会再调，所以脚本不会越建越多窗口）。
- **怎么判"调了几次"**：脚本里 `internal static int CalledCount` 自增，宿主把它打进日志 `[脚本] OnEngineReady 调用 #N`。
- **边界**：传进来的 `windows` 在引擎换默认实例之前一直有效，**不要存进静态字段跨重绑使用**；建出来的窗口是弱持有，不 `CloseWindow()` 就一直活到进程退出（`Dispose` 只释放句柄）。
- **失败**：脚本抛异常由宿主 `Tick` 的 `catch` 转成非零返回码 + `LastError`（带异常类型与消息），不会静默。

## 构建

```powershell
python build.py --config Debug
```

托管侧由 `build.py` 的托管段编译，产物（`Scripting.Managed.dll`、`Scripting.Managed.runtimeconfig.json`、`Scripting.Managed.deps.json`）拷到 `out/build/x64-debug/bin/`，与 `CSharpHost.dll` 同目录 —— `hostfxr` 按路径找程序集，同目录最省事。

- **缺 .NET SDK 时构建失败**（不静默跳过）：报 `托管侧构建失败：缺少 .NET SDK：PATH 里找不到 dotnet 命令…`。
- 要走"明知没有托管侧"的路，加 `--without-managed`：构建成功，并打印 `已跳过托管侧，C# 不可用（--without-managed）：产物目录里不会有 Scripting.Managed.dll`。

`nethost` 由 CMake 在 `%ProgramFiles%\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\*\runtimes\win-x64\native` 里**取最新的一份**（不写死版本）。找不到时引擎照样能编出来，运行期会报错并列出找过的路径。

## 环境变量

| 变量 | 作用 | 默认 |
|---|---|---|
| `CASCADED_CSHARP_RUNTIMECONFIG` | 指定托管侧的 `runtimeconfig.json` 路径 | `<bin>\Scripting.Managed.runtimeconfig.json` |
| `CASCADED_CSHARP_SELFTEST=abi-mismatch` | **仅 Debug**：引擎侧故意把 `abi_version` 加一，用来构造"ABI 不符"失败路径 | 不设 |
| `CASCADED_CSHARP_SELFTEST=throw` | **仅 Debug**：托管侧第一次 `Tick` 抛一个异常，用来构造"托管抛异常"失败路径 | 不设 |
| `CASCADED_CSHARP_SELFTEST=layout-mismatch` | **仅 Debug**：引擎侧把布局清单第一条字段的偏移加 8，模拟"引擎与托管清单不同源"；`Host.Boot` 必须拒绝启动并点名字段 | 不设 |
| `CASCADED_CSHARP_SELFTEST=monitor-mismatch` | **仅 Debug**：托管侧打印 `GetMonitor` 读数时把 `m_MonitorRect.width` 加 1，用来证明 `Tools/check_window_system_bindings.py` 会报字段错配 | 不设 |
| `CASCADED_CSHARP_SELFTEST=monitor-skip-invalid` | **仅 Debug**：托管侧跳过两次无效显示器编号调用，用来证明脚本会因"放过无效编号"变红 | 不设 |
| `CASCADED_CSHARP_CALLBACK_PROBE=1` | 启用 10 类窗口回调探针（只有 `TestCSharpWindowCallbacks` 设置） | 不设 |
| `CASCADED_CSHARP_CALLBACK_PHASE` | 回调探针阶段机（`verify-all` / `verify-replace` / `verify-exception` / `verify-thread` / `verify-rebind` / `verify-rebind-done`），由测试在两次 `Tick` 之间设置 | 空串 |

## 失败语义（五条，全部实测过）

初始化失败时 `CSharpHost` 打印一行错误并**让进程以退出码 3 退出** —— 引擎里没有错误回传通道，带着"没有 C# 支持"继续跑正是规范里 MUST NOT 的静默降级。

| 情形 | 实测输出（节选） |
|---|---|
| 托管程序集缺失 | `CSharpHost 初始化失败（退出码 3）：解析托管入口 Boot 失败，返回 -2146233079（程序集 …\bin\Scripting.Managed.dll）；找过的路径：…\hostfxr.dll、…\Scripting.Managed.runtimeconfig.json` |
| 配置路径不存在 | `CSharpHost 初始化失败（退出码 3）：找不到托管侧的 runtimeconfig.json：…\bad.runtimeconfig.json；找过的路径：C:\Program Files\dotnet\host\fxr\10.0.2\hostfxr.dll、…\bad.runtimeconfig.json` |
| 配置要的运行时版本本机没有 | `hostfxr_initialize_for_runtime_config 返回 -2147450730（…；配置里要的框架版本：7.0.0；本机已装的运行时版本：10.0.2、8.0.23、8.0.6、9.0.12）` |
| 函数表 ABI 版本不符 | `CSharpHost 自检开关已生效：故意把 abi_version 改成 6（真实值 5）` + `[托管侧] ABI 版本不符：引擎 6，托管侧期望 5` + `托管侧 Boot 返回错误码 1（引擎侧 ABI 版本 6、函数表 56 字节；0 = 成功）` |
| 托管方法抛异常 | `[托管侧] Tick 抛异常：InvalidOperationException: …` + 引擎侧收到错误码 **4**；进程**不崩** |

## 关闭

`hostfxr_close` 在 `CSharpHost` 实例析构时调用（早于模块卸载），并打印 `[CSharpHost] 运行时已关闭（hostfxr_close 已调用）`。**不 `FreeLibrary(hostfxr)`**：卸载它会让仍在运行的托管代码崩（实测 `0xC0000005`，两次）；`hostfxr` 的生命周期就是进程的生命周期，退出时由系统回收。模块的 `TryRelease` 导出会再调一次关闭，是幂等空转。

## 可直接复制运行的检查

跑一次测试宿主（`--headless 2` 跑两个测试），看托管侧与引擎侧的读数：

```powershell
& 'out\build\x64-debug\bin\GPUBackendTester.exe' --backend vulkan --headless 2 2> $env:TEMP\csharp.err *> $env:TEMP\csharp.out
Select-String -LiteralPath $env:TEMP\csharp.out -Pattern '托管侧|IScriptHost|CSharpHost|Tick #'
```

实测输出（节选）：

```
[托管侧] 托管侧已启动：ABI 版本 5、函数表 56 字节
[托管侧] 回调引擎 Echo(42) 得到 43
CSharpHost：托管运行时已就绪（加载耗时 = 39 ms；函数表 56 字节，ABI v5）
IScriptHost 已取到：IsAvailable = true
[托管侧] Tick #1：dt = 0.016 秒
托管侧 Tick #1 回值 1（引擎侧计数与之一致）
第一次跨边界 Tick 的回值 = 1（与托管侧日志里的 #N 逐字比对）
```

**已知限制（醒目）**：

- 托管侧只认每个接口的**默认实例**（`get_interface_api` 的实例名传 `nullptr`）；按名寻址目前只有引擎与测试在用 —— 同一个接口的多份实现要能被脚本分别拿到，还得等托管表面的那一轮（`csharp-window-creation` 任务 5.6）。
- 阻塞调用（`WaitIdle`、回读的 `Wait`）若从托管侧发起会卡住引擎主线程；现在的托管侧只做日志与计数，不碰这些。
- 托管侧只在主线程被调用；线程拓扑、多脚本、热重载都不在本 change 的范围。
- `nethost` 找不到时构建不失败、运行期报错 —— 这是有意的（让缺 .NET 的机器也能编引擎），但意味着"构建成功"不等于"C# 可用"。
