# Proposal: GPU 后端测试器统一化与命令行驱动

**Change ID**: gpu-backend-tester
**Status**: Proposed
**Created**: 2026-07-05

---

## Why

`D3D12RendererBackendTester` 是当前唯一可编译运行的 GPU 测试程序，但存在三个问题：(1) 名称误导——实际已加载 Vulkan 后端，D3D12 行被注释；(2) 测试函数通过源码注释切换，无法从命令行选择运行哪个；(3) 所有测试靠 `while (!WindowShouldClose())` 无限循环，Claude（或 CI）无法驱动。Vulkan 后端经过 `fix-vulkan-backend-render-pipeline` 实质性修改后，急需一个可自动化运行的测试入口。

## What Changes

- **重命名**：目录 `Test/D3D12RenderBackendTester` → `Test/GPUBackendTester`，PROJECT_NAME 同步改名
- **命令行参数**：`--backend <vulkan|d3d12>` 选择后端、`--test <name>` 按名运行、`--list` 列出所有测试、`--headless <N>` 跑 N 帧退出、`--headless-timeout <N>` 超时保护、`--help` 帮助
- **`--test` 匹配**：用简单 `if/else` 字符串匹配选择测试函数，不引入注册框架
- **TestContext 结构体**：将 `g_GPUBackend` / `g_WindowSystem` 等全局变量提取为结构体，传入各测试函数
- **Headless 模式**：在所有 7 个测试的渲染循环中添加帧数计数分支，跑满 N 帧自动退出
- **Vulkan Validation Layer**：headless 模式下自动启用 `VK_INSTANCE_LAYERS`

## Capabilities

### New Capabilities

- `cli-test-selection`: 命令行驱动的测试选择——`--list` 列出、`--test <name>` 指定运行、`--backend` 切换后端、`--headless <N>` 无窗口自动退出
- `test-context`: TestContext 结构体替代文件级全局变量，统一测试函数签名为 `void(TestContext&)`

### Modified Capabilities

无。

## Impact

- **Test/D3D12RenderBackendTester/** → `Test/GPUBackendTester/`：目录重命名
- **Test/GPUBackendTester/CMakeLists.txt**：PROJECT_NAME 改名
- **Test/GPUBackendTester/Main.cpp**：新增参数解析、TestContext、headless 分支；重构 main 函数
- **根 CMakeLists.txt**：`add_subdirectory` 路径更新
- **.claude/skills/build-project/SKILL.md**：exe 名从 `D3D12RendererBackendTester.exe` 改为 `GPUBackendTester.exe`

## Non-goals

- 不添加测试注册宏/注册表框架（`--test` 用简单 if/else 匹配）
- 不添加截图功能
- 不从 VulkanRendererBackendTester 搬测试
- 不修改现有 7 个测试函数的内部渲染逻辑
- 不添加 `test_harness.py` 包装脚本
