## 1. 句柄：能释放、能回收、弱引用不挡回收

- [ ] 1.1 把两张平行状态合成一张句柄表：`Tools/CodeGen/CodeGenMain.cpp` 里现在分开的 `handles`（`{void* object, uint32 generation}`，按第一个空槽分配）与 `keepAlive`（`vector<shared_ptr<void>>`，按下标追加）合成 `vector<Entry>`，`Entry = { void const* object; uint32 generation; uint8 kind; shared_ptr<void> strong; weak_ptr<void> weak; }`，`registerHandle` / `lookupHandle` / `releaseHandle` 都只操作这一张表（design D2）
  验收：`Test/CoreTests/BindingsHandleTest.cpp` 里现有的 2 条 1001 断言与 1 条错误文本断言 MUST 仍通过；新增断言读表项的 `kind` 字段区分强句柄与弱句柄、并断言同一张表里两类能共存 —— **今天是编译失败（没有 `Entry` 这个类型，也没有 `kind` 字段），红态成立**。（MUST NOT 用"acquire A → release A → acquire B 复用同槽"当红态：今天就会通过，`registerHandle` 本来就从第一个空槽找、`releaseHandle` 本来就置空并进位）

- [ ] 1.2 让释放真的放开强引用：`releaseHandle` MUST 同时 `strong.reset()` 与 `weak.reset()`
  验收：**直接读表项** —— 释放后该表项的 `strong` MUST 为空（产物被 `#include` 进测试，`static` 表在同一翻译单元里可见）。今天释放只清 `handles`、不动 `keepAlive`，所以这条在修好前 MUST 红。判据 MUST NOT 用 `use_count()` 差值（差值能被"删得不干净"骗过，也会把外部合法持有者判红）

- [ ] 1.3 给每个被绑定接口导出两个泛用入口：`<接口>_ReleaseHandle(void* self, uint64_t handle)`（释放）与 `<接口>_ResolveHandle(uint64_t handle)`（句柄 → 对象指针，失败置 1001 并返回空）。**`ResolveHandle` 是必需的**：现有 22 + 5 个 C ABI 入口的第一个参数全是 `void* self`，没有任何一个收 `uint64_t` 句柄 —— 没有它，C# 拿到句柄之后**没有任何办法**用它调 `IWindow` 的方法，"旧句柄报 1001"这条验收也写不出来。句柄表是每接口一份，所以 `NewWindow` 拿到的弱句柄要用 `IWindowSystem_ResolveHandle` 解出 `IWindow*`，再当 `self` 传给 `IWindow` 的方法（两处都是 `void*`）。既有的按方法生成的 `<接口>_Release<方法名>` 与其三处调用点保留不动（design D2）
  验收：`IWindow.Bindings.cpp` 里 MUST 出现 `IWindow_ReleaseHandle` 与 `IWindow_ResolveHandle`；`IBindRow09_ReleaseAcquireThing` MUST 仍在且那三处调用一行都不用改；夹具里 `ResolveHandle` 一个刚释放的句柄 MUST 返回空且错误码为 1001

- [ ] 1.4 弱句柄投影：`castl::weak_ptr<T>` 返回值投影成 `uint64_t` 弱句柄，句柄表存弱引用、不抬高 `use_count()`，`lock()` 失败时置 1001 并返 0（design D1）。连带要改的一整套：矩阵第 11 行从"不投影"改成"弱句柄"、`BindableShapes.h` 加正向夹具 `IBindRow11`、`BindableShapes.expected.txt` 第 21 行换成真签名、`BindableShapes.Reject.h` 里 `IBindRow11Reject` 那条 `weak_ptr` 方法**改成一个真正的裸指针方法**（保留"带理由的 ② 拒绝夹具"这一类，README 第十四节示例一跑的就是它）
  验收：`check_bindings.py` 的读数 MUST 从"签名行 23 / 非签名行 1"变成"非签名行 0"，不符数 MUST 仍为 0；断言"拿到弱句柄前后 `use_count()` 不变"；**判别性用例必须绕开 `ReleaseHandle` 且必须经过 `ResolveHandle`**：C# 侧建窗拿句柄 → `IWindowSystem_ResolveHandle` 解出 `self` → 用 `self` 调 `IWindow::GetWindowSize` 读回 800x600 → 由引擎自己回收窗口（**全程不调任何释放入口**）→ 再 `ResolveHandle` MUST 返回空且错误码 1001，并用 `std::weak_ptr` 见证者的 `expired()` 证明析构已发生。（MUST NOT 拿 `IWindow_LastErrorCode` 的全局值当"该句柄失效"的证据 —— 它忽略 `self`、是每接口一个 static 全局，任何先前的 1001 都会让它读到 1001）

