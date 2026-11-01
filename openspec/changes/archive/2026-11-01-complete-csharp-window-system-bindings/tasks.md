## 1. 红态与测试骨架

- [x] 1.1 [P][US2] 在 `Tools/CodeGen/BindableShapes.h` 增加嵌套纯标量记录与固定非零夹具，在 `Test/CoreTests/BindingsNestedLayoutTest.cpp` 新增布局、按值返回、默认比较运算符和非法嵌套成员测试，并在 `Test/CoreTests/SerializerTest.cpp` 注册。另在 `Test/GPUBackendTester/Main.cpp` 的 `TestCSharpWindow` 增加 `GetMonitor` 两侧日志探针骨架。｜**验收**：当前生成器不支持嵌套镜像，构建或新增测试必须实际失败；把失败命令、退出码和决定性输出存入 `evidence/nested-mirror-red.log`。本条只要求红态与测试骨架。
- [x] 1.2 [P][US1][US3] 把 `Tools/CodeGen/BindableShapes.h` 的 `IBindRow13::SetCallback` 改成正向行为夹具，在 `Test/CoreTests/BindingsCallbackTest.cpp` 新增原生回调注册、参数顺序和注销测试并注册；在 `Scripting/Managed/Host.cs` / `UserScript.cs` 增加回调探针骨架。｜**验收**：当前生成器必须因未排除的 `castl::function` 非零退出并点名 `SetCallback`，完整输出存入 `evidence/callback-red.log`；不得通过保留 `CA_BIND_SKIP` 让红态消失。

## 2. 先实现原生回调，解除构建拒绝

- [x] 2.1 [US1] 在 `Tools/CodeGen/CodeGenMain.cpp` 实现 `castl::function<void(...)>` 的原生函数指针、用户令牌、内部注册号与通用注销伴生，更新 `Tools/CodeGen/BindableShapes.expected.txt` 和 `Test/CoreTests/BindingsCallbackTest.cpp`。｜**验收**：`IBindRow13` 原生回调按声明顺序收到非零参数；替换后用旧注册号注销不会误删新回调；空函数指针不会调用。删掉真实桥调用或让注销误删新回调时测试必须变红，证据存入 `evidence/` 后恢复。完成后 1.2 的生成器拒绝必须消失，为后续全量构建扫清顺序冲突。

## 3. 嵌套记录与 GetMonitor

- [x] 3.1 [US2] 在 `Tools/CodeGen/CodeGenMain.cpp` 实现递归纯标量记录发现、镜像、布局清单和按值搬运，允许 defaulted 比较运算符但拒绝其他自定义方法；非法嵌套成员必须从“只跳过布局并退出 0”收紧为非零退出并点名路径。另生成托管启动可读的布局清单入口，在 `Host.Boot` 逐项比对后才允许绑定。｜**验收**：`python build.py --config Debug` 通过，嵌套布局与固定非零读数全过；非法嵌套夹具非零并点名成员路径。临时改嵌套字段宽度且不重新生成时，托管启动自检必须失败并点名字段路径，红态存入 `evidence/` 后恢复。
- [x] 3.2 [US2] 移除 `WindowSystem/header/CAWindow/WindowSystem.h` 的 `GetMonitor` 排除，让公开 C# `MonitorInfo` / `FloatRect` 保留字段名与顺序；生成的 thunk 在原生调用前用 `GetMonitorCount` 校验编号，无效编号走统一错误通道，不得进入原生 clamp。扩展 `Test/GPUBackendTester/FakeWindowSystem.h` 返回固定非零显示器数据，并新增 `Tools/check_window_system_bindings.py` 比较两侧日志。｜**验收**：`TestCSharpWindow` 退出 0，两个矩形与 DPI 全字段一致；负数和不小于数量的编号都抛 `IWindowSystemException`，不得返回另一个显示器。脚本退出 0，临时错配一个字段或放过无效编号时非零，恢复后回 0。

## 4. 回调托管生命周期

