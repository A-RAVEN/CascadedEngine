## Context

Vulkan 1.3 后端，`TestSimpleTriangle` 单 render pass（1 color attachment，无 depth）。GPL feature 已启用，pipeline 4 个 library parts + link 全部成功。VUID-07904 已修复（vertex input lookup）。对抗验证发现剩余 3 个 validation error 归结为 **2 个根因**。

## Goals / Non-Goals

**Goals:**
- ~~消除 VUID-07904（vertex input location 不匹配）~~ ✅
- 消除 VUID-06055 + VUID-02684（同一根因：GPL library renderPass=NULL）
- ~~消除 VUID-09600 + UNASSIGNED-non-acquired-swapchain-image-used（同一根因：acquire semaphore 未 wait）~~
- 消除 VUID-09600 + UNASSIGNED（acquire semaphore 未 wait）
- 修复 command buffer 单例别名（所有 batch 共用一个 VkCommandBuffer，draw 被擦掉）

**Non-Goals:**
- 不修改 monolithic pipeline path
- 不引入 `VK_KHR_maintenance9` 或 `VK_EXT_extended_dynamic_state3`

## Decisions

### Decision 1: 修复 vertex input attribute lookup（VUID-07904）✅ 已完成

**根因**：`VulkanGraphExecutor.cpp:1420` 用 shader 反射的 semantic name（`"POSITION"` / `"COLOR"`）做 key 查 `batch.m_VertexInputDescs`，但该 map 的 key 是 stream name（`"TestVerticesInput"`），永远 miss → `vertexAttributes` 为空。

**修法**（已实现）：遍历所有 stream 按 `semanticName + sematicIndex` 匹配，binding index 按 stream name 分配。参照 D3D12 后端 `GPUPipelineInstance.cpp:14-35`。

### Decision 2: GPL library 传入实际 VkRenderPass（统一修复 VUID-06055 + VUID-02684）

**根因分析**（对抗验证确认）：

所有 4 个 GPL library 创建函数（`CreateVertexInputLibrary`、`CreatePreRasterizationLibrary`、`CreateFragmentLibrary`、`CreateFragmentOutputLibrary`）都用 value-initialized `vk::GraphicsPipelineCreateInfo{}`，**从不设 `createInfo.renderPass`** → 默认 VK_NULL_HANDLE。`CreateFragmentOutputLibrary` 还额外链了 `VkPipelineRenderingCreateInfo`（Decision 2 旧版添加的），把 library 强制设为 dynamic-rendering 模式。

GPL 规范：library 的 renderPass 是 fragment output interface state 的一部分，由 **library 定义**，link 时不能覆盖（"Parts specified in the pipeline must not overlap those defined by libraries"）。所以 `LinkPipeline` 设的 `createInfo.renderPass` 被忽略——linked pipeline 继承 library 的 NULL renderPass → dynamic-rendering pipeline。

draw 路径用 `cmdBuf.beginRenderPass()`（传统 render pass），不用 `vkCmdBeginRendering`（dynamic rendering）→ renderpass pipeline 与 dynamic-rendering pipeline 不兼容 → VUID-02684。

**VUID-06055 和 02684 是同一根因**：06055 的前提是 `renderPass == NULL`。传入实际 renderPass 后 06055 前提不成立（不需要 VkPipelineRenderingCreateInfo），linked pipeline 变为 renderpass pipeline（02684 消失）。

**~~旧修法：添加 VkPipelineRenderingCreateInfo~~**（已废弃）：这反而把 library 锁定为 dynamic-rendering 模式，制造了 02684。必须回退。

**正确修法**：
1. `CreateFragmentLibrary` 和 `CreateFragmentOutputLibrary` 添加 `vk::RenderPass renderPass` 参数
2. 在函数内设置 `createInfo.renderPass = renderPass;`（fragment shader 和 fragment output 两个 library part 都需要）
3. `CreateFragmentOutputLibrary` 中**删除** `VkPipelineRenderingCreateInfo` 相关代码（回退 task 2.1）
4. 调用方（`VulkanGraphExecutor.cpp` GPL path）传入 `renderPass`（已有 `GetOrCreateRenderPass(rpKey)` 的结果）
5. `LinkPipeline` 的 `createInfo.renderPass = renderPass` 保留（与 library 的 renderPass 一致，不冲突）

### Decision 3: 修复 acquire semaphore wait（统一修复 VUID-09600 + UNASSIGNED）

**根因分析**（对抗验证确认）：

