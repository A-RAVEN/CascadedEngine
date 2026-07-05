# Tasks: 修复 Vulkan 子对象 App 指针未传播导致的崩溃

**Change ID**: fix-vulkan-subobject-app-pointer
**Created**: 2026-07-05

---

## 1. VulkanSubobjectBase 初始化

- [x] 1.1 修改 `VulkanRenderBackendNew/private/Utils/VulkanSubobjectBase.h`：将 `pApp` 成员声明改为 `RenderBackend_Vulkan* pApp = nullptr;`，确保未显式设置时为空指针而非未初始化值

## 2. VulkanGraphExecutor 子对象传播

- [x] 2.1 修改 `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：在 `CompileAndExecute` 方法开头（`m_CurrentFrameContext = std::move(frameContext);` 之后），添加 `GetApp()->InitSubObj(&m_LocalResourceManager);` 和 `GetApp()->InitSubObj(&m_ConstantBufferManager);`，将 App 指针传播给直接子对象

## 3. VulkanGraphLocalResourceManager 子对象传播

- [x] 3.1 修改 `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp`：在 `AllocateAliasedResources` 方法开头，添加 `GetApp()->InitSubObj(&m_AliasingManager);`，将 App 指针传播给 AliasingManager 子对象

## 4. 编译验证

- [x] 4.1 运行 `build.py`，确保 VulkanRenderBackend 编译成功，若失败则分析并修复直到 BUILD SUCCESSFUL
