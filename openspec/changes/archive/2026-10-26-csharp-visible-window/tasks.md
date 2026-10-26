# 任务清单：csharp-visible-window

> 勾选依据一律是**实测**。每条都写清"怎么算做完"，能失败的那条必须能红。
> 构建一律 `python build.py --config Debug`（前台）。

## 1. 先修会被脚本窗口打破的既有断言

- [x] 1.1 改 `Test/GPUBackendTester/Main.cpp` 里 `TestCSharpWindow` 的两条计数断言：现在的口径是 `countAfterSurfaceTick == countBaseline + 1` 与 `countAfterSurfaceUpdate == countBaseline`，脚本窗口常驻之后这两条必红。把基线挪到**引擎自己那个 800×600 窗口被回收之后**读一次（也就是现有 `UpdateSystem()` 读 `countAfterUpdate` 那一处之后），使两条断言的语义仍是"表面建的窗口 +1 / 回收之后回基线"，但把引擎窗口与脚本窗口都排除在外。**不能取在第一次 `Tick` 之后**：那时引擎那个窗口还在（它要到 `UpdateSystem()` 之后才被回收），基线会把它算进去，`+1` 就永远差 1
  验收：不带 `--show-window-seconds` 跑 `--test TestCSharpWindow --report test_output/result_show.json`，`status` 为 `pass`；**红态不依赖 2.3 的脚本窗**（那时脚本还没建窗，改动前后读数都是 `0 → 1 → 0`，靠脚本窗造不出红）—— 红态要自己造：临时在测试里、读新基线之前多建一个 320×240 窗口且不关（模拟"常驻窗口"），此时新口径仍是 `基线 1 → 2 → 1` 且 `pass`，而把基线改回旧口径（建 C++ 窗口之前那个）就必红并报 `E_SURFACE_COUNT`。贴出三次输出：新口径不造窗口（绿）、新口径造常驻窗口（绿）、旧口径造常驻窗口（红）。改前/改后原文写进本条正文
  实测（2026-10-26）：改前原文 —— `if (countAfterSurfaceTick != countBaseline + 1)` / `if (countAfterSurfaceUpdate != countBaseline)`，`countBaseline` 读在建 C++ 窗口之前；改后原文 —— 在第三次 `Tick` 之前**就地重读** `int32_t const countSurfaceBaseline = ctx.pWindowSystem->GetWindowCount();`，两条断言与两条错误文案全部改用它，注释里写明"不能取在第一次 `Tick` 之后，那时引擎窗口还没回收"。三态实测：① 新口径、不造窗口 ⇒ `表面建窗计数：基线 0 → Tick #3 之后 1 / UpdateSystem 之后 0`、`status=pass`；② 新口径 + 临时常驻窗口（`NewWindow(320,240,"脚本窗口（模拟）")` 且不关）⇒ `基线 1 → 2 → 1`、`status=pass`；③ **旧口径** + 同一个常驻窗口 ⇒ `status=fail`，`error=托管侧通过表面建的窗口没有出现在计数里：Tick #3 之后 = 2、基线 = 0（期望 +1）`（即 `E_SURFACE_COUNT`）。模拟窗口与标记注释已删除（`grep simulatedScriptWindow|临时红态` 命中 0）

## 2. 脚本层