barrier 本身**完全正确**——`PrepareBatchResourceBarriers` 为 backbuffer 生成了 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL barrier，在同一 `directCommandBuffer` 上，在 `beginRenderPass` 之前执行。

VUID-09600 的真正原因是 **acquire semaphore 未被 wait**：`AcquireNextImage(sync.acquireSemaphore)` 在 `CompileAndExecute` 入口调用（line 475），但 semaphore wait 仅在 `SubmitBatches` 的 `isLastBatch && hasFinalizePass` 路径添加（line 2230）。

**TestSimpleTriangle 的 3 个 batch**（深度追踪确认）：

| Batch | 内容 | isLastBatch | 当前 wait |
|---|---|---|---|
| 0 | vbuffer transfer upload | no | 无 |
| **1** | **swapchain barrier (UNDEFINED→COLOR_ATTACHMENT_OPTIMAL) + beginRenderPass + draw** | **no** | **无 ← bug** |
| 2 | finalize (COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC barrier) + presentSemaphore signal | yes | acquireSemaphore |

acquire wait 在 batch 2（submit #3），但 swapchain image 在 batch 1（submit #2）就被 barrier + draw 使用。

**修法**（精确方案）：
1. `SubmitBatches` 循环前预计算 `firstSwapchainBatch`：遍历 `m_ExecutionBatches`，找第一个 `batchRWStates.imageRWStates` 中含 `ImageHandle::ImageType::Backbuffer` 的 batch index
2. 将 `applyWindowSync` lambda 拆分为两个独立关注点：
   - `addAcquireWait`：条件 `(int)batchIdx == firstSwapchainBatch`（不再依赖 `isLastBatch`），push acquireSemaphore + waitStage `eColorAttachmentOutput`
   - `addPresentSignal`：条件不变（`isLastBatch && hasFinalizePass`），push presentSemaphore
3. 更新 3 条 submit 路径（Path A/B/C）的调用点
4. binary semaphore 只被 wait 一次（`firstSwapchainBatch` 唯一），不会 double-wait

### Decision 4: 修复 command buffer 单例别名（阻塞性 bug）

**根因**（深度追踪发现）：`VulkanCommandListManager::GraphicsCommand()`（VulkanCommandListManager.cpp:62-69）惰性分配**一个** `m_GraphicsCommand` 并永远返回同一 handle。executor 每个 batch 调用一次（line 1788），每个 batch 的 `begin(eOneTimeSubmit)` 隐式 reset → **擦掉前一个 batch 的录制内容**。

后果：
- 3 个 batch 的 `directCommandBuffer` 是**同一个** VkCommandBuffer
- 最终只包含 batch 2 的内容（finalize barrier）
- vertex upload（batch 0）和 **draw（batch 1）被擦掉，从未执行**
- 同一个 `eOneTimeSubmit` buffer 被提交 3 次 → 第一次完成后进入 invalid state

**这才是三角形画不出来的直接原因。** 即使修好 semaphore 和 renderPass，不修这个也画不出来。

**修法**：`GraphicsCommand()` 每次调用分配新 command buffer（从 pool allocate），在 `Reset()` 时统一 free。`ComputeCommand()` 同理。

## Risks / Trade-offs

- **[风险]** 传入 renderPass 后 PipelineLibraryCache 的 cache key 不含 renderPass handle → 不同 renderPass 的 pipeline 可能 cache 冲突 → **缓解**: 当前 cache key 已含 colorFormats/depthFormat/sampleCount，与 renderPass 的 attachment 配置一一对应；handle 不同但配置相同的 renderPass 是兼容的
- **[风险]** semaphore wait 修改可能影响多 batch / 多 window 场景 → **缓解**: 当前只有单 window 场景；修法通过识别 swapchain image 操作精确定位 wait 位置，不影响无 swapchain 操作的 batch
- **[风险]** `CreateFragmentLibrary` 和 `CreateFragmentOutputLibrary` 的 API 变更影响所有调用方 → **缓解**: 当前只有 `VulkanGraphExecutor.cpp` GPL path 一个调用方
- **[风险]** command buffer 改为每次分配可能增加 pool 碎片 → **缓解**: `Reset()` 统一 reset pool，每帧分配数量等于 batch 数（当前 3），开销可忽略
- **[风险]** compute fence 死锁 → **已纳入 scope**（task 5）：unconditional `waitForFences(computeFence)` 在无 compute 的 batch 后死锁，阻塞所有验证。修法：仅在当 batch 有 compute submit 时 wait
