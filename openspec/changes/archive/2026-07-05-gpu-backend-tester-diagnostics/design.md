# Design: GPU 测试器崩溃诊断与结构化输出

**Change ID**: gpu-backend-tester-diagnostics
**Created**: 2026-07-05

---

## Context

`GPUBackendTester` 运行测试时 crash 输出仅有 "Segmentation fault"，无法定位崩溃位置。旧版 `VulkanRendererBackendTester` 已有完整的 `MiniDump` 类（`Test/VulkanRendererBackendTester/private/MiniDump.{h,cpp}`），但未在 GPUBackendTester 中集成。

项目已安装 WinDbg（含 `cdb.exe` v10.0.29617），可用于命令行自动化分析 `.dmp` 文件。

**约束**：
- `BuildPipelineStates` 中 GPL 路径已确认正确（`VK_NULL_HANDLE` render pass 符合规范 VUID-06579）
- Vulkan debug messenger callback 当前仅写入 CA_LOG（stdout）
- 进程内 try/catch 无法捕获 SIGSEGV，需 SEH 异常处理器

## Goals / Non-Goals

**Goals:**
- 崩溃时自动生成 MiniDump（`.dmp`）+ 文本调用栈（stderr）
- JSON 测试结果报告（`--report` 参数）
- Headless 模式下按测试分离 application 日志和 Vulkan validation 日志
- 文本调用栈仅需地址和模块名，无需符号解析（留给 cdb）

**Non-Goals:**
- 不添加进程隔离
- 不截图
- 不做符号解析（`SymInitialize`/`SymFromAddr`）
- 不修改现有 7 个测试渲染逻辑
- 不修改原始 MiniDump 文件

## Decisions

### D1: MiniDump 集成方式

**选择**: 复制（非移动）`MiniDump.{h,cpp}` 到 `Test/GPUBackendTester/private/`，扩展增加文本堆栈输出。

```
Test/VulkanRendererBackendTester/private/MiniDump.{h,cpp}  ← 保留不动
Test/GPUBackendTester/private/MiniDump.{h,cpp}              ← 复制 + 扩展
```

**扩展内容**: 在 `ApplicationCrashHandler` 中，先输出文本调用栈，再调用 `MiniDumpWriteDump` 生成 .dmp，最后 exit 进程。**移除对 `FatalAppExit` 的调用**（原代码在 dump 后调用 `FatalAppExit` 弹出消息框，headless/CI 模式下无人点击会永久阻塞）。改用 `TerminateProcess(GetCurrentProcess(), 1)` 或 `_exit(1)` 直接退出。

**原理**: 文本堆栈用 `CaptureStackBackTrace` 抓帧，`GetModuleFileName(NULL, ...)` 取 exe 名，`GetModuleFileNameEx` 取各帧模块名。不依赖 PDB 符号。

**备选方案**: 直接移动 MiniDump 到共享库 → 拒绝，不想改动 VulkanRendererBackendTester。

### D2: 异常处理器中的安全保证

**选择**: 异常处理器使用规则——只用 async-signal-safe 操作，不在处理器内分配堆内存。

具体措施：
- `CaptureStackBackTrace` — Windows API，SEH 安全
- `GetModuleFileName` — 内核 API，安全
- `MiniDumpWriteDump` — dbghelp API，设计用于 crash handler
- 使用 `WriteFile(GetStdHandle(STD_ERROR_HANDLE), ...)` 直接写控制台，**禁止**在 crash handler 中使用 `fprintf` 或任何 CRT FILE* API（MSVC CRT 的 `fprintf` 内部会获取 FILE* 锁，若崩溃线程正持有该锁则死锁）
- 在处理器入口立即设置 `static volatile bool s_InCrashHandler = true`，防止递归

**原理**: 崩溃处理器中堆分配可能死锁（malloc lock）。必须用栈内存 + 静态缓冲区。

### D3: JSON 结果输出时机

**选择**: 每次 `try/catch` 块结束后，追加一行 JSON 到文件（而非等所有测试结束后一次性写入）。使用 `std::ofstream` 的 `app` 模式逐行追加。

**原理**: crash 时不会执行 finally，追加模式确保崩溃前的测试结果不丢失。每条结果一行 JSON，最后用逗号分隔的 JSON 数组格式。

```json
[
  {"name":"TestSimpleTriangle","status":"pass","duration_ms":1234,"exit_code":0},
  {"name":"TestTriangleWithConstantColor","status":"fail","error":"exception message","exit_code":1}
]
```

