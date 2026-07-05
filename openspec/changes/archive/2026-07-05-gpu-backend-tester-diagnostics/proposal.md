# Proposal: GPU 测试器崩溃诊断与结构化输出

**Change ID**: gpu-backend-tester-diagnostics
**Status**: Proposed
**Created**: 2026-07-05

---

## Why

`GPUBackendTester` 已可命令行驱动运行 Vulkan 后端测试，但实际运行中发生 SIGSEGV 时，输出仅有 "Segmentation fault"，无法定位崩溃位置和原因。同时 Vulkan validation layer 输出与测试日志混在 stdout，难以按测试分离排查。Vulkan 后端处于开发阶段，崩溃频繁，亟需可自动化分析的诊断能力。

## What Changes

- **崩溃诊断**：集成已有 `MiniDump` 类（从 `VulkanRendererBackendTester/private/` 搬入），捕获未处理异常时生成 `.dmp` 文件；额外打印文本调用栈到 stderr，包含异常地址和模块名
- **测试结果 JSON 输出**：`--report <path>` 参数，每个测试结束后输出通过/失败/崩溃/超时状态，写入结构化 JSON 文件
- **Validation 日志分离**：每个测试运行时将 Vulkan debug messenger 输出重定向到 `test_output/<TestName>_validation.log`
- **应用程序日志分离**：headless 模式下将 CA_LOG 输出写入 `test_output/<TestName>.log`

## Capabilities

### New Capabilities

- `crash-diagnostics`: 崩溃时生成 MiniDump 文件 + 文本调用栈输出到 stderr，支持 cdb 自动化分析
- `test-output`: 结构化测试结果输出——JSON 状态报告、按测试分离的 application 日志和 Vulkan validation 日志

### Modified Capabilities

无。

## Impact

- **Test/GPUBackendTester/private/MiniDump.{h,cpp}** — 从 `Test/VulkanRendererBackendTester/private/` 复制并扩展：增加文本堆栈输出（`WriteFile` 替代 `fprintf`）、移除 `FatalAppExit`（替换为 `TerminateProcess`）
- **Test/GPUBackendTester/CMakeLists.txt** — 新增 `dbghelp.lib` 链接，改为显式源文件列表
- **Test/GPUBackendTester/Main.cpp** — main 第一行 `sync_with_stdio(false)`；`mi_version()` 后调用 `MiniDump::EnableAutoDump(true)`；新增 `--report` 参数解析；新增 stdout 日志重定向（stderr 保持指向控制台）；通过 `GetProcAddress` 调用 Vulkan validation log setter
- **VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp** — 新增 `g_ValidationLogFile` 全局变量和 `SetValidationLogFile` setter 函数；修改 debug messenger callback 支持同时写入文件
- **VulkanRenderBackendNew/private/RenderBackend_Vulkan.h** — 声明 `SetValidationLogFile`

## Non-goals

- 不修改现有 7 个测试函数的渲染逻辑
- 不添加测试进程隔离（subprocess per test）
- 不添加截图/像素对比功能
- 不添加 Python 包装脚本
- 不添加符号解析（需要 `SymInitialize` + PDB 路径配置，留在后续）
- 不生成 HTML 报告，JSON 仅包含原始状态数据
- 不修改 `Test/VulkanRendererBackendTester/private/MiniDump.{h,cpp}`（复制到 GPUBackendTester 后在副本上扩展）
