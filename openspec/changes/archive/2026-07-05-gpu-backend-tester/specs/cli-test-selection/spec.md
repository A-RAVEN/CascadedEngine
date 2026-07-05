# CLI Test Selection

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: Backend 选择

系统 SHALL 支持 `--backend <name>` 参数在运行时选择 GPU 后端。`vulkan` 加载 VulkanRenderBackend，`d3d12` 加载 D3D12RenderBackend。未指定时默认 `vulkan`。非法值 SHALL 输出错误并返回 exit code 1。

#### Scenario: 默认 Vulkan

- **WHEN** 未指定 `--backend`
- **THEN** 加载 VulkanRenderBackend 模块

#### Scenario: 指定 D3D12

- **WHEN** `--backend d3d12`
- **THEN** 加载 D3D12RenderBackend 模块

#### Scenario: 非法后端名

- **WHEN** `--backend opengl`
- **THEN** 错误信息输出到 stderr，exit code 1

#### Scenario: 缺少参数值

- **WHEN** `--backend` 作为最后一个参数（无值）或 `--test`/`--headless`/`--headless-timeout` 缺少值
- **THEN** 错误信息输出到 stderr，exit code 1

### Requirement: 参数值合法性校验

系统 SHALL 校验数值参数的有效性。`--headless` 为负数或非数字时 SHALL 视为非法并报错退出。`--headless-timeout` 为零或负数时 SHALL 视为非法并报错退出。

#### Scenario: --headless 非法值

- **WHEN** `GPUBackendTester.exe --headless -1` 或 `--headless abc`
- **THEN** 错误信息输出到 stderr，exit code 1

#### Scenario: --headless-timeout 非法值

- **WHEN** `GPUBackendTester.exe --headless-timeout 0` 或 `--headless-timeout -5`
- **THEN** 错误信息输出到 stderr，exit code 1

### Requirement: Backend 初始化失败处理

系统 SHALL 在获取 `CRenderBackend` 实例后检查指针是否为空。若为空 SHALL 输出错误信息并返回 exit code 1，不进入任何测试。

#### Scenario: 后端 DLL 加载失败

- **WHEN** `GetInstance<CRenderBackend>()` 返回 null
- **THEN** 错误信息输出到 stderr，exit code 1

### Requirement: 测试列举

`--list` 参数 SHALL 打印所有可用测试名称到 stdout 后退出，不初始化 GPU 后端。

#### Scenario: 列出测试

- **WHEN** `GPUBackendTester.exe --list`
- **THEN** 所有测试名输出到 stdout，exit code 0

### Requirement: 按名运行测试

`--test <name>` 参数 SHALL 仅运行指定的测试函数。名称不存在时 SHALL 输出错误并返回 exit code 1。未指定时 SHALL 运行全部测试。

#### Scenario: 运行指定测试

- **WHEN** `GPUBackendTester.exe --test TestSimpleTriangle`
- **THEN** 仅 `TestSimpleTriangle` 被执行

#### Scenario: 运行不存在的测试

- **WHEN** `GPUBackendTester.exe --test NonExistent`
- **THEN** 错误输出到 stderr，exit code 1

#### Scenario: 运行所有测试

- **WHEN** 未指定 `--test`
- **THEN** 按顺序运行全部 7 个测试

### Requirement: Headless 帧数控制

当 `--headless <N>` 指定且 N > 0 时，每个测试 SHALL 在渲染循环中执行 N 帧后自动退出。窗口仍然创建，但循环由帧计数器控制。

#### Scenario: Headless 模式

- **WHEN** `GPUBackendTester.exe --headless 10 --test TestSimpleTriangle`
- **THEN** 创建窗口、渲染 10 帧、自动退出

#### Scenario: 正常模式不受影响

- **WHEN** 未指定 `--headless`
- **THEN** 渲染循环等待用户关闭窗口

### Requirement: Headless 超时保护

`--headless-timeout <N>` 参数 SHALL 设置 headless 模式的最大运行秒数（默认 60）。超时后 SHALL 触发退出。

#### Scenario: 超时退出

- **WHEN** `GPUBackendTester.exe --headless 1000 --headless-timeout 5`
- **THEN** 若 5 秒未完成则触发退出

#### Scenario: 非 headless 模式忽略超时

- **WHEN** `GPUBackendTester.exe --headless-timeout 30`（未指定 `--headless`）
- **THEN** `--headless-timeout` 被忽略，按正常窗口模式运行

### Requirement: Vulkan Validation Layer

Headless 模式下系统 SHALL 设置 `VK_INSTANCE_LAYERS` 环境变量启用 Vulkan validation layer。仅设置 `VK_INSTANCE_LAYERS`，不设置其他非标准变量。

#### Scenario: Headless 启用 validation

- **WHEN** `GPUBackendTester.exe --headless 10 --backend vulkan`
- **THEN** Vulkan 实例创建时加载 validation layer

#### Scenario: 正常模式不启用

- **WHEN** 未指定 `--headless`
- **THEN** 不强制启用 validation layer

### Requirement: 帮助信息

`--help` 参数 SHALL 打印所有支持的参数及说明后退出。

#### Scenario: 显示帮助

- **WHEN** `GPUBackendTester.exe --help`
- **THEN** 帮助信息输出到 stdout，exit code 0
