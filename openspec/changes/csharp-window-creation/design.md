## Context

**前置：`binding-stub-and-shapes` 归档之后再做这个 change。** 它提供两件东西：结构桩判据（构建里能看出"thunk 有没有真调实现"）与 `IWindow` 的 22 个槽全走真实现。没有它，本 change 的端到端验收会撞上"`WindowShouldClose` 恒为假"这种假绿。

本 change 开始时的事实：

| 事实 | 复核命令与读数 |
|---|---|
| 句柄有两张平行表，释放只清一张 | `Tools/CodeGen/CodeGenMain.cpp` 里 `handles`（按第一个空槽分配）与 `keepAlive`（按下标追加）；`releaseHandle` 只清 `handles` 的项、完全不动 `keepAlive` |
| `IWindow` 没有释放入口 | 产物里 `IWindow_ReleaseHandle` 零命中；只有内部 `static IWindow_releaseHandle` |
| `IWindowSystem` 未被绑定 | `WindowSystem.h` 里它没有 `CA_BIND`；`build.py` 的那一条只产出 `IWindow` |
| 归档审计表把 `NewWindow` 判成不可绑 | `openspec/changes/archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md` 里 `IWindowSystem::NewWindow` 出现**两处**（主表与不可绑清单），都写着"② …建议改成返回窗口 id" |
| 生成的 `.cs` 从没编进任何工程 | `git grep -F "Scripting.Generated.Bindings"` 命中 3 处：`Host.cs` 的一句注释、发射器写命名空间那一行、`docs/TODO.md` 里复述这句话的那一行；工程侧没有 `<Compile Include>` |
| 只有一处登记过接口实例 | 全仓 `SetInterfaceInstance` 只有 `Test/GPUBackendTester/Main.cpp` 里的一处、编号 1；`TestScriptHost` 不建窗、不调 `UpdateSystem` |

## Goals / Non-Goals

**Goals:**

- 句柄能被释放，且释放后生成物不再持有强引用（对象能析构）。
- `castl::weak_ptr<T>` 返回值有确定的投影形状（弱句柄），不再被当成 ② 类整体拒绝。
- `IWindowSystem` 进函数表，托管侧用生成的绑定建一个 800x600 窗口、读回尺寸、关掉，C++ 侧窗口数 N → N+1 → N（N 由实跑读数确定）。

**Non-Goals:**

- 不修 `IWindow` 的桩（前置 change 做完了）。
- 不实现回调投影路径、不实现非标量成员的结构体镜像、不实现容器形状（用"本轮未实现"显式排除，见 D4）。
- 不放宽函数表版本校验；不加锁；不改句柄位宽；不动接口编号上限 8。
- 不从 C# 发起渲染。

## Decisions

### D1 弱句柄：句柄表新增"弱引用"一类，而不是让 `NewWindow` 改签名

- `castl::weak_ptr<T>` 返回值投影成 `uint64_t` 弱句柄：句柄表里存 `std::weak_ptr<void>`，不增加 `use_count()`；取用时 `lock()`，失败则置错误码 1001 并返回 0。
- 依据：`castl::weak_ptr` 在本仓库就是 `std::weak_ptr`（`CACore/header/CASTL/CAContainerBase.h` 里 `using namespace std;`），有 `lock()` 与 `expired()`。
- 依据：`WindowSystem_Impl.cpp` 的回收条件是 `m_Windows[currentIndex].use_count() == 1 && m_Windows[currentIndex]->WindowShouldClose()`。弱句柄不抬高 `use_count()`，这条回收条件保持成立。

**判别性用例必须绕开 ReleaseHandle。** 1001 有两条来源：释放时代际进位、以及 `lock()` 失败。如果用例写成"拿弱句柄 → 调 `ReleaseHandle` → 再调 MUST 1001"，那么 `lock()` 分支一行不写也全绿。所以用例必须是：让引擎自己回收对象（不经 `ReleaseHandle`），再用旧弱句柄调方法，MUST 得 1001 且不崩；并用 `std::weak_ptr` 见证者的 `expired()` 判定析构已经发生。

被否决 A：`NewWindow` 改返回 `castl::shared_ptr<IWindow>`（强句柄）。强句柄让 `use_count()` 永远 ≥ 2，窗口永不回收 —— 上表那条回收条件直接失效，要救就得要求 C# 每次记得释放。

被否决 B：审计表建议的"`NewWindow` 改成返回 `uint64_t` 窗口 id"。它违反 `csharp-bindable-surface` 的 Purpose 原话「不靠改接口签名去迁就 C ABI」；弱句柄把负担放在投影层，不改 C++ 接口。