- [x] 2.1 新建 `Scripting/Managed/UserScript.cs`：静态类 + 唯一入口 `public static void OnEngineReady(IWindowSystem windows)`，实现体先 `{ }`（空）；另加一个 `internal static int CalledCount` 计数器（2.2、2.3 都要用它，不必再改文件）
  验收：**只证明"这个文件真参与编译"**，不要求跑出运行时红态（调用点在 2.2 才加，此刻 `OnEngineReady` 不可能被调到）—— 把文件里故意写坏一处（例如删掉一个分号），`python build.py --config Debug` 必须失败且报错里点名 `UserScript.cs`；改回后必须 `BUILD SUCCESSFUL`。两步输出都贴出来。运行时的红态（throw → `Tick()` 非零）放到 2.2 验
  实测（2026-10-26）：① 新建后构建 ⇒ `BUILD SUCCESSFUL`（同时证明 `IWindowSystem` 这个表面类型在脚本里能被解析）；② 把 `CalledCount++;` 的分号删掉 ⇒ `build=1`，报错逐字为 `E:\Projects\CascadedEngine\Scripting\Managed\UserScript.cs(31,26): error CS1002: 应输入 ; [E:\Projects\CascadedEngine\Scripting\Managed\Scripting.Managed.csproj]` —— 点名了该文件；③ 分号改回 ⇒ `BUILD SUCCESSFUL`。文件里同时带了 `internal static int CalledCount`（在 `OnEngineReady` 里自增）供 2.2、2.3 的判据读

- [x] 2.2 `Host.cs` 加调用点：可核条件写成 `s_system != null`，且**重绑失败时要把 `s_system` 置空**（否则它保留上一次的旧对象，"两表都绑好"这个条件会被旧值骗过）；用 `s_userScriptCalled` 保证只调一次；`OnEngineReady` 里把 `UserScript.CalledCount` 加一（供 2.2/2.3 的判据读）；脚本抛异常时沿用现有错误路径，转成 `Tick()` 非零返回码 + `s_lastError` 文本
  验收：五条各有一次实测 —— ① 两张表都绑定成功后那次 `Tick` 调用一次：`Tick` 之后读 `UserScript.CalledCount == 1`（**不要**拿 `[脚本] 建窗：` 那行当判据，那是 2.3 才加的）；② 重绑不调：改一次默认实例指针再 `Tick`，`CalledCount` 仍为 1；③ 未绑不调：跑 `--test TestScriptHost`，`first` 仍为 1、`LastError` 里**没有**脚本相关字样（`自检` / `OnEngineReady`）—— 注意该测试**本来就失败**（`LastError` = "接口编号 1 下没有登记实例名"，与本 change 无关），所以判据只能看"有没有脚本字样"，不能要求它绿；④ 只绑到一半不调：**可以构造** —— 在测试里只调 `SetInterfaceInstance(1, …)` 不调编号 2，再 `Tick`，此时 `CalledCount` 仍为 0 且不吃空引用；⑤ 运行时红态：在 `OnEngineReady` 里临时写 `throw new InvalidOperationException("自检");`，`Tick()` 必须返回非零且 `LastError` 带 `自检`，删掉后恢复绿
  实测（2026-10-26）：`Host.cs` 里加了 `s_userScriptCalled` 字段、重绑分支里 `s_system = null;`、以及绑定成功后调脚本并打日志 `[脚本] OnEngineReady 调用 #N`（引擎侧只能看日志，用它判"只调一次"）。五条读数：① `--test TestCSharpWindow` ⇒ `status=pass`，日志里 `[脚本] OnEngineReady 调用 #1` 在 `Tick #1` 之后**只出现 1 次**；② 同一次运行里 `Tick #3` 之前引擎重新登记了新窗口（重绑），`OnEngineReady` 次数仍为 1；③ `--test TestScriptHost` ⇒ `Tick #1` 回值 1、日志里 `OnEngineReady` **0 次**、`自检` **0 次**（该测试自身仍 fail，原因是既有的"接口编号 1 下没有登记实例名"，与本条无关）；④ 临时把编号 2 的登记注释掉（只绑到一半）⇒ `OnEngineReady` **0 次**、`NullReference` **0 次**、`GetWindowCount = -1`（哨兵值）；**这条第一次跑是红的**：`ProbeWindowSize` 与 `ProbeSurfaceWindow` 直接解引用 `s_system`，半绑定态下抛 `NullReferenceException` —— 已在两处加判空（`s_system != null ? … : -1`、`s_system == null` 时直接 return），这正是验收④要抓的东西；⑤ 在 `OnEngineReady` 里临时 `throw new InvalidOperationException("自检")` ⇒ 日志出现 `Tick 抛异常：InvalidOperationException: 自检`、`自检` 计数 1，`Tick` 走 `ErrException` 分支（非零）；删掉后 `BUILD SUCCESSFUL`、`status=pass`、`OnEngineReady` 次数回到 1。临时改动与注释均已清除（`grep 自检|临时半绑定态` 仅剩 `UserScript.cs` 注释里那句"ABI 自检"）

