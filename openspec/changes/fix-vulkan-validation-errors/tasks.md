## 1. 修复 vertex input attribute lookup（VUID-07904）✅

- [x] 1.1 `VulkanGraphExecutor.cpp`：将 semantic-name map lookup 改为遍历 stream 列表按 `semanticName + sematicIndex` 匹配
- [x] 1.2 `seenSlotKeys` 改为按 stream name 分配 binding index

## 2. GPL library 传入实际 VkRenderPass（VUID-06055 + VUID-02684）

- [x] 2.1 回退 `VulkanPipelineLibrary.cpp` `CreateFragmentOutputLibrary`：删除 line 177-180（`colorFormats` vector + `renderingInfo` 声明和赋值）和 line 190-192（pNext 链修改），保留 line 193 `createInfo.pNext = &libraryInfo`（已存在）
- [x] 2.2 `VulkanPipelineLibrary.h` + `.cpp`：`CreateFragmentLibrary` 签名在 `cache` 参数前插入 `vk::RenderPass renderPass`（不设默认值），函数内 `createInfo.renderPass = renderPass;`。注意 .h 声明和 .cpp 定义都要改
- [x] 2.3 `VulkanPipelineLibrary.h` + `.cpp`：`CreateFragmentOutputLibrary` 同理，在 `cache` 参数前插入 `vk::RenderPass renderPass`
- [x] 2.4 `VulkanGraphExecutor.cpp` GPL path 调用点（~line 1595-1598）：传入 `renderPass`（line 1534 的 `GetOrCreateRenderPass(rpKey)` 结果）

## 3. 修复 command buffer 单例别名（阻塞性：draw 被擦掉）

- [x] 3.1 `VulkanCommandListManager.h`：`m_AllocatedCommandBuffers` 改为存储 pool+buffer 对（如 `castl::vector<castl::pair<vk::CommandPool, vk::CommandBuffer>>`），或拆为 `m_AllocatedGraphicsCmdBufs` / `m_AllocatedComputeCmdBufs` 两个 vector
- [x] 3.2 `VulkanCommandListManager.cpp` `GraphicsCommand()`（line 62-69）：每次调用从 `m_GraphicsPool` 分配新 command buffer，记录到 graphics vector。返回值改为 by value（避免 vector realloc 导致引用失效）
- [x] 3.3 `ComputeCommand()`（line 71-78）：同理，从 `m_ComputePool` 分配，记录到 compute vector
- [x] 3.4 `Reset()`（line 101-119）：分别 free graphics/compute vector 中的 buffer（从对应 pool），clear vector，然后 resetCommandPool

## 4. 修复 acquire semaphore wait（VUID-09600 + UNASSIGNED）

注意：必须在 section 3 完成后才能验证（singleton cmdbuf 下 batch 1 的 submit 内容是 batch 2 的）

- [x] 4.1 `VulkanGraphExecutor.cpp` `SubmitBatches` 循环前（~line 2244 后）：预计算 `firstSwapchainBatch` —— 遍历 `m_ExecutionBatches`，找第一个 `batchRWStates.imageRWStates` 中含 `ImageHandle::ImageType::Backbuffer` 的 batch index
- [x] 4.2 将 `applyWindowSync` lambda（line 2225-2244）拆分为：`addAcquireWait`（条件 `(int)batchIdx == firstSwapchainBatch`，append acquireSemaphore + waitStage `eColorAttachmentOutput` 到已有 vector）和 `addPresentSignal`（条件不变 `isLastBatch && hasFinalizePass`，append presentSemaphore）
- [x] 4.3 更新 3 条 submit 路径的调用点：Path A（line 2272-2275，lambda 调用）、Path B（line 2311-2321，**inline 代码块**不是 lambda 调用，注意保留 `crossQueueSemaphore` 在 waitSems 首位）、Path C（line 2352，lambda 调用）

## 5. 修复 compute fence 死锁（阻塞所有验证）

- [x] 5.1 `VulkanGraphExecutor.cpp` `SubmitBatches` per-batch sync block（line 2360-2374）：将 unconditional `waitForFences(computeFence)` 改为仅在当 batch 有 compute submit 时 wait+reset（添加 `bool computeSubmittedThisBatch` 标志，在 compute submit 路径 A/B/C 中设置）
- [x] 5.2 同理检查 `waitForFences(directFence)`：确认 direct fence 在当 batch 有 direct submit 时才 wait（当前 path C 总是有 direct submit 所以没问题，但 path A/B 需确认）

## 6. 验证

- [ ] 6.1 `python build.py` 编译通过
- [ ] 6.2 headless 模式（`--headless 3`）运行 `TestSimpleTriangle`，grep VUID 无报错（注意：修完 command buffer 后 VUID 集合可能变化，以实际输出为准）
- [ ] 6.3 运行 `TestTriangleWithConstantColor` / `TestDoublePass` / `TestTriangleWithImageBuffer` headless 各 3 帧，无新增 VUID（回归测试）
- [ ] 6.4 **MANUAL**：窗口显示三角形
