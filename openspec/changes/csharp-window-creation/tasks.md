## 1. 句柄：能释放、能回收、弱引用不挡回收

- [x] 1.1 把两张平行状态合成一张句柄表：`Tools/CodeGen/CodeGenMain.cpp` 里现在分开的 `handles`（`{void* object, uint32 generation}`，按第一个空槽分配）与 `keepAlive`（`vector<shared_ptr<void>>`，按下标追加）合成 `vector<Entry>`，`Entry = { void const* object; uint32 generation; uint8 kind; shared_ptr<void> strong; weak_ptr<void> weak; }`，`registerHandle` / `lookupHandle` / `releaseHandle` 都只操作这一张表（design D2）
  验收：`Test/CoreTests/BindingsHandleTest.cpp` 里现有的 2 条 1001 断言与 1 条错误文本断言 MUST 仍通过；新增断言读表项的 `kind` 字段区分强句柄与弱句柄、并断言同一张表里两类能共存 —— **今天是编译失败（没有 `Entry` 这个类型，也没有 `kind` 字段），红态成立**。（MUST NOT 用"acquire A → release A → acquire B 复用同槽"当红态：今天就会通过，`registerHandle` 本来就从第一个空槽找、`releaseHandle` 本来就置空并进位）

- [x] 1.2 让释放真的放开强引用：`releaseHandle` MUST 同时 `strong.reset()` 与 `weak.reset()`
  验收：**直接读表项** —— 释放后该表项的 `strong` MUST 为空（产物被 `#include` 进测试，`static` 表在同一翻译单元里可见）。今天释放只清 `handles`、不动 `keepAlive`，所以这条在修好前 MUST 红。判据 MUST NOT 用 `use_count()` 差值（差值能被"删得不干净"骗过，也会把外部合法持有者判红）

- [x] 1.3 给每个被绑定接口导出两个泛用入口：`<接口>_ReleaseHandle(void* self, uint64_t handle)`（释放）与 `<接口>_ResolveHandle(uint64_t handle)`（句柄 → 对象指针，失败置 1001 并返回空）。**`ResolveHandle` 是必需的**：现有 22 + 5 个 C ABI 入口的第一个参数全是 `void* self`，没有任何一个收 `uint64_t` 句柄 —— 没有它，C# 拿到句柄之后**没有任何办法**用它调 `IWindow` 的方法，"旧句柄报 1001"这条验收也写不出来。句柄表是每接口一份，所以 `NewWindow` 拿到的弱句柄要用 `IWindowSystem_ResolveHandle` 解出 `IWindow*`，再当 `self` 传给 `IWindow` 的方法（两处都是 `void*`）。既有的按方法生成的 `<接口>_Release<方法名>` 与其三处调用点保留不动（design D2）
  验收：`IWindow.Bindings.cpp` 里 MUST 出现 `IWindow_ReleaseHandle` 与 `IWindow_ResolveHandle`；`IBindRow09_ReleaseAcquireThing` MUST 仍在且那三处调用一行都不用改；夹具里 `ResolveHandle` 一个刚释放的句柄 MUST 返回空且错误码为 1001

- [x] 1.4 弱句柄投影：`castl::weak_ptr<T>` 返回值投影成 `uint64_t` 弱句柄，句柄表存弱引用、不抬高 `use_count()`，`lock()` 失败时置 1001 并返 0（design D1）。连带要改的一整套：矩阵第 11 行从"不投影"改成"弱句柄"、`BindableShapes.h` 加正向夹具 `IBindRow11`、`BindableShapes.expected.txt` 第 21 行换成真签名、`BindableShapes.Reject.h` 里 `IBindRow11Reject` 那条 `weak_ptr` 方法**改成一个真正的裸指针方法**（保留"带理由的 ② 拒绝夹具"这一类，README 第十四节示例一跑的就是它）
  验收：`check_bindings.py` 的读数 MUST 从"签名行 42 / 非签名行 1"变成"非签名行 0"，不符数 MUST 仍为 0；夹具级判别性用例落在**独立 TU** `Test/CoreTests/BindingsWeakHandleTest.cpp`（一个 TU 只能 include 一份产物 —— 两份都会产出 `BindScalarThingC`，同 TU 会 C2011）：`IBindRow11_PeekThing` 前后 `use_count()` MUST 不变、`IBindRow11_ResolveHandle` MUST 解出实现持有的同一指针；持有方**自己** `reset()`（全程不调任何释放入口）后，`std::weak_ptr` 见证者的 `expired()` MUST 为真、`ResolveHandle` MUST 返回空且错误码 1001。（**原验收里"建窗 → `IWindowSystem_ResolveHandle` → `GetWindowSize` 读回 800x600"那套已移到 2.8** —— 它要用到 `IWindowSystem` 的产物，而那个要到 2.1 加 `CA_BIND` 之后才存在，放在 1.4 的位置上不可能满足）

