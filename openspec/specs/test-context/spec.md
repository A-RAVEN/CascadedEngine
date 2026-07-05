# Test Context

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: TestContext 结构体

系统 SHALL 定义 `TestContext` 结构体，包含所有 GPU 后端和窗口系统指针、资源管理系统指针、路径配置和 headless 参数。结构体 SHALL 替代 Main.cpp 中现有的文件级全局变量。

#### Scenario: TestContext 替代全局变量

- **WHEN** 测试函数需要访问 GPU 后端
- **THEN** 通过 `ctx.pGPUBackend` 访问，而非 `g_GPUBackend` 全局变量

#### Scenario: 路径配置集中管理

- **WHEN** 测试函数需要资源路径
- **THEN** 通过 `ctx.resourcePath` 访问，而非文件级 `resourceString` 变量

### Requirement: 统一测试函数签名

所有测试函数 SHALL 使用 `void(TestContext&)` 签名。`TestIMGUI` 不再单独接收路径参数，改用 `ctx.editorConfigPath`。

#### Scenario: 测试函数签名一致

- **WHEN** 查看任一测试函数的声明
- **THEN** 签名为 `void TestXxx(TestContext& ctx)`

### Requirement: 空指针保护

系统 SHALL 在调用测试函数前验证 `TestContext` 的关键字段（`pGPUBackend`、`pWindowSystem`、`pThreadManager`）非空。若为空 SHALL 输出错误并返回 exit code 1。

#### Scenario: 后端未初始化时拒绝运行

- **WHEN** `ctx.pGPUBackend` 为 null
- **THEN** 错误信息输出到 stderr，exit code 1，不进入任何测试

### Requirement: Headless 参数透传

`TestContext` SHALL 包含 `headlessFrames` 和 `headlessTimeout` 字段，由 main 从命令行参数解析后填入，测试函数仅从结构体读取。

#### Scenario: Headless 参数透传

- **WHEN** 用户指定 `--headless 10`
- **THEN** `ctx.headlessFrames == 10`，所有测试函数可访问该值控制渲染循环