- [x] 2.3 脚本里写真正的建窗代码：`windows.NewWindow(...)` 建一个窗口、打一行日志（标题 + 尺寸），**不关窗、不 Dispose**
  验收：日志里出现 `[脚本] 建窗：<标题> <宽> x <高>`；同一次运行里 `GetWindowCount()` 比**脚本建窗之前**大 1（与 `spec.md` 里那条场景的措辞一致），且 `UpdateSystem()` 之后**不**回落
  实测（2026-10-26）：`UserScript.OnEngineReady` 里建 320×240、`visible/focused/decorate=true`、`floating=false`，读回尺寸打 `[脚本] 建窗：脚本建的窗口 320 x 240`，**不关窗、不 Dispose**。引擎侧在第一次 `Tick` 之前记 `countBeforeScript`、之后读计数并调一次 `UpdateSystem` 再读一次，两条断言写进 `TestCSharpWindow`（短码 `E_SCRIPT_WINDOW` / `E_SCRIPT_KEEP`）。实测读数：`脚本建窗计数：建窗前 1 → Tick #1 之后 2 / UpdateSystem 之后 2`（+1 且不回落）、`status=pass`；同一次运行里表面建窗那两条读数仍是 `基线 1 → Tick #3 之后 2 / UpdateSystem 之后 1`，说明脚本窗常驻没把既有断言带红
## 3. 引擎侧跑法

- [x] 3.1 `Test/GPUBackendTester/Main.cpp` 解析 `--show-window-seconds <N>`：缺值或非正整数 → 非零退出 + stderr 说明用法；缺省（不传）= 不进入保持循环
  验收：**七种传法**各跑一次 —— 合法侧两种：`--show-window-seconds 3` 与不传都退回 0、`--report` 指定的文件都生成（"日志里有没有保持循环那几行"由 3.2 判，本条只判参数收不收）。非法侧五种：`--show-window-seconds`（缺值）、`abc`、`0`、`-1`、外加**溢出值 `9999999999`**（`strtol` 会饱和成 `2147483647`，不额外判溢出就会当合法值收下）都以非零退出并在 stderr 打出该参数的用法。注意命令行必须带 `--report <路径>`：不带 `--headless <N>` 时程序不会自动生成 `result.json`，判据拿不到文件就是判据写错了
  实测（2026-10-26）：解析加在 `--report` 之后（`errno = 0` + `strtol` + 查 `end == text` / `*end != '\0'` / `errno == ERANGE` / `val <= 0` / `val > INT_MAX`），变量 `showWindowSeconds` 初值 0 表示"不进入保持循环"。七次实测：`--show-window-seconds 3` ⇒ exit **0**、`test_output/r31_ok.json` 生成；不传 ⇒ exit **0**、`test_output/r31_none.json` 生成；缺值 ⇒ exit **1**、stderr `Error: --show-window-seconds requires a positive integer (seconds)`；`abc` / `0` / `-1` / `9999999999` ⇒ 各 exit **1**、stderr 逐字 `Error: --show-window-seconds value must be a positive integer, got: <原值>`。溢出值没被当合法收下（`INT_MAX` 那条判据起了作用）