- [ ] 1.5 修订审计表里 `IWindowSystem::NewWindow` 的**三处**：主表那一行、`## 汇总：不可绑清单` 里同名那一行、以及汇总下面「② 所有权不明的裸指针 —— 6 条」那条计数（要改成 5 条并从名单里删掉 `NewWindow`）。只改一处会让同一份文件同时说"可绑"和"不可绑 ②"。文件是 `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md` —— **全仓只能有一份**，新建第二份会让对账脚本直接退出 2（design D6）
  验收：主表该行的「可绑性」列 MUST 逐字等于 `可绑`（MUST NOT 用子串判 —— 现值 `**不可绑**` 含"可绑"二字会假绿），「投影形状」列 MUST 写成弱句柄；不可绑清单里 MUST 不再出现该方法；那个计数行 MUST 变成 5 条；`python Tools/CodeGen/check_bindable_audit.py --json` MUST 报 `"ok": true`

- [ ] 1.6 处理 `GetMonitor`：审计表判它是"可绑（按值镜像）"，本 change 只是这一轮没做非标量成员的镜像，所以**可绑性列不动**（仍写"可绑"），只在备注列写"本轮未实现：成员是 `cacore::Rect<float>` 模板实例化类型，布局镜像只收成员全标量的结构体"。这一类属于 `specs/csharp-bindable-surface/spec.md` 新增要求里的第三态，MUST NOT 写成 ①～④（design D4）
  验收：`openspec validate csharp-window-creation` MUST 通过；审计表该行的「可绑性」列 MUST 仍是 `可绑`、备注列 MUST 出现"本轮未实现"；`check_bindable_audit.py --json` MUST 报 `"ok": true`

- [ ] 1.7 给 `Tools/CodeGen/check_bindable_audit.py` 加一条能失败的检查：被 `CA_BIND_SKIP` 点名、而表里「可绑性」列是"可绑"的方法，排除理由里 MUST 出现"本轮未实现"并点名缺的投影路径
  验收：把某个 `CA_BIND_SKIP` 的理由临时改成只写"④ 语义越界"（不提本轮未实现），脚本 MUST 非零退出并点名该行；还原后 `--json` MUST 报 `"ok": true`

## 2. 接线：`IWindowSystem` 进函数表，托管侧真的建窗口

- [ ] 2.1 在 `WindowSystem/header/CAWindow/WindowSystem.h` 的 `IWindowSystem` 上加 `CA_BIND()`，并给 11 个方法逐个加 `CA_BIND_SKIP` 并写明理由（10 个 `Set*Callback` 写"本轮未实现：缺回调投影路径（保活表 + 注销句柄）"；`GetMonitor` 写"本轮未实现：成员是 `cacore::Rect<float>` 模板实例化类型…"。**两条都 MUST NOT 写成 ①～④ 中的任何一类**，见 design D4），`CA_BIND_SKIP` 放类体末尾。**`build.py` 的 `CODEGEN_BINDINGS` 不用加生成行**（`IWindow` 与 `IWindowSystem` 在同一个头里），但那一行的**产物前缀清单** MUST 从 `["IWindow"]` 扩成 `["IWindow", "IWindowSystem"]` —— 否则 `IWindowSystem.Bindings.*` 没落地也不会让构建红
  验收：`check_bindable_audit.py` MUST 仍 0 问题（方法数 60 不变）；生成器 MUST 打印 11 条带理由的排除、退出码 0；`out/build/<配置>/codegen/bindings/` 下 MUST 多出 `IWindowSystem.Bindings.cpp` 与 `.cs`，日志里 MUST 出现 `[接口] IWindowSystem → 5 条`；把该产物临时删掉再构建 MUST 失败（证明前缀清单真的在管它）