- [x] 1.5 修订审计表里 `IWindowSystem::NewWindow` 的**三处**：主表那一行、`## 汇总：不可绑清单` 里同名那一行、以及汇总下面「② 所有权不明的裸指针 —— 6 条」那条计数（要改成 5 条并从名单里删掉 `NewWindow`）。只改一处会让同一份文件同时说"可绑"和"不可绑 ②"。文件是 `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md` —— **全仓只能有一份**，新建第二份会让对账脚本直接退出 2（design D6）
  验收：主表该行的「可绑性」列 MUST 逐字等于 `可绑`（MUST NOT 用子串判 —— 现值 `**不可绑**` 含"可绑"二字会假绿），「投影形状」列 MUST 写成弱句柄；不可绑清单里 MUST 不再出现该方法；那个计数行 MUST 变成 5 条；`python Tools/CodeGen/check_bindable_audit.py --json` MUST 报 `"ok": true`

- [x] 1.6 处理 `GetMonitor`：审计表判它是"可绑（按值镜像）"，本 change 只是这一轮没做非标量成员的镜像，所以**可绑性列不动**（仍写"可绑"），只在备注列写"本轮未实现：成员是 `cacore::Rect<float>` 模板实例化类型，布局镜像只收成员全标量的结构体"。这一类属于 `specs/csharp-bindable-surface/spec.md` 新增要求里的第三态，MUST NOT 写成 ①～④（design D4）
  验收：`openspec validate csharp-window-creation` MUST 通过；审计表该行的「可绑性」列 MUST 仍是 `可绑`、备注列 MUST 出现"本轮未实现"；`check_bindable_audit.py --json` MUST 报 `"ok": true`

- [x] 1.7 给 `Tools/CodeGen/check_bindable_audit.py` 加一条能失败的检查：被 `CA_BIND_SKIP` 点名、而表里「可绑性」列是"可绑"的方法，排除理由里 MUST 出现"本轮未实现"并点名缺的投影路径
  验收：把某个 `CA_BIND_SKIP` 的理由临时改成只写"④ 语义越界"（不提本轮未实现），脚本 MUST 非零退出并点名该行；还原后 `--json` MUST 报 `"ok": true`

## 2. 接线：`IWindowSystem` 进函数表，托管侧真的建窗口

- [x] 2.1 在 `WindowSystem/header/CAWindow/WindowSystem.h` 的 `IWindowSystem` 上加 `CA_BIND()`，并给 11 个方法逐个加 `CA_BIND_SKIP` 并写明理由（10 个 `Set*Callback` 写"本轮未实现：缺回调投影路径（保活表 + 注销句柄）"；`GetMonitor` 写"本轮未实现：成员是 `cacore::Rect<float>` 模板实例化类型…"。**两条都 MUST NOT 写成 ①～④ 中的任何一类**，见 design D4），`CA_BIND_SKIP` 放类体末尾。**`build.py` 的 `CODEGEN_BINDINGS` 不用加生成行**（`IWindow` 与 `IWindowSystem` 在同一个头里），但那一行的**产物前缀清单** MUST 从 `["IWindow"]` 扩成 `["IWindow", "IWindowSystem"]` —— 否则 `IWindowSystem.Bindings.*` 没落地也不会让构建红
  验收：`check_bindable_audit.py` MUST 仍 0 问题（方法数 60 不变）；生成器 MUST 打印 11 条带理由的排除、退出码 0；`out/build/<配置>/codegen/bindings/` 下 MUST 多出 `IWindowSystem.Bindings.cpp` 与 `.cs`，日志里 MUST 出现 `[接口] IWindowSystem → 5 条`；把 `CA_BIND()` 从 `IWindowSystem` 上临时去掉再构建 MUST 失败，并点名「缺少这些接口的产物：IWindowSystem」；还原后 MUST 恢复。（**实测取了个等价造法**：给期望清单临时加一个不存在的接口名 `ProbeMissingIntf` → `BUILD EXIT=1`、报「缺少这些接口的产物：ProbeMissingIntf」、还原后恢复绿。**MUST NOT 用"删掉产物再构建"** —— 绑定每次构建都会重新生成，删掉的会被立刻重建；也**MUST NOT 靠注释掉 `CA_BIND()`** —— 标记扫描虽然已经剥注释，但接口名在头文件里还以 `IWindowSystem*` 等形式到处出现，去掉标记后生成器仍产出该接口）

- [x] 2.2 让生成器对"带 `castl::function` 参数、又没写 `CA_BIND_SKIP`"的方法非零退出并点名（今天它是照固定签名 `void (*)(void*, uint32_t, float)` 硬投影，正是静默发桩的来源）。同时给夹具 `IBindRow13::SetCallback` 加 `CA_BIND_SKIP`、把 `BindableShapes.expected.txt` 第 23 行从签名改成"拒绝"、同步矩阵第 13 行的行文与 `check_bindings.py` 的非签名行计数
  验收：把 `IBindRow13` 的 skip 临时删掉跑生成器，MUST 非零退出并点名 `IBindRow13::SetCallback`；还原后退出码 0；`check_bindings.py` 的"非签名行"读数 MUST 从 0 变成 **1**（第 13 行改成拒绝）

- [x] 2.3 给矩阵补 `castl::string_view const&` 入参这一行（`NewWindow` 第 3 个参数就是它）：在 `Tools/CodeGen/BindableShapes.h` 加夹具行、在 `BindableShapes.expected.txt` 加期望签名、在 `Tools/CodeGen/README.md` 第十三节的矩阵表加一行；并给 `check_bindings.py` 加一条能失败的交叉检查 —— **README 第十三节矩阵的数据行数 MUST 等于 `expected.txt` 里不同行号前缀的个数**（今天脚本打印的"矩阵行数 14"是写死的字面量，不比对任何东西，所以矩阵漏写一行是绿的）
  验收：矩阵行数与期望文件前缀数 MUST 都从 14 变 15，且这条交叉检查在"只在 README 加行、不加 expected"时 MUST 报错；生成物里该夹具的签名 MUST 是 `char const* + uint64_t` 两个参数