- [x] 4.1 [US1][US3] 实现生成的 C# `Action<...>` 表面、`GCHandle` 保活、替换、置空和表面对象释放；`Host` 重绑先 `Dispose` 旧表面对象。新增托管 `Host.Shutdown`，并在 `CSharpHost/private/CSharpHost.cpp` 的 `Shutdown()` 中于 `hostfxr_close` 前加载并调用。公开 setter 仍返回 `void`，注册号与注销伴生只在 `internal` 层。｜**验收**：焦点回调替换后只有新委托执行，置空、重绑与关闭后旧委托不再执行，生成注册表无对应保活项；把注销改成只删原生注册、不释放 `GCHandle` 时测试必须失败，恢复后通过。
- [x] 4.2 [US3] 实现回调 `IWindow*` 的借用表面与返回后失效：回调内可调真实 `IWindow` 方法，返回后撤销借用状态，保存后调用得到明确失效异常。｜**验收**：回调内读到真实窗口身份；保存引用在回调外调用不崩溃且抛预期异常。临时让借用对象离回调仍可用，测试必须失败并保存红态后恢复。
- [x] 4.3 [US3] 实现主线程限制、同回调重入保护和异常错误槽，并把回调错误标识同时加入 `Tools/CodeGen/CodeGenMain.cpp` 的生成期错误表、`Tools/CodeGen/check_bindings.py` 的 `ERROR_CODE_TABLE` 与 `Tools/CodeGen/README.md`。｜**验收**：重入和跨线程用例都有可诊断错误；指定异常文本在下一次 `UpdateSystem` 中原样出现，进程不崩溃且后续正常回调仍执行；`check_bindings.py` 接受新错误标识。让异常穿出桥或只更新一侧错误表时测试必须失败，恢复后通过。

## 5. IWindowSystem 全量接入

- [x] 5.1 [US1][US2] 移除 `WindowSystem/header/CAWindow/WindowSystem.h` 剩余 10 个回调排除并更新方法计数注释；更新唯一 `bindable-audit.md`：删除 `GetMonitor` 的一处“本轮未实现”备注，并把 10 个回调行改成“函数指针 + 用户令牌 + 内部注册号 + 注销伴生 + 回调期间借用 `IWindow`”，删除“首参改成窗口 id / 不许出现 `IWindow*`”旧结论。同步 `CSharpHost/private/CSharpHost.cpp` 的真实函数表大小断言与注释，以及 `Tools/CodeGen/README.md` 的回调行和未覆盖清单。｜**验收**：`IWindowSystem` 的 16 个方法全部进入生成物，构建不再触发旧 9 槽断言；两个检查脚本为 0，其他接口排除不变；审计表不再有旧回调形状，README 的“回调参数本轮未实现”和“还剩三类形状未覆盖”零命中。
- [x] 5.2 [US1] 扩展 `Test/GPUBackendTester/FakeWindowSystem.h` 保存 10 类回调并提供公开确定性触发方法，新增 `TestCSharpWindowCallbacks`；不得包含 `WindowSystem/private/WindowSystem_Impl.h` 或依赖私有 GLFW 路径。用 `Tools/check_window_system_bindings.py` 比对窗口身份和全部标量参数。｜**验收**：新测试退出 0，10 类委托都真实执行且参数一致；临时屏蔽任意一类回调，脚本必须非零并点名该类，红态存入 `evidence/` 后恢复。`GPUBackendTester.exe --list` 与新测试注册一致。
- [x] 5.3 [US1][US2][US3] 更新 `docs/testing.md` 的 CoreTests 文件清单、tester 清单和第三层脚本清单；另清理 `docs/TODO.md` 与 `TODO/005-web-editor.md` 中“回调未实现 / 嵌套镜像只收标量”的旧现状，更新 `Tools/CodeGen/README.md` 和 `docs/TODO.md` 的相关计数。｜**验收**：`docs/testing.md` 的 CoreTests 清单与 `Test/CoreTests/*.cpp` 实际文件逐项一致，tester 清单与 `--list` 一致，第三层包含新检查且路径存在；`docs/TODO.md` 与 `TODO/005-web-editor.md` 的旧表述零命中。不得要求 `docs/testing.md` 本身含有那两句旧表述。

## 6. 编译验证与修复

- [x] 6.1 [US1][US2][US3] 运行 `python build.py --config Debug`，若失败则分析并修复直到 `BUILD SUCCESSFUL`；随后跑 `CoreTests.exe`、两个后端的 `TestCSharpWindow`、`TestCSharpWindowCallbacks` 与 `TestScriptHost`、`python Tools/check_window_system_bindings.py`、`python Tools/CodeGen/check_bindable_audit.py`、`python Tools/CodeGen/check_bindings.py --product-dir out/build/x64-debug/codegen/bindings`、`openspec validate --all` 和 `openspec instructions apply --change complete-csharp-window-system-bindings --json`。｜**验收**：全部退出 0，新增测试均通过，无新增崩溃或验证错误；apply 的 `remaining` 为 0，`tasks.md` 无未勾项。所有红态与恢复证据保留在本 change 的 `evidence/`。
