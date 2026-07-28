## 1. 修复 vertex input attribute lookup（VUID-07904）✅

- [x] 1.1 `VulkanGraphExecutor.cpp`：将 semantic-name map lookup 改为遍历 stream 列表按 `semanticName + sematicIndex` 匹配
- [x] 1.2 `seenSlotKeys` 改为按 stream name 分配 binding index

## 2. GPL library 传入实际 VkRenderPass（VUID-06055 + VUID-02684）✅

- [x] 2.1 回退 `VulkanPipelineLibrary.cpp` `CreateFragmentOutputLibrary` 中的 `VkPipelineRenderingCreateInfo`
- [x] 2.2 `CreateFragmentLibrary` 添加 `vk::RenderPass renderPass` 参数
- [x] 2.3 `CreateFragmentOutputLibrary` 添加 `vk::RenderPass renderPass` 参数
- [x] 2.4 GPL path 调用点传入 `renderPass`

## 3. 修复 command buffer 单例别名 ✅

- [x] 3.1 `m_AllocatedCommandBuffers` 拆为 graphics/compute 两个 vector
- [x] 3.2 `GraphicsCommand()` 每次分配新 command buffer，返回 by value
- [x] 3.3 `ComputeCommand()` 同理
- [x] 3.4 `Reset()` 分 pool free + clear + resetCommandPool

## 4. 修复 acquire semaphore wait ✅

- [x] 4.1 预计算 `firstSwapchainBatch`
- [x] 4.2 拆分 `applyWindowSync` 为 `addAcquireWait` + `addPresentSignal`
- [x] 4.3 更新 Path A/B/C 调用点

## 5. 修复 compute fence 死锁 ✅

- [x] 5.1 `waitForFences(computeFence)` 改为仅在 `computeSubmitted` 时 wait
- [x] 5.2 `waitForFences(directFence)` 改为仅在 `directSubmitted` 时 wait

## 6. 修复 hash_combine 返回值丢弃 ✅

- [x] 6.1 `RenderBackend_Vulkan.cpp`：`GetOrCreateFramebuffer`/`GetOrCreateRenderPass`/`GetOrCreatePipelineLayout` 的 `cacore::hash_combine` 调用改为 `hash = cacore::hash_combine(hash, x);`
- [x] 6.2 `VulkanGraphExecutor.cpp`：`VulkanShaderResourceSet::Init` 同理
- [x] 6.3 `ShaderLibrary.h`：`VulkanDescriptorSetLayoutInfo::GetHash` 同理

## 7. 修复 frame 3 死锁 ✅

- [x] 7.1 `VulkanFrameManager.cpp` `Aquire`：移除 redundant fence wait（per-batch CPU 序列化已保证完成）

## 8. 修复 present semaphore 复用（VUID-vkQueueSubmit-pSignalSemaphores-00067）✅

- [x] 8.1 `VulkanWindowHandle.h`：添加 `uint32_t GetSwapchainImageCount() const { return (uint32_t)m_SwapchainImages.size(); }`
- [x] 8.2 `VulkanFrameManager.h/.cpp`：`WindowSync` vector 改为按 swapchain image count 扩展（不再是 per-window 1 对）。`EnsureWindowSync(uint32_t imageCount)` 签名改为接收 image count，grow-to-count。每个 frame context 持有 imageCount 对 semaphore
- [x] 8.3 `VulkanGraphExecutor.cpp` `CompileAndExecute`（line 461-479）：`EnsureWindowSync` 传入 `pWindow->GetSwapchainImageCount()`；acquire 后用 `acquiredImageIndex` 索引 semaphore 对
- [x] 8.4 `SubmitBatches` 的 `addAcquireWait` / `addPresentSignal`：**删除 windowCount for 循环**，改为从 graph backbuffers 获取 acquired image index（`graph.GetFinalizePass().m_PresentBackBuffers[w].GetWindowPtr<VulkanWindowHandle>()->GetCurrentImageIndex()`），仅推入该 image 的一对 semaphore（不能推入所有 image 的——unsignaled 的会 hang）
- [x] 8.5 `PresentWindows`（line ~2471）：`GetWindowSync(windowIdx)` 改为 `GetWindowSync(pWindow->GetCurrentImageIndex())`，确保 present wait 的 semaphore 与 SubmitBatches signal 的是同一个

## 9. 修复 teardown 销毁顺序 ✅

- [x] 9.1 `RenderBackend_Vulkan.cpp` `Release()`：在销毁 window handle 之前，先遍历 `m_FramebufferCache` 销毁所有 framebuffer（`device.destroyFramebuffer`），然后 clear cache

## 10. 验证

- [x] 10.1 `python build.py` 编译通过
- [x] 10.2 `--headless 6` 运行 `TestSimpleTriangle`，validation log 为空（0 bytes），test status=pass。⚠️ 进程退出时 ThreadManager.DLL 有 teardown crash（pre-existing，非 Vulkan 后端问题，exit code 仍为 0）
- [x] 10.3 回归测试：三个测试均 status=pass，6 帧渲染完成。发现的 VUID 均为 pre-existing（depth buffer 创建、descriptor type、multi-pass layout 等），与本次修改无关：TestTriangleWithConstantColor 有 08931/02251/01758/01209/02633；TestDoublePass 有 00337/08114/00344；TestTriangleWithImageBuffer 有 09600（纹理 layout，非 semaphore 问题）
- [ ] 10.4 **MANUAL**：窗口显示三角形
