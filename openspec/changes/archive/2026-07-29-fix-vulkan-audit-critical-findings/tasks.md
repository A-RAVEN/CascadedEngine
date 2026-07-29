# Tasks: 修复 Vulkan 审计关键缺陷

**Change ID**: fix-vulkan-audit-critical-findings

---

## 1. 资源注册完整性（根因修复）

- [x] 1.1 `VulkanGraphExecutor.cpp` — `CollectResources` line 664：在 `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle(ImageHandle(handleKey), resourceId)` 注册 render pass attachment texture【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.2 `VulkanGraphExecutor.cpp` — `CollectResources` line 694：在 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle(BufferHandle(handleKey), resourceId)` 注册 index buffer【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.3 `VulkanGraphExecutor.cpp` — `CollectResources` line 705：在 vertex buffer 循环中 `RegisterTemporaryBuffer` 后调用 `RegisterBufferHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.4 `VulkanGraphExecutor.cpp` — `CollectResources` line 731：在 transfer image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.5 `VulkanGraphExecutor.cpp` — `CollectResources` line 745：在 finalize pass image `RegisterTemporaryTexture` 后调用 `RegisterTextureHandle`【D-01】✅ 已修复（commit 9f3fe542，对抗验证 CONFIRMED）
- [x] 1.6 `VulkanGraphExecutor.cpp` — `PrepareBatchResourceBarriers` Image barrier 路径：在 4 处 `GetTexture(image)` 后添加 null guard，与 buffer 路径同构 ✅ 已修复

## 2. 空句柄防御

- [x] 2.1 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateShaderModule` 返回 null 时 `continue` 跳过管线创建 + 简化死代码三元 ✅ 已修复
- [x] 2.2 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreateRenderPass` 返回 null 时 `continue` 跳过 ✅ 已修复
- [x] 2.3 `VulkanGraphExecutor.cpp` — `BuildPipelineStates`：`GetOrCreatePipelineLayout` 返回 null 时 `continue` 跳过（raster + compute） ✅ 已修复
- [x] 2.4 `VulkanPipelineLibrary.cpp` — `CreatePreRasterizationLibrary`/`CreateFragmentLibrary`：验证 `.module` 非空，null 时 `return nullptr` ✅ 已修复
- [x] 2.5 `VulkanBuffer.cpp` + `VulkanTexture.cpp` — `UploadData`：`AllocateBuffer` 后检查 staging allocation 非 VK_NULL_HANDLE，失败则 return【D-14】✅ 已修复（对抗验证 CONFIRMED）
- [x] 2.6 `VulkanGraphExecutor.cpp` — `RecordRenderPass`：null pipeline → continue 跳过整个 draw batch ✅ 已修复
- [x] 2.7 `VulkanGraphExecutor.cpp` — `RecordComputePass`：null pipeline → continue 跳过整个 dispatch ✅ 已修复

## 3. 资源泄漏修复

- [x] 3.1 `VulkanTexture.cpp` — `Init`：`createImageView` try-catch + VkImage/VMA 清理 ✅ 已修复
- [x] 3.2 `VulkanGraphLocalResourceManager.cpp` — `AllocateAliasedResources` Phase B：bind*/createImageView try-catch + Route A/B 分支清理 ✅ 已修复
- [x] 3.3 `VulkanBuffer.cpp` — `UploadData`：`createFence` try-catch + waitForFences 返回值检查 ✅ 已修复
- [x] 3.4 `VulkanTexture.cpp` — `UploadData`：`createFence` try-catch + waitForFences 返回值检查 ✅ 已修复
- [x] 3.5 `VulkanResourceAliasing.cpp` — `FreeAliasedPool`：追加 `m_AliasedAllocations.clear()` ✅ 已修复
- [x] 3.6 `VulkanCommandListManager.cpp` — `Init`：每个 `createCommandPool` try-catch + 逆序清理 ✅ 已修复
- [x] 3.7 `RenderBackend_Vulkan.cpp` — `Init`：(a) 步进清理 try-catch (b) enumeratePhysicalDevices 空检查 ✅ 已修复

## 4. 同步修复

- [x] 4.1 `VulkanFrameManager.h/cpp` — 删除 `m_FenceSubmitted`/`MarkFenceSubmitted()`/`IsFenceSubmitted()`，`WaitIdle` 改为 `device.waitIdle()` ✅ 已修复
- [x] 4.2 `VulkanGraphExecutor.cpp` — `SubmitBatches` batch-ordering sync：仅 wait+reset 已提交的 fence ✅ 已由局部 bool（line 2292-2293, 2393-2413）实现，功能等价于原始设计（审查 CONFIRMED）
- [x] 4.3 全量 `waitForFences` 返回值检查（4 处）：(a)(b) 在 3.3/3.4 中完成，(c)(d) SubmitBatches direct/compute ✅ 已修复
- [x] 4.4 `VulkanWindowHandle.cpp` — `acquireNextImageKHR`/`presentKHR`：DEVICE_LOST/SURFACE_LOST 检查 + 错误传播 ✅ 已修复

## 5. 描述符与管线安全

- [x] 5.1 `VulkanResourceBindingInstance.cpp` — `BuildDescriptors`：reserve() 防止 &back() 悬垂 ✅ 已修复
- [x] 5.2 `VulkanSamplerManager.cpp` — `maxLod = VK_LOD_CLAMP_NONE` + `boarderColor` → `vk::BorderColor` 映射 ✅ 已修复
- [x] 5.3 `VulkanSamplerManager.cpp` — `GetOrCreateSampler` try-catch 外层 ✅ 已修复
- [x] 5.4 `ShaderLibrary.h` — 指针稳定性保证：文档化 maps 初始化后不可修改的不变量 ✅ 已修复

## 6. LinearMemoryManager 加固

- [x] 6.1 `VulkanLinearMemoryManager.cpp` — `AllocatePage`：验证 `vmaCreateBuffer` 返回 VK_SUCCESS 且 stagingAlloc 非 VK_NULL_HANDLE；失败时返回错误【审查新发现】✅ 已修复（对抗验证 CONFIRMED）
- [x] 6.2 `VulkanLinearMemoryManager.cpp` — `AllocUploadStagingBuffer`：超大分配返回空 + CA_LOG_ERR ✅ 已修复
- [x] 6.3 `VulkanLinearMemoryManager.cpp` — `Reset`：页面裁剪（保留最后 1 页） ✅ 已修复

## 7. 编译验证与测试

- [x] 7.1 运行 `build.py`，确认 BUILD SUCCESSFUL ✅ 24/24 编译通过（仅 pre-existing warnings）
- [x] 7.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30` ✅ Submit Count:1444 完成，ThreadManager.DLL teardown crash 为 pre-existing
