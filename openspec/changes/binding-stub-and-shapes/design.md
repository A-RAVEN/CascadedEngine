## Context

`csharp-bindings-generator` 归档后，绑定发射器的实际状态与它的自报不一致：

| 产物事实 | 复核命令与读数 |
|---|---|
| `IWindow` 的函数表 22 个槽里 **10 个是桩**（thunk 体里一次都没出现 `object->`，根本没调实现） | 解析每份产物里的 `_thunk_<方法>` 函数体、体内不含 `object->` 即算桩：`IWindow.Bindings.cpp` = 10，全部夹具合计 = 11 |
| 桩在产物里的**文字指纹只覆盖一部分**：`return {};   // 零值（标量…` 只在返回类型非 `void` 时写 | 数该标记：`IWindow` 8 处、全部产物 11 处（`IBindRow01` 2、`IBindRow02` 1、`IWindow` 8）。差出来的 `SetWindowName` / `GetWindowName` 是 `void` 返回的桩体，体内只有一行形状注释 |
| 两段式伴生 `IWindow_GetWindowNameLen` / `IWindow_CopyGetWindowName` 各只有 1 处命中（第 23、24 行的声明），无函数体、不进函数表 | 按符号数命中数 = 1 / 1 |
| 构建会检查"产物落地了没有"，也会把审计脚本传给发射器，但**从不复核签名与桩** | `Select-String -Path "build.py" -Pattern "check_bindings"` 零命中 |

所以这个 change 的第一件事不是补形状，而是**让判据能看见桩**；否则后面每一项"改好了"都不可核。

## Goals / Non-Goals

**Goals:**

- 构建里跑绑定判据，**真实接口的桩数必须为 0**；今天的产物必须报 10 处。
- `IWindow` 的 22 个槽全部走真实现路径（`bool`、枚举、`void*`、字符串入参、两段式）。
- 每一条修法都有能变红的判据，且判据**不能靠一句注释文本**成立。

**Non-Goals:**

- 不动接口签名、不动句柄表、不动 `weak_ptr` 投影、不动托管侧。
- 不实现容器 / 镜像按值返回 / 回调这三类形状（夹具里仍是桩，判据对夹具只打印）。
- 不新增夹具行、不改投影矩阵。

## Decisions

### D1 判据扩进 `Tools/CodeGen/check_bindings.py`，并给它加"产物目录"参数

它已经在做夹具签名的逐行对拍。桩计数与它共用"哪一份产物"这个前提，放在同处最省事。

但它现在**只对夹具符号 `IBindRow\d\d_\w+` 做签名对拍**，`IWindow` 的 22 条签名根本不在比对范围；而且产物目录硬编码成 `out/build/x64-debug/codegen/bindings`，`build.py` 的 `DEFAULT_CONFIG` 却是 `RelWithDebInfo`（产物落 `x64-relWithDebugInfo`）。所以必须先加 `--product-dir`，由 `build.py` 传当前构建目录；目录不存在时按"判据自身失效"退出 2，与"发现桩"的退出 1 区分开 —— 否则全新机器上正确的构建会被判红，逼实现者放松判据。

被否决：新脚本 `check_binding_stubs.py` —— 两个脚本要各自维护一份产物枚举与目录解析，任一份漏改就一侧绿一侧红。

### D2 桩的判定口径：看 thunk 函数体有没有调实现，不看注释文本

对每份 `<接口>.Bindings.cpp`，从 `<接口>Api_v1` 的**成员表**出发，按成员名找到对应的 `_thunk_<方法名>` 函数体。判定口径必须写死到能失败，否则三条路都能骗过它：

1. **先剥注释**（`//` 与 `/* */`）。桩体里本来就有发射器写的注释，不剥的话把那句注释改写成提到 `object->` 就能让 10 个桩一起消失。
2. **匹配 `object->\w+\s*\(`**，不是裸子串 `object->`。`decltype(object->M())`、`sizeof(object->M())`、`(void)object->M();` 都含子串但不等于调了实现。
3. **要求结果被用**：调用的值必须出现在 `return` 里、或被赋给命名变量；投影返回 `void` 的槽允许只调用。
4. **另加一条独立红条件**：投影后的返回类型是 `void`、而实现返回的原始拼写去掉 `const` 后不是 `void` ⇒ 算缺陷。否则 `GetNativeWindowHandle` 那种"调了但把返回值丢掉"的形态永远不算桩，D4 一行不做也能过 2.6。

再加一条**覆盖率下限**：某接口的 thunk 数必须等于该接口**独立清单**里的方法数（清单取自期望文件，不取自生成器）。把方法整段 `CA_BIND_SKIP` 掉、或从发射循环里过滤掉，thunk 与函数表成员会一起消失 —— 桩数照样 0、两侧对齐照样 0 处，整个方法静默不见。

判定分两档：

