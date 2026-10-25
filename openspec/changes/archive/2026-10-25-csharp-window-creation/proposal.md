## Why

**前置：`binding-stub-and-shapes` 必须先归档。** 这个 change 假设"thunk 有没有真调实现"已经能被构建判据看出来（那个 change 加的结构桩判据），否则下面每一项"改好了"都不可核。

在此之上，它要做的是**让 C# 真的能创建与销毁窗口**。今天做不到，原因有三层：

1. `IWindowSystem` 至今没有 `CA_BIND`，发射器根本没读到过这个接口。
2. `NewWindow` 返回 `castl::weak_ptr<IWindow>` —— C# 侧没有弱引用的对应物，而**句柄表本来就能表达它**（存弱引用 + 用已有的 1001 号错误码判失效），只是发射器没这条路。
3. 句柄释放不掉：释放只清表项、不从保活数组里删元素，对象永不析构；而 `WindowSystem_Impl.cpp` 回收窗口的条件是 `use_count() == 1 && WindowShouldClose()` —— 一条永远不成立的强引用就把整个回收路径堵死了。

## What Changes

- **句柄表合成一张**：现在有两张平行的 `static` 容器 —— `handles`（`{void* object, uint32 generation}`，按第一个空槽分配）与 `keepAlive`（`vector<shared_ptr<void>>`，按下标追加）。两者下标本来就不是一回事，释放时只清前者、完全不动后者。改成一张 `vector<Entry>`，`Entry = { object, generation, kind, strong, weak }`，释放时把 `strong` / `weak` 一起清掉、代际进位。
- **释放入口无条件生成**：现在只有"每个返回句柄的方法各生成一个 `<接口>_Release<方法名>`"，`IWindow` 一个方法都不返回句柄，所以它没有释放入口。改为**每个被绑定接口无条件导出一个泛用入口** `<接口>_ReleaseHandle(void* self, uint64_t handle)`；既有的按方法入口与其调用点保持不动（不为了改名去改测试）。
- **弱句柄投影（BREAKING：改投影矩阵第 11 行的含义，不增行）**：`castl::weak_ptr<T>` 返回值投影成弱句柄 —— 句柄表存弱引用、不抬高 `use_count()`，对象析构后旧句柄按 1001 判失效。归档审计表把 `weak_ptr` 归进 ② 类（所有权不明**裸指针**）判成不可绑，而 `weak_ptr` 不是裸指针、归不进 ②，按 `csharp-bindable-surface` 自己写的规矩「归不进去的行说明矩阵漏了一类」。
- **接入 `IWindowSystem`**：加 `CA_BIND()`；16 个方法里 5 个进函数表（`NewWindow` / `GetWindowCount` / `GetMonitorCount` / `UpdateSystem` / `GetSystemNativeHandle`），其余 11 条逐个 `CA_BIND_SKIP`。这 11 条的排除理由**不写成 ①～④**（那四类说的是"形状本身不可投影"，而回调与非标量成员镜像是"这一轮还没做"）—— 按新增的**第三态**写"本轮未实现：缺哪条投影路径"。
- **矩阵补 `string_view const&` 入参**：`NewWindow` 第 3 个参数就是它，矩阵今天没有这一行；C ABI 面带不了 5 个默认实参，必须显式展开成全参数。
- **托管侧真的用它**：生成的 `.cs` 现在从没编进任何工程，常数 `8 + 8 * 22`、`56` 是手抄的。改为编进 `Scripting.Managed.csproj`，托管侧用生成的绑定代替手写探针。
- **端到端可观察**：C# 建一个 800x600 窗口 → 用句柄读回 `800 x 600` → C++ 侧 `GetWindowCount()` 由 N 变 N+1 → C# 关掉 → 泵帧前两侧 `WindowShouldClose` 都为真 → 引擎泵一帧 → 计数回到 N、旧句柄报 1001。（N 由实跑读数确定，不预设。）

## Capabilities

