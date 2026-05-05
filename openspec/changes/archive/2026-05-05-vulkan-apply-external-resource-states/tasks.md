## 1. VulkanResourceState 头文件抽取

- [x] 1.1 从 `VulkanGraphExecutor.h` 中提取 `VulkanResourceState` 结构体至 `GPUGraph/VulkanResourceState.h`，更新 `VulkanGraphExecutor.h` include 此新头文件。确保 `VulkanTexture.h`、`VulkanBuffer.h`、`VulkanWindowHandle.h` 也能 include 此新头文件（它们此前不依赖 `VulkanGraphExecutor.h`，注意不引入循环依赖）。

## 2. 资源对象添加状态存储

- [x] 2.1 `VulkanTexture.h`: 添加 `VulkanResourceState m_LastResourceState` 成员（初始化为 `{ eNone, eTopOfPipe, eUndefined, eDirect, true }`）+ `SetResourceState()` / `GetResourceState()` 方法
- [x] 2.2 `VulkanBuffer.h`: 添加 `VulkanResourceState m_LastResourceState` 成员（初始化为 `{ eNone, eTopOfPipe, eUndefined, eDirect, false }`）+ `SetResourceState()` / `GetResourceState()` 方法
- [x] 2.3 `VulkanWindowHandle.h/.cpp`: 添加 `castl::vector<VulkanResourceState> m_BackBufferResourceStates` 成员（per-swapchain-image 存储）+ `ApplyCurrentBackBufferResourceState()` 方法（按 `m_CurrentImageIndex` 索引写入）。在 `CreateSwapchain()` 和 `RecreateSwapchain()` 中 resize 并初始化数组。



## 3. 实现 ApplyExternalResourceStates

- [x] 3.1 `VulkanGraphExecutor.cpp`: 实现 `ApplyExternalResourceStates()` 函数体 — 遍历 `m_ImageLifetimes` → 对非 Internal image 断言 `states` 非空 → External texture 写回 VulkanTexture、Backbuffer 写回 VulkanWindowHandle；遍历 `m_BufferLifetimes` → 对非 Internal buffer 断言 `states` 非空 → External buffer 写回 VulkanBuffer

## 4. 编译验证

- [x] 4.1 运行 build.bat 验证编译通过，若失败则分析并修复直到 BUILD SUCCESSFUL