被否决 C：把 `weak_ptr` 塞进 ② 类（现状）。② 的定义是"所有权不明的**裸指针**"，`weak_ptr` 不是裸指针；`csharp-bindable-surface` 自己写着「归不进去的行说明矩阵漏了一类，必须回头补本条而不是把它塞进现有四类里」。

**这条改动的连带面比"加一行夹具"大，必须一起算。** 矩阵第 11 行今天是"不投影"，它的夹具是 `Tools/CodeGen/BindableShapes.Reject.h` 里的 `IBindRow11Reject`（用 `CA_BIND_SKIP(PeekThing, "② …")` 排除），`BindableShapes.expected.txt` 第 21 行逐字写着 `11|拒绝：非零退出，类别 ②（所有权不明裸指针 / weak_ptr 不投影）`，README 第十三节矩阵那一段也写着"不投影"。让 `weak_ptr` 可投影意味着：这一行从"不投影"改成"弱句柄"、`IBindRow11` 变成 `BindableShapes.h` 里的正向夹具、`expected.txt` 第 21 行换成一条真签名、`BindableShapes.Reject.h` 里那条 SKIP 删掉、README 第十三节同步。矩阵**行数不变（仍 14）**，变的是这一行的含义；`check_bindings.py` 的"非签名行（拒绝或错误码）1"会变成 0，这个读数变化要在任务里写清，否则做完 1.4 会因为期望文件对不上而红。D6 之后再加 `string_view const&` 入参一行，那时才 14 → 15。

### D2 句柄表合成一张表，并给每个接口加一个泛用释放入口

现在有**两张**平行的 `static` 容器：`handles`（`{void* object, uint32 generation}`）与 `keepAlive`（`vector<shared_ptr<void>>`）。`keepAlive` 按下标追加，`handles` 按"第一个空槽"分配，两者下标本来就不是一回事；释放时只清 `handles`、完全不动 `keepAlive`，所以对象永不析构。

改为**一张表**：`vector<Entry>`，`Entry = { void const* object; uint32 generation; uint8 kind; shared_ptr<void> strong; weak_ptr<void> weak; }`。释放时 `strong.reset()`、`weak.reset()`、代际进位、`object` 置空 —— 一个动作同时完成四件事。

**释放入口的现状是"每个返回句柄的方法各生成一个 `<接口>_Release<方法名>`"**（`IBindRow09_ReleaseAcquireThing` 就是现成的），`IWindow` 一个方法都不返回句柄，所以它没有释放入口。本 change 不替换既有伴生，而是**无条件再导出两个泛用入口**：

- `<接口>_ReleaseHandle(void* self, uint64_t handle)` —— 释放；
- `<接口>_ResolveHandle(uint64_t handle)` —— 句柄换对象指针，失败置 1001 并返回空。

**`ResolveHandle` 不是可选项。** 现有 22 + 5 个 C ABI 入口的第一个参数全是 `void* self`，没有任何一个收 `uint64_t` 句柄 —— 没有它，C# 拿到句柄之后没有任何办法用它调 `IWindow` 的方法，"用旧句柄调方法必须报 1001"这条验收根本写不出来（能写出来的只有"读那个忽略 `self` 的每接口全局错误码"）。

句柄表是每接口一份，所以 `NewWindow` 拿到的弱句柄要用 `IWindowSystem_ResolveHandle` 解出 `IWindow*`，再当 `self` 传给 `IWindow` 的方法 —— 两边都是 `void*`，C# 侧只需要一个 `void*` 变量。

既有的按方法释放入口与它的调用点（`Test/CoreTests/BindingsHandleTest.cpp` 三处）保持不动，避免为一个改名去改测试。

"释放后生成物不再持有强引用"这条，判据写成**直接读表项**（产物是被 `#include` 进测试的，`static` 表在同一个翻译单元里可见）：释放后该表项的 `strong` MUST 为空。不用 `use_count()` 差值 —— 差值既能被"删得不干净"骗过（3 → 2 仍然减一），也会把"窗口同时被渲染后端持有"这种合法情况判红。

### D3 生成的 `.cs` 由 `build.py` 传属性编进托管工程

`build.py` 的 `build_managed()` 已经是 `dotnet build <csproj> -c <cfg> -o <dir>` 的列表调用，追加 `-p:CascadedBindingDir=<BUILD_DIR>/codegen/bindings` 即可；csproj 里用这个属性收 `<Compile Include="$(CascadedBindingDir)/**/*.cs" />`。

不往源码树里落生成物 —— 生成物已经在构建目录里，清理构建目录就一并清掉。