- [x] 2.4 让 `CSharpHost/private/CSharpHost.cpp` 支持第二个接口编号：include 生成的 `IWindowSystem.Bindings.cpp`，`OnGetInterfaceApi` 按编号 2 返回 `IWindowSystemApi_v1` 的表与实例
  验收：按审计表里 5 个入表方法的**名字顺序**断言生成的表成员名序列逐字相同，并断言 `sizeof(IWindowSystemApi_v1)` 等于 `8 + 8 * 5 = 48`（这个数从方法条数算出来，**不从生成物里读** —— 两侧都读生成物就是同源恒等，抓不到生成器漏方法）；编号 2 在实例登记前 MUST 返回 0、登记后 MUST 非 0

- [x] 2.5 把生成的 `.cs` 编进托管工程：`Scripting/Managed/Scripting.Managed.csproj` 用 `$(CascadedBindingDir)` 收生成物，`build.py` 的 `build_managed()` 追加 `-p:CascadedBindingDir=<BUILD_DIR>/codegen/bindings`；属性为空或目录里没有 `.cs` 时 csproj MUST 报错（design D3）
  验收：托管侧 MUST 能取到生成物里的类型与常量（不只是"csproj 里有一行 Compile"）；故意不传该属性重编 MUST 失败并说明缺生成物目录

- [x] 2.6 托管侧改用生成的绑定：`Scripting/Managed/Host.cs` 删掉手抄的 `WindowApiSize = 8 + 8 * 22` 与 `WindowApiGetWindowSize = 56`，改成引用生成物里的常量；把第一步的手写探针换成"用生成的绑定建窗 → `ResolveHandle` 解出 `self` → 读尺寸 → 关窗"
  验收：`Host.cs` 里这两个常量的字面赋值 MUST 0 命中；**「函数表大小不符」在 `TestScriptHost` 日志里 MUST 0 命中**（实测：改之前 1 命中「引擎 176 字节，托管侧 184 字节」，改之后 0 命中）；`GetWindowSize` 探针的转绿**在 2.7 的建窗测试里验** —— `TestScriptHost` 不建窗，`s_interfaceInstances[1]` 永远是空，探针在那里只能读到「写入 0 字节、实例 0」，这条不是 2.6 能造出来的读数。名为准

- [x] 2.7 新增一条端到端测试并注册进 `--list`：在 `Test/GPUBackendTester/Main.cpp` 里加一个测试函数，由它自己登记实例编号 2（全仓现在只有 `TestSimpleTriangle` 登记过编号 1，`TestScriptHost` 不建窗、不调 `UpdateSystem`）
  验收：`out/build/<配置>/bin/GPUBackendTester.exe --list` MUST 列出该测试名；**用 `--headless N` 跑**（只有带这个参数才会建 `test_output/`、把 stdout 重定向到 `test_output/<测试名>.log`），日志里 MUST 同时出现带 `[托管侧]` 前缀与无前缀的窗口尺寸读数，且数值相同；再补一条反向：不登记实例编号 2 时该测试的 `status` MUST 是 fail（证明它不是恒 pass）

- [x] 2.8 关窗与回收的读数：C# 调 `CloseWindow()` 后，先在泵帧**之前**让两侧各读一次 `WindowShouldClose()`（都应为真），再由 C++ 侧显式调一次 `ctx.pWindowSystem->UpdateSystem()`（回收只在这个函数里做，不调就永远回不到原值），最后读一次 `GetWindowCount()`，并用 `IWindowSystem_ResolveHandle` 解旧句柄、断言拿到空 + 错误码 1001
  验收：日志里 MUST 出现：泵帧前两侧 `WindowShouldClose` 都为真、建窗计数比基线 +1、泵帧后回到基线、旧句柄 `ResolveHandle` 返回空且 1001。顺序 MUST 正确 —— 窗口被回收后句柄就失效。**外加从 1.4 移过来的那条**：C# 侧建窗拿句柄 → `IWindowSystem_ResolveHandle` 解出 `self` → 用 `self` 调 `IWindow::GetWindowSize` 读回 **800x600** → 引擎自己回收窗口（**全程不调任何释放入口**）→ 再 `ResolveHandle` MUST 返回空且 1001（MUST NOT 拿 `IWindow_LastErrorCode` 的全局值当证据 —— 它忽略 `self`、是每接口一个 static 全局）

## 3. 收尾：文档、遗留、构建验证

