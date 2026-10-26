## Context

托管侧已经有完整可用的表面（`public sealed class IWindow` 23 个成员、`IWindowSystem` 5 个成员，都由 `csharp-window-creation` 交付；成员数是从生成的 `out/build/x64-debug/codegen/bindings/` 下两份 `<接口>.Bindings.cs` 数的 —— `IWindow` 在那份文件里 24 个 `public` 成员，去掉 `Dispose` 是 23；`IWindowSystem` 在它自己那份里 6 个，去掉 `Dispose` 是 5）。`Host.cs` 里那段 `ProbeSurfaceWindow()` 会在**引擎关掉自己那个窗口之后的那次 Tick**（`s_engineWindowClosed` 置真之后）建一个 320×240 的窗口、读尺寸、关窗、`Dispose` —— 它证明"表面调得通"，但窗口活不过一次 Tick。

要交付"肉眼可见"，差的不是绑定能力，是**两件工程安排**：

1. 使用者没有"自己的 C# 文件"可写 —— 现在只能改 `Host.cs` 这种引擎胶水，改错了会连 ABI 自检一起弄坏；
2. 没有"让进程活到人看见"的跑法 —— 测试跑完就退出，窗口最多闪一下。

约束（来自已生效的规范与既有实现，都能用命令核）：

- `WindowSystem::UpdateWindows()` 的回收条件是 `use_count() == 1 && WindowShouldClose()`；托管表面只持裸 `Self` + 弱句柄，**不抬高** `use_count()`。所以只要脚本不关窗，窗口就不会被回收 —— **窗口的存活时间等于进程的存活时间**，与 `--show-window-seconds` 无关。那个参数延长的是**进程**。
- `Host.cs` 每次 Tick 都会 `EnsureBound()`：引擎侧的默认实例指针一变就重绑并**新建表面对象**；指针不变时它早退，表面对象不重建。
- 构建里实际产出的可执行文件有三个：`Test/CoreTests`、`Test/GPUBackendTester`、`Tools/CodeGen`（`add_executable` 在 CMakeLists 里共 4 处，`ShaderProcessor/CMakeLists.txt` 那处没被根 `CMakeLists.txt` 的 `add_subdirectory` 纳入）。所以"引擎"= `GPUBackendTester.exe`。
- `TestCSharpWindow` 现在有两条计数断言：`countAfterSurfaceTick` 必须等于 `countBaseline + 1`、`countAfterSurfaceUpdate` 必须等于 `countBaseline`。**在单测跑法（`--test TestCSharpWindow`）下，脚本窗口会让这两条必红**（脚本窗口常驻，基线之后就一直多一个）；在全量跑法下基线本来就含脚本窗口，两条照过 —— 所以本 change 必须先改掉这两条断言，让两种跑法用同一条基线口径。这也是本 change 的第一条代码任务。
- 验收必须能失败：日志里没有脚本建窗那行、或计数没有按新的口径对上，就是红。

## Goals / Non-Goals

**Goals**

- 有一个**用户可写**的 C# 文件，里面调 `IWindowSystem.NewWindow(...)` 建窗口并让它保持可见。
- 有一套可复现的跑法：一条命令行，测试跑完后进程再活 N 秒，屏幕上能看见那个窗口。
- 证据是**屏幕截图**（`desktop-screenshot` 拍的 PNG）+ 同一次运行的日志，一起存进 `evidence/`。

**Non-Goals**

- 不渲染进这个窗口（要接 `GPUBackend` 的 swapchain，另开 change）。
- 不做热重载 / 沙箱 / 多脚本程序集。
- 不改 `WindowSystem` 的回收条件，也不给窗口加"防关闭"标记。

## Decisions

### D1 脚本入口用固定签名的静态方法，而不是接口实现或反射发现

`UserScript.OnEngineReady(IWindowSystem windows)`，一个静态类、一个静态方法。

- 为什么不用接口（`IScript`）：接口会把"脚本生命周期"这个概念提前固化，而本轮只有一个时机点；静态方法的签名本身就是契约，改签名编译期就炸。
- 为什么不用反射发现：反射发现要处理"找不到 / 找到多个 / 签名不对"三种失败态，全是本轮不需要的复杂度。
- 代价：脚本只能有一个入口、一个时机。这是本轮的设计选择，不是外部要求。

### D2 调用时机定在"两张表都绑定成功之后"，而不是 Boot 里，且只调一次

可核条件写成 `s_system != null`（`EnsureBound()` 是 `void`，没有成功/失败语义，而且它有"只绑到一半"的出口：`IWindowSystem` 表取不到时 `s_system` 仍是空）。用一个 `s_userScriptCalled` 标志保证只调一次。

- 为什么不在 `Boot`：`Boot` 时引擎还没登记接口实例（`SetInterfaceInstance` 发生在测试体内），托管侧**取表那一步**（`size` 传具体值）会拿到 0 字节、实例为空，脚本拿到的是空表，建不出窗口。（注意别写成"`GetInterfaceApi` 一律返回 0"：`size` 传 0 的那个探针分支排在实例查找之前，未登记时它仍然返回表大小 —— `IWindow` 是 232 字节；只有当 CSharpHost 模块实例本身还没 `Init` 时，那更前面的一处守卫才会返回 0。）
- 为什么不在每次绑定后都调：`EnsureBound` 在引擎换默认实例时会重绑，若每次都调脚本，脚本会越建越多窗口。**只调一次**是最小惊讶的选择 —— 这是我的设计取舍，不是用户要求；代价写在 Risks 里。
- 为什么用 `s_system != null` 而不是"函数返回之后"：按字面实现会在"编号 1 已登记、编号 2 未登记"时把空引用交给脚本，直接吃空引用异常。
- 这个条件本身有个洞：`s_system` 一旦赋值就再没人清，引擎换默认实例后若重绑失败，旧对象还在，"两表都绑好"会被旧值骗过。所以实现里必须**在每次绑定的开头把 `s_system` 置空**，绑定成功再赋新值（`tasks.md` 2.2 已写成要求）。