- **真实接口**（本 change 之后是 `IWindow`）：桩数必须为 **0**。**"哪些算真实接口"由脚本自己算** —— 产物里的接口减去夹具头 `Tools/CodeGen/BindableShapes*.h` 里声明的接口。算出来是空集时按"判据自身失效"退出 2。不让调用方传名单、也不在脚本里抄一份：名单一旦为空，"真实接口桩数为 0"就恒真，而那正是要防的东西。
- **夹具接口**：桩数**只打印、不判定**。夹具里容器、镜像按值返回、回调三类形状本轮不覆盖，把它们算进红灯会让这一轮永远做不完；把它们当成"忽略清单"又会让判据退化成恒绿。打印出来就没人能假装它们不存在。

再加一条**变异用例**证明判据对新形状有效，且必须落在真实接口上（落在夹具头没用 —— 夹具只打印不判定）：把 `IWindow` 某个已修的槽临时改回 `return {};`，判据 MUST 退出 1 并点名 `IWindow::<方法>`；还原后 MUST 恢复。

被否决 A：数注释文本 `零值（标量`。今天 `IWindow` 只有 8 处、全产物 11 处，且对 `void` 返回的桩体恒盲 —— 修完 8 个 `bool` 就归零，两个空体桩还在，判据全绿。
被否决 B：在 `CodeGenMain.cpp` 里断言那个标记字面量存在。把标记改个词、同时在任何注释或诊断串里留一句原文，就能同时满足"字面量存在"与"产物 0 桩"；反过来，形状全接完、`else` 分支被合法删掉时，这条断言还会假红。

**顺带一条投影规则的收口**：`IWindow::GetWindowName` 这种"返回字符串视图"的方法，函数表里那一槽永远不可能是真实现（返回值只能从两个伴生取），所以它 **MUST NOT 进函数表**；而且**直出入口与直出 thunk 也一并不发** —— 只删表槽会让那个空体 thunk 留在产物里、被桩判据算成一个桩（`IWindow` 变 1、夹具变 8），整段 `CA_BIND_SKIP` 又会把刚发的两个伴生声明一起删掉（期望文件会报 6 行不符）。三个都不发才自洽：`IWindowApi_v1` 从 22 个成员变 21 个，期望文件少 2 行（`03|void IBindRow03_GetName`、`04|void IBindRow04_GetNameView`）。桩数因此仍然收敛：`IWindow` 的 10 个桩 = 修 9（8 个 `bool` + `SetWindowName`）+ 删 1（`GetWindowName`）；夹具 11 个 = 修 3（`GetBool` / `GetMode` / `SetName`）+ 删 2（`GetName` / `GetNameView`）⇒ 剩 6。

### D3 `bool` 与枚举返回：把"能直出"的判据从"类型文本相等"换成显式集合

现在的条件是 `bareType(method.returnSpelling) == returnType`，它想拦的是"镜像结构体返回会加 `C` 后缀"，副作用是把 `bool`（投影成 `int32_t`）与枚举（同样投成 `int32_t`）也拦了。改成：返回值是 `void`、是标量、是枚举、或是指针时走直出；只有"要加 `C` 后缀的镜像类型"不直出。

两种特例的 thunk 形式必须分开写，否则编不过：

- `bool` → `return object->M() ? 1 : 0;`
- 枚举（`enum class`，不能隐式转 `int32_t`）→ `return static_cast<int32_t>(object->M());`

被否决：只给 `bool` 加一个特例 —— 枚举这一条会立刻踩同一个坑（`IBindRow01::GetMode` 的返回类型是 `enum class EBindMode : uint8_t`，直出成 `return object->GetMode();` 编不过，而这份产物被 `BindingsGetApiTest.cpp` 直接 `#include`，红的是整个 CoreTests）。

### D4 `void*` 返回映射成 `uint64_t`

`bareType` 会把 `void*` 剥成 `void`，于是 `GetNativeWindowHandle` 的返回值被丢掉。改成投影 `uint64_t` 不透明令牌，thunk 里 `return reinterpret_cast<uint64_t>(object->M());`，C# 侧 `ulong`。

**只限 `void*` 这一种拼写**，不是"所有指针返回"。别的 `T*` 返回（例如 `IShaderCompiler*`）在审计表里要求走句柄表 + 配对释放；一律转成裸令牌等于把所有权不明的指针静默绑成裸地址，违反 `csharp-bindings` 现有那条「② 类必须 `CA_BIND_SKIP`」。所以条件写成"去掉 `const` 后正好是 `void *`"，其它指针返回仍走拒绝并要求写理由。

（审计表对 `IWindowSystem::GetSystemNativeHandle` 已有同一规定："`void*` → `uint64` 不透明令牌"，所以这个口径与既有审计结论一致。）

### D5 字符串：入参造临时量；两段式的两个伴生各自独立取一次