风险处置：属性为空或目录里没有 `.cs` 时，csproj 用一个目标**报错**，不静默编出缺绑定的程序集。判据要能区分"真编进去了"与"只是把文件列进了 `Compile`"：断言托管侧能取到生成物里的类型与常量（而不是只断言 csproj 里有那行）。

### D4 回调与非标量成员镜像走"可投影但本轮未实现"的第三态，不伪装成 ①–④

归档审计表对 10 个 `Set*Callback` 与 `GetMonitor` 的结论都是"可绑"，本 change 只是这一轮没做那两条投影路径，所以生成器侧必须显式排除。

**不能按 ④ 类写。** ④ 的定义是「技术上绑得了、但脚本不该调的入口（模块初始化期的依赖注入、销毁整个后端之类）」；装一个回调不是"脚本不该调"，是"我们这轮还没做保活表"。把实现进度写成语义越界，等于把范围裁剪伪装成项目规则。

也不能按 ② 写（那是裸指针）。所以补的不是"第五类不可投影"，而是**第三态**：`可投影但本轮尚未实现`。它落在本 change 在 `specs/csharp-bindable-surface/spec.md` 新增的那条要求里：审计表结论保持"可绑"，代码里用 `CA_BIND_SKIP` 写「本轮未实现：缺…投影路径」，生成器对"既没实现、又没排除"的这类方法非零退出。

这样三件事各自归位：形状可不可投影 = 四类说了算；这一轮做没做 = 第三态说了算；两者的结论都不互相冒充。

被否决：把回调安装硬说成 ④ 语义越界（见上，把进度写成规则）；把它归进 ②（`castl::function` 不是裸指针）；真去实现保活表与注销句柄（本 change 的范围装不下，且它不是"C# 能建窗口"的前提）。

### D5 接口编号（1 = `IWindow`，2 = `IWindowSystem`）仍手写在两处

`CSharpHost.cpp` 与 `Host.cs` 各写一份。本 change 不动它：编号表要由谁产出、产出到哪个文件，是独立的一件事，且它不挡窗口创建。另外 `CSharpHost.cpp` 里还有一处静默丢弃：`interfaceId >= 8u` 直接返回 0，编号上限 8 且不可配。两条一起记进 `docs/TODO.md`。

### D6 审计表只能改归档那一份，不能新建

`Tools/CodeGen/check_bindable_audit.py` 的 glob 是 `openspec/changes/*/evidence/bindable-audit.md` 加 `openspec/changes/archive/*/evidence/bindable-audit.md`，**找到两份直接退出 2**（"找到多份审计表，无法判断以哪份为准"）。全仓唯一一份在归档目录下，所以是**改它**。

`NewWindow` 在表里有**三处**：主表那一行、`## 汇总：不可绑清单` 里同名那一行、以及汇总下面「② 所有权不明的裸指针 —— 6 条」那条计数（要连同名单一起改成 5 条）。只改前两处会让同一份文件同时说"可绑"和"不可绑"。

`GetMonitor` 那一行不同：它的结论是"可绑（按值镜像）"，本 change 只是这一轮没做非标量成员的镜像，所以**可绑性列不动**（仍写"可绑"），只在备注列写"本轮未实现：成员是 `cacore::Rect<float>` 模板实例化类型"。

**矩阵补 `string_view const&` 入参这一行**（`NewWindow` 第 3 个参数就是它）：在 `BindableShapes.h` 加夹具行、在 `expected.txt` 加期望签名、在 README 第十三节矩阵表加一行。行数 14 → 15。

对账脚本再加一条能失败的检查：被 `CA_BIND_SKIP` 点名、而表里结论是"可绑"的方法，理由里必须出现"本轮未实现"（今天脚本只查"每个纯虚方法都有行"与"不可绑的行必须有理由"，从不把两边比）。

## Risks / Trade-offs

- **句柄表合成一张表要同时改三类操作**（登记、查找、释放），而强弱两种句柄混在一张表里 → 最容易留悬垂指针。缓解：`lock()` 失败必须置 1001 的用例绕开 `ReleaseHandle`（D1），且先在夹具上验再动真实接口。
- **代际号回绕** → 位宽是 `--handle-generation-bits 16`（参数表在 `Tools/CodeGen/README.md` 的**第二节**，第 75–78 行；第十四节没有参数表），同一个槽被复用 65536 次之后，旧句柄可能命中新对象。本 change 不改位宽，接受这个上限并记 TODO。
- **两个接口各有自己的句柄表** → 跨接口的句柄互不通用。这是现状，本 change 不改。
- **`.cs` 编译依赖 `build.py` 传属性** → 直接用 IDE 编 `Scripting.Managed.csproj` 会缺生成物。缓解见 D3 的报错目标。
- **`NewWindow` 有 `castl::string_view const&` 入参与 5 个默认实参** → C ABI 面带不了默认值，托管侧必须传全 8 个参数（除 `void* self` 外）；矩阵里今天没有这个入参形状，要新增一行。
- **接口编号上限 8 且静默丢弃** → `CSharpHost.cpp` 里 `interfaceId >= 8u` 直接返回 0，超限不报错。本 change 只用编号 2，碰不到这个上限，但它是个静默失败点，记进 `docs/TODO.md`。

