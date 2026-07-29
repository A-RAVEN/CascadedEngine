## Why

对 Vulkan 后端的并行审查（8 维度 × 3 视角对抗验证）发现了 46 个已验证缺陷，其中 8 个 critical 和 10 个 high。经对抗验证审查，确认本 change 覆盖其中 18 个 P0+P1 缺陷，外加审查新发现的 1 个遗漏项。所有修复旨在一次性打通 TestSimpleTriangle 的完整渲染链路。

## What Changes

### A. 资源注册完整性（根因修复）
- `CollectResources` 中每个 `RegisterTemporaryBuffer`/`RegisterTemporaryTexture` 调用点配对 `RegisterBufferHandle`/`RegisterTextureHandle`，建立 Handle→resourceId 映射。覆盖：render pass attachment textures、index buffer、vertex buffers、transfer images、finalize pass images
- `PrepareBatchResourceBarriers` 中 Image barrier 路径添加 null guard（与 buffer barrier 同构，line 1060/1089/1112/1140）

### B. 空句柄防御
- `GetOrCreateShaderModule` 返回值在管线创建前 null 检查（GPL + Monolithic + Compute 三条路径）
- `GetOrCreateRenderPass` 返回值在 `BuildPipelineStates` 中 null 检查
- `GetOrCreatePipelineLayout` 返回值在 `BuildPipelineStates` 中 null 检查（审查新发现）
- `UploadData`（Buffer + Texture 双路径）中 staging buffer/image 分配结果检查
- `RecordRenderPass`：batchData.pipeline 为 null 时跳过整个 draw batch（与 RecordComputePass 同构）
- `RecordComputePass`：dispatchData.pipeline 为 null 时跳过整个 dispatch
- `PipelineLibrary` 中 shader stage module null 验证

### C. 资源泄漏修复
- `VulkanTexture::Init`：`createImageView` 抛异常时清理已分配的 VkImage/VMA
- `AllocateAliasedResources` Phase B：imageView 创建失败时按 Route A/B 分支清理（Route A 用 device.destroyImage，Route B 用 VMA FreeImage）；buffer 创建失败时同样分支清理。**扩展**：Route A 的 `bindBufferMemory`/`bindImageMemory` 也需 try-catch（审查新增）
- `UploadData`（Buffer + Texture）：fence 创建失败时清理 staging buffer + command buffer
- `VulkanResourceAliasing::FreeAliasedPool`：清除 `m_AliasedAllocations` 防止 use-after-free
- `VulkanCommandListManager::Init`：部分命令池创建失败时清理已创建的池
- `RenderBackend_Vulkan::Init`：部分初始化失败时步进清理已初始化的组件（不使用全量 Release()）。**扩展**：`enumeratePhysicalDevices()` 返回空列表时不能 `.front()`（UB），需提前返回错误（审查新增）

### D. 同步修复（修订）
- ~~使用 `m_DirectFenceSubmitted` + `m_ComputeFenceSubmitted` 两个 bool~~ → **修订**：SubmitBatches 已用局部 bool 实现 per-batch sync（4.2 已完成）。删除持久 `m_FenceSubmitted` / `MarkFenceSubmitted()` / `IsFenceSubmitted()`，`WaitIdle` 改为 `device.waitIdle()`（消除首帧 hang 风险，审查新增）
- `waitForFences` 返回值检查：覆盖 **4 处**调用（Buffer/Texture upload + SubmitBatches ×2）。原 6 处中 FrameContext::Aquire 的 2 处已被 frame-3 修复删除。GPUFrameManager::WaitIdle 的 2 处在 4.1 重构中一并处理
- `acquireNextImageKHR` / `presentKHR` 致命错误（DEVICE_LOST）传播

### E. 描述符与管线安全
- `VulkanResourceBindingInstance::BuildDescriptors`：写入循环前对 `m_BufferInfos` 和 `m_ImageInfos` 显式 `reserve()` 防止重分配悬垂指针
- `ShaderLibrary` 中 `FindShaderEntry` 等返回裸指针的 getter 改为返回值副本或索引
- `VulkanSamplerManager`：`maxLod` 设为 `VK_LOD_CLAMP_NONE`；映射 descriptor 的 borderColor 字段
- `GetOrCreateSampler` 添加 try-catch 防止崩溃

### F. LinearMemoryManager 加固（审查新发现）
- `AllocatePage`：验证 vmaCreateBuffer 结果和 stagingAlloc 非空
- `AllocUploadStagingBuffer`：超大分配时返回错误而非静默溢出
- `Reset`：添加页面裁剪逻辑防止帧间无界内存增长

## Capabilities

### New Capabilities
- `resource-handle-registration`: GPU 图资源在 CollectResources 中建立完整的 Handle→resourceId 映射
- `null-handle-vulkan-safety`: 所有 Get* 查找结果在传入 Vulkan API 前进行 null 检查
- `resource-leak-prevention`: 错误路径上正确清理部分分配的资源
- `fence-sync-correctness`: Fence 同步无死锁、无丢弃返回值
- `descriptor-pipeline-safety`: 描述符写入指针稳定性 + Sampler 配置完整性 + 管线创建空安全

### Modified Capabilities
- `buffer-barrier-null-safety`: 扩展至 Image barrier 路径的同构 null guard（原 spec 仅覆盖 buffer 路径）

## Impact

- `VulkanGraphExecutor.cpp` — CollectResources（注册）、PrepareBatchResourceBarriers（image null guard）、BuildPipelineStates（shader/renderPass/pipelineLayout null check）、RecordRenderPass（null pipeline guard）、RecordComputePass（null pipeline guard）、SubmitBatches（fence 双 bool + waitForFences 检查）、AllocUploadStagingBuffer（错误日志）
- `VulkanGraphLocalResourceManager.cpp` — AllocateAliasedResources Phase B 泄漏修复（Route A/B 分支）
- `VulkanResourceBindingInstance.cpp` — 描述符写入 reserve()
- `VulkanSamplerManager.cpp` — maxLod 修复 + try-catch（外层）+ borderColor 映射
- `VulkanResourceAliasing.cpp` — FreeAliasedPool stale 清理
- `VulkanCommandListManager.cpp` — Init 部分失败清理
- `VulkanLinearMemoryManager.cpp` — AllocatePage 验证 + 超大分配处理 + Reset 裁剪
- `RenderBackend_Vulkan.cpp` — Init 步进清理 + GPUFrameManager::WaitIdle 检查
- `VulkanBuffer.cpp` — UploadData 分配检查 + fence 泄漏 + waitForFences 检查
- `VulkanTexture.cpp` — Init 泄漏修复 + UploadData 分配检查 + fence 泄漏 + waitForFences 检查
- `VulkanWindowHandle.cpp` — acquireNextImageKHR / presentKHR 错误传播
- `VulkanSamplerManager.cpp` — maxLod 修复 + try-catch + borderColor 映射
- `VulkanPipelineLibrary.cpp` — shader module null 验证
- `ShaderLibrary.cpp` — 指针稳定性