### New Capabilities
- `csharp-window-runtime`: C# 通过生成的绑定创建、查询、关闭窗口，并保证关闭后 C++ 侧真的回收 —— 这一层描述**用户能观察到什么**，与"生成器怎么发形状"分开。

### Modified Capabilities
- `csharp-bindings`: 修改"句柄分配与释放"（释放要放开强引用、每个接口无条件有泛用释放入口、弱句柄判失效走弱引用过期这条路）、修改"回调必须带保活与注销"（本轮不生成回调行，改成显式排除并写"本轮未实现"）、修改"每个矩阵行必须有 conformance 夹具"（`weak_ptr` 那一行从拒绝夹具改成正向弱句柄夹具）；新增"托管侧绑定产物必须参与编译"。
- `csharp-bindable-surface`: 矩阵第 11 行从"`weak_ptr` 不投影"改成"弱句柄"；新增一条要求——**可投影但本轮还没实现的形状必须显式排除并写"本轮未实现"，不许静默发桩，也不许伪装成 ①–④**。

## Impact

- 发射器：`Tools/CodeGen/CodeGenMain.cpp`（句柄表、释放入口、弱句柄投影）、`Tools/CodeGen/check_bindable_audit.py`（新增一条对账检查）、`Tools/CodeGen/check_bindings.py`（矩阵行数那一行的读数）、`Tools/CodeGen/BindableShapes.h` + `BindableShapes.expected.txt` + `BindableShapes.Reject.h`（第 11 行改正向夹具 + `string_view const&` 入参行）、`build.py`。
- 接口：`WindowSystem/header/CAWindow/WindowSystem.h`（`IWindowSystem` 加 `CA_BIND` + 11 处排除理由；`NewWindow` 保持 `weak_ptr` 返回）。
- 审计表：`openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`（`NewWindow` 的两处结论改写；`GetMonitor` 的可绑性不动、只加"本轮未实现"备注）。**这份表全仓只能有一份** —— 对账脚本找到两份会直接退出 2。
- 引擎侧接线：`CSharpHost/private/CSharpHost.cpp`（include 第二份生成产物、登记接口编号 2 = `IWindowSystem`）。
- 托管侧：`Scripting/Managed/Host.cs`、`Scripting/Managed/Scripting.Managed.csproj`（编译生成物）。
- 测试：`Test/GPUBackendTester/Main.cpp`（新增一条会建窗、登记实例、泵帧的端到端测试）。
- 文档：`Tools/CodeGen/README.md` 第十三节矩阵与第十四节说明、`docs/testing.md`、`docs/TODO.md`。
- **不改**：`IWindow::GetWindowSystem` 仍排除；不做 ABI 版本兼容档；句柄表不加锁；接口编号上限 8 不动。

## Non-goals

- **不修 `IWindow` 的桩**：那个在前置 change 里做完了。本 change 假设 `IWindow` 的 22 个槽已经全走真实现。
- **本轮不实现回调投影路径**（保活表 + 注销句柄）。10 个 `Set*Callback` 逐个 `CA_BIND_SKIP` 并写明"本轮未实现：缺回调投影路径"，审计表对它们的结论仍保持"可绑"、不改。
- **本轮不实现非标量成员的结构体镜像**（`MonitorInfo` 的成员是 `cacore::Rect<float>` 这个模板实例化类型，布局镜像只收成员全是标量的结构体），`GetMonitor` 因此排除并写同上理由；审计表结论仍保持"可绑"，只在备注列写"本轮未实现"。
- **不实现容器形状的行为**：夹具里 4 个容器参数的 thunk、1 个镜像按值返回的 thunk、1 个回调 thunk 本轮仍是桩（前置 change 明确不做），桩判据对夹具产物只打印不判定。
- 不放宽函数表版本校验（仍要求"版本相等 + 大小相等"）。
- 不给句柄表加锁、不改 `registerHandle` 的线性找空槽。
- 不从 C# 发起渲染：窗口只建、只查、只关，画面仍由 C++ 侧的测试画。