## Migration Plan

三步，每步单独构建通过、可单独回滚（撤销该步的提交）：

1. 句柄：D2（合成一张表 + 泛用释放入口 + 表项 `strong` 断言）、D1（弱句柄 + 绕开 `ReleaseHandle` 的 1001 用例）、审计表修订与对账脚本新检查，先在夹具上验再动真实接口。
2. 接线：`IWindowSystem` 加标注与 11 处"本轮未实现"排除、`string_view const&` 入参夹具行、`CSharpHost` 登记编号 2、托管侧编生成的 `.cs` 并用它建窗口、新增一条会建窗与泵帧的端到端测试。
3. 收尾：README / `docs/testing.md` / `docs/TODO.md` 同步，跑构建与全部测试。

## Open Questions

- 弱句柄的错误码沿用 1001（句柄已失效）还是新增一个"背后对象已析构"的码？倾向前者：C# 侧看到的行为一样，都是"这个句柄不能再用了"。
- `UpdateSystem()` 该不该进函数表：审计表结论是可绑（"每帧调一次的事件泵，由引擎调；脚本调也不破坏语义"），但脚本自己泵事件会让引擎侧的帧节奏与脚本的调用次数耦合。本 change 先按可绑放进去，若实测出问题再收紧。

## 返工决策（2026-10-25 加，对应 tasks.md 第 5 节）

以下四条是**用户在验收阶段打回**之后补的：`IWindow` 的 C ABI 面虽然通了，但托管侧拿到的是 `public static delegate* unmanaged<…>` 加偏移常量，用起来是 C 风格；用户的要求是**托管表面镜像原始 C++ 接口声明**（`WindowSystem.h` 里 `IWindow` 那份），C ABI 连同错误码、句柄、`uint64` 全部退成 `internal` 实现细节。

### D7 两张投影矩阵，而不是一张

现有投影矩阵只描述「C++ 声明 → C ABI」。返工要加第二张「**C++ 声明 → 托管表面**」，两张同源、同一次生成：

- `bool` 返回：C ABI 是 `int32_t`（0/1），托管表面还原成 `bool`；
- `T&` 出参：C ABI 是 `T*`，托管表面按 C# 惯例给元组返回值（例如 `(int Width, int Height) GetWindowSize()`）；
- `castl::string const&` / `string_view`：C ABI 是 `char const* + len`（两段式用两个伴生），托管表面还原成 `string`；
- `void*`：C ABI 是 `uint64` 令牌，托管表面还原成 `IntPtr`；
- 错误码：**只存在于 C ABI**，托管表面按"失败即抛"处理（原声明没有错误通道，镜像之后就不能让使用者看到错误码）。

拒绝理由：手写包装层能更快达标，但那等于把已生效规范里「两侧由同一次生成产出」作废，且下一次改接口又要手改一遍。

### D8 表面失败一律抛异常，异常类型由生成器产出

C ABI 的错误码在托管表面**不可见**。生成器为每个接口产出一个 `<接口>Exception`（带 `Code` 与来自 `LastErrorText` 的 `Message`），表面方法调用后检查错误码、非 0 即抛。拒绝理由：让使用者逐调用判返回值，等于把 C 风格原样搬到 C#，与"镜像原接口"矛盾（原接口没有返回值可判）。

### D9 "借用句柄"：非拥有的裸指针返回可以绑，不再一律拒绝

`IWindow::GetWindowSystem()` 返回 `IWindowSystem*`，所有权在引擎（模块生命周期）。现在的做法是把它判成 ② 类整体排除，但镜像要求它必须出现在表面上。改法：句柄表新增一种 kind（借用：只存裸指针，`ReleaseHandle` 只清槽不销毁），把"所有权不明的裸指针返回"从"必须拒绝"改成"必须显式声明为借用"。

拒绝理由：让 `GetWindowSystem()` 继续缺席，则托管表面与原声明不同形，"镜像"这条要求当场就不成立。