- [x] 3.2 `main` 里在**所有选中的测试跑完之后**加保持循环：每轮 `Tick()` + `UpdateSystem()`，累计墙钟满 N 秒退出，并把**实际循环次数与实际秒数**都打进日志
  验收：判据是**可数事件**，且必须把 `Tick()` 的实际调用次数写进日志再判它：`--show-window-seconds 5` 跑完后，日志里要同时有"循环次数""`Tick()` 调用次数""实际秒数"，判据要求 `Tick()` 调用次数 **> 0** 且与循环次数相等；**另加一条计数判据**：保持结束时 `GetWindowCount()` 仍把脚本窗口算在内（否则保持期窗口被关掉也能绿，可见性就只剩 4.1 的人工截图）。**红态**：把循环体改成只 `Sleep`、不调 `Tick()`。参考读数来自主循环用 PowerShell 复刻的两种循环体（各 1 秒：泵帧版循环 8 次 / `Tick` 8 次；只 Sleep 版循环 9 次 / `Tick` 0 次）—— **那是复刻、不是本 change 的产物**，实现后要贴真实读数。只看"循环次数 × 单轮耗时 ≈ 秒数"两版都会绿，所以判据必须读 `Tick` 次数，读到 0 就红。贴出这两次输出
  实测（2026-10-26）：循环写在测试分派之后、`closeJsonArray` 之前，每轮 `Tick()` + `UpdateSystem()` + 让出 1 ms，满 N 秒退出；三条判据（`Tick` 次数 > 0、等于循环次数、计数不回落）在代码里算成一个 `keepOk`，结果落到**独立文件** `test_output/keep_window.log`。**为什么不用 `CA_LOG` 或 `stderr`**：测试自己的日志文件此时已关闭、`stdout`/`stderr` 都被改道到它上面，实测两条路都收不到这行（第一次跑时墙钟确实多了 3 秒，但日志里一个字都没有）；改成 `fopen("test_output/keep_window.log","w")` 才稳定。三次实测：① `--show-window-seconds 3` ⇒ `保持窗口判据：通过（循环 1489 次 / Tick 调用 1489 次 / 实际 3.00 秒、要求 3 秒 / 计数 1 → 1）`、exit 0；② **红态**（把 `Tick` 与 `++tickCalls` 注释掉、只留 sleep）⇒ `保持窗口判据：失败（循环 1006 次 / Tick 调用 0 次 / 实际 2.00 秒、要求 2 秒 / 计数 1 → 1）`；③ 不传 `--show-window-seconds` ⇒ 不进入循环、`test_output/keep_window.log` **不存在**（这条原属 3.1 的验收，按"3.2 才有保持循环"挪到这里）。红态注释已删（`grep 红态构造` 0 命中）

## 4. 证据与文档

- [x] 4.1 跑一次带保持模式的运行并截图，图里能按标题认出脚本建的窗口；截图与日志存 `openspec/changes/csharp-visible-window/evidence/`
  验收：`evidence/visible-window.png` 存在且人工看图能认出标题；`evidence/show-window.log` 里有 `[脚本] 建窗：` 与保持循环那两行。**这张图是人工核过的单次证据，不参与自动化判定**（仓库里没有截图比对工具）；自动化判据是 2.3 与 3.2 里的日志与计数
  实测（2026-10-26）：命令 `GPUBackendTester.exe --backend vulkan --test TestCSharpWindow --headless 60 --report test_output/r41.json --show-window-seconds 10`，进程起来约 5 秒后用 `desktop-screenshot` 拍全屏，图里能直接看到一个标题为**「脚本建的窗口」**、客户区白色的 320×240 窗口（2560×1600 全屏图，549,181 字节）。`evidence/show-window.log` 里三行读数：`[托管侧] [脚本] OnEngineReady 调用 #1`、`[托管侧] [脚本] 建窗：脚本建的窗口 320 x 240`、`保持窗口判据：通过（循环 5021 次 / Tick 调用 5021 次 / 实际 10.00 秒、要求 10 秒 / 计数 1 → 1）`，报告 `status=pass`。**一处返工**：`[脚本] 建窗：` 原本由 `UserScript` 自己 `Console.WriteLine` 打，实测同一条命令两次落点不同（一次进测试日志、一次只进控制台）—— 改成脚本只把尺寸放进 `UserScript.LastCreatedSummary`、由宿主 `Host.LogLine` 统一打，两次运行就都稳定进日志了

