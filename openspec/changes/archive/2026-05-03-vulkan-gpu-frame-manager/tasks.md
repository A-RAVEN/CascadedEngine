## 1. VulkanLinearMemoryManager 新建

- [x] 1.1 创建 `VulkanRenderBackendNew/private/ResourceManagement/VulkanLinearMemoryManager.h`：定义类接口（AllocUploadStagingBuffer、Reset、Release、页面列表管理）
- [x] 1.2 创建 `VulkanRenderBackendNew/private/ResourceManagement/VulkanLinearMemoryManager.cpp`：实现 VMA staging buffer 分配、线性 offset 管理、溢出时分配新页面

## 2. VulkanFrameManager 新建

- [x] 2.1 创建 `VulkanFrameBoundResourceManager`：持有 VulkanCommandListManager + vk::DescriptorPool + VulkanLinearMemoryManager + 双 Fence（direct/compute），实现 Reset()、Release() 和 `EnsurePoolCapacity(maxSets, poolSizes)`（预检 pool 容量，不足则销毁重建）
- [x] 2.2 创建 `VulkanFrameContext`：binary_semaphore + m_FrameBoundResourceManager + vk::Fence pair + `castl::vector<WindowSync>`（每窗口一对 `vk::Semaphore` = acquire + present），实现 Aquire() / Reset() / Release() / `EnsureWindowSync(idx)`
- [x] 2.3 创建 `VulkanGPUFrameManager`：m_FrameContexts[N] + m_FrameIndex + maxFrameCount，实现 AquireFrameContext() 返回 PFrameContext（unique_ptr + custom deleter）、WaitIdle()、Release()

## 3. VulkanGraphExecutor 改造

- [x] 3.1 修改 `CompileAndExecute` 签名：新增 `VulkanGPUFrameManager::PFrameContext&&` 参数，内部存储 `m_CurrentFrameContext`
- [x] 3.2 将 DescriptorPool 管理从 executor 移到 FrameContext：Prepare 阶段末尾统计描述符需求 → 调用 `frameContext->GetResourceManager().EnsurePoolCapacity()` → 确保 pool 容量足够 → `BuildDescriptors()` 使用此 pool 分配 descriptor set
- [x] 3.3 将 CommandBuffer 获取从自管理的 CommandListManager 改为从 `frameContext->GetResourceManager().GetCommandListManager()` 获取
- [x] 3.3a 在 `CompileAndExecute` 的 Prepare 阶段末尾对每个 backbuffer 调用 `frameContext->EnsureWindowSync(idx)` + `pWindow->AcquireNextImage(sync.acquireSemaphore)`，将 swapchain acquire 从 `PresentWindows` 移到此处
- [x] 3.4 修改 `SubmitBatches()`：去掉 `waitForFences(UINT64_MAX)`，最后 batch 的 `SubmitInfo` wait 所有 window acquire semaphore + signal 所有 window present semaphore。移除 `m_Fences` / `m_Semaphores` 成员向量
- [x] 3.5 将 CBuffer 和 image upload staging buffer 分配改为使用 `frameContext->GetResourceManager().GetStagingMemoryManager().AllocUploadStagingBuffer()`，在 `RecordBatchCommands()`（CBuffer）和 `RecordTransferPass()`（image upload）中直接调用。移除 `m_PendingStagingBuffers` 和 `CleanupStagingBuffers()`
- [x] 3.6 修改 `Reset()`：清空 pass states/batches/ImageLifetimes/BufferLifetimes/CBufferLifetimes/LocalResourceManager 等帧级临时数据。移除 m_Fences/m_Semaphores/m_DescriptorPool/m_PendingStagingBuffers 成员（转由 FrameContext/LinearMemoryManager 管理）
- [x] 3.7 修改 `Release()`：释放帧级资源（m_LocalResourceManager + m_ConstantBufferManager + m_ShaderResourceInstances），释放 FrameContext 引用。**不再负责跨帧缓存的生命周期**（已搬到 RenderBackend_Vulkan）
- [x] 3.8 在 `CompileAndExecute` 返回前清理所有 `VulkanResourceBindingInstance` 的 descriptor set 引用（`vkResetDescriptorPool` 将隐式 free 它们），下帧重新调用 `BuildDescriptors`

## 4. RenderBackend_Vulkan 改造

- [x] 4.1 在 `RenderBackend_Vulkan` 中持有 `VulkanGPUFrameManager` 成员，在 DeviceInit 回调中构造
- [x] 4.2 将 `m_ShaderModuleCache`、`m_RenderPassCache`、`m_FramebufferCache` 从 `VulkanGraphExecutor` 搬到 `RenderBackend_Vulkan` 作为持久成员，提供 `GetOrCreate*` 访问方法
- [x] 4.3 将 `m_PipelineLayoutCache` 合并到已有 `m_PipelineLayoutContainer`，`m_DescriptorSetLayoutCache` 合并到已有 `m_DescriptorSetLayoutContainer`
- [x] 4.4 修改 `ExecuteGraph()`：调用 `m_GPUFrameManager.AquireFrameContext()` → 每帧创建 VulkanGraphExecutor → `executor.CompileAndExecute(graph, std::move(frameContext))` → `executor.Release()`。与 D3D12 生命周期完全一致
- [x] 4.5 在 `RenderBackend_Vulkan::Release()` 中调用 `m_GPUFrameManager.Release()` 并销毁新增的跨帧缓存

## 5. VulkanCommandListManager 适配

- [x] 5.1 确保 `VulkanCommandListManager` 支持 `Reset()` 语义（vkResetCommandPool + 重新分配 CommandBuffer），而非销毁重建。同时确认 `vkResetCommandPool` 后重新 `AllocateCommandBuffer` 能正常获取新的 cmd buffer handle

## 6. 文档更新

- [x] 6.1 更新 `openspec/specs/vulkan-backend-alignment/spec.md`：Phase 2 项目 1-3 标记为已完成