### D4: 日志重定向实现

**选择**: 使用 `freopen` 重定向 `stdout` 到文件，而非修改 CA_LOG 宏。

每个测试开始前：
```cpp
if (ctx.headlessFrames > 0) {
    freopen(logPath.c_str(), "w", stdout);
}
```

测试结束后 `freopen("CON", ...)` 恢复。

**关键约束**：
- `std::ios::sync_with_stdio(false)` 必须在 `main()` 函数**第一行**（`mi_version()` 之前）调用，否则 C++/C stream 已同步，后续 `freopen` 后 `std::cout` 可能失效
- `stderr` **不做重定向**——保持指向控制台，确保 crash handler 的文本调用栈能输出到控制台而非被埋入日志文件
- CA_LOG 输出仅通过 `stdout` 重定向，crash text 通过 `WriteFile(GetStdHandle(STD_ERROR_HANDLE), ...)` 写到原始 stderr

**原理**: CA_LOG 宏直接写入 stdout，重定向 stdout 无需改动日志系统。保留 stderr 给 crash handler 使用。

**备选方案**: Modifier CA_LOG 接受可选文件参数 → 拒绝，改动面太大。

### D5: Vulkan Validation 输出到文件

**选择**: 在 `RenderBackend_Vulkan` 中定义全局 `FILE* g_ValidationLogFile` 和 setter 函数 `SetValidationLogFile(FILE*)`。由于 GPUBackendTester 通过 `LoadLibrary` 动态加载 VulkanRenderBackend.dll，**裸 `extern` 声明不可跨 DLL 边界解析**。因此 Main.cpp 通过 `GetProcAddress` 调用 setter，或通过 `CRenderBackend` 接口暴露该方法。

**原理**: 改动最小——只改 callback 内部，不影响其他 Vulkan 使用场景。全局指针为 null 时行为与原来完全一致（只写 CA_LOG）。

### D6: 输出目录结构

```
test_output/
├── TestSimpleTriangle.log
├── TestSimpleTriangle_validation.log
├── TestTriangleWithConstantColor.log
├── TestTriangleWithConstantColor_validation.log
├── ...
└── result.json                          (--report 指定时)
```

`test_output/` 目录在 main 函数初始化阶段自动创建（`CreateDirectory`）。

### D7: `--report` 参数设计

**选择**: `--report <filepath>` 接受相对或绝对路径。如果 headless 模式未指定 `--report`，JSON 默认写入 `test_output/result.json`。非 headless 模式下除非显式指定 `--report`，否则不生成 JSON。

**原理**: headless 模式是自动化场景，默认输出 JSON 合理。交互模式默认不污染文件系统。

## Risks / Trade-offs

- **[双份 MiniDump 代码]**: VulkanRendererBackendTester 和 GPUBackendTester 各有一份，且 GPUBackendTester 版本被修改（移除 FatalAppExit）。
  → 缓解：改动量小，后续可统一到共享库。本 change 不解决。

- **[freopen 对多线程的影响]**: CA_LOG 可能在其他线程调用，freopen 改变 stdout 的全局状态。
  → 缓解：测试在 main 线程串行执行，渲染在 GPU 线程。CA_LOG 主要在 main 线程调用。stderr 不做重定向，crash handler 不受影响。风险低。

- **[Validation log 竞争]**: Vulkan validation 回调在 Vulkan loader 线程，GPUBackendTester 设置全局指针在 main 线程。
  → 缓解：全局指针在测试开始前设置、测试结束后清空。测试函数内 Vulkan 调用在测试线程，不存在并发读写指针的问题。

- **[cdb 路径硬编码]**: cdb.exe 路径包含版本号 `Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe`。
  → 缓解：在 build-project SKILL.md 中记录实际路径。后续可通过 `where cdb` 动态发现。

- **[`CaptureStackBackTrace` 帧数有限]**: 默认最多 62 帧。
  → 缓解：对于 Vulkan crash 通常帧数 < 20，16 帧足够。

- **[`freopen` 后 std::cout 失效]**: C++ stream 和 C FILE* 不同步。
  → 缓解：`std::ios::sync_with_stdio(false)` 在 `main()` 第一行调用，确保 C++ stream 独立于 C FILE*。且仅重定向 `stdout`，`std::cerr` 仍可正常输出到控制台。
