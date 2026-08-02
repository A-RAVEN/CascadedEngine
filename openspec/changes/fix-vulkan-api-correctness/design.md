## Context

2026-08-02 外部正确性审查（57 agent / 47 finding / 44 CONFIRMED）暴露了 Vulkan 后端的系统性问题。多数问题属"外部 API 使用前提不满足"：错误码语义误解（throwing 模式白名单）、未做 capability/feature 查询就硬编码、状态跟踪与真实 GPU 状态脱节、生命周期双持有。当前后端可跑通单帧三角形测试，但多帧/非 headless 会触发崩溃与 VMA -8，与其中 4 条候选根因强相关。

约束：无新依赖；Vulkan 1.3 + VK_EXT_graphics_pipeline_library + VMA；不改变公共渲染接口（RenderInterface）。

## Goals / Non-Goals

**Goals:**
- 44 条 CONFIRMED finding 全部修复，每条修复以官方文档 VUID/前提为准绳
- 消除测试崩溃的候选根因（别名池越界绑定、semaphore 索引错配、队列族 -1）
- 修复后 headless 100 帧 + 非 headless 实测通过
- 审查结论可追溯到 Review Log 中的 finding 编号

**Non-Goals:**
- 不修 GPUBackendTester 崩溃本身（`fix-vulkan-test-crash` 独立追踪）
- 不引入 async frames-in-flight（VulkanFrameManager TODO 保留）
- 不实现 dynamic rendering 真路径
- 不做大规模重构（每个修复保持最小 diff）

## Decisions

### D1: swapchain 错误处理改为非 throwing 重载

