# 测试规范

本项目把测试分三层。**任何新功能都必须落在其中至少一层，并且留下一条"在缺陷存在时会变红"的断言** —— 只贴日志、只手工造一次红态、只写"已验证"，都不算测试。

## 三层，按"要不要进程 / 模块 / GPU"划分

| 层 | 载体 | 适合测什么 | 不适合测什么 |
|---|---|---|---|
| **① 纯逻辑** | `Test/CoreTests`（13 个 `.cpp`，`main` 在 `SerializerTest.cpp`） | 序列化字节、哈希值、反射与类型表、容器语义这类**不碰模块、不开窗口、不需要 GPU**的判定 | 需要加载模块、需要真实后端、需要多进程或"启动即退出"的行为 |
| **② 进程内行为** | `Test/GPUBackendTester`（当前 **9** 个测试，注册在 `Main.cpp` 的 `runTestWithDiagnostics("TestXxx", TestXxx)` 列表里） | 要真后端、真要窗口、要在引擎内部断言某模块的行为（例：`IScriptHost` 可用、`Tick` 回值正确） | 进程级失败（启动就该非零退出）、需要改环境变量的场景 |
| **③ 进程级 / 多进程** | `Tools/check_*.py`（现有 6 个脚本） | "缺文件 / 版本不符 / ABI 不符 ⇒ 退出码与错误串"这类**一启动就失败**的路径；生成物与审计表的对账 | 需要引擎内部状态的断言 |

## 统一约定

1. **命名**：①层 `<主题>Test.cpp` 里一个函数一个用例；②层 `Test<主题>`（会成为 `--list` 里的一行）；③层 `check_<主题>.py`。
2. **必须能失败**：每条测试都要在文档或注释里写清"**怎么把它弄红**"。写不出红态构造方法 = 这条测试不算数。
3. **红态构造优先用运行时开关，不改源码**：需要模拟"版本不符 / 抛异常"这类异常路径时，用环境变量开关（例：`CASCADED_CSHARP_SELFTEST=abi-mismatch`）而不是临时改 `.cpp`/`.cs` 再改回来 —— 后者既脏工作区，又不可重复。
4. **进程级测试必须自己还原**：③层脚本只改 `%TEMP%` 里的副本与输出目录；跑完必须让工作区与产物回到跑之前的状态（脚本里写在 `finally` 段）。
5. **判定与读数**：①层打印 `PASSED` 行、退出码 0/1；②层每个测试在 `test_output/result.json` 里有一行 `{"name","status","duration_ms","exit_code"}`，退出码 0 只表示"跑完了"，**判定要看 `status`**；③层退出码 `0` 全过 / `1` 有失败 / `2` 判据自身失效（找不到被测程序或产物）。
6. **失败信息要能直接定位**：`期望什么 / 实际什么 / 怎么重跑这一条`。禁止只写"失败"。
7. **产物不入库**：`test_output/`、`crash_*.dmp`、`%TEMP%` 下的中间文件都是跑测试的产物，跑完清掉。

## 现有测试清单

### ① `Test/CoreTests`（13 个文件）

`CodeGenKindsTest` / `CodeGenMetaTest` / `CodeGenRuntimeTest` / `EmitReflectionTest` / `FriendLikeTest` / `GeneratedContainerTest` / `GeneratedHashContainerTest` / `GeneratedHashTest` / `GeneratedSerializerTest` / `PipelineStateObjectTest` / `ResourceBatchTest` / `SerializerTest`（含 `main` 与分配烟测）/ `ShaderLibraryBytesTest`。

跑法：`out\build\x64-debug\bin\CoreTests.exe`（退出码 0，`PASSED` 行数当前为 **12**）。

### ② `Test/GPUBackendTester`（9 个测试）

`TestSimpleTriangle` / `TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` / `TestTriangleWithImageBuffer` / `TestDoublePass` / `TestComputeBuffer` / `TestReadback` / `TestIMGUI` / `TestScriptHost`。

跑法：`GPUBackendTester.exe --backend vulkan|d3d12 --headless 10 --report <json>`；`--list` 列出可用测试；`--test <名字>` 只跑一个。每个测试各自写 `test_output/<名字>.log`，崩溃会写 `crash_YYYYMMDD_HHMMSS.dmp` 并在 stderr 打印异常名、异常码、崩溃模块。

### ③ `Tools/check_*.py`（6 个）

