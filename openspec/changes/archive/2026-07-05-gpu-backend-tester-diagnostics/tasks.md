# Tasks: GPU 测试器崩溃诊断与结构化输出

**Change ID**: gpu-backend-tester-diagnostics
**Created**: 2026-07-05

---

## 1. MiniDump 集成与文本堆栈

- [x] 1.1 复制 `Test/VulkanRendererBackendTester/private/MiniDump.{h,cpp}` 到 `Test/GPUBackendTester/private/`
- [x] 1.2 修改 `MiniDump.cpp`：在 `ApplicationCrashHandler` 中 `MiniDumpWriteDump` 之前，添加 `CaptureStackBackTrace` + `GetModuleHandleEx` + `GetModuleFileName` 文本调用栈输出（16 帧），包含异常类型、异常地址、序号和模块名。用 `WriteFile(GetStdHandle(STD_ERROR_HANDLE), ...)` 输出到 stderr（**不用 fprintf**，避免 CRT FILE* 锁死锁）。**不用 psapi.lib**——`GetModuleHandleEx` + `GetModuleFileName` 均为 kernel32 API
- [x] 1.3 修改 `MiniDump.cpp`：在 crash handler 入口处添加 `static volatile bool s_InCrashHandler = true` 递归保护，若递归则返回 `EXCEPTION_CONTINUE_SEARCH`
- [x] 1.4 修改 `MiniDump.cpp`：将原代码的 `FatalAppExit(-1, szMsg)` 替换为 `TerminateProcess(GetCurrentProcess(), 1)`，避免 headless/CI 模式下弹消息框永久阻塞
- [x] 1.5 修改 `MiniDump.h`：保留现有 `#pragma comment(lib, "dbghelp.lib")`，无需新增 psapi.lib（改用 kernel32 API 后不需要）
- [x] 1.6 修改 `Test/GPUBackendTester/CMakeLists.txt`：新增 `PRIVATE` 源文件 `private/MiniDump.cpp`（需改为显式源文件列表），确保 `dbghelp.lib` 链接
- [x] 1.7 修改 `Test/GPUBackendTester/Main.cpp`：在 `mi_version()` 之前，第一行调用 `std::ios::sync_with_stdio(false)`；`mi_version()` 之后立即调用 `MiniDump::EnableAutoDump(true)`
- [x] 1.8 修改 `Test/GPUBackendTester/Main.cpp`：新增 `#include "private/MiniDump.h"`

## 2. JSON 测试结果报告

- [x] 2.1 在 Main.cpp main 中添加 `--report <path>` 参数解析（含越界检查），存储为 `castl::string reportPath`
- [x] 2.2 实现 JSON 写入函数：测试结束后，根据 try/catch 结果追加一行 JSON 到报告文件。JSON 包含字段：`name`、`status`（pass/fail/crash）、`duration_ms`、`exit_code`、`error`（可选）。`duration_ms` 用 `std::chrono::steady_clock` 在测试前后计时
- [x] 2.3 在 `--test` 单测分发和"运行全部"循环的 try/catch 块中，插入 JSON 写入调用
- [x] 2.4 Headless 模式下未指定 `--report` 时，默认输出到 `test_output/result.json`
- [x] 2.5 输出 JSON 数组格式：开头的 `[` 在首次写入时输出，每个结果一行逗号分隔 JSON 对象，最后一行无逗号，关闭 `]`

## 3. Application 日志分离

- [x] 3.1 在 Main.cpp 中添加 `createDirectoryIfNeeded("test_output")` 辅助函数（使用 Windows `CreateDirectoryA`）
- [x] 3.2 Headless 模式下，每个测试开始前 `freopen` **仅 stdout** 到 `test_output/<TestName>.log`。**不重定向 stderr**（保留给 crash handler 输出到控制台）
- [x] 3.3 每个测试结束后 `freopen("CON", "w", stdout)` 恢复 stdout
- [x] 3.4 确保 `std::ios::sync_with_stdio(false)` 在 `main()` 第一行调用（`mi_version()` 之前）

## 4. Vulkan Validation 日志分离

- [x] 4.1 在 `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` 中定义 `static FILE* g_ValidationLogFile = nullptr` 和 setter 函数 `SetValidationLogFile(FILE*)`
- [x] 4.2 在 `VulkanRenderBackendNew/private/RenderBackend_Vulkan.h` 中声明 `void SetValidationLogFile(FILE*)`
- [x] 4.3 修改 `debugUtilsMessengerCallback`：构造完 `messageStream` 后，检查 `g_ValidationLogFile`，若非 null 则 `fprintf` 写入文件（callback 在 Vulkan loader 线程，但测试在 main 线程串行，无并发写入风险）
- [x] 4.4 在 Main.cpp 中通过 `GetProcAddress` 获取 `SetValidationLogFile` 函数指针，每个测试开始前调用设置到 `test_output/<TestName>_validation.log`，测试结束后 `fclose` 并传 null 复位

## 5. SKILL.md 更新

- [x] 5.1 更新 `.claude/skills/build-project/SKILL.md`：添加 cdb 自动化分析指令——`cdb -z crash.dmp -c "!analyze -v; k 50; q"` 命令格式，记录 cdb.exe 路径

## 6. 编译验证与修复

- [x] 6.1 运行 `build.py`，确保 GPUBackendTester 编译成功，若编译失败则分析并修复直到 BUILD SUCCESSFUL
- [x] 6.2 运行 `GPUBackendTester.exe --backend vulkan --headless 10 --report test_output/result.json`，验证日志文件、validation log 和 JSON 报告生成
- [x] 6.3 如测试 crash，使用 cdb 分析生成的 .dmp 文件，输出调用栈和异常信息
