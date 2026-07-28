# Tasks: 修复 Vulkan 审计关键缺陷

**Change ID**: fix-vulkan-audit-critical-findings

---

## 1. 资源注册完整性（根因修复）

- [x] 1.1 `VulkanGraphExecutor.cpp` — `CollectResources` line 664：在 `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle(ImageHandle(handleKey), resourceId)` 注册 render pass attachment texture【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.2 `VulkanGraphExecutor.cpp` — `CollectResources` line 694：在 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle(BufferHandle(handleKey), resourceId)` 注册 index buffer【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.3 `VulkanGraphExecutor.cpp` — `CollectResources` line 705：在 vertex buffer 循环中 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.4 `VulkanGraphExecutor.cpp` — `CollectResources` line 731：在 transfer image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.5 `VulkanGraphExecutor.cpp` — `CollectResources` line 745：在 finalize pass image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [ ] 1.6 `VulkanGraphExecutor.cpp` — `PrepareBatchResourceBarriers` Image barrier 路径：在 4 处 `GetTexture(image)` 后添加 null guard（line 1065, 1094, 1117, 1145），与 buffer 路径同构（`if (!acquireBarrier.image) continue;` 等字段形式）【D-04】⚠️ 行号已对齐 2026-07-29 基线审查

## 2. 空句柄防御

- [ ] 2.1 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateShaderModule` 返回 null 时 `continue` 跳过管线创建（vertex line 1387、fragment line 1388、compute line 1677）。附带：1387-1388 的 `hasVertex ? ... : nullptr` 三元是死代码（1381-1385 已保证 true），可顺手简化为直接调用【D-02】⚠️ 行号已对齐
- [ ] 2.2 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateRenderPass` 返回 null 时 `continue` 跳过（line 1537）。注意 RecordRenderPass 侧已有守卫（1871-1872），唯独 BuildPipelineStates 缺【D-11】⚠️ 行号已对齐
- [ ] 2.3 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreatePipelineLayout` 返回 null 时 `continue` 跳过（raster line 1390、compute line 1678）【审查新发现】⚠️ 行号已对齐
- [ ] 2.4 `VulkanPipelineLibrary.cpp` — `CreatePreRasterizationLibrary`（line 94）/`CreateFragmentLibrary`（line 147）：push/assign shaderStages 前验证 `.module` 非空，null 时 `return nullptr`。2.1 落地后此处为纵深防御【D-41】
- [x] 2.5 `VulkanBuffer.cpp` + `VulkanTexture.cpp` — `UploadData`：`AllocateBuffer` 后检查 staging allocation 非 VK_NULL_HANDLE，失败则 return【D-14】✅ 已修复（对抗验证 CONFIRMED）
- [ ] 2.6 `VulkanGraphExecutor.cpp` — `RecordRenderPass`：将 line 1920-1921 的条件 bind（`if (batchData.pipeline) cmdBuf.bindPipeline(...)`）改为 `if (!batchData.pipeline) continue;` + 无条件 bindPipeline，跳过整个 draw batch（描述符绑定 + vertex buffer 绑定 + draw call）【审查扩展】⚠️ 行号已对齐，实现方式明确化
- [ ] 2.7 `VulkanGraphExecutor.cpp` — `RecordComputePass`：将 line 2036-2037 的条件 bind 改为 `if (!dispatchData.pipeline) continue;` + 无条件 bindPipeline，跳过整个 dispatch（描述符绑定 + dispatch call）。与 2.6 同构【D-47】⚠️ 行号已对齐

## 3. 资源泄漏修复

- [ ] 3.1 `VulkanTexture.cpp` — `Init`：`createImageView` 包在 try-catch 中，异常时清理 VkImage + VmaAllocation【D-07】
- [ ] 3.2 `VulkanGraphLocalResourceManager.cpp` — `AllocateAliasedResources` Phase B：(a) buffer 创建失败 catch（~line 348）(b) image 创建失败 catch（~line 389）(c) imageView 失败 catch（~line 431）按 Route A/B 分支清理。Route A（managed.allocation 为空）：device.destroyBuffer/destroyImage；Route B（managed.allocation 非空）：VMA FreeBuffer/FreeImage。**新增**：(d) Route A 的 `bindBufferMemory`（line 360）和 `bindImageMemory`（line 401）也需要 try-catch 包裹（审查发现 bind* 调用同样可抛异常）【D-08】⚠️ scope 扩展
- [ ] 3.3 `VulkanBuffer.cpp` — `UploadData`：`createFence` 包在 try-catch 中，异常时清理 staging buffer + command buffer【D-16】
- [ ] 3.4 `VulkanTexture.cpp` — `UploadData`：`createFence` 包在 try-catch 中，异常时清理 staging buffer + command buffer【D-16】
- [ ] 3.5 `VulkanResourceAliasing.cpp` — `FreeAliasedPool`：`vmaFreeMemory` 后追加 `m_AliasedAllocations.clear()`【D-13】
- [ ] 3.6 `VulkanCommandListManager.cpp` — `Init`：每个 `createCommandPool` 包在 try-catch 中，失败时销毁已创建的池【D-20】
- [ ] 3.7 `RenderBackend_Vulkan.cpp` — `Init`：(a) 部分初始化失败时步进清理已初始化的组件（逆序逐个销毁），不使用全量 `Release()`；(b) **新增**：`enumeratePhysicalDevices()` 返回空列表时（无 GPU）不能直接 `.front()`（line 222，UB/crash），需检查并提前返回错误【D-22】⚠️ scope 扩展（审查发现 .front() 无空检查）