### D3 保持可见靠"延长进程存活时间"的命令行参数 `--show-window-seconds <N>`，不给默认值

`GPUBackendTester.exe --backend vulkan --test TestCSharpWindow --report test_output/result_show.json --show-window-seconds 5`：所选测试跑完之后，主循环继续 `Tick()` + `UpdateSystem()` 共 N 秒，然后退出。

- 为什么必须显式传：秒数给默认值就是凭空发明一个数（规则要求数字能溯源）。不传就不进入这段循环，这样"这条判据能失败"成立 —— 不传就没有那几行保持循环日志。
- 为什么是"秒"不是"帧"：肉眼可见是人的感知，用时间表达最直接；帧率受 vsync 影响，用帧数反而不好估。
- 为什么放在 `main` 里、跑完所选测试之后，而不是塞进 `TestCSharpWindow`：塞进单条测试会让全量跑时语义变形（前 9 条测试期间窗口已经存在，却只有最后一条在"保持"）。放在 `main` 里对"单测"和"全量"是同一种语义：跑完 → 保持 N 秒 → 退出。
- **窗口不是这个参数给的**：脚本一建出来窗口就活着，直到进程退出。参数只决定"进程活多久"，所以不带参数时窗口同样会出现，只是来不及看。

### D4 脚本建的窗口不接后端渲染，且这一点写在 proposal 的 Non-goals 里

窗口是 GLFW 建出来的空窗口（会按 `NewWindow` 的 `windowName` 显示标题）。

- 为什么不做渲染：要让 `GPUBackend` 给这个窗口建 swapchain，得把 `IWindow*` 从托管侧交回引擎侧 —— 现在**没有**这条通道（`EngineApi` 是引擎→托管单向，`IScriptHost` 只会被引擎调）。这属于新能力，混进来会让本 change 变成两件事。
- 后果：截图里那个窗口内容是空的。**这仍然是"肉眼可见的窗口"**：窗口边框 + 标题可见，验收就是按标题认它。

### D5 截图是人工核过的单次证据，自动化判据落在日志与计数上

截图由 `desktop-screenshot` 拍，人工确认后存 `evidence/visible-window.png`；自动化判据是日志（脚本建窗那行、保持循环那几行）与计数断言。

- 为什么不让自动化判截图：仓库里现在没有任何截图比对工具（`Tools` 树内 5 个 `check_*.py` —— 根目录 1 个、`Tools/CodeGen/` 4 个 —— 全是文本对拍与退出码判据，没有一支读图像），本轮不做图像判据。把"我看到了"当唯一证据会变成恒真断言，所以截图只作人工证据，不进自动化。

## Risks / Trade-offs

- **脚本只被调一次 ⇒ 重绑之后脚本再也拿不到新绑定，窗口被用户关掉也不会重建** → 写进脚本注释；要"重建"是后续能力，不在本轮。
- **窗口管理器把窗口放到别的虚拟桌面 / 被别的窗口完全遮住** → 截图前用 `--show-window-seconds` 给足时间；截图里认不出就重拍，判据不变（判据是日志 + 计数，不是截图）。
- **既有两条计数断言会被脚本窗口打破** → 本轮第一条代码任务就是按新口径改它们（基线改成"脚本建窗之后"），并在 `tasks.md` 里留下改前/改后原文。
- **保持模式把 CI 卡住** → `--show-window-seconds` 只在显式传参时生效，默认路径不变；全量与单测的跑法都不带它。
- **`NewWindow` 抛异常时脚本失败** → `Host.cs` 的 `Tick` 把异常转成非零返回码 + `LastError`，引擎侧那条"LastError 必须为空"的判定会红，不会静默。**但 `LastError` 不是无损的**：托管侧那份缓冲 511 字节、原生侧 256 字节，长消息会被截断（实测过）—— 所以判据只认"非空 + 短码 + 开头那段"，别指望从它读出完整异常文本。

## Migration Plan

一轮做完，无迁移：

1. 改 `TestCSharpWindow` 的两条计数断言（先做，否则后面全红）。
2. 加 `UserScript.cs` 与 `Host.cs` 的调用点（默认什么都不做，行为不变）。
3. 加 `--show-window-seconds` 与 `main` 里的保持循环。
4. 跑一次带参数的运行，截图 + 日志存 `evidence/`。

回滚：删掉 `UserScript.cs` 与那两处调用、把两条断言改回原样即可，绑定层与表面不受影响。

## Open Questions

- 脚本要不要能拿到 `IWindow` 之外的东西（比如日志）？本轮不给 —— `Host.cs` 已经有一份日志通道，脚本先只拿 `IWindowSystem`。
- 多个脚本的注册顺序？本轮不做多脚本。
