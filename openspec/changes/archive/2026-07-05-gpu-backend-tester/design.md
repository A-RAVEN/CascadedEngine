# Design: GPU 后端测试器统一化与命令行驱动

**Change ID**: gpu-backend-tester
**Created**: 2026-07-05

---

## Context

`Test/D3D12RendererBackendTester/Main.cpp`（696 行，PROJECT_NAME 为 `D3D12RendererBackendTester`）包含 7 个测试函数，实际已加载 Vulkan 后端（line 652 `CA_ADD_MODULE(g_ModuleManager, VulkanRenderBackend)`，D3D12 行被注释）。所有测试共享 5 个文件级全局变量（`g_ModuleManager`、`g_ThreadManager`、`g_GPUBackend`、`g_WindowSystem`、`g_IMGUIContext`）和 3 个文件级路径变量（`rootPathFS`、`rootPath`、`resourceString`）。当前只有 `TestIMGUI` 在 main 中被调用，其余 6 个测试被注释。

**关键约束**：
- 所有测试函数内部创建窗口并通过 `while (!WindowShouldClose())` 无限循环
- 测试函数签名不统一：6 个 `void()`，`TestIMGUI` 为 `void(castl::string const&)`
- `CA_ADD_MODULE` 宏在非静态构建时走运行时 DLL 动态加载路径，支持按需选择后端

## Goals / Non-Goals

**Goals:**
- 重命名为 `GPUBackendTester`（目录 + CMake 项目名）
- 手写参数解析，支持 `--backend`、`--test`、`--list`、`--headless`、`--headless-timeout`、`--help`
- `--test` 用简单 `if/else` 字符串精确匹配，不引入注册框架
- 全局变量重构为 `TestContext` 结构体
- 每个测试的渲染循环增加 headless 帧数分支
- Headless 模式启用 Vulkan validation layer

**Non-Goals:**
- 不添加测试注册宏/注册表
- 不添加截图
- 不从 VulkanRendererBackendTester 搬测试
- 不修改现有测试函数的渲染逻辑

## Decisions

### D1: 目录与 CMake 重命名

**选择**: `Test/D3D12RenderBackendTester` → `Test/GPUBackendTester`，PROJECT_NAME 改为 `GPUBackendTester`

**注意**: 现有 PROJECT_NAME 是 `D3D12RendererBackendTester`（多一个 "er"），与目录名 `D3D12RenderBackendTester` 不一致。本次改名统一消除这个拼写差异。

**影响范围**: 根 CMakeLists.txt line 77 的 `add_subdirectory` 路径、tester 自身的 CMakeLists.txt line 1 的 PROJECT_NAME。

### D2: 命令行参数解析

**选择**: 手写 `argc/argv` 循环 + `strcmp` 精确匹配。

**原理**: 参数仅 6 个，不引入第三方库。使用精确匹配（非 `strncmp` 前缀匹配）避免 `--headless` 与 `--headless-timeout` 的歧义。每次读取参数值前 SHALL 检查 `i + 1 < argc`，若越界则输出错误并 exit(1)。C 标准规定 `argv[argc] == NULL`，`strcmp`/`atoi` 对 NULL 是未定义行为。

| 参数 | 值类型 | 默认值 | 行为 |
|------|--------|--------|------|
| `--backend` | string | `vulkan` | 选择 VulkanRenderBackend 或 D3D12RenderBackend |
| `--test` | string | 无（运行全部） | 运行指定测试，未找到则报错退出 |
| `--list` | flag | — | 打印硬编码的测试名列表后退出 |
| `--headless` | uint | `0` | >0 时启用帧数循环，跑 N 帧退出 |
| `--headless-timeout` | uint | `60` | headless 模式超时秒数 |
| `--help` | flag | — | 打印帮助信息后退出 |

### D3: --test 匹配（不用注册框架）

**选择**: 在 main 中用 `if/else if` 链精确匹配参数值，直接调用对应函数。`--list` 打印硬编码的测试名列表。

```cpp
if (testName == "TestSimpleTriangle") {
    TestSimpleTriangle(ctx);
} else if (testName == "TestTriangleWithConstantColor") {
    TestTriangleWithConstantColor(ctx);
} else if (...) {
    ...
} else {
    CA_LOG_ERR("Unknown test: %s", testName.c_str());
    return 1;
}
```

**原理**: 7 个测试全在一个 .cpp 文件里，注册宏是过度设计。新增测试需在 3 处同步更新：`--list` 硬编码名称列表 + `--test` 的 `if/else` 链 + "运行全部"的顺序调用链。三者在代码中物理相邻，审阅时易于发现遗漏，但无编译器保障。

**备选方案**: `REGISTER_GPU_TEST` 宏 + 全局注册表 —— 被拒绝，对单文件 7 个测试杀鸡用牛刀，还引入静态初始化顺序复杂性。

### D4: TestContext 结构体

**选择**: 将所有文件级全局变量和路径提取到 `TestContext`，统一测试函数签名为 `void(TestContext&)`。