- 入参：thunk 里用 `castl::string_view(name, nameLen)` 造临时量传给实现；实现要 `castl::string` 的，由临时量再构造一个 `castl::string`。
- 返回：`_GetXxxLen(self)` 返回 `object->M().size()`，`_CopyXxx(self, dst, cap)` **自己再取一次** `object->M()`、拷 `min(cap - 1, size)` 字节并补 NUL。两个伴生互不共享状态，所以 `string_view` 与 `castl::string` 按值返回都能成立 —— 不存在"上一次取到的临时量已经析构"的问题。
- **`cap == 0` 必须先判**：`cap` 是无符号的，`cap - 1` 在 0 时下溢成最大值，`min` 会取到 `size`，于是往 0 容量缓冲写 `size` 字节再补 NUL —— 越界写。生成物里既有的同类实现就是这么防的（`(cap > 0u) ? ((length < cap - 1u) ? length : cap - 1u) : 0u`），本轮的两段式必须照抄这个守卫。
- **两个伴生之间必须可对齐**：行为断言写成三方等式 —— `Len` 的值 == 设进去的名字长度 == `Copy` 写入的字节数。只断言"`Len` 非 0、`Copy` 得到同名"会被"`Len` 算错 / `Copy` 只写一半"的实现骗过，调用方按 `Len` 分配缓冲就拿到未初始化内存。
- 直出入口与直出 thunk 都不发（见 D2 末尾那段收口）。
- `Copy` 保持返回 `void`，长度由 `Len` 提供；不改成返回长度（`BindableShapes.expected.txt` 里逐字期望 `void`，改签名要连带改期望文件与矩阵）。

被否决：用静态暂存保存返回值 —— 会在句柄表之外引入第二处"跨调用状态"，且没有明确的失效时机（句柄表本身是另一个 change 的事）。

被否决：只支持 `string_view` 返回、`castl::string` 按值返回在生成期拒绝 —— 矩阵第 3 行（字符串入参与返回 `castl::string`）与第 4 行（`castl::string_view` 返回 → 两段式）合起来已经把两者都收进去了，`IBindRow03` 的产物与期望签名（`GetName` + `GetNameLen` + `CopyGetName`）也已经按这个口径存在，改成拒绝会让期望文件与产物同时不符。而上一段的实现方式让这个拒绝没有必要。

## Risks / Trade-offs

- **夹具里本轮只修掉一部分桩**：按结构口径今天的夹具产物共 **11** 个桩 —— `IBindRow01` 的 `GetBool`、`GetMode`；`IBindRow02` 的 `GetThing`（镜像按值返回）；`IBindRow03` 的 `SetName`、`GetName`（字符串）；`IBindRow04` 的 `GetNameView`（字符串视图返回）；`IBindRow05::SetThings`、`IBindRow06::GetThings`、`IBindRow07::SetNested`、`IBindRow08::SetMapping`（容器）；`IBindRow13::SetCallback`（回调）。本 change **修掉 3 个**（`GetBool`、`GetMode`、`SetName`）、**删掉 2 个**（`GetName`、`GetNameView` 的直出入口连 thunk 一起去掉），剩 **6** 个属于三类未覆盖形状。判据对夹具只打印不判定，靠 `docs/TODO.md` 记着；风险是没人真去看那行打印。缓解：`build.py` 转发判据 stdout、把夹具桩数打进构建日志（任务 2.8），并在 `Tools/CodeGen/README.md` 第十四节写明这三类形状未覆盖。
- **判据与被判物脱钩** → 结构判据不依赖注释文本，但依赖"thunk 里一定有 `object->\w+(`"这个前提。若将来某个形状需要调同文件的 helper（helper 里才调实现），要把 helper 的调用也算进判据；`(*static_cast<T*>(self)).M()` 这种等价写法也算。缓解：D2 里那两条变异用例（注释里加 `object->` 必须仍判桩、`(void)object->M();` 必须仍判桩）。
- **两段式每次重取一次实现** → 对"每次返回新对象"的实现意味着取长度与拷出可能拿到不同内容。`IWindow::GetWindowName` 是视图（指向窗口自己持有的字符串），不受影响；若将来有方法返回按值拼接出来的新字符串，需要另定口径。记进 TODO。
- **`IWindowApi_v1` 22→21 会让托管侧那个手抄常量失效** → `Scripting/Managed/Host.cs:135` 写着 `WindowApiSize = 8 + 8 * 22`（184），引擎回填的 `realSize` 变 176 之后，托管侧取表会打印"函数表大小不符"并直接 `return`，`GetWindowSize` 探针不再执行。本 change 按 Non-goals 不动托管侧，所以把这条写成已知后果（任务 4.1）并交给后继 change 修。

## Migration Plan

三步，每步单独构建通过、可单独回滚（撤销该步的提交）：

1. 判据：`check_bindings.py` 加 `--product-dir` + 结构桩判据 + 两档判定 + 变异用例。**此时脚本必须红：`IWindow` 报 10 个桩。** 先把判据做出来、**暂不接进构建** —— 接早了会让下一步期间的构建一直红。
2. 形状：D3 / D4 / D5，把 `IWindow` 的桩清零，`bool` 与枚举两条特例分开写；清零之后再把判据接进 `build.py`（传当前构建目录、失败传到构建）。
3. 收尾：README / `docs/testing.md` / `docs/TODO.md` 同步，跑构建与全部测试。

## Open Questions

- 夹具里那 6 个未覆盖的桩要不要也在这一轮做掉？倾向不做：容器投影涉及"谁分配 + 数量怎么回填"，与"C# 能调 `IWindow`"没有关系，硬塞进来会让这个 change 变成两件事。
