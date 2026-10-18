## 1. 判原型与建骨架

- [ ] 1.1 判掉 `Experimental/DotNetHost`：把 `private/CSharpHost.cpp` 与**上一级的** `Experimental/DotNetHost/CMakeLists.txt`（该目录共 2 个文件）**读完**，逐条记录"它覆盖了什么 / 缺什么（错路没交回原因与路径、main 只调 load_hostfxr、nethost 路径写死 8.0.4 而本机没有）"，结论写进 `evidence/prototype-verdict.md`（复用哪几段、删掉什么、依据）。**怎么算做完**：`evidence/prototype-verdict.md` 里能数出"读到 N 行、覆盖 M 项、缺 K 项"，且结论是"搬进模块 + 删目录"或"保留 + 理由"其中之一；`git grep -n 'Experimental/DotNetHost'` 的每一处（含 `CMakeLists.txt` 的第 91 行）都被结论覆盖。
- [ ] 1.2 建 `CSharpHost/` 模块骨架：CMake 目标（DLL）、导出 `TryInit` / `TryLink` / `TryRelease`、一个最小公开接口 `Interface/ScriptHost/header/IScriptHost.h`（只有 `bool IsAvailable()` 与 `int Tick(double)` 两个方法）并 `CA_MODULE_INSTANCE` 登记。**怎么算做完**：`python build.py --config Debug` 产出 `out/build/x64-debug/bin/CSharpHost.dll`；引擎启动日志里出现 `Register Module Factory: class …IScriptHost` 一行；`GetInstance<IScriptHost>()` 返回非空（打印一行读数）。
- [ ] 1.3 建托管工程 `Scripting/Managed/`：`Scripting.Managed.csproj` + `Host.cs`，实现 `[UnmanagedCallersOnly] static int Boot(IntPtr api)`、`static int Tick(double dt)`、函数表里 `log(level, utf8, len)` 与 `echo(int32)->int32`、`abi_version`/`size` 自检、所有入口 try/catch。**怎么算做完**：`dotnet build` 退出码 0，产出 `Scripting.Managed.dll` 与 `Scripting.Managed.runtimeconfig.json`（贴两条 `dir` 读数与文件大小）。
- [ ] 1.4 接 `build.py`：托管侧编译与产物落地（与 `CSharpHost.dll` 同目录）；缺 .NET SDK 时构建**失败**并说明缺什么；`--without-managed` 显式跳过时打印"已跳过托管侧，C# 不可用"。**怎么算做完**：正常构建 `BUILD SUCCESSFUL` 且托管产物存在；把 `dotnet` 从 PATH 里藏掉再构建 → **失败**且输出含"缺少 .NET SDK"；带 `--without-managed` 构建 → 成功且打印跳过提示（三组读数）。
- [ ] 1.5 端到端一次调用：`TryInit` 里加载并 `Boot`，主循环里一次 `Tick`。**怎么算做完**：引擎日志里出现托管侧写的那一行（含 `abi_version` 与 `size` 读数），且 `Tick` 的回值与托管侧打印的值逐字一致；启动到加载完成的耗时被记录（`加载耗时 = X ms`）。

- [ ] 1.6 删掉 `Experimental/DotNetHost/`（2 个文件）与 `CMakeLists.txt` 第 91 行 `#add_subdirectory ("Experimental/DotNetHost")`，理由写进提交信息（引用 1.1 的结论）。**怎么算做完**：`Test-Path Experimental/DotNetHost` 为 **False**；`Select-String -LiteralPath CMakeLists.txt -Pattern DotNetHost` 命中 **0**；`python build.py --config Debug` 仍 `BUILD SUCCESSFUL`（三组读数）。

## 2. 失败判据（每条都要红一次）

- [ ] 2.1 缺程序集：把 `Scripting.Managed.dll` 移走后启动。**怎么算做完**：模块初始化失败、错误信息点名缺的文件、进程退出码非零；把文件放回 → 恢复正常（两组读数）。
- [ ] 2.2 找不到运行时，两半都要红：**a** 把 `CASCADED_CSHARP_RUNTIMECONFIG` 设成一个不存在的路径启动；**b** 把配置里的 `TargetFramework` 改成 `net7.0`（本机没有 7.x）后启动。**怎么算做完**：a 的错误信息里**同时**出现该不存在的路径与 `nethost` 返回的 `hostfxr` 路径；b 的错误信息里出现 `net7.0` 与本机实际运行时版本清单（至少含 8.0.6 与 9.0.12）；两半各贴原始输出，还原后都能正常启动（共四组读数）。
- [ ] 2.3 ABI 不符：临时把引擎侧 `abi_version` 改成托管侧不认识的值。**怎么算做完**：`Boot` 返回非零错误码、引擎拒绝继续、错误信息里出现两边的版本号；还原后恢复（两组读数）。
- [ ] 2.4 托管抛异常：临时让 `Tick` 抛异常。**怎么算做完**：引擎收到非零错误码 + 一条非空文本，进程不崩溃；还原后恢复（两组读数）。
- [ ] 2.5 关闭顺序：正常退出。**怎么算做完**：日志里 host 的 `TryRelease` 出现在它依赖的模块之后，且出现"运行时已关闭"一行；`hostfxr_close` 与卸载各有一行日志。

## 3. 文档与校验

- [ ] 3.1 写 `CSharpHost/README.md`：它做什么 / 不做什么、失败语义（四类）、构建开关（`--without-managed`）、退出码与 `ABI` 字段含义、以及一条可直接复制的运行示例（带真实输出）。**怎么算做完**：示例命令原样跑过、输出与文档一致；文档里四条失败语义与 2.1–2.4 的实测一致。
- [ ] 3.2 `openspec validate csharp-hostfxr-bootstrap` → **valid**。**怎么算做完**：贴出 validate 输出。

## 4. 收尾

- [ ] 4.1 末条：`python build.py --config Debug` → `BUILD SUCCESSFUL`；`out/build/x64-debug/bin/CoreTests.exe` → 退出码 **0**、`PASSED$` **12** 行、`FAILED` **0** 行；两个后端各 `--headless 10` → 各退出码 **0**、**8 条 status 全 pass**；并记录启动耗时（加载托管侧的 ms 数）。**怎么算做完**：四组读数贴出；任何一项失败就修根因，不许改松判据。
