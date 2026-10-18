# 原型判定：`Experimental/DotNetHost`

**读到多少行**：`private/CSharpHost.cpp` **79 行** + `CMakeLists.txt` **34 行** = **113 行**（两文件都逐行读完）。

## 它覆盖了什么（5 项）

| # | 内容 | 位置 |
|---|---|---|
| 1 | `nethost` 的 `get_hostfxr_path` 定位 `hostfxr.dll`（`char_t` 缓冲 + `MAX_PATH`） | `CSharpHost.cpp:28-35` |
| 2 | `LoadLibraryW` + `GetProcAddress` 取三个导出：`hostfxr_initialize_for_runtime_config` / `hostfxr_get_runtime_delegate` / `hostfxr_close` | `CSharpHost.cpp:15-44` |
| 3 | `hostfxr_initialize_for_runtime_config(config_path, …)` 拉起运行时 | `CSharpHost.cpp:51` |
| 4 | `hostfxr_get_runtime_delegate(cxt, hdt_load_assembly_and_get_function_pointer, …)` | `CSharpHost.cpp:60-63` |
| 5 | `hostfxr_close(cxt)` 关闭上下文 | `CSharpHost.cpp:55`、`67` |

## 它缺什么（7 项）

| # | 缺什么 | 依据 |
|---|---|---|
| 1 | **"加载并调用托管方法"这半截从没跑过**：`main` 只调 `load_hostfxr()` 就 `return EXIT_SUCCESS`；`get_dotnet_load_assembly` **只有定义、零处调用** | `CSharpHost.cpp:71-79`；`git grep -n 'get_dotnet_load_assembly'` → 1 处定义（第 46 行）+ 0 处调用 |
| 2 | 错路不交回调用方：只往 `stderr` 打文本、返回 `bool`/`EXIT_FAILURE`，没有错误码、没有"找过哪些路径" | `CSharpHost.cpp:34-35`、`54-56`、`64-65`、`75-76` |
| 3 | 两处 `assert` 在 Release 里等于没有（`LoadLibraryW` / `GetProcAddress` 失败时直接走到空指针） | `CSharpHost.cpp:18`、`24` |
| 4 | **nethost 路径写死在 8.0.4**：`C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/8.0.4/...`，本机没有这个版本 | `CMakeLists.txt:7`；`Test-Path` 该目录 = **False**；本机 Host pack 只有 **10.0.2 / 8.0.23 / 9.0.12** |
| 5 | 形态不对：目标是 `add_executable`（exe），不是引擎模块 —— 没有任何 `CA_MODULE_API` / `TryInit` / `CA_MODULE_INSTANCE` 导出，不参与模块生命周期 | `CMakeLists.txt:15`；导出符号命中 **0** |
| 6 | 没有托管侧：仓库 `.cs` 文件 0 个，没有 `.csproj`、没有 `runtimeconfig.json` 的生成 | 全仓 `*.cs` 计数 = 0 |
| 7 | 三个 hostfxr 函数指针是**全局变量**（`init_fptr` / `get_delegate_fptr` / `close_fptr`），且 `cxt == nullptr` 时也对它调 `close_fptr` | `CSharpHost.cpp:11-13`、`55` |

## 结论：**搬加载序列进 `CSharpHost` 模块 + 删目录**

- **搬**：上表第 1–5 项（`get_hostfxr_path` 定位、三导出获取、`initialize_for_runtime_config`、`get_runtime_delegate`、`close`）的调用顺序与类型，直接搬进 `CSharpHost/` 模块的手写壳；但**改用 `get_hostfxr_path` 的返回路径，不沿用 `CMakeLists.txt:7` 写死的 8.0.4**。
- **不搬**：`assert`（换成错误码 + 路径清单）、全局函数指针（收进模块对象，失败可重入）、`std::cerr`（改用引擎日志，并把"找过的路径"交回调用方）。
- **删**：`Experimental/DotNetHost/`（2 个文件）与根 `CMakeLists.txt` 第 91 行的 `#add_subdirectory ("Experimental/DotNetHost")` —— 由本 change 任务 **1.6** 执行，理由引用本文件。

## `git grep -n 'Experimental/DotNetHost'` 的每一处归宿

| 命中处 | 归宿 |
|---|---|
| `CMakeLists.txt:91`（注释掉的 add_subdirectory） | 任务 **1.6 删除** |
| `docs/TODO.md:170-177`（那条"判原型"的待办） | 本文件即它的验收物（"写进该 change 的 design 或明确删除目录"两件都在做）；任务 **1.6** 里把该条目移到"已解决记录" |
| `openspec/changes/archive/001-reorganize-subprojects/*`（历史归档：当时把 `DotNetHost/` 移到 `Experimental/`） | 历史记录，**不动** |
| `openspec/changes/csharp-hostfxr-bootstrap/*`（本 change 的 proposal / design / tasks） | 自引用；任务 1.6 完成后这些引用就是"已删除对象的说明" |