`check_legacy_symbols.py`（旧符号守卫）、`check_hash_sources.py`（哈希来源盘点）、`check_bindable_audit.py`（可绑定面审计对账）、`verify_readme.py`（README 里的命令逐条重跑）、`read_dump.py`（读崩溃转储）、`spin_check.py`（轮界进度检查）。它们自己就是"进程级断言"的样板：跑别人的程序，只看**退出码 + 输出里的可核子串**。

**绑定发射器的判据在另一个目录，但已经接进构建**：`Tools/CodeGen/check_bindings.py` 做六件事 —— 期望签名逐行对拍、C++/C# **两层对齐**（native 字段层 + 托管表面层）、**结构桩判据**（剥注释后看 thunk 有没有真调实现；真实接口桩数必须 0、夹具只打印）、**覆盖率下限**（thunk 数必须等于期望文件里的独立清单）、**托管表面判据**（`public` 类型里不许出现 `delegate*` / `void*` / `ulong`，表面成员数必须等于「接口头里的纯虚方法数 − `CA_BIND_SKIP` 数」）、**第二张矩阵的六行 conformance**（每行断言生成出来的整条签名）。它由 `build.py` 在绑定生成之后调用并传 `--product-dir`，非零退出直接让构建失败，stdout 逐行转发（所以"夹具桩数"那行每次构建都看得见）。行为断言落在 `Test/CoreTests/`（第 ① 层）：`BindingsGetApiTest.cpp`（取表握手）、`BindingsHandleTest.cpp`（强/弱/借用三类句柄、代际回绕、并发、1002 的触发与清零反例）、`BindingsWeakHandleTest.cpp`（弱引用不抬高计数、对象析构后 1001）、`WindowBindingsCompileTest.cpp`（`IWindow` 产物编译 + 借用句柄 + 句柄发行者 + 1001）。

**托管表面这一层由谁把守**（`csharp-window-creation` 任务 5.2/5.6/5.7，三条互相独立的判据）：

| 判据 | 位置 | 它抓的是 |
|---|---|---|
| `check_surface()` | `check_bindings.py`，每次构建都跑 | 表面类型在不在、名字是不是逐字照抄、`public` 类型里有没有 native 细节（`delegate*`/`void*`/`ulong`）、表面成员数对不对（期望数从**接口头**算，不从产物读） |
| `check_surface_matrix()` | 同上 | 第二张矩阵六行各自的**整条签名**（`public bool GetFlag()` 之类）—— 只比名字抓不住 `bool`→`int` |
| `SurfaceSignatureCheck.cs` | `Scripting/Managed/`，编进 `Scripting.Managed` | 28 条 `Func<>`/`Action<>` 赋值：签名映射错就是**编译错误** |

`check_error_codes()` 另管错误码：产物里的 `<接口>_ERROR_*` 常量与所有 `_setError` 的码逐个对 `design.md` D13 的码表，裸字面量直接红。

**跨边界端到端落在第 ② 层**：`Test/GPUBackendTester` 的 `TestCSharpWindow` 是唯一真的把「C++ 建窗 → 托管表面 → 引擎侧对账」走通的用例。C++ 侧只负责建窗、登记两个实例（编号 1 = `IWindow`、编号 2 = `IWindowSystem`，实例名见 `CSharpHost/README.md`）并读计数；读数与断言全在托管侧走生成的表面（`GetWindowSize` 回元组、`WindowShouldClose` 回 `bool`、`GetWindowCount` 回 `int`、`NewWindow` + `Dispose` 一整圈、异常码 1001 与"失败之后成功调用不抛"）。跑法：`GPUBackendTester.exe --backend vulkan --test TestCSharpWindow --headless 30`，读数在 `test_output/TestCSharpWindow.log`（基线留了一份在 `openspec/changes/csharp-window-creation/evidence/`），判定在 `test_output/result.json` 的 `status`。注意 `main` **永远返回 `EXIT_SUCCESS`**，所以退出码不是判定依据，必须看 `result.json`。

## C#Host 的测试怎么做（本规范的第一份应用样板）

**已实现（2026-10-18，`csharp-hostfxr-bootstrap` 的 5.1–5.3）**：下面两层都在跑，红态都构造过。实测读数：

