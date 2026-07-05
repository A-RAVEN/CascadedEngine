# Tasks: GPU 后端测试器统一化与命令行驱动

**Change ID**: gpu-backend-tester
**Created**: 2026-07-05

---

## 1. 目录重命名与 CMake

- [x] 1.1 将 `Test/D3D12RenderBackendTester` 目录重命名为 `Test/GPUBackendTester`
- [x] 1.2 更新 `Test/GPUBackendTester/CMakeLists.txt` line 1：`PROJECT_NAME` 改为 `GPUBackendTester`
- [x] 1.3 更新根 `CMakeLists.txt` line 77：`add_subdirectory` 路径改为 `Test/GPUBackendTester`

## 2. TestContext 与全局变量重构

- [x] 2.1 定义 `TestContext` 结构体，包含 `pModuleManager`、`pThreadManager`、`pGPUBackend`、`pWindowSystem`、`pIMGUIContext`、`pResourceManagingSystem`、`pImportingSystem`、`editorConfigPath`、`assetPath`、`resourcePath`、`headlessFrames`、`headlessTimeout`
- [x] 2.2 移除所有文件级全局变量（`g_ModuleManager`、`g_ThreadManager`、`g_GPUBackend`、`g_WindowSystem`、`g_IMGUIContext`），相关逻辑移入 main 的 TestContext 初始化
- [x] 2.3 将所有 7 个测试函数签名改为 `void(TestContext& ctx)`，函数体内 `g_` 前缀引用改为 `ctx.` 成员访问
- [x] 2.4 `TestIMGUI` 移除 `editorConfigsPath` 参数，改用 `ctx.editorConfigPath`
- [x] 2.5 将文件级路径变量（`rootPathFS`、`rootPath`、`resourceString`）移入 main 并填充到 TestContext 的路径字段
- [x] 2.6 适配 `TestTriangleWithImageBuffer` (line ~281) 和 `TestDoublePass` (line ~397) 中的路径拼接：从 `std::filesystem::path::operator/` 改为 `castl::string` 字符串拼接（如 `ctx.resourcePath + "/Images/test.png"`）

## 3. 命令行参数解析

- [x] 3.1 手写 `argc/argv` 循环 + `strcmp` 精确匹配，解析 `--backend`、`--test`、`--list`、`--headless`、`--headless-timeout`、`--help`。每次读取参数值前检查 `i + 1 < argc`，越界则输出错误并 exit(1)
- [x] 3.2 实现 `--backend`：`"d3d12"` → `CA_ADD_MODULE(D3D12RenderBackend)`，`"vulkan"` → `CA_ADD_MODULE(VulkanRenderBackend)`，其他值报错 exit(1)
- [x] 3.3 实现 `--list`：打印硬编码的 7 个测试名列表到 stdout 后退出，不初始化后端
- [x] 3.4 实现 `--test`：用 `if/else if` 链精确匹配测试名并调用对应函数，未找到报错；未指定则顺序调用全部 7 个
- [x] 3.5 实现 `--help`：打印参数说明后退出
- [x] 3.6 在 `GetInstance<CRenderBackend>()` 和 `GetInstance<IWindowSystem>()` 后判空，若空输出错误并 exit(1)
- [x] 3.7 数值参数合法性校验：`--headless` 的 `atoi` 结果 <= 0 时报错（headless 模式需要正整数帧数）；`--headless-timeout` <= 0 时报错

## 4. Headless 模式

- [x] 4.1 在每个测试函数的渲染循环处添加分支：`if (ctx.headlessFrames > 0)` 用 `for (int i = 0; i < ctx.headlessFrames; ++i)` 替代 `while (!WindowShouldClose())`，循环体保持不变
- [x] 4.2 Headless 模式下在 main 中设置 `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`（仅此一个变量）
- [x] 4.3 实现 `--headless-timeout`：`std::thread` 看门狗 sleep 超时秒数后设置 `std::atomic<bool>` 标志；启动后立即 `detach()` 避免析构时 `std::terminate()`；测试帧循环中检查该标志
- [x] 4.4 在 `--test` 分发和"运行全部"循环中，用 `try/catch` 包裹每个测试调用，捕获 `std::exception` 输出错误信息后继续下一测试

## 5. 遗留引用更新

- [x] 5.1 更新 `.claude/skills/build-project/SKILL.md` line 49：exe 名从 `D3D12RendererBackendTester.exe` 改为 `GPUBackendTester.exe`

## 6. 编译验证与修复

- [x] 6.1 运行 `build.py`，确保 GPUBackendTester 编译成功，若编译失败则分析并修复直到 BUILD SUCCESSFUL
- [x] 6.2 运行 `GPUBackendTester.exe --list` 验证列表输出正确
- [x] 6.3 运行 `GPUBackendTester.exe --backend vulkan --headless 10` 验证 headless 模式可正常执行并退出