### D10 同接口多实例：登记要带实例名这一维

`CAModuleManager` 的实例表是 `接口类型 → { 实例名 → void* }`，同一接口可以注册多个实现（`CA_MODULE_INSTANCE(TInterface, TImplemented, InstanceName)` 的第三个参数就是实例名；仓库里已经这么用：`RenderBackend_D3D12` / `IOManager_FS` / `ShaderCompilerManager_Slang`）。当前 `SetInterfaceInstance(uint32 id, void*)` 只按"接口编号"定位，**隐含假设每个接口只有一个实例**，两个 window system 并存时 `GetWindowSystem()` 会张冠李戴。改法：登记与查找都加实例名这一维（`接口名 + 实例名 → 实例`），编号只作为兼容旧调用点的糖。

拒绝理由：本 change 早段就提过这个场景（Vulkan 与 D3D12 同时注册），留成已知缺陷比现在多做一点更贵。

### D11 错误通道必须搬进函数表，且每次调用前清零

**问题（实测）**：D8 要求"表面方法失败即抛、异常带 `Code` 与 `LastErrorText`"，但托管侧**根本读不到这两个值**：

- `LastErrorCode` / `LastErrorText` 是模块里的独立 `extern "C"` 函数（`IWindow.Bindings.cpp` 声明在第 9–10 行、定义在第 364–377 行），**不在函数表里**；
- `CSharpHost.dll` 的导出表实测只有 `TryInit` 与 `TryRelease`（`dumpbin /exports` → `2 number of functions`），所以托管侧连符号都拿不到；
- 生成的 `.cs` 里 `LastErrorCode` / `LastErrorText` 是 `public static` 字段，全仓 `git grep "LastErrorCode\s*="` **零命中** —— 从没被赋值，调用就是空指针；
- 错误码是**每接口一份 static 全局**、`LastErrorCode(void* self)` 直接丢弃 `self`；方法 thunk 既不置错也不清零（产物里唯一的清零点在 `releaseHandle` 尾部）。所以一次 `ResolveHandle` 失败写下 1001 之后，紧接着一次完全成功的调用在表面看来也是"错误码 1001"，D8 就会误抛。

**改法**：`IWindowApi_v2` 的表头在 `abi_version` / `size` 之后加两个槽 —— `int32_t (*last_error_code)(void* self)` 与 `void (*last_error_text)(void* self, char* dst, uint64_t cap)`，并且：

1. **每个方法 thunk 入口先清一次错误码**（`<接口>_lastErrorCode = 0;`），保证读到的只能是"这次调用写的"；
2. **错误码按实例分槽**（与 D10 一起落）：key 是 `(接口, 实例)`，不是接口级的 static 全局；
3. 版本升到 2，两侧的 `GetApi` 校验（version + size 双相等）自然拦住旧产物。

拒绝理由：只给那两个函数加 `__declspec(dllexport)` 保持现状 —— 跨模块导出的仍是单实例 static 全局，D10 一落地就串台，而且"上次残留"的问题一个都解决不了。

### D12 表面对象的 `Dispose` 必须用"发行者"的 release

**问题（实测）**：句柄表是**每接口一份独立的 static 表**（`IWindow_handles` 与 `IWindowSystem_handles`），两张表的索引都从 1 开始、代际初值都是 0，所以**头几个句柄的数值完全相同**。而 `lookupHandle` 只比对「索引 + 代际」，不认"这个句柄是哪张表发的"。于是把 A 表的句柄交给 B 表的 `releaseHandle`，会在 B 表里命中同号活项、把它清掉并让代际进位 —— **销毁的是一个无关对象**。5.2/5.6 要求每个表面类各自 `Dispose` 调 `ReleaseHandle`，正好踩这个坑。

**改法（二选一，实现时选定并写进本条）**：

- **甲**：表面对象额外持有"发行者"的两个函数指针（`resolve` 与 `release`），由 `NewWindow` / `GetWindowSystem` 的 thunk 回填；`Dispose` 用发行者的那一对；
- **乙**：两张表合成**一张进程级表**，表项带接口类型标签，`release` 时校验标签。

配套：借用句柄（D9）**没有存活检测**（`lookupHandle` 对非弱句柄直接返回指针），宿主销毁后旧借用句柄会解出悬垂指针 —— 必须补一条判据（借用句柄额外存一个弱引用见证者，或规定宿主销毁时必须由宿主注销该句柄）。

拒绝理由：靠"约定两个接口的句柄不要混用"——没有判据能守住约定，而这里踩错的后果是静默销毁别人的对象。