- [ ] 2.2 让生成器对"带 `castl::function` 参数、又没写 `CA_BIND_SKIP`"的方法非零退出并点名（今天它是照固定签名 `void (*)(void*, uint32_t, float)` 硬投影，正是静默发桩的来源）。同时给夹具 `IBindRow13::SetCallback` 加 `CA_BIND_SKIP`、把 `BindableShapes.expected.txt` 第 23 行从签名改成"拒绝"、同步矩阵第 13 行的行文与 `check_bindings.py` 的非签名行计数
  验收：把 `IBindRow13` 的 skip 临时删掉跑生成器，MUST 非零退出并点名 `IBindRow13::SetCallback`；还原后退出码 0；`check_bindings.py` 的"非签名行"读数 MUST 从 0 变成 **1**（第 13 行改成拒绝）

- [ ] 2.3 给矩阵补 `castl::string_view const&` 入参这一行（`NewWindow` 第 3 个参数就是它）：在 `Tools/CodeGen/BindableShapes.h` 加夹具行、在 `BindableShapes.expected.txt` 加期望签名、在 `Tools/CodeGen/README.md` 第十三节的矩阵表加一行；并给 `check_bindings.py` 加一条能失败的交叉检查 —— **README 第十三节矩阵的数据行数 MUST 等于 `expected.txt` 里不同行号前缀的个数**（今天脚本打印的"矩阵行数 14"是写死的字面量，不比对任何东西，所以矩阵漏写一行是绿的）
  验收：矩阵行数与期望文件前缀数 MUST 都从 14 变 15，且这条交叉检查在"只在 README 加行、不加 expected"时 MUST 报错；生成物里该夹具的签名 MUST 是 `char const* + uint64_t` 两个参数

- [ ] 2.4 让 `CSharpHost/private/CSharpHost.cpp` 支持第二个接口编号：include 生成的 `IWindowSystem.Bindings.cpp`，`OnGetInterfaceApi` 按编号 2 返回 `IWindowSystemApi_v1` 的表与实例
  验收：按审计表里 5 个入表方法的**名字顺序**断言生成的表成员名序列逐字相同，并断言 `sizeof(IWindowSystemApi_v1)` 等于 `8 + 8 * 5 = 48`（这个数从方法条数算出来，**不从生成物里读** —— 两侧都读生成物就是同源恒等，抓不到生成器漏方法）；编号 2 在实例登记前 MUST 返回 0、登记后 MUST 非 0

- [ ] 2.5 把生成的 `.cs` 编进托管工程：`Scripting/Managed/Scripting.Managed.csproj` 用 `$(CascadedBindingDir)` 收生成物，`build.py` 的 `build_managed()` 追加 `-p:CascadedBindingDir=<BUILD_DIR>/codegen/bindings`；属性为空或目录里没有 `.cs` 时 csproj MUST 报错（design D3）
  验收：托管侧 MUST 能取到生成物里的类型与常量（不只是"csproj 里有一行 Compile"）；故意不传该属性重编 MUST 失败并说明缺生成物目录

- [ ] 2.6 托管侧改用生成的绑定：`Scripting/Managed/Host.cs` 删掉手抄的 `WindowApiSize = 8 + 8 * 22` 与 `WindowApiGetWindowSize = 56`，改成引用生成物里的常量；把第一步的手写探针换成"用生成的绑定建窗 → `ResolveHandle` 解出 `self` → 读尺寸 → 关窗"
  验收：`Host.cs` 里这两个常量的字面赋值 MUST 0 命中；建窗调用 MUST 传全参数（除 `void* self` 外 8 个：`width`、`height`、名字指针、名字长度、`visible`、`focused`、`decorate`、`floating`），以生成物里的签名为准

- [ ] 2.7 新增一条端到端测试并注册进 `--list`：在 `Test/GPUBackendTester/Main.cpp` 里加一个测试函数，由它自己登记实例编号 2（全仓现在只有 `TestSimpleTriangle` 登记过编号 1，`TestScriptHost` 不建窗、不调 `UpdateSystem`）
  验收：`out/build/<配置>/bin/GPUBackendTester.exe --list` MUST 列出该测试名；**用 `--headless N` 跑**（只有带这个参数才会建 `test_output/`、把 stdout 重定向到 `test_output/<测试名>.log`），日志里 MUST 同时出现带 `[托管侧]` 前缀与无前缀的窗口尺寸读数，且数值相同；再补一条反向：不登记实例编号 2 时该测试的 `status` MUST 是 fail（证明它不是恒 pass）

