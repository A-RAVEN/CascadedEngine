## Why

托管侧（C#）现在**能**建窗口，但那个窗口活不过一次 Tick：`Host.cs` 里的表面探针建完立刻 `CloseWindow()` + `Dispose()`，是为了验"表面调得通"，不是给人看的。真正的使用者既没有自己的 C# 代码位置，也没有一段"窗口留在屏幕上"的跑法 —— "C# 能操作窗口"这句话因此只有日志证据，没有肉眼证据。

这个 change 交付第一版**用户可写的 C# 脚本**：脚本里建一个窗口并让它留在屏幕上，引擎持续泵帧，屏幕上能看见那个窗口。

## What Changes

- **新增脚本层**：`Scripting/Managed/UserScript.cs`，一个固定入口签名的静态类 ——
  `public static void OnEngineReady(IWindowSystem windows)`。它由 `Host.cs` 在**表面第一次绑定成功之后**调用一次。使用者要写 C# 就改这个文件（重新构建即可生效）。
- **脚本里建一个保持可见的窗口**：`windows.NewWindow(...)` 之后**不关窗、不 Dispose**，窗口由引擎的 `UpdateSystem()` 在"关闭 + 引用计数归 1"时才回收，所以它会一直留在屏幕上。
- **引擎侧跑法**：`GPUBackendTester.exe` 新增 `--show-window-seconds <N>`，在**所选测试全部跑完之后**继续 `Tick()` + `UpdateSystem()` 共 N 秒并**不**关闭脚本建的窗口，然后正常退出。窗口本身不依赖这个参数 —— 脚本一建出来它就活到进程退出；这个参数延长的是**进程的存活时间**，好让人来得及看见与截图。
- **可核证据**：跑该模式时用 `desktop-screenshot` 截一张全屏图，图里必须能看到脚本建的那个窗口（按标题认）。截图与真实输出一起存进本 change 的 `evidence/`。

## Capabilities

### New Capabilities

- `csharp-script`: 引擎里的 C# 脚本层 —— 脚本文件放哪、入口签名是什么、什么时候被调、能被调几次、失败怎么报。

### Modified Capabilities

- 无。`csharp-window-runtime` 里"托管侧表面必须镜像原始 C++ 接口声明""端到端读数必须两侧可比"这两条**不改**，本 change 只是第一次用那个表面去建一个给人看的窗口。

## Non-goals

- **不做热重载**：改完 `UserScript.cs` 要重新构建（与归档 change `2026-10-18-csharp-hostfxr-bootstrap` 的「不做热重载」一致）。
- **不做沙箱、不做多脚本程序集**：`Scripting.Managed` 就一份，脚本入口只有一个。
- **不渲染进这个窗口**：本 change 只要"窗口出现在屏幕上"。要把画面渲进去，得把这个窗口接进 `GPUBackend` 的 swapchain（另开 change）。
- **不动窗口系统本身的回收条件**：`use_count() == 1 && WindowShouldClose()` 这条不改。
