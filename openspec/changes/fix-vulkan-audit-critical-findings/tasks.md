# Tasks: 修复 Vulkan 审计关键缺陷

**Change ID**: fix-vulkan-audit-critical-findings

---

## 1. 资源注册完整性（根因修复）

- [ ] 1.1 `VulkanGraphExecutor.cpp` — `CollectResources` line 664：在 `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle(ImageHandle(handleKey), resourceId)` 注册 render pass attachment texture【D-01】
- [ ] 1.2 `VulkanGraphExecutor.cpp` — `CollectResources` line 694：在 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle(BufferHandle(handleKey), resourceId)` 注册 index buffer【D-01】
- [ ] 1.3 `VulkanGraphExecutor.cpp` — `CollectResources` line 705：在 vertex buffer 循环中 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle`【D-01】
- [ ] 1.4 `VulkanGraphExecutor.cpp` — `CollectResources` line 731：在 transfer image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】
- [ ] 1.5 `VulkanGraphExecutor.cpp` — `CollectResources` line 745：在 finalize pass image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】
- [ ] 1.6 `VulkanGraphExecutor.cpp` — `PrepareBatchResourceBarriers` Image barrier 路径：在 4 处 `GetTexture(image)` 后添加 `if (!img) continue;`（line 1060, 1089, 1112, 1140）【D-04】

## 2. 空句柄防御

