## ADDED Requirements

### Requirement: VulkanGPUFrameManager 管理多帧上下文
系统 SHALL 提供 `VulkanGPUFrameManager` 类，管理 N 个 `VulkanFrameContext` 的轮转调度，N 由 `maxFrameCount` 参数指定（默认 2，最小 1）。

#### Scenario: 双缓冲轮转
- **WHEN** 连续调用 `AquireFrameContext()` 两次
- **THEN** 第一次返回 FrameContext[0] 的 PFrameContext，第二次返回 FrameContext[1] 的 PFrameContext

#### Scenario: 三帧轮转后回到第一个
- **WHEN** `maxFrameCount=2` 且连续调用 `AquireFrameContext()` 三次
- **THEN** 第三次调用等待 FrameContext[0] 的 GPU fence 完成后返回 FrameContext[0]

#### Scenario: maxFrameCount 边界检查
- **WHEN** 构造 `VulkanGPUFrameManager` 时 `maxFrameCount=0`
- **THEN** 系统触发 assertion 失败

#### Scenario: WaitIdle 等待所有 GPU 工作完成
- **WHEN** 调用 `VulkanGPUFrameManager::WaitIdle()`
- **THEN** 遍历所有 FrameContext，对每个 fence 调用 `waitForFences`，确保所有在途 GPU 工作完成

#### Scenario: Release 释放所有帧上下文
- **WHEN** 调用 `VulkanGPUFrameManager::Release()`
- **THEN** 对每个 FrameContext 调用 `Release()`（内含 semaphore acquire + GPU wait idle + 资源销毁），然后清空 context 容器

### Requirement: VulkanFrameContext 管理单帧资源生命周期
系统 SHALL 提供 `VulkanFrameContext` 类，封装一帧内所有资源的生命周期，包括 CPU 互斥量、GPU fence、present semaphore 和 `VulkanFrameBoundResourceManager`。

#### Scenario: Aquire 等待上一帧 GPU 完成
- **WHEN** FrameContext 被 `Aquire()` 且该 context 的 fence 处于 signaled 状态（上一帧已完成）
- **THEN** 等待 fence、重置 fence、调用 `FrameBoundResourceManager::Reset()` 复用资源

#### Scenario: 首帧 Aquire 无需等待
- **WHEN** FrameContext 首次被 `Aquire()`（fence 尚未创建）
- **THEN** 跳过 fence 等待，直接初始化资源

#### Scenario: CPU 互斥保护
- **WHEN** FrameContext 已被 `Aquire()` 但尚未 `Reset()`（即 PFrameContext 未析构）
- **THEN** 再次 `Aquire()` 同一 context 将阻塞在 `binary_semaphore::acquire()`

#### Scenario: 持有 Present Semaphore 用于 Swapchain 同步
- **WHEN** FrameContext 提交渲染命令并需要 present 到 swapchain
- **THEN** FrameContext 提供 per-window semaphore pair（acquire + present），`EnsureWindowSync(idx)` 按需创建，在 submit 时 wait acquire、signal present，`vkQueuePresentKHR` 时 wait present

### Requirement: VulkanFrameBoundResourceManager 管理每帧可复用资源
系统 SHALL 提供 `VulkanFrameBoundResourceManager` 类，封装每帧可复用的 GPU 资源：CommandPool、DescriptorPool、LinearMemoryManager 和提交 Fence。

#### Scenario: Reset 重置所有子资源
- **WHEN** 调用 `FrameBoundResourceManager::Reset()`
- **THEN** CommandPool 通过 `vkResetCommandPool` 重置、DescriptorPool 通过 `vkResetDescriptorPool` 重置、LinearMemoryManager offset 归零

#### Scenario: Release 销毁所有子资源
- **WHEN** 调用 `FrameBoundResourceManager::Release()`
- **THEN** 销毁 DescriptorPool、CommandPool、LinearMemoryManager 中的所有 buffer、以及 Fence 对象

#### Scenario: Reset 后 DescriptorSet handle 失效
- **WHEN** `FrameBoundResourceManager::Reset()` 调用了 `vkResetDescriptorPool`
- **THEN** 所有之前从此 pool 分配的 `vk::DescriptorSet` handle 变为无效，executor 中持有的 `VulkanResourceBindingInstance` 必须在 Reset 前释放 descriptor set 引用，由下帧重新分配

#### Scenario: EnsurePoolCapacity 预检容量
- **WHEN** `CompileAndExecute` 的 Prepare 阶段统计完当前帧 descriptor 需求后调用 `EnsurePoolCapacity(maxSets, poolSizes)`
- **THEN** 若当前 pool 满足需求则保留（下轮 Aquire 时 Reset 复用），若不足则销毁重建更大 pool（此时 GPU fence 已保证旧 pool 安全可销毁）

#### Scenario: 分配失败不在 BuildDescriptors 中发生
- **WHEN** `BuildDescriptors` 调用 `vkAllocateDescriptorSets`
- **THEN** 分配必定成功，因为 `EnsurePoolCapacity` 已预先保证 pool 容量足够