- [x] 4.2 文档三处：`docs/testing.md` 补 `--show-window-seconds` 的语义，并修正那里**四处**过期数字（第 10、31 行的"9 个测试" → 实测 10；第 29、119 行的 `PASSED` 12 → 实测 17；第 119 行的"两份 report 里 8 条 status" → 实测 10 条；第 33 行那份测试清单也还是旧的）；`CSharpHost/README.md` 补脚本文件位置、入口签名、调用时机、只调一次
  验收：**按出现次数判，不按"命中即绿"** —— 改完后跑三组 grep 并贴计数：① `9 个测试` 命中 **0** 次、`10 个测试` 命中 ≥1 次；② `PASSED` 后面跟 `12` 命中 **0** 次、跟 `17` 命中 ≥1 次；③ `8 条 status` 命中 **0** 次、`10 条 status` 命中 ≥1 次。另加：`--show-window-seconds` 命中 ≥1 次；`CSharpHost/README.md` 里 `OnEngineReady` 与"只调一次"各命中 ≥1 次
  实测（2026-10-26）：`docs/testing.md` 改了四处 —— 第 10 行表格里 `**9** 个测试` → `**10**`、第 31 行标题 `（9 个测试）` → `（10 个测试）`、第 29 行 `PASSED 行数当前为 **12**` → `**17**`、第 119 行 `PASSED 12 行 / 8 条 status` → `PASSED 17 行 / 10 条 status`；第 33 行那份清单本来就只是指向 `runTestWithDiagnostics` 列表、没有数字，未动；第 71 行原写"加第 9 个测试"，这句话还留着 `9 个测试` 这个串，改成"加它的时候是第 9 条；`TestCSharpWindow` 之后一共 10 条"（既保住历史事实，又让计数归零）。文件末尾新增一节「C# 脚本层与 `--show-window-seconds`」：脚本位置、入口时机、只调一次、参数语义、以及判据为什么落在 `test_output/keep_window.log`。`CSharpHost/README.md` 新增一节「脚本层：`Scripting/Managed/UserScript.cs`」：写在哪、入口签名、什么时候被调、怎么判调了几次、两条边界、失败怎么传。六项计数实测：① `9 个测试`=**0**、`10 个测试`=1；② `PASSED 12`=**0**、`PASSED 17`=1；③ `8 条 status`=**0**、`10 条 status`=1；④ `--show-window-seconds`=2；⑤ `OnEngineReady`=2、`只调一次`=1

