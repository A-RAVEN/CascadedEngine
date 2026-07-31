# Tasks: 修复 Vulkan 审计剩余缺陷

**Change ID**: fix-vulkan-audit-remaining-findings
<!-- Audited 2026-07-31 against baseline (post vulkan-resource-aliasing-full): 18/19 STILL_PRESENT, 1 OBSOLETE -->

---

## 1. Barrier 与管线效率

- [ ] 1.1 `VulkanGraphExecutor.cpp` — `ExecuteBarriers`：收集所有 barrier 的 stageFlags 按位或得到最紧 src/dst mask；将 image+buffer barrier 合并为单次 `vkCmdPipelineBarrier`【D-32】
- [ ] 1.2 `VulkanGraphExecutor.cpp` — `RecordComputePass`：仅在有 UAV 写入的 dispatch 后注入 memory barrier（~line 2067）【D-46】
- [ ] 1.3 `VulkanGraphExecutor.cpp` — `PrepareBatchResourceBarriers` stateHaveGap 路径：同队列（`VK_QUEUE_FAMILY_IGNORED`）时仅创建 acquire barrier，跳过冗余 release（~line 1148）【D-53】
- [ ] 1.4 `VulkanGraphExecutor.cpp` — `CompileAndExecute`：`m_ComputeQueueFamily == -1` 时禁用所有 batch 的 asyncCompute 并输出警告（~line 340）【D-33】

## 2. 错误路径加固

- [ ] 2.1 `VulkanGraphExecutor.cpp` — `RecordRenderPass`：任一 `GetTextureView(attachment)` 返回 null 时跳过整个 render pass，而非构建不完整 attachment 列表（~line 1863）【D-31】
- [ ] 2.2 `VulkanCommandListManager.cpp` — `AllocateCommandBuffer`：空向量分支添加 `return vk::CommandBuffer{}` 防止无返回值 UB（~line 172）【D-34】
- [ ] 2.3 `RenderBackend_Vulkan.cpp` — `Init`：`#ifndef NDEBUG` 包裹 validation layer 启用；`createInstance` 外层 try-catch 防止 layer 缺失崩溃（~line 162-177）【D-48】
- [ ] 2.4 `VulkanMemoryManager.cpp` — `AllocateBuffer`/`AllocateImage`/`AllocateMemory`：失败路径显式将 out 参数置 `VK_NULL_HANDLE`（~line 50）【D-59】
- [ ] 2.5 `VulkanGraphExecutor.cpp` — `RecordBatchCommands`：`AllocUploadStagingBuffer` 返回空时 `CA_LOG_ERR` 记录错误（~line 1717）【D-64】
- [ ] 2.6 `RenderBackend_Vulkan.cpp` — `Init`：pipeline cache 回退 `createPipelineCache` 添加二次 try-catch（~line 246-251）【D-65】
- [ ] 2.7 `RenderBackend_Vulkan.cpp` — `ExecuteGraph`/`SubmitBatches`：`waitForFences` 使用有限超时（5s），检测 VK_TIMEOUT/VK_ERROR_DEVICE_LOST（~line 264）【D-68】

## 3. 内存与缓存清理

- [ ] 3.1 `VulkanResourceAliasing.cpp` — `ReplanWithRealAlignment`：结束前遍历所有 lifetime 重新计算 `m_TotalUnaliasedSize`（~line 28）【D-37】
- [ ] 3.2 `VulkanGraphExecutor.cpp` — `BuildPipelineStates` GPL 路径：`LinkPipeline` 失败时销毁本次创建的 4 个 library part（~line 1570）【D-49】
- [ ] 3.3 `RenderBackend_Vulkan.cpp` — swapchain 重建时清空 `m_FramebufferCache`（~line 341）【D-50】
- [ ] 3.4 `VulkanLinearMemoryManager.cpp` — `AllocatePage`：`m_Pages.push_back(page)` 包在 try-catch 中，失败时 vmaDestroyBuffer（~line 68）【D-58】

## 4. API 字段正确性

- [ ] 4.1 `VulkanResourceState.h` — 拆分 `InitializedState()` 为 `InitializedImageState()` 和 `InitializedBufferState()`；更新 `PrepareBatchResourceBarriers` 中 buffer 路径调用点（~line 36）【D-54】
- [ ] 4.2 `Interface/RenderInterface/header/TextureSampler.h` — `Create` 中 `desc.integerFormat = integerFormat`（修复被忽略的参数）（~line 53）【D-62】

## 5. 编译验证

- [ ] 5.1 运行 `build.py`，确认 BUILD SUCCESSFUL

## 6. 审查（Review & Adversarial Verify）

- [ ] 6.1 对抗验证：对全部 18 个修复点做正确性/完整性/诚实性审查，开 workflow ≥3 票对抗验证，新发现的问题追加 `[AUDIT]` task。循环直到无新问题或 3 轮。结果写入 `## Review Log`

---

## Review Log
