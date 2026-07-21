## Why

`GPUBackendTester` 中 Vulkan 后端的 shader 资源路径解析存在**两个独立 bug**：

1. **字符串拼接缺失分隔符**（次要 bug）：`rootPath.string() + "CAResources"` 在 `rootPath` 非盘符根时不包含尾部分隔符，产生粘连路径如 `E:\Projects\CascadedEngineCAResources`
2. **CWD 依赖导致 `../../../../` 解析到盘符根**（主要 bug）：`std::filesystem::absolute("../../../../")` 的解析结果随运行目录深度变化。CWD 深度不足时解析到盘符根 `E:\`，`resourcePath` 变为 `E:\CAResources`（不存在），`assetPath` 变为 `E:\CAAssets`（同样不存在）

Bug 2 导致 `ShaderImporter_Vulkan::ImportResource` 找不到源目录直接退出 —— `VulkanShaderLibrary.shLib` 从未被创建，pipeline 始终为 null，渲染白屏。Bug 1 在 CWD 深度恰当时独立导致路径粘连。

D3D12 后端能工作仅因为其 `D3D12ShaderLibrary.shLib` 在之前 CWD 正确的运行中已序列化到 `CAAssets/`。

## What Changes

- **Main.cpp 路径拼接修复**（修 Bug 1）：将 `rootPath.string() + "CAResources"` 改为 `(rootPath / "CAResources").string()`，消除缺失分隔符
- **Main.cpp 共享项目根推导工具**（修 Bug 2）：使用 exe 路径（`GetModuleFileName`）+ sentinel 文件（`CAResources/` 或 `CLAUDE.md`）向上遍历目录树，替代脆弱的 `../../../../`，所有路径消费者（resourcePath、assetPath、editorConfigPath、纹理路径）统一使用推导结果
- **ShaderImporter_Vulkan 源目录回退机制**：新增 `SetSourceDirectory()` 方法，当 `ScanSourceDirectory` 传入路径不存在时自动回退，提供纵深防御
- **诊断日志增强**：源目录不存在时输出预期路径和实际 CWD，回退触发时记录完整 fallback 链
- **清理遗留调试代码**：移除 25 个 `[DIAG] fprintf` 调用和 4 个 `.bak` 文件

## Capabilities

### New Capabilities
- `resource-path-resolution`: 资源路径解析独立于 CWD，后端通过 exe/DLL 位置反推项目根目录

### Modified Capabilities
- _（无需修改已有 spec——此变更修正 Main.cpp 和 RenderBackend_Vulkan.cpp 的实现细节，不影响已有 spec 定义的行为契约）_

## Impact

- `Test/GPUBackendTester/Main.cpp`: 路径拼接方式修正 + 共享项目根推导工具替换 `../../../../`
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`: Init() 中为 ShaderImporter 设置回退源目录
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h`: 新增 `m_SourceDirectory` 成员和 `SetSourceDirectory()` 方法
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.cpp`: ImportResource 支持回退路径 + 增强诊断日志
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`: 移除 13 个 `[DIAG] fprintf` 调用
- `VulkanRenderBackendNew/private/ResourceManagement/VulkanCommandListManager.cpp`: 移除 12 个 `[DIAG] fprintf` 调用
- `Test/VulkanRendererBackendTester/private/Main.cpp`: 添加 deprecation 注释（已有相同 bug，但不在构建中）