- [ ] 2.1 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateShaderModule` 返回 null 时 `continue` 跳过管线创建（vertex ~line 1382、fragment ~line 1387、compute ~line 1660）【D-02】
- [ ] 2.2 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateRenderPass` 返回 null 时 `continue` 跳过（~line 1516）【D-11】
- [ ] 2.3 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreatePipelineLayout` 返回 null 时 `continue` 跳过（raster ~line 1378、compute ~line 1656）【审查新发现】
- [ ] 2.4 `VulkanPipelineLibrary.cpp` — `CreatePreRasterizationLibrary`/`CreateFragmentLibrary`：push shaderStages 前验证 `.module` 非空【D-41】
- [ ] 2.5 `VulkanBuffer.cpp` + `VulkanTexture.cpp` — `UploadData`：`AllocateBuffer` 后检查 staging allocation 非 VK_NULL_HANDLE，失败则 return【D-14】
- [ ] 2.6 `VulkanGraphExecutor.cpp` — `RecordRenderPass`：`batchData.pipeline` 为 null 时跳过整个 draw batch（~line 1908），包括描述符绑定 + vertex buffer 绑定 + draw call【审查扩展】
- [ ] 2.7 `VulkanGraphExecutor.cpp` — `RecordComputePass`：`dispatchData.pipeline` 为 null 时跳过整个 dispatch（~line 2024），包括描述符绑定 + dispatch call【D-47】

## 3. 资源泄漏修复

- [ ] 3.1 `VulkanTexture.cpp` — `Init`：`createImageView` 包在 try-catch 中，异常时清理 VkImage + VmaAllocation【D-07】
- [ ] 3.2 `VulkanGraphLocalResourceManager.cpp` — `AllocateAliasedResources` Phase B：buffer 创建失败 catch（~line 345）和 imageView 失败 catch（~line 431）按 Route A/B 分支清理。Route A（managed.allocation 为空）：device.destroyBuffer/destroyImage；Route B（managed.allocation 非空）：VMA FreeBuffer/FreeImage【D-08】
- [ ] 3.3 `VulkanBuffer.cpp` — `UploadData`：`createFence` 包在 try-catch 中，异常时清理 staging buffer + command buffer【D-16】
- [ ] 3.4 `VulkanTexture.cpp` — `UploadData`：`createFence` 包在 try-catch 中，异常时清理 staging buffer + command buffer【D-16】
- [ ] 3.5 `VulkanResourceAliasing.cpp` — `FreeAliasedPool`：`vmaFreeMemory` 后追加 `m_AliasedAllocations.clear()`【D-13】
- [ ] 3.6 `VulkanCommandListManager.cpp` — `Init`：每个 `createCommandPool` 包在 try-catch 中，失败时销毁已创建的池【D-20】
- [ ] 3.7 `RenderBackend_Vulkan.cpp` — `Init`：部分初始化失败时步进清理已初始化的组件（逆序逐个销毁），不使用全量 `Release()`【D-22】

## 4. 同步修复

- [ ] 4.1 `VulkanFrameManager.h/cpp` + `VulkanGraphExecutor.cpp` — `SubmitBatches`：将 `m_FenceSubmitted` 拆分为 `m_DirectFenceSubmitted` + `m_ComputeFenceSubmitted`；在每条提交路径设置对应标志；`MarkFenceSubmitted()` 拆分为 `MarkDirectFenceSubmitted()` / `MarkComputeFenceSubmitted()` 或添加 queueType 参数【D-10】
- [ ] 4.2 `VulkanGraphExecutor.cpp` — `SubmitBatches` batch-ordering sync（~line 2351-2365）：仅 wait+reset 已提交的 fence；wait+reset 后清除对应标志【D-10】【D-51】
- [ ] 4.3 全量 `waitForFences` 返回值检查（6 处）：(a) VulkanBuffer::UploadData、(b) VulkanTexture::UploadData、(c) SubmitBatches direct fence、(d) SubmitBatches compute fence、(e) FrameContext::Aquire ×2、(f) GPUFrameManager::WaitIdle ×2。DEVICE_LOST 时记录错误并中止/设置标志【D-06】
- [ ] 4.4 `VulkanWindowHandle.cpp` — `acquireNextImageKHR`/`presentKHR`：添加 DEVICE_LOST、SURFACE_LOST 检查，传播错误【D-17】

## 5. 描述符与管线安全

- [ ] 5.1 `VulkanResourceBindingInstance.cpp` — `BuildDescriptors`：写入循环前显式 `m_BufferInfos.reserve(CBufferBindings.size() + BufferBindings.size())`；`m_ImageInfos.reserve(ImageBindings.size() + SamplerBindings.size())`【D-09】
- [ ] 5.2 `VulkanSamplerManager.cpp` — `MakeSamplerCreateInfo`：(a) `info.maxLod = VK_LOD_CLAMP_NONE`；(b) 映射 `desc.boarderColor` → `vk::BorderColor`（添加 ETextureSamplerBorderColor → vk::BorderColor 转换函数）【D-26】【D-39】
- [ ] 5.3 `VulkanSamplerManager.cpp` — `GetOrCreateSampler`：`createSampler` 包在 try-catch 中，失败返回 VK_NULL_HANDLE【D-19】
- [ ] 5.4 `ShaderLibrary.cpp` — 返回裸指针的 getter（`FindShaderEntry` 等）改为返回副本或确保底层容器指针稳定【审查新发现】

## 6. LinearMemoryManager 加固

- [ ] 6.1 `VulkanLinearMemoryManager.cpp` — `AllocatePage`：验证 `vmaCreateBuffer` 返回 VK_SUCCESS 且 stagingAlloc 非 VK_NULL_HANDLE；失败时返回错误【审查新发现】
- [ ] 6.2 `VulkanLinearMemoryManager.cpp` — `AllocUploadStagingBuffer`：超大分配（超过页面大小）时返回空 StagingAllocation + CA_LOG_ERR，而非静默溢出【审查新发现】
- [ ] 6.3 `VulkanLinearMemoryManager.cpp` — `Reset`：添加页面裁剪逻辑，超过阈值时释放最旧的页面防止无界内存增长【审查新发现】

## 7. 编译验证与测试

- [ ] 7.1 运行 `build.py`，确认 BUILD SUCCESSFUL
- [ ] 7.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30`，确认不崩溃
