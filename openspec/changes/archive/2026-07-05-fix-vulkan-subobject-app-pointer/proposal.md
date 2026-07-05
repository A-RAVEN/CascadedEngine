# Proposal: 修复 Vulkan 子对象 App 指针未传播导致的崩溃

**Change ID**: fix-vulkan-subobject-app-pointer
**Status**: Proposed
**Created**: 2026-07-05

---

## Why

`GPUBackendTester` 运行 Vulkan 后端测试时，在首个测试的首帧即发生 ACCESS_VIOLATION 崩溃。cdb 分析定位到 `VulkanMemoryManager::AllocateMemory`（`this` 指针为 `0x1dc`），根因是 `VulkanGraphExecutor` 内部嵌套的 `VulkanResourceAliasing` 子对象的 `pApp` 指针从未被初始化，导致 `GetApp()->GetMemoryManager()` 在空指针上做成员偏移访问。

## What Changes

- **VulkanGraphExecutor**: 在 `CompileAndExecute` 入口处将 `pApp` 传播给 `m_LocalResourceManager` 和 `m_ConstantBufferManager`
- **VulkanGraphLocalResourceManager**: 在 `AllocateAliasedResources` 中将 `pApp` 传播给 `m_AliasingManager`
- **VulkanSubobjectBase**: 将 `pApp` 成员显式初始化为 `nullptr`，确保未初始化时的崩溃是可预测的空指针解引用而非随机地址访问

## Capabilities

### New Capabilities

- `vulkan-subobject-app-propagation`: 确保 `VulkanGraphExecutor` 及其嵌套的 `VulkanSubobjectBase` 子对象在首次使用前已获得有效的 `RenderBackend_Vulkan*` 指针。所有通过 `pApp` 间接访问的成员（`GetDevice()`、`GetInstance()`、`GetMemoryManager()` 等）在子对象调用链上均能正常工作。

### Modified Capabilities

无。

## Impact

- **VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp** — `CompileAndExecute` 开头新增两行 `GetApp()->InitSubObj(...)` 调用
- **VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp** — `AllocateAliasedResources` 开头新增一行 `GetApp()->InitSubObj(&m_AliasingManager)`
- **VulkanRenderBackendNew/private/Utils/VulkanSubobjectBase.h** — `pApp` 成员添加 `= nullptr` 初始化

## Non-goals

- 不改变 `InitSubObj` 的模板设计
- 不引入自动递归传播机制（保持改动最小）
- 不修改渲染逻辑