- [x] 3.1 同步对外文档 `Tools/CodeGen/README.md`：**第十三节**矩阵的编辑归 1.4 与 2.3（本任务只复核那两处已就位，不重改）；**第十四节**补上弱句柄投影、泛用释放入口与解析入口、释放后不再持有强引用的规则，并改掉第十四节「不做什么」那段里"不生成 `castl::weak_ptr`"的说法（在 1.4 之后它是错的），以及示例一的真实输出（`IBindRow11Reject` 的形状在 1.4 里换过了）。`docs/testing.md` 补端到端断言落在哪一层
  验收：`Select-String -Path "Tools/CodeGen/README.md" -Pattern '不生成.*weak_ptr'` MUST 0 命中；「弱句柄」「ReleaseHandle」「ResolveHandle」三个词 MUST 各至少 1 处命中；示例一那段真实输出 MUST 用 `python Tools/CodeGen/verify_readme.py` 跑过并逐字一致（该脚本对带 `--out-dir` 的命令只比退出码、跳过文本，所以**必须**另外人工核对那几行输出）

- [x] 3.2 更新 `docs/TODO.md`：锚点 MUST 从文件里**逐字复制**，执行前先跑出"命中 1 处"、执行后"命中 0 处"两个读数
  验收：这四条各 MUST 从命中 1 变命中 0 —— `` `keepAlive` 只增不减 ``、`` 伴生 `ReleaseXxx` 只在 ``、`` `weak_ptr` 返回 / 入参整体拒绝 ``、`` 错误码 1001 只有 ``；未修的四条 MUST 仍各命中 1 处（`` `castl::function` 参数一律投影成固定签名 ``、`` **句柄入参的反查根本没实现** `` —— 本 change 只加了 `ResolveHandle` 这个新入口，**没有**实现"接口参数里的 `shared_ptr` / `weak_ptr` 反查"，那条原样留着、`` `setError` 往 `char[256]` ``、`` 只有 1001 / 1002 两个码 ``）；再补三条新的（代际号回绕、接口编号手抄在两处且上限 8 超限静默返回 0、句柄表无锁）

- [x] 3.3 构建与测试验证：跑 `python build.py --config Debug`，失败则分析并修到 `BUILD SUCCESSFUL`；然后跑 `out/build/x64-debug/bin/CoreTests.exe`，以及 `Test/GPUBackendTester` 用 vulkan 与 d3d12 各跑一遍全部测试
  验收：构建 `BUILD SUCCESSFUL`；`CoreTests.exe` 退出码 0 且 stdout 里 `FAILED` 行数为 0；两个后端**各用 `--headless N --report test_output/result_<后端>.json` 跑**（`result.json` 每次运行都以 trunc 打开，两个后端用同一路径只会留最后一份），两份报告里 `--list` 打印的**每一条**测试 `status` MUST 全为 `pass`，且新加的那条测试名 MUST 出现在里面（**MUST NOT 写死"9 条"** —— 2.7 新增一条之后就变成 10 条）；`check_bindings.py --product-dir <当前目录>` 报真实接口 0 桩、0 处签名不符；`check_bindable_audit.py --json` 报 `"ok": true`

**注（2026-10-25 用户裁定）**：本条原文要求把"代际号回绕 / 接口编号手抄 / 句柄表无锁"三条**补进 `docs/TODO.md`**。用户要求改为记在**本 change 里**，所以那三条现在是下面的 4.1–4.3；`docs/TODO.md` 那边只留一句指向这里的说明，不再有条目。

## 4. 收尾实测到、尚未修的项（记在本 change 里，不记 `docs/TODO.md`）

- [x] 4.1 代际号回绕：代际是 16 位、释放时 `(generation + 1) & ((1 << HANDLE_GEN_BITS) - 1)`，同一个槽被登记/释放 65536 次之后旧句柄会重新变成"有效"。要么加保护，要么把"回绕后旧句柄必须仍然失效"写成判据
  **实施记录（判据改过一次）**：修法是**槽位退休** —— 代际要归零时把该槽标成 `kind = 3`（`lookupHandle` 也认它失效），该槽永不再分配。判据第一版写成「重复 `1<<16` 次后，最早那个句柄必须解析失败」，实测**取消退休也照样绿** —— 因为循环里每次都在同一个槽上取用又释放，回绕那一刻发出的句柄被循环自己消费掉了，最早那个句柄从头到尾都是 stale 的，测不出回绕。改成**「句柄数字永不重复」**：循环中任何一次拿到的句柄 MUST 不等于最早那个；红态实测为 `代际回绕：同一个句柄数字被重新发了出去（旧句柄会复活）` + `Bindings handle table test FAILED`、`CoreTests` 退 1；还原后 `BUILD SUCCESSFUL`、退 0、`FAILED 0`。
  验收：`Test/CoreTests/BindingsHandleTest.cpp` 里加一段循环 —— 拿一个句柄、释放、再登记，重复 `1 << 16` 次，最后用**最早那个**句柄调 `ResolveHandle` MUST 返回空且错误码 1001；今天这段 MUST 失败（回绕后它又解析成功了）。若选择"不允许回绕"而不是"回绕也安全"，则改为断言第 65537 次登记返回 0 并置错误码，且该分支 MUST 有测试覆盖

