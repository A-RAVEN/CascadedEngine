# Vulkan Subobject App Propagation

**Version**: 1.0
**Created**: 2026-07-05

---

## ADDED Requirements

### Requirement: VulkanGraphExecutor 子对象获得有效 App 指针

`VulkanGraphExecutor` SHALL 在 `CompileAndExecute` 执行任何操作之前，将其持有的 `RenderBackend_Vulkan*` （即 `pApp`）通过 `InitSubObj` 传播给直接子对象 `m_LocalResourceManager` 和 `m_ConstantBufferManager`。

`VulkanGraphLocalResourceManager` SHALL 在 `AllocateAliasedResources` 中调用 `GetApp()->GetMemoryManager()` 之前，将 `pApp` 传播给 `m_AliasingManager`。

#### Scenario: 嵌套子对象可正常访问后端设施

- **WHEN** `RenderBackend_Vulkan::ExecuteGraph` 创建 `VulkanGraphExecutor` 并设置其 `pApp`，随后调用 `CompileAndExecute`
- **THEN** `m_LocalResourceManager` 和 `m_ConstantBufferManager` 的 `GetApp()` 返回有效的 `RenderBackend_Vulkan*`
- **AND** `m_AliasingManager` 的 `GetApp()` 在 `AllocateAliasedResources` 被调用时返回有效的 `RenderBackend_Vulkan*`
- **AND** `m_AliasingManager::AllocateAliasedPool` 中的 `GetApp()->GetMemoryManager().AllocateMemory(...)` 正常执行不崩溃

#### Scenario: 无 App 指针时为可预测的空指针崩溃

- **WHEN** 任何 `VulkanSubobjectBase` 子对象在 `pApp` 未被设置的条件下调用 `GetApp()`
- **THEN** 返回 `nullptr`（而非未初始化的随机值）
- **AND** 后续对 `GetApp()` 返回值的成员访问在地址 0 附近崩溃，便于调试器定位