```
② 层：out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 2 --report %TEMP%\v.json
       → 9/9 pass（含 TestScriptHost）
     红态：把 Host.cs 里 Tick 的 return s_tickCount; 改成 return 0; → pass=8 / fail=1，唯一的 fail 就是 TestScriptHost

③ 层：python Tools\check_csharp_host_failures.py
       → 5 个场景：5 通过、0 失败（退出码 0）
     红态：把场景 1 的期望退出码临时改成 4 → [失败] 缺托管程序集…、4 通过、1 失败（退出码 1）
```

`CSharpHost` 有两类行为要测，正好各落一层：

### 层 ②：`TestScriptHost`（在 `GPUBackendTester` 里加第 9 个测试）

测"运行时起来了且双向可调"，断言全部来自进程内状态：

```
GetInstance<cs_script::IScriptHost>() 非空
  → IsAvailable() == true
  → LastError() 是空串
  → Tick(0.016) == 1         // 托管侧回的是"第几次 Tick"
  → Tick(0.016) == 2         // 递增，证明状态在托管侧真的保留了
  → LastError() 仍是空串
```

- **为什么在 ②层**：它需要模块加载与真实运行时；①层不开模块。
- **怎么弄红**：在 `Host.cs` 里把 `Tick` 的 `return s_tickCount;` 改成 `return 0;` ⇒ 只有这一条红、其余 8 条仍绿（证明它真在断言，而不是跟着别人一起绿）。
- **注意区分**：`Scripting.Managed.dll` 缺失属于"宿主级前置失败"（进程退出码 3、整批红），不是本条的断言失败 —— 这种情形交给下面 ③层的脚本断言退出码。

### 层 ③：`Tools/check_csharp_host_failures.py`

测"五条失败路径都响"，每条都是**启动即失败**，只能从进程外面断言：

| 场景 | 怎么制造（用运行时开关，不改源码） | 断言 |
|---|---|---|
| 缺托管程序集 | 把 `bin\Scripting.Managed.dll` 临时改名 | 退出码 **3** 且输出含 `Scripting.Managed.dll` 与 `找过的路径` |
| 配置路径不存在 | `CASCADED_CSHARP_RUNTIMECONFIG=<%TEMP% 里不存在的路径>` | 退出码 **3** 且输出含该路径与 `hostfxr.dll` |
| 要的运行时版本本机没有 | `CASCADED_CSHARP_RUNTIMECONFIG` 指向一份 `"version":"7.0.0"` 的副本 | 退出码 **3** 且输出含 `7.0.0` 与 `本机已装的运行时版本` |
| 函数表 ABI 不符 | `CASCADED_CSHARP_SELFTEST=abi-mismatch`（引擎侧把 `abi_version` 加一） | 退出码 **3** 且输出含 `ABI 版本不符：引擎 2` |
| 托管方法抛异常 | `CASCADED_CSHARP_SELFTEST=throw`（托管侧 `Tick` 抛一次） | 退出码 **0**、输出含 `Tick 抛异常` 与错误码 **4**、且**不产生**新 `crash_*.dmp` |

- **两个自检开关是这条测试能自动化的前提**：现在这两条要靠临时改源码才能构造，改成开关后脚本才能重复跑、不脏工作区。开关只在 Debug 下生效，并在 `CSharpHost/README.md` 里登记。
- **脚本结构**：`for each 场景 → 造环境 → 跑 tester（--headless 1）→ 断言退出码与子串 → finally 还原`；退出码 `0/1/2` 同第 5 条约定。
- **跑一次要多久**：每个场景一次 headless 启动（实测 1~2 秒），五条加起来 10 秒量级，可以放进"改完必须跑"的那一组。

### 两层的关系

②层管"**正常能跑通**"，③层管"**坏的时候会响**"。两层都绿才算 C#Host 有测试；只做 ②层会漏掉全部失败路径，只做 ③层会漏掉"Tick 回值其实错了"这类静默错误。

## 改完代码必须跑的最小集合

```powershell
python build.py --config Debug
out\build\x64-debug\bin\CoreTests.exe
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 10 --report $env:TEMP\v.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --headless 10 --report $env:TEMP\d.json
python Tools\CodeGen\check_bindable_audit.py
python Tools\CodeGen\verify_readme.py
```

判定：CoreTests 退出码 0 且 `PASSED` 12 行；两份 report 里 8 条 `status` 全为 `pass`；两个脚本退出码 0。新增/改动 C#Host 时再加 `TestScriptHost` 与 `Tools\check_csharp_host_failures.py`。