- [x] 4.2 接口编号手抄在两处，且上限 8 静默返回 0：`CSharpHost/private/CSharpHost.cpp` 的 `s_interfaceIdWindow = 1` / `s_interfaceIdWindowSystem = 2` 与 `Scripting/Managed/Host.cs` 的 `InterfaceIdWindow` / `InterfaceIdSystem` 各写一份，只能靠人眼对齐；`OnGetInterfaceApi` 里 `interfaceId >= 8u` 之后**静默返回 0**，加第 9 个接口时既不报错也不提示
  **实施记录**：① 编号改成单向来源 —— `EngineApi` 升到 v3，末尾加 `window_interface_id` / `window_system_interface_id`，`Boot` 填 `s_interfaceIdWindow` / `s_interfaceIdWindowSystem`；`Host.cs` 那两个常量换成 `=> s_api->WindowInterfaceId` / `=> s_api->WindowSystemInterfaceId`，`git grep "InterfaceIdWindow = 1\|InterfaceIdSystem = 2" -- Scripting/Managed/Host.cs` = **0**。② 未知编号写进 `m_lastError`，并给 `IScriptHost` 加了 `GetInterfaceApiForTest`（只给测试用），`TestCSharpWindow` 里断言 `GetInterfaceApiForTest(9, …)` 返回 0 且 `LastError()` 含「未知接口编号」；实测日志：`未知接口编号读数：返回 0、LastError = 未知接口编号 9：当前只接线了 1 = IWindow、2 = IWindowSystem（上界 8 是 CSharpHost 自己的约定）`，该测试 `status = pass`。
  验收：① 编号只留一处来源（例如由生成物导出、或由 `CSharpHost` 单向传给托管侧），`git grep -F "InterfaceIdWindow" -- Scripting/Managed/Host.cs` MUST 不再出现手写数字；② 传入超限编号（例如 9）时 MUST 不再静默返回 0 —— 要么写进 `LastError`，要么在编译期就挡住；用一条测试断言 `GetInterfaceApi(9, …)` 之后 `LastError()` 非空

- [x] 4.3 句柄表无锁：`<接口>_handles` 是每接口一份的静态表（现在是 `std::vector<Entry>& = *new …`），所有 thunk 直接读写，没有任何互斥
  **实施记录**：选"加锁"那条路。生成物里现在有 `static std::recursive_mutex <接口>_handleMutex;`，`registerStrongHandle` / `registerWeakHandle` / `releaseHandle` 三个入口各一把 `std::lock_guard<std::recursive_mutex>`（递归是因为 `releaseHandle` 内部会调一次 `lookupHandle`，普通互斥量会自锁）。判据：`Test/CoreTests/BindingsHandleTest.cpp` 里 8 个线程各做 1000 次「取 → 解析 → 释放」，要求零失败、且每个线程最后那个句柄解析必须为空。**红态实测**：把三行 `lock_guard` 换成注释重跑，同一条 `CoreTests.exe` **挂死超过 10 分钟**（有锁版本秒级通过、退出码 0、`FAILED 0`），进程要靠外部杀掉 —— 数据竞争在无锁版本上是"挂住"而不是"报错"，这一条如实记下，因为"能失败"在这里的表现是超时而非断言失败。
  验收：加一条多线程测试 —— 8 个线程并发对同一个接口做「登记 → `ResolveHandle` → 释放」各 1000 次，`CoreTests.exe` MUST 不崩、且结束后 `ResolveHandle` 对全部已释放句柄都返回空；今天这段 MUST 能在若干次运行内复现数据竞争（崩溃或表项损坏）。若判定"托管侧永远单线程调用"因而不加锁，则必须在 `design.md` 里写出这条前提，并在 `CSharpHost` 的接口文档里写明"跨线程调用未支持"，同时用一条测试断言该前提被记录（缺记录即失败）

- [x] 4.4 `result.json` 的 `error` 字段是乱码：2.7 新加的 `Host.GetLastError` 写回的是 UTF-8 字节，落盘那条路径按本地代码页写出，于是失败原因变成一串 `�`
  验收：造一次失败（把 `TestCSharpWindow` 的 `SetInterfaceInstance(2, …)` 注释掉），`test_output/result_<后端>.json` 里该条的 `error` 字段 MUST 是**可读的中文**（含「IWindowSystem」字样），不得出现 `\uFFFD` 或连续 `?`；今天实测是一长串 `�`
  **实施记录（根因与我原来写的不同）**：不是"落盘按本地代码页"。实测三步 —— ① `CSharpHost::LastError()` 返回的 62 字节是对的（`E5 8F 96 20 49 57 69 6E` = "取 IWin"）；② 但 `e.what()` 与 stderr 都是 `0xDD`（MSVC debug 堆**已释放**填充）；③ 在同一抛出点改抛 15 字节 ASCII `E_HANDLE_TABLE` 时报告里完全正常。结论：**这个可执行文件里长于 SSO 阈值的 `std::string` 缓冲区在异常抛出与捕获之间被释放**，启动日志里 `mimalloc-redirect.dll seems to be initialized after ucrtbase.dll` 说明两个堆共存。修法：跨 catch 边界只抛短码，详细文字写进静态 `char g_errorDetail[512]`，`appendJsonResult` 直接按 `char*` 写出（不经 `std::string`）。实测读数：`error 头部字节 = E5 BB BA E7 AA 97 …`、按 UTF-8 读 = `建窗 + Tick 之后 LastError 非空：取 IWindowSystem 函…`。

## 5. 返工：托管表面必须镜像原始 C++ 接口声明（2026-10-25 用户验收打回后追加）