## 4. 同步修复

- [ ] 4.1 `VulkanFrameManager.h/cpp` — **重新 scope**（原设计 D4 双 bool 与当前架构冲突）：SubmitBatches 已用局部 `directSubmitted`/`computeSubmitted` bool 实现了 per-batch fence sync（line 2292-2293），持久 `m_FenceSubmitted` 仅被 `WaitIdle` 使用。修改为：删除 `m_FenceSubmitted` / `MarkFenceSubmitted()` / `IsFenceSubmitted()`，`WaitIdle` 改为 `device.waitIdle()`（与 `RenderBackend_Vulkan::WaitIdle` 一致）或无条件 waitForFences 两个 fence【D-10 修订】⚠️ 设计变更
- [x] 4.2 `VulkanGraphExecutor.cpp` — `SubmitBatches` batch-ordering sync：仅 wait+reset 已提交的 fence ✅ 已由局部 bool（line 2292-2293, 2393-2413）实现，功能等价于原始设计（审查 CONFIRMED）
- [ ] 4.3 全量 `waitForFences` 返回值检查（**4 处**，原 6 处中 FrameContext::Aquire 的 2 处已被 frame-3 修复删除）：(a) VulkanBuffer::UploadData（line 191）、(b) VulkanTexture::UploadData（line 339）、(c) SubmitBatches direct fence（line 2400）、(d) SubmitBatches compute fence（line 2409）。DEVICE_LOST 时记录错误并中止/设置标志。另：GPUFrameManager::WaitIdle（VulkanFrameManager.cpp line 271/276）的 2 处 waitForFences 在 4.1 重构中一并处理【D-06】⚠️ 调用点数量修正
- [ ] 4.4 `VulkanWindowHandle.cpp` — `acquireNextImageKHR`/`presentKHR`：添加 DEVICE_LOST、SURFACE_LOST 检查，传播错误【D-17】

## 5. 描述符与管线安全

- [ ] 5.1 `VulkanResourceBindingInstance.cpp` — `BuildDescriptors`：clear() 后、写入循环前显式 `m_BufferInfos.reserve(m_CBufferBindings.size() + m_BufferBindings.size())`；`m_ImageInfos.reserve(m_ImageBindings.size() + m_SamplerBindings.size())`。防止 push_back 重分配导致已存储的 `&back()` 指针悬垂【D-09】⚠️ 成员名对齐
- [ ] 5.2 `VulkanSamplerManager.cpp` — `MakeSamplerCreateInfo`：(a) `info.maxLod = VK_LOD_CLAMP_NONE`（当前默认 0.0f 禁用了 mipmapping）；(b) 映射 `desc.boarderColor` → `vk::BorderColor`（添加 ETextureSamplerBorderColor → vk::BorderColor 转换函数）【D-26】【D-39】
- [ ] 5.3 `VulkanSamplerManager.cpp` — `GetOrCreateSampler`：try-catch 包在 `m_SamplerCache.get_or_create()` **外层**（不能在 lambda 内 catch，否则会缓存 null sampler）。失败返回 VK_NULL_HANDLE【D-19】⚠️ catch 位置明确化
- [ ] 5.4 `ShaderLibrary.cpp` — 返回裸指针的 getter（`GetShaderFileInfo`/`GetShaderCode`/`GetShaderStruct`/`GetShaderRootStruct`）改为返回副本或确保底层容器指针稳定。当前 unordered_map 在 rehash 时使指针失效【审查新发现】⚠️ 函数名对齐

## 6. LinearMemoryManager 加固

- [x] 6.1 `VulkanLinearMemoryManager.cpp` — `AllocatePage`：验证 `vmaCreateBuffer` 返回 VK_SUCCESS 且 stagingAlloc 非 VK_NULL_HANDLE；失败时返回错误【审查新发现】✅ 已修复（对抗验证 CONFIRMED）
- [ ] 6.2 `VulkanLinearMemoryManager.cpp` — `AllocUploadStagingBuffer`：超大分配（超过页面大小 64MB）时返回空 StagingAllocation + `CA_LOG_ERR`。当前仅 CA_LOG_WARN 后继续执行，导致调用方 memcpy 越界写入（**内存安全问题**，非仅加固）【审查新发现】⚠️ 严重度提升
- [ ] 6.3 `VulkanLinearMemoryManager.cpp` — `Reset`：添加页面裁剪逻辑。`AllocUploadStagingBuffer` 仅探测 `m_Pages.back()`（line 77），因此裁剪最旧页面安全。保留至少 1 页【审查新发现】

## 7. 编译验证与测试

- [ ] 7.1 运行 `build.py`，确认 BUILD SUCCESSFUL
- [ ] 7.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30`，确认不崩溃