```cpp
struct TestContext {
    cacore::CAModuleManager moduleManager;
    cacore::IModuleManager* pModuleManager = nullptr;
    CThreadManager* pThreadManager = nullptr;
    CRenderBackend* pGPUBackend = nullptr;
    IWindowSystem* pWindowSystem = nullptr;
    imgui_display::IMGUIContext* pIMGUIContext = nullptr;
    ResourceManagingSystem* pResourceManagingSystem = nullptr;
    ResourceImportingSystem* pImportingSystem = nullptr;
    castl::string editorConfigPath;
    castl::string assetPath;
    castl::string resourcePath;
    int headlessFrames = 0;
    int headlessTimeout = 60;
};
```

**路径变量迁移**: 当前文件级 `std::filesystem::path resourceString` 在 `TestTriangleWithImageBuffer` 和 `TestDoublePass` 中用于路径拼接（`resourceString / "Images/test.png"`）。迁移到 `TestContext` 时改用 `castl::string`，路径拼接改为字符串操作。

### D5: 后端切换

**选择**: 用 `if/else` 根据 `--backend` 参数值选择 `CA_ADD_MODULE` 的模块名。

```cpp
if (backendName == "d3d12") {
    CA_ADD_MODULE(g_ModuleManager, D3D12RenderBackend);
} else if (backendName == "vulkan") {
    CA_ADD_MODULE(g_ModuleManager, VulkanRenderBackend);
} else {
    CA_LOG_ERR("Unknown backend: %s", backendName.c_str());
    return 1;
}
```

### D6: Headless 模式

**选择**: 在每个测试函数的渲染循环中，用 `if (ctx.headlessFrames > 0)` 分支替换循环条件。循环体保持不变，仅条件从 `while (!WindowShouldClose())` 改为 `for (int i = 0; i < ctx.headlessFrames; ++i)`。窗口仍然创建（需 WSI surface）。

7 个测试的循环结构一致，改动模式相同。`TestTriangleWithConstantColor` 的动画时间在零帧间隔下不推进，但不影响正确性。

### D7: Vulkan Validation Layer

**选择**: Headless 模式下，在创建 Vulkan 实例前设置环境变量 `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`。仅此一个变量，Vulkan Loader 规范中不存在 `VK_LAYER_ENABLES`，不设。如果 SDK 未安装导致 layer 不可用，Vulkan Loader 静默忽略，不影响运行。

### D8: --headless-timeout 实现

**选择**: 在 main 函数 headless 分支中，启动一个 `std::thread` 作为看门狗，sleep 超时秒数后将 `std::atomic<bool>` 置为 true。每个测试函数在帧循环中检查该标志，为 true 时退出。看门狗线程在启动后立即 `detach()`，避免 `std::thread` 析构时因线程仍 joinable 而触发 `std::terminate()`。不使用 `std::exit()`（会跳过 GPU 资源释放）。

**注意**: detach 后看门狗线程在进程退出前持续运行，`std::atomic<bool>` 标志和超时秒数需保证在看门狗线程生命周期内有效（放在 static 或 main 栈帧顶层）。

## Risks / Trade-offs

- **[改名后遗留引用]**: `.claude/skills/build-project/SKILL.md` 硬编码了旧 exe 名 `D3D12RendererBackendTester.exe`，改名后 skill 报错。
  → 缓解：本次 change 同步更新 SKILL.md 中的 exe 名。

- **[`--headless` 与 `--headless-timeout` 前缀冲突]**: 如果用手写的 `strncmp` 前缀匹配，`--headless` 会误匹配 `--headless-timeout`。
  → 缓解：D2 明确使用 `strcmp` 精确匹配。

- **[全局变量重构后空指针]**: 测试函数通过 TestContext 指针访问原来全局的资源，如果初始化遗漏会崩溃。
  → 缓解：main 在调用测试前统一初始化所有字段，并在调用前 assert 关键指针非空。

- **[D3D12 后端可能链接失败]**: D3D12 后端长期未编译，`--backend d3d12` 可能暂时无法编译或运行。
  → 缓解：Non-goal，默认 `--backend vulkan`。

- **[Backend 初始化失败]**: `GetInstance<CRenderBackend>()` 返回 null 时后续解引用崩溃。
  → 缓解：main 中获取指针后立即判空，失败则输出错误并 exit(1)。

- **[测试崩溃无隔离]**: 运行多个测试（`--test` 未指定或指定多个时），一个测试 crash 整个进程终止，后续测试不执行。
  → 缓解：在 `--test` 分发和"运行全部"循环中用 `try/catch` 包裹每个测试调用；crash（SIGSEGV）无法被 catch 但会被 MiniDump 捕获。本文档记录此限制。

- **[stbi_load 空指针解引用（已有 bug）]**: `TestTriangleWithImageBuffer` (line 283) 和 `TestDoublePass` (line 398) 中 `stbi_load` 返回 null 时直接传给 `ScheduleData`。这是已有代码的 bug，本次 change 不修复（Non-goal："不修改现有测试函数的渲染逻辑"），但 headless 模式跑更多测试时暴露概率增大。
  → 缓解：在本 change 的 Non-goal 中明确记录。后续 change 修复。

- **[GPU TDR / Device Lost 无检测]**: `TestComputeBuffer` 涉及 compute shader dispatch，若 shader 无限循环触发 TDR，headless 模式下无恢复机制。
  → 缓解：`--headless-timeout` 看门狗会触发退出；但 GPU 状态恢复需后续 change 处理。