**背景**：第 1–4 节做完之后，`IWindow` 的 C ABI 面通了、`check_bindings.py` 全绿，但用户在验收时打回：托管侧拿到的是 `public static delegate* unmanaged<…>` 加偏移常量，用起来是 C 风格；要求是**表面同形于 `WindowSystem.h` 里 `IWindow` 的声明**。决策见 `design.md` 的 D7–D10。**本节任务一律在 1–4 节之上返工，不许改动已勾条目。**

- [ ] 5.1 判据加严：`Tools/CodeGen/check_bindings.py` 的"C# 侧对齐"今天**只认一种写法** —— `CS_MEMBER_RE = re.compile(r"public static delegate\* unmanaged<[^>]*>\s*(\w+)\s*;")`，它只匹配 `delegate*` **字段**，方法声明一个都不认（`SYMBOL_RE` 只作用在期望文本与 C++ 产物上，不是这里的根因）；且它按**整文件**拼一个名字列表，不区分"方法属于哪个类、是不是 static"
  验收：① 解析 `.cs` MUST 按**类作用域**建树，方法集合只收**方法声明**（`public <ret> <Name>(<params>)`），字段不计入；② MUST 断言这些方法属于一个**非 static、非抽象、实现 `IDisposable`** 的类型（空壳类不算通过）；③ 原生字段层（`internal` 类里的 `delegate*`）MUST 继续被接受 —— 改判据时不能把现有 native 层判成"C# 缺方法"；④ 造红态的顺序是「先 5.2 产出方法，再改判据」：5.2 之后才能把某个方法改成同名字段来造红，改前/改后/还原后三个读数都要贴。**今天（5.2 之前）方法一个都没有，红态造不出来，本条不得先于 5.2 勾选**

- [ ] 5.2 发射器产出托管表面（`IWindow` 先行）：`Tools/CodeGen/CodeGenMain.cpp` 为每个被绑定接口多产出一个 `public sealed class <接口>`（类型名**逐字照抄**原声明，`IWindow` → `IWindow`，不去 `I`），成员逐个镜像原声明：`CloseWindow()` / `bool WindowShouldClose()` / `void SetWindowName(string)` / `(int Width, int Height) GetWindowSize()` / `float GetDpiScale()` / `IntPtr GetNativeWindowHandle()` / `string GetWindowName()`；内部持函数表与 `void* self`，`IDisposable.Dispose()` 调 `ReleaseHandle`；native 层（`delegate*`、偏移常量、句柄表）MUST 降为 `internal`
  验收：**不许用 `git grep` 扫工作区**（生成物在 `out/` 下被 `.gitignore:14` 忽略，`git grep` 永远读到 0；且 `Scripting/Managed/**/*.cs` 这个 pathspec 连 `Host.cs` 都匹配不到）。判据 MUST 直接读 `out/build/<配置>/codegen/bindings/*.Bindings.cs`：① 先正向断言生成的 `public` 类型存在（路径写错时也要红）；② 在**非 `internal`** 的类型体里数 `delegate* unmanaged` / `void*` / `ulong` 句柄，MUST 全为 0（`internal` 的 native 类不计）；③ 表面方法数 MUST 等于「原声明纯虚方法数 − 被排除数」= 23 − 1 = **22**（`GetWindowName` 靠 `GetWindowNameLen` + `CopyGetWindowName` 两个伴生实现、**不进函数表**，所以 22 与「表槽 21」不是同一个数，MUST NOT 混用）；④ 失败时 MUST 抛出生成器产出的 `IWindowException`（类型名逐字含 `IWindow`），带 `Code` 与来自 `LastErrorText` 的 `Message`，MUST NOT 返回错误码

- [ ] 5.3 借用句柄：句柄表新增**第 4 种** kind（借用 = 4；已有 0 空 / 1 强 / 2 弱 / 3 已退休，若占用 3 会被 `lookupHandle` 当退休槽直接判失效），并把 `IWindow::GetWindowSystem()` 从 `CA_BIND_SKIP` 里解开——它返回 `IWindowSystem*`，所有权在模块，本质是借用。**只加 kind 不够**：`projectMethod` 对"非 `void*` 的裸指针返回"是硬拒绝（`② 所有权不明裸指针` → `return false`），删掉 `CA_BIND_SKIP` 之后生成器会以退出码 1 失败。必须同时改投影分支：返回是指针、且被指类型是已 `CA_BIND` 的接口 ⇒ 投成借用句柄；其它 `T*` 仍按 ② 拒绝
  验收：`WindowSystem.h` 里 `CA_BIND_SKIP(GetWindowSystem, …)` MUST 不再存在（命中 0），`IWindowApi_v1` 的成员数 MUST 从 21 变 22；销毁一个借用句柄后原对象 MUST 仍然可用（用测试断言：释放句柄 → 原 `IWindowSystem` 实例的方法照常能调）；若判定不许解开，必须在 `design.md` 写明"镜像这条要求对 `GetWindowSystem` 不适用"并说明理由，同时 5.2 的验收里把该方法从"必须出现"里剔除