- [ ] 2.8 关窗与回收的读数：C# 调 `CloseWindow()` 后，先在泵帧**之前**让两侧各读一次 `WindowShouldClose()`（都应为真），再由 C++ 侧显式调一次 `ctx.pWindowSystem->UpdateSystem()`（回收只在这个函数里做，不调就永远回不到原值），最后读一次 `GetWindowCount()`，并用 `IWindowSystem_ResolveHandle` 解旧句柄、断言拿到空 + 错误码 1001
  验收：日志里 MUST 出现：泵帧前两侧 `WindowShouldClose` 都为真、建窗计数比基线 +1、泵帧后回到基线、旧句柄 `ResolveHandle` 返回空且 1001。顺序 MUST 正确 —— 窗口被回收后句柄就失效

## 3. 收尾：文档、遗留、构建验证

- [ ] 3.1 同步对外文档 `Tools/CodeGen/README.md`：**第十三节**矩阵的编辑归 1.4 与 2.3（本任务只复核那两处已就位，不重改）；**第十四节**补上弱句柄投影、泛用释放入口与解析入口、释放后不再持有强引用的规则，并改掉第十四节「不做什么」那段里"不生成 `castl::weak_ptr`"的说法（在 1.4 之后它是错的），以及示例一的真实输出（`IBindRow11Reject` 的形状在 1.4 里换过了）。`docs/testing.md` 补端到端断言落在哪一层
  验收：`Select-String -Path "Tools/CodeGen/README.md" -Pattern '不生成.*weak_ptr'` MUST 0 命中；「弱句柄」「ReleaseHandle」「ResolveHandle」三个词 MUST 各至少 1 处命中；示例一那段真实输出 MUST 用 `python Tools/CodeGen/verify_readme.py` 跑过并逐字一致（该脚本对带 `--out-dir` 的命令只比退出码、跳过文本，所以**必须**另外人工核对那几行输出）

- [ ] 3.2 更新 `docs/TODO.md`：锚点 MUST 从文件里**逐字复制**，执行前先跑出"命中 1 处"、执行后"命中 0 处"两个读数
  验收：这四条各 MUST 从命中 1 变命中 0 —— `` `keepAlive` 只增不减 ``、`` 伴生 `ReleaseXxx` 只在 ``、`` `weak_ptr` 返回 / 入参整体拒绝 ``、`` 错误码 1001 只有 ``；未修的四条 MUST 仍各命中 1 处（`` `castl::function` 参数一律投影成固定签名 ``、`` **句柄入参的反查根本没实现** `` —— 本 change 只加了 `ResolveHandle` 这个新入口，**没有**实现"接口参数里的 `shared_ptr` / `weak_ptr` 反查"，那条原样留着、`` `setError` 往 `char[256]` ``、`` 只有 1001 / 1002 两个码 ``）；再补三条新的（代际号回绕、接口编号手抄在两处且上限 8 超限静默返回 0、句柄表无锁）

- [ ] 3.3 构建与测试验证：跑 `python build.py --config Debug`，失败则分析并修到 `BUILD SUCCESSFUL`；然后跑 `out/build/x64-debug/bin/CoreTests.exe`，以及 `Test/GPUBackendTester` 用 vulkan 与 d3d12 各跑一遍全部测试
  验收：构建 `BUILD SUCCESSFUL`；`CoreTests.exe` 退出码 0 且 stdout 里 `FAILED` 行数为 0；两个后端**各用 `--headless N --report test_output/result_<后端>.json` 跑**（`result.json` 每次运行都以 trunc 打开，两个后端用同一路径只会留最后一份），两份报告里 `--list` 打印的**每一条**测试 `status` MUST 全为 `pass`，且新加的那条测试名 MUST 出现在里面（**MUST NOT 写死"9 条"** —— 2.7 新增一条之后就变成 10 条）；`check_bindings.py --product-dir <当前目录>` 报真实接口 0 桩、0 处签名不符；`check_bindable_audit.py --json` 报 `"ok": true`