**决策**：acquireNextImageKHR/presentKHR 改用 5 参/返回 result 的非 throwing 重载（或包 try/catch 捕获 vk::OutOfDateKHRError 等），使 eErrorOutOfDateKHR/eErrorSurfaceLostKHR/eErrorDeviceLost 以返回值可见，恢复 resize 路径。
**理由**：throwing 模式下成功码白名单（{eSuccess, eTimeout, eNotReady, eSuboptimalKHR}）不含这三个错误码，现有 236-246/261-268 行的分支是死代码（[vkAcquireNextImageKHR](https://registry.khronos.org/vulkan/specs/latest/man/html/vkAcquireNextImageKHR.html) 返回语义）。
**备选**：全局定义 VULKAN_HPP_NO_EXCEPTIONS —— 影响面太大，否决。

### D2: 队列族选择：通用族回退

**决策**：QueueContext::InitQueueCreationInfo 改为：先找独立 compute/transfer 族；找不到时 graphics 族回退为 compute/transfer。所有使用 m_*QueueFamilyIndex 的下游（createCommandPool、getQueue、VMA pool）在使用前断言 ≥0。
**理由**：单通用族设备（Intel 核显、多数独显）上 -1 被隐式转 uint32 变成 0xFFFFFFFF 传入 [vkGetDeviceQueue](https://registry.khronos.org/vulkan/specs/latest/man/html/vkGetDeviceQueue.html) → 非法队列索引，是崩溃候选。
**备选**：拒绝单族设备 —— 否决，测试机就是核显。

### D3: VMA 别名池：offset 全链路传播 + 销毁顺序

**决策**：
1. AllocateAliasedPool/CommitVirtualAllocations 保存 VmaAllocationInfo.offset（`deviceMemory + offset` 而非 `deviceMemory + 0`），消费者 bindBufferMemory/bindImageMemory 用同一 offset；
2. DestroyVirtualBlocks 先遍历 m_ActiveVirtualAllocs 做 vmaVirtualFree 再 vmaDestroyVirtualBlock（[VMA virtual allocator 文档](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/virtual_allocator.html)要求）；
3. 内存类型改为查询 VkPhysicalDeviceMemoryProperties 后按 deviceLocal+可映射偏好选择，不再硬编码 0/1。
**理由**：VMA 文档明确 "Destroying a virtual block is only allowed after all virtual allocations within it are freed"；offset 丢失导致别名池内所有资源 bind 到同一地址互相覆盖。

### D4: 别名池绑定越界：BindBufferToAliasedPool 校验

**决策**：late 注册（池已分配后 AddBuffer）路径先计算 alignedOffset，若 ≥ 池总大小则拒绝并报错（CA_LOG_ERR + 返回失败），不再盲目 bindBufferMemory。
**理由**：绑定 offset ≥ 池大小是 GPU 地址越界，直接产生设备崩溃/不可预测行为。
**备选**：动态扩展池 —— 引入 per-batch 重新规划复杂度，超出本 change 范围。

### D5: 纹理上传：aspectMask 单 bit + layout 修复

**决策**：VkBufferImageCopy.imageSubresource.aspectMask 按 depth/stencil 分离传单 bit（VUID-vkCmdCopyBufferToImage-aspectMask-09103）；上传后 barrier 的 newLayout 用目标布局而非原 layout（新纹理 UNDEFINED 时禁止 newLayout=UNDEFINED）；CUBE 视图创建前给 image flags 加 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT；D16_UNORM bytesPerPixel 修正为 2。
**理由**：全部有明确 VUID 或格式表支撑（[copies 章节](https://docs.vulkan.org/spec/latest/chapters/copies.html)、[VkImageMemoryBarrier](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html)）。

### D6: GPL 管线库：VUID 补全 + 运行时验证

**决策**：
1. CreatePreRasterizationLibrary 补 pMultisampleState（VUID-pRasterizationState-09039）、tess 时补 pTessellationState（VUID-pStages-09022）；
2. VertexInputStateManager GPL 分支加 VK_PIPELINE_CREATE_LIBRARY_BIT_EXT；
3. GPL 分支同时受运行时 IsPipelineLibrarySupported() 门控，编译期常量不足；
4. RenderBackend 初始化先 getFeatures2 查询 graphicsPipelineLibrary，不支持则回退 monolithic（当前已有 monolithic 路径）。
**理由**：规范明确 library 管线 flags 必须含 eLibraryKHR（VUID-VkGraphicsPipelineCreateInfo-graphicsPipelineLibrary-06606 等）。

### D7: compute cmd buffer 上的 barrier 阶段修正

**决策**：3 处 compute cmd buffer 记录的 barrier（ExecuteBarriers 的 compute acquire/release、uploadCBufferBarriers、compute UAV barrier）的 stageMask 改为 COMPUTE_SHADER|TRANSFER|HOST 等 compute 族合法阶段，删除 VERTEX/FRAGMENT。
**理由**：compute-only 队列族不支持 VERTEX_SHADER/FRAGMENT_SHADER 阶段（[vkCmdPipelineBarrier](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html) stage 支持要求），违反即 UB/验证错误。
**备选**：把这些 barrier 挪到 graphics 队列 —— 破坏现有 async compute 结构，否决。

### D8: transfer pass layout 状态跟踪统一

**决策**：CollectResources 对 transfer pass 图像的目标状态改为 eShaderReadOnlyOptimal（与 RecordTransferPass 实际 inline 转换一致），或 RecordTransferPass 转换回 eTransferDstOptimal —— 二者取其一，以"状态跟踪 = 真实 layout"为唯一标准。
**理由**：状态机与真实 GPU layout 脱节会让后续 barrier 按错误 oldLayout 执行（验证层报 layout mismatch）。

### D9: pipeline 生命周期：单一所有权

**决策**：PipelineLibraryCache 不再持有从 VulkanPipelineLibrary 返回的 pipeline 句柄的所有权（只缓存指针/引用），销毁统一由 VulkanPipelineLibrary::Release（m_CreatedPipelines）负责；LinkPipeline 失败路径由调用方不显式 destroy，交给 Release。
**理由**：同一句柄被 destroyPipeline 两次是未定义行为（[vkDestroyPipeline](https://registry.khronos.org/vulkan/specs/latest/man/html/vkDestroyPipeline.html)）。

### D10: window sync semaphore 索引统一

**决策**：present 与 acquire 统一按窗口位置（windowIdx）索引 GetWindowSync，不再用 GetCurrentImageIndex 索引 flat 数组；m_WindowSyncs 语义改为"每窗口一套 sync"。
**理由**：imageIndex 在 multi-image 下与 flat 数组索引语义不一致，导致 semaphore 错配/死锁。

### D11: 描述符写入数量匹配 + depth-stencil 采样布局

**决策**：
1. BuildDescriptors 对 elementCount>1 的绑定按元素逐个写 descriptorCount=1 或一次性写 descriptorCount=elementCount（与 layout 声明一致）；
2. depth-stencil 格式的 sampled 绑定：view 的 aspectMask 只含对应单 bit（depth 或 stencil，随 layout），imageLayout 用 SHADER_READ_ONLY_OPTIMAL 仍不合法时改为 GENERAL 并检查格式的 sampled 支持。
**理由**：descriptorCount 声明与写入不一致违反 [VkWriteDescriptorSet](https://vkdoc.net/man/VkWriteDescriptorSet) 要求；深度采样布局合法性依 VUID-VkDescriptorImageInfo-imageLayout-00344。

### D12: 杂项清理

- VK_KHR_MAINTENANCE_4 从设备扩展列表移除（1.3 已 core）
- memcpy 用 size/4 或按字节拷贝并检查 4 倍数（ShaderImporter_Vulkan.cpp:414）
- VMA standalone fallback 分配失败检查
- 删除 FragmentOutputState::Init / GeometryShaderState::Init 死声明
- backbuffer 描述符格式用 m_Format 映射
- 上传 barrier 的 dstAccess/dstStage 用显式 mask（VulkanBuffer.cpp:169）
- 2D view layerCount 用 VK_REMAINING_ARRAY_LAYERS 或 2D_ARRAY（VUID-imageViewType-04973）
- 上传 sync scope 补 vertex/compute（VulkanGraphExecutor.cpp:2242）
- frame 级 fence 等待后重置 pool：保持现状（SubmitBatches 已串行化）但把前提写成注释/断言

## Risks / Trade-offs

- [barrier 阶段改动可能引入渲染错误] → 修复后 headless 100 帧 + 非 headless 实测验证；Review Log 逐条对账
- [offset 传播改动触及别名池全链路] → 最小 diff：只加 offset 字段传递，不改分配算法
- [queue 族回退改变队列使用] → transfer/compute 共用 graphics 族时保持现有提交路径不变，仅修索引
- [描述符写入改动影响 shader 数组绑定] → 对照 ShaderImporter 的 elementCount 推导逻辑测试

## Migration Plan

1. 按 tasks.md 分节实施，每节完成即 `python build.py` 编译验证
2. 全部完成后再统一跑 GPUBackendTester（headless 100 帧 + 非 headless）
3. 审查任务（workflow 对抗验证）验收全部 44 条 finding 的修复与文档 URL 复查

## Open Questions

- 无（44 条 finding 的修复路径均已确定；实施中发现偏差时回到 tasks 追加 [AUDIT] 任务）