- [ ] 5.4 同接口多实例：`CAModuleManager` 的实例表是 `接口类型 → { 实例名 → void* }`，同一接口能注册多个实现（`CA_MODULE_INSTANCE` 的第三个参数就是实例名）；当前 `IScriptHost::SetInterfaceInstance(uint32 id, void*)` 只按编号定位，隐含"每接口一个实例"
  验收：登记与查找都 MUST 带实例名这一维；加一条测试 —— 用两个不同实例名登记同一个接口的两份实现，对第二份上的窗口调 `GetWindowSystem()`，返回值 MUST 来自第二份（指针相等断言），且按第一个实例名登记的那一份 MUST 不受影响；今天这段 MUST 失败（只按编号取，会拿到第一份）

- [ ] 5.5 托管侧与端到端测试改用表面：`Scripting/Managed/Host.cs` 删掉手写的函数指针与偏移常量，改用 5.2 生成的 `IWindow` 类型；`Test/GPUBackendTester/Main.cpp` 的 `TestCSharpWindow` 里同样改用表面
  验收：① `git grep -c "delegate\* unmanaged" -- Scripting/Managed/Host.cs` MUST 为 0，且 `Host.cs` 里 MUST 出现 `IWindow` 这个类型名（不能只把函数指针搬到另一个新文件就交差）；② `Test/GPUBackendTester/Main.cpp` 里 MUST 不再出现 `IWindow_` / `IWindowSystem_` 前缀的 C ABI 调用（今天 `TestCSharpWindow` 里那一整段裸调用 —— `IWindowSystem_NewWindow` / `ResolveHandle` / `IWindow_GetWindowSize` / `IWindow_CloseWindow` / `IWindowSystem_LastErrorCode` —— MUST 全部消失）；③ 读数比对 MUST 有基准：先把改动前的实跑日志存进本 change 的 `evidence/`，再逐行 diff —— 要逐条出现的读数是 `C++ 侧窗口尺寸 = 800 x 600`、`[托管侧] 窗口尺寸 = 800 x 600`、`关窗后计数：基线 0 → 建窗后 1 / 泵帧后 1 / UpdateSystem 之后 0`（**四条数都要在，泵帧后那个 1 不许漏**）、`句柄版读数：… 错误码 1001`、`status = pass`

- [ ] 5.6 `IWindowSystem` 的托管表面（与 5.2 同一套生成路径，不许只做 `IWindow` 一个）：生成 `public sealed class IWindowSystem : IDisposable`（类型名逐字照抄），5 个未排除方法逐个镜像原声明 —— `IWindow NewWindow(int width, int height, string windowName, bool visible, bool focused, bool decorate, bool floating)`（**类型名逐字照抄：仓库里没有也不许新造 `Window` 这个类型**；原返回 `castl::weak_ptr<IWindow>` → 托管侧给同名的 `IWindow` 引用，"句柄已失效即抛"）、`int GetWindowCount()`、`int GetMonitorCount()`、`void UpdateSystem()`、`IntPtr GetSystemNativeHandle()`；`int` 在原声明里就是 `int`，MUST NOT 变成 `int32_t` 或 `long`
  验收：① 生成的 `IWindowSystem` 类型 MUST 有这 5 个方法、名字与原声明逐字相同；② `NewWindow` 的 7 个参数 MUST 全部保留且按矩阵映射（`char const* + uint64` → `string`、四个 `int` 参数 → `bool`）—— 且 MUST 有**一条判据能抓住映射错**：在 `Test/CoreTests` 里把 `NewWindow` 赋给 `Func<int,int,string,bool,bool,bool,bool,IWindow>`、把 `GetWindowCount` 赋给 `Func<int>`，签名映射错就编译不过（只比名字的判据抓不住这一格）；③ 表面里 `delegate*`/`void*`/`ulong` 句柄 MUST 为 0，判据直接扫 `out/build/<配置>/codegen/bindings/IWindowSystem.Bindings.cs`（**不许用 `git grep`**，生成物在 `.gitignore` 的 `out/` 下）；④ 只做 `IWindow` 不做 `IWindowSystem` = 本条不合格

- [ ] 5.7 第二张矩阵必须有 conformance 夹具：现有规范「每个矩阵行必须有 conformance 夹具」针对的是「C++ → C ABI」那张表；新增的「C++ 声明 → 托管表面」这张表**一行夹具都没有**，映射写错了没有任何判据会红
  **矩阵定为六行**（全文统一口径，5.9 与 design D7 都用这六行）：① `bool` 返回 → `bool`；② `T&` 出参 → 元组返回值；③ `castl::string const&` 入参 → `string`；④ `castl::string_view` 返回 → `string`（两段式伴生拼回）；⑤ `void*` → `IntPtr`；⑥ `castl::weak_ptr<T>` 返回 → 同名接口引用（失效即抛）。**错误码不算矩阵行**，它不进表面（见 D8/D11）
  验收：为六行各补一个夹具接口（放在 `Tools/CodeGen/BindableShapes.h`，按既有 `IBindRowNN` 编号继续往后排），并为每行各写一条**断言生成结果字符串**的判据（例如 `bool` 那行 MUST 生成 `bool <名字>()` 而不是 `int <名字>()`）；把任一行的映射故意写错（例如把 `bool` 映射成 `int`）MUST 让该行判据报错并点名；`void` / `float` / `int` 这类"本来就同名"的形状不单列行，但 MUST 至少有一个夹具覆盖（否则 `float GetDpiScale()` 会被矩阵漏掉）