- [x] 4.3 收尾全量验证：`python build.py --config Debug`；`CoreTests.exe` 退 0 且 `FAILED` 0 行、`PASSED` 17 行；两后端各 `--backend <x> --headless 60 --report test_output/result_<x>.json`，两份报告各 10 条且全部 `pass`
  验收：四条数字逐条贴出。**不再写"读数与改动前一致"** —— 脚本窗口会让 `GetWindowCount` 那行多 1，这是预期的变化，已在 1.1 与 2.3 里定义。具体读数：全量跑（不带 `--test`）**实测**是 `11`（当时已有 10 个窗口未回收），脚本窗口再加 1 就是 `12`；单测跑（`--test TestCSharpWindow`）**实测**是 `基线 0 → 建窗后 1 → 回收后 0`，脚本窗口进来之后是 `0 → 1`（脚本窗）`→ 2`（表面窗）`→ 1`（表面窗回收）—— 这两个 `2` 与 `1` 是**预期值**，不是实测值，贴读数时要说清哪个是实测哪个是预期
  实测（2026-10-26）：① `python build.py --config Debug` ⇒ `BUILD SUCCESSFUL`；② `CoreTests.exe` ⇒ 退 0、`PASSED` **17** 行、`FAILED` **0** 行，**连跑 3 次都是这个读数**；③ vulkan ⇒ 报告 **10** 条、`pass` **10**；④ d3d12 ⇒ 报告 **10** 条、`pass` **10**。第一遍全量跑时 `TestCSharpWindow` 曾红过一次：`脚本建窗计数不符：Tick 回值 3（脚本早建过，期望不涨）、Tick 之后 = 12、建窗前 = 12` —— **全量跑时脚本早在更早的测试里就建过窗**，`+1` 断言在那种跑法下不成立（单测跑回值 1 才成立）。已改成按 `Tick` 回值分支：回值 1 ⇒ 期望 `+1`（实测 `建窗前 1 → 2`）；回值 > 1 ⇒ 期望不涨（实测 `12 → 12`）。改完两后端各 10/10、单测仍 `pass`
  **另需单开一条**：`CoreTests.exe` 那条 `Bindings handle table test` 有已知偶发红（首跑时出现过 `并发用例：1 次调用失败（取不到句柄或解析拿到空）`，3 次重跑全绿）。根因在生成物的 `_ResolveHandle` 无锁读句柄表，而登记/释放都持 `cascaded_bindings::HandleMutex()`（`out/build/x64-debug/codegen/bindings/IBindRow09.Bindings.cpp` 里 `IBindRow09_ResolveHandle` 直接调 `IBindRow09_lookupHandle`，第 175/189/203/247 行才取锁）。这属于 CodeGen 的修复 —— **2026-10-26 用户裁定改为在本 change 内修，见 5.1**（修完重取读数：`CoreTests` 连跑 10 次全绿、23/23 产物 `_ResolveHandle` 带锁）

## 5. 验证阻塞项（用户 2026-10-26 裁定：修 CodeGen 属于本 change 范围，因为它阻碍 4.3 的验证）

- [x] 5.1 修 `Tools/CodeGen/CodeGenMain.cpp`：生成 `<接口>_ResolveHandle` 时**取 `cascaded_bindings::HandleMutex()`**（现在只有登记/释放取，解析这条路直接读表），使 `_ResolveHandle` 与 `_releaseHandle` / 登记三路共用同一把锁
  验收：① **结构判据（主判据）**：重生成后，每个被绑定接口的产物里 `<接口>_ResolveHandle` 函数体内都必须出现 `lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex())` —— 按产物逐个计数，0 例外（这条是确定性的，不靠跑）；② `python build.py --config Debug` 通过；③ **佐证（不是判据）**：`CoreTests.exe` 连跑 10 次全绿（`PASSED` 17 / `FAILED` 0）。取样逻辑要说清：旧的偶发率约 2.5%（353 次里 9 次红），10 次全绿只能发现高频故障、不能证明修好，所以真判据是 ①
  实测（2026-10-26）：CodeGenMain.cpp 里生成 `_ResolveHandle` 的段落加了一行
  `std::lock_guard<std::recursive_mutex> guard(cascaded_bindings::HandleMutex());`（放在 `lookupHandle` 之前），理由写进代码注释：
  登记时 `push_back` 会扩容、释放时清项，两路都持锁，只有解析裸读。① 重生成后逐产物检查**定义体**（不是声明）：
  `_ResolveHandle(void* self, uint64_t handle)` 后面紧跟 `{` 的那一段里必须出现锁 —— **23 个产物全带锁、0 个无锁**；
  ② `python build.py --config Debug` ⇒ `BUILD SUCCESSFUL`；③ `CoreTests.exe` 连跑 **10 次**：10 绿 0 红，每次都是 `PASSED` 17 / `FAILED` 0、退出码 0。
  一次自查纠错：第一次跑的检查脚本匹配到了**声明行**（`…_ResolveHandle(void* self, uint64_t handle);`），于是误报"23 个全无锁"，
  改成按"下一行是 `{`"锁定义体之后是 23/23 带锁