### Requirement: VulkanLinearMemoryManager 线性分配 staging buffer
系统 SHALL 提供 `VulkanLinearMemoryManager` 类，从 VMA 大块 staging buffer 中线性分配子范围，帧末统一回收。

#### Scenario: 帧内线性分配
- **WHEN** 调用 `AllocUploadStagingBuffer(size, alignment)`
- **THEN** 返回当前 buffer 中的子范围（offset, mapped_ptr），offset 递增

#### Scenario: 空间不足时分配新块
- **WHEN** 当前 staging buffer 剩余空间不足以满足分配请求
- **THEN** 从 VMA 分配新的 64MB staging buffer，从新 buffer 中分配

#### Scenario: 对齐分配
- **WHEN** 调用 `AllocUploadStagingBuffer(size, alignment)` 且 alignment > 1
- **THEN** 返回的子范围的 offset 对齐到 alignment 边界（当前 offset 向上取整到 alignment 倍数后再分配）

#### Scenario: 持久映射
- **WHEN** VMA staging buffer 被分配
- **THEN** 整个 buffer 保持持久映射（`VMA_ALLOCATION_CREATE_MAPPED_BIT`），`AllocUploadStagingBuffer` 返回的 `mapped_ptr` 指向 base + offset，无需每次 map/unmap

#### Scenario: Reset 回收所有块
- **WHEN** 调用 `Reset()`
- **THEN** 所有 staging buffer 的 offset 归零，不释放 VMA 内存，持久映射保持有效

#### Scenario: Release 销毁所有 buffer
- **WHEN** 调用 `Release()`
- **THEN** 销毁所有 staging buffer 并释放 VMA 内存（含 unmap）

### Requirement: ExecuteGraph 异步提交
`RenderBackend_Vulkan::ExecuteGraph` SHALL 在提交 GPU 命令后立即返回，不阻塞等待 GPU 完成。

#### Scenario: 提交后 CPU 继续
- **WHEN** `ExecuteGraph` 完成命令提交（`vkQueueSubmit` 返回）
- **THEN** 方法立即返回，不调用 `waitForFences(UINT64_MAX)`

#### Scenario: 帧完成由下一轮 Aquire 保证
- **WHEN** 下一帧调用 `AquireFrameContext()` 时上一帧的 GPU 工作尚未完成
- **THEN** `FrameContext::Aquire()` 阻塞在 `waitForFences` 直到 GPU 完成

### Requirement: VulkanGraphExecutor 接受 FrameContext 参数
`VulkanGraphExecutor::CompileAndExecute` SHALL 每帧创建、每帧销毁（与 D3D12 `D3D12GPUGraphExecutor` 一致），接受 `VulkanGPUFrameManager::PFrameContext&&` 参数，从中获取 CommandBuffer、DescriptorPool 和 StagingMemoryManager。跨帧缓存（ShaderModule、PipelineLayout、DescriptorSetLayout、RenderPass、Framebuffer）由 `RenderBackend_Vulkan` 作为持久成员持有，executor 通过 `GetApp()` 访问。

#### Scenario: 从 FrameContext 获取命令缓冲区
- **WHEN** `CompileAndExecute` 需要录制命令
- **THEN** 通过 `frameContext->GetResourceManager().GetCommandListManager()` 获取 CommandBuffer

#### Scenario: 从 FrameContext 获取描述符池
- **WHEN** `CompileAndExecute` 需要分配 DescriptorSet
- **THEN** 通过 `frameContext->GetResourceManager().GetDescriptorPool()` 获取 DescriptorPool


#### Scenario: Acquire swapchain image 在 CompileAndExecute 中
- **WHEN** `CompileAndExecute` 需要渲染到多窗口 swapchain
- **THEN** Prepare 阶段对每个 backbuffer 调用 `frameContext->EnsureWindowSync(idx)` 获取 semaphore pair，然后 `pWindow->AcquireNextImage(sync.acquireSemaphore)`，最后 batch submit 时 wait acquire + signal present
#### Scenario: 跨帧缓存由 RenderBackend_Vulkan 管理
- **WHEN** `CompileAndExecute` 多次执行（跨多帧）
- **THEN** ShaderModuleCache、PipelineLayoutCache、DescriptorSetLayoutCache、RenderPassCache、FramebufferCache 由 `RenderBackend_Vulkan` 持久持有，executor 通过 `GetApp()` 访问，不随 executor 销毁而丢失

### Requirement: CBuffer staging 使用 LinearMemoryManager
`VulkanConstantBufferManager` 或其调用方 SHALL 通过 `VulkanLinearMemoryManager::AllocUploadStagingBuffer()` 获取 staging 内存，替代独立的 `vmaAllocateMemory` / `vkDestroyBuffer` / `vmaFreeMemory` 模式。

#### Scenario: CBuffer 上传使用线性分配
- **WHEN** 录制 CBuffer 上传命令
- **THEN** staging buffer 从 `LinearMemoryManager` 线性分配，而非独立 VMA 分配