- [ ] 5.8 异常映射表：托管表面的失败一律抛 `<接口>Exception`，但"哪些入口会失败、失败码是什么"目前没有明确规定，容易变成有的抛有的不抛。**先决条件**：错误码目前托管侧根本读不到（`CSharpHost.dll` 导出表实测只有 `TryInit`/`TryRelease`；生成的 `.cs` 里 `LastErrorCode`/`LastErrorText` 是 `public static` 字段、全仓没有任何赋值点），所以本条与 D11 一起做，先把错误通道做成可达且**每次调用清零**
  验收：① `design.md` MUST 给出每个可失败入口的码表；1002 现在是裸字面量，MUST 改成 `<接口>_ERROR_*` 具名常量并导出；② 脚本 MUST 把生成器发出的全部错误码与码表逐条比对，`setError` 的码不在表里即红（**MUST NOT** 用"异常消息里不许出现某句话"当判据 —— 那句文案全仓只有本任务写过，恒绿）；③ 每个可失败入口 MUST 各有一条"触发 → 抛出的异常 `Code` 等于表里的值"的测试；④ MUST 有一条反向用例：上一次失败之后紧接着一次成功调用 MUST NOT 抛（错误码是每接口一份 static 全局，不清零就残留）

- [ ] 5.9 文档与收尾验证：`Tools/CodeGen/README.md` 第十四节补"第二张矩阵（C++ 声明 → 托管表面，六行，见 5.7 的口径）"与"表面里不许出现 delegate*/void*/uint64"；`docs/testing.md` 补"托管表面这一层由哪条判据把守"；`docs/agent-rules-rationale.md` 不用动
  验收：① README 里 MUST 出现第二张矩阵的完整表，**六行各一行**（`bool` / `T&` 出参 / `string` 入参 / `string_view` 返回 / `void*` / `weak_ptr`），错误码另起一段说明"不进表面"；`python Tools/CodeGen/verify_readme.py` MUST 0 处不符（注意它**只跑命令、不读表格**，所以"表少一行"要把校验写进 `check_bindings.py` 才判得出）；② `python build.py --config Debug` MUST `BUILD SUCCESSFUL`；③ `CoreTests.exe` MUST 退 0 **且** stdout 里 `FAILED` 行数 = 0 **且** `PASSED` 行数为期望值 —— 因为 `Scripting/Managed` 那条弱句柄用例失败时走的是 `return false`（在 `int main` 里等于退 0），只看退出码会漏；同一条任务里 MUST 把 `Test/CoreTests/SerializerTest.cpp` 那处 `return false` 改成置 `allOk=false`；④ 两个后端 MUST 各用**显式 `--report`** 跑：`GPUBackendTester.exe --backend vulkan --headless 60 --report test_output/result_vulkan.json`、`--backend d3d12 … --report test_output/result_d3d12.json`（默认路径是 `result.json` 且以 trunc 打开，两次会互相覆盖）；⑤ 每份报告里条目数 MUST 等于 `--list` 打印的测试条数（今天 10），缺一条即红；每条 `status` MUST 为 `pass`

- [ ] 5.10 错误通道进函数表（D11）：`IWindowApi_v2` 表头在 `abi_version` / `size` 之后加 `last_error_code` / `last_error_text` 两个槽；每个方法 thunk **入口先清错误码**；错误码按 `(接口, 实例)` 分槽（与 5.4 一起）；版本从 1 升到 2，两侧 `GetApi` 的双相等校验自然拦住旧产物
  验收：① 生成物里 `IWindowApi_v2` MUST 含那两个槽，且 `IWindow.Bindings.cs` 里对应的偏移常量 MUST 与 C++ 侧 `static_assert(offsetof(...))` 一致（现有做法就是这样钉的，照抄）；② 托管侧 MUST 能拿到并调用这两个槽 —— 判据是 `Host.cs` 里不再有未赋值的 `LastErrorCode`/`LastErrorText` 字段（`git grep` 那两个名字后，命中处 MUST 都是"从函数表取地址"的写法，**不许**是裸字段声明）；③ 反向用例：先造一次失败（例如对已释放句柄调 `ResolveHandle`），紧接着调一次必成功的 `GetWindowSize`，后者 MUST NOT 抛 —— 证明"入口清零"生效；④ 两个实例各自失败一次，`Code` MUST 各自独立（证明不是接口级 static 全局）

- [ ] 5.11 句柄发行者（D12）：表面对象的 `Dispose` MUST 用**发行它的那张表**的 `release`，不许按类型猜。二选一并在 `design.md` 的 D12 里注明选了哪条：甲 = 表面对象持发行者的 `resolve`/`release` 两个函数指针（由 `NewWindow` / `GetWindowSystem` 的 thunk 回填）；乙 = 两张表合成一张带接口类型标签的进程级表
  验收：① 交叉句柄用例 MUST 红过再绿：拿 `IWindowSystem_NewWindow` 发的句柄，交给 `IWindow` 表的 `releaseHandle`，改动前 MUST 能在另一张表里清掉一个活项（用"之后 `ResolveHandle` 该活项返回空"证明）；改动之后这种错配 MUST 报错或什么都不做，**另一张表的活项 MUST 仍可解析**；② 借用句柄的存活判据：宿主销毁后旧借用句柄 MUST 不再解出指针（今天 `lookupHandle` 对非弱句柄直接返回指针，这条必红）
