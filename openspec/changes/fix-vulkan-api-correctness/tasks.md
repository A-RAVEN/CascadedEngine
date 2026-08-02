# Tasks — fix-vulkan-api-correctness

来源：2026-08-02 外部正确性审查（44 CONFIRMED findings）。每条任务标注 finding 编号（F#），与 Review Log 对应。

## 1. 设备初始化与扩展

- [ ] 1.1 [F1] RenderBackend_Vulkan.cpp：`vkCreateDevice` 前用 `vkGetPhysicalDeviceFeatures2` 查询 `graphicsPipelineLibrary`/`dynamicRendering`，物理设备选择基于 capability 检查（不再盲目 `front()`）；feature 不支持时回退 monolithic 并置 `m_PipelineLibrarySupported=false`
- [ ] 1.2 [F2] 从 `GetDeviceExtensionNames()` 移除 `VK_KHR_MAINTENANCE_4_EXTENSION_NAME`（Vulkan 1.3 已 promoted 到 core，无使用）
- [ ] 1.3 [F30] VertexInputStateManager 的 GPL 分支加运行时 `IsPipelineLibrarySupported()` 门控（不能只靠编译期常量 `VULKAN_SUPPORT_PIPELINE_LIBRARY`）

## 2. 队列族选择

- [ ] 2.1 [F7] QueueContext::InitQueueCreationInfo：先找独立 compute/transfer 族，找不到时回退 graphics 族索引；所有下游使用前断言 `≥0`
- [ ] 2.2 [F20/F21] VulkanCommandListManager::Init：createCommandPool 前验证 `queueFamilyIndex ≥ 0`，非法则失败并 `CA_LOG_ERR`（不携带 -1 调 Vulkan）

## 3. swapchain 与窗口同步

- [ ] 3.1 [F3/F4] VulkanWindowHandle.cpp:236/261：acquireNextImageKHR/presentKHR 改用非 throwing 重载（或捕获 `vk::OutOfDateKHRError` 等），使 OutOfDate/SurfaceLost/DeviceLost 分支实际可达
- [ ] 3.2 [F5/F6] swapchain 创建：`imageUsage` 与 `supportedUsageFlags` 求交集、`compositeAlpha` 与 `supportedCompositeAlpha` 校验回退
- [ ] 3.3 [F8] backbuffer 描述符格式用 `m_Format` 映射（不硬编码 B8G8R8A8_UNORM）
- [ ] 3.4 [F36] VulkanGraphExecutor：present 与 acquire 统一按窗口位置索引 `GetWindowSync`（不用 `GetCurrentImageIndex` 索引 flat 数组）

## 4. 纹理创建与上传

- [ ] 4.1 [F9] VulkanTexture::UploadData：`VkBufferImageCopy.aspectMask` 按 depth/stencil 分离传单 bit
- [ ] 4.2 [F10] cube map 纹理：`VkImageCreateInfo.flags` 加 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`
- [ ] 4.3 [F11] 上传后 `TransitionLayout`：`newLayout` 禁止 UNDEFINED（新纹理原布局为 UNDEFINED 时目标布局用明确值）
- [ ] 4.4 [F13] `bytesPerPixel` 修正 D16_UNORM=2 字节；staging 尺寸/bufferOffset/memcpy 长度同步修正
- [ ] 4.5 [F14] 深度/模板 aspect 拷贝：检查命令池队列族支持 GRAPHICS（或 maintenance1 扩展），否则拒绝/走 graphics 队列

## 5. VMA 别名池

- [ ] 5.1 [F15] `DestroyVirtualBlocks()`：先 `vmaVirtualFree` 全部活分配（或 `vmaClearVirtualBlock`），再 `vmaDestroyVirtualBlock`；`m_ActiveVirtualAllocs` 清理移到销毁前
- [ ] 5.2 [F16] `AllocateAliasedPool`/`CommitVirtualAllocations`：保存并传播 `VmaAllocationInfo.offset`，`bindBufferMemory`/`bindImageMemory` 用 `deviceMemory + offset`
- [ ] 5.3 [F17] `CreateVirtualBlocks`：用 `vkGetPhysicalDeviceMemoryProperties` 查询内存类型，不硬编码 0/1
- [ ] 5.4 [F39] `BindBufferToAliasedPool` late 路径：`alignedOffset >= GetTotalAliasedSize()` 时拒绝绑定 + `CA_LOG_ERR`
- [ ] 5.5 [F43] VMA standalone fallback（GraphLocalResourceManager Route B）：`AllocateBuffer` 失败检查，失败不存储空分配

## 6. GPL 管线库

- [ ] 6.1 [F23] `CreatePreRasterizationLibrary`：补 `pMultisampleState`（VUID-pRasterizationState-09039）
- [ ] 6.2 [F26] tessControl/tessEval stage 存在时补 `pTessellationState`（VUID-pStages-09022）
- [ ] 6.3 [F28] `CreateFragmentLibrary`：subpass 使用 depth/stencil attachment 时提供 `pDepthStencilState`（VUID-renderPass-09028）
- [ ] 6.4 [F27] `LinkPipeline`：校验 layout/renderPass/subpass 与库一致（VUID-flags-06612 等），不一致报错
- [ ] 6.5 [F24/F25] 管线双销毁修复：PipelineLibraryCache 只引用不销毁；LinkPipeline 失败路径调用方不再显式 destroy（统一由 `Release` 走 `m_CreatedPipelines`）
- [ ] 6.6 [F29] VertexInputStateManager GPL 分支：`VkGraphicsPipelineCreateInfo.flags` 加 `VK_PIPELINE_CREATE_LIBRARY_BIT_EXT`

## 7. GraphExecutor barrier 与同步

- [ ] 7.1 [F32] `ExecuteBarriers` compute acquire/release 屏障：stage mask 改为 compute 族支持阶段（去掉 VERTEX/FRAGMENT）
- [ ] 7.2 [F33] `uploadCBufferBarriers`：`dstStageMask` 改为 compute 族支持阶段
- [ ] 7.3 [F34] compute UAV 写入屏障：`dstStageMask` 去掉 `eFragmentShader`
- [ ] 7.4 [F35] transfer pass：`CollectResources` 状态跟踪与 `RecordTransferPass` 实际转换统一（实际转 `eShaderReadOnlyOptimal`）
- [ ] 7.5 [F38] buffer 上传 barrier：`dstStageMask` 补齐 `VERTEX_SHADER`/`COMPUTE_SHADER`；VulkanBuffer.cpp:169 的 post-copy barrier 弃用默认 `eNone`/`TOP_OF_PIPE`，显式指定（F12）
- [ ] 7.6 [F37] 顶点绑定索引一致性：`BuildPipelineStates` 与 `RecordRenderPass` 的顶点流绑定顺序统一（按属性位置排序，不用哈希迭代序）

## 8. 描述符

- [ ] 8.1 [F42] `BuildDescriptors`：数组绑定（elementCount>1）写入覆盖全部元素
- [ ] 8.2 [F41] depth-stencil 采样绑定：`imageLayout` 用 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`（或 GENERAL）+ aspect 单 bit

## 9. 图像视图与杂项

- [ ] 9.1 [F40] GraphLocalResourceManager 的 image view：多 layer 用 `2D_ARRAY` 或 `VK_REMAINING_ARRAY_LAYERS`；depth-only 格式 aspect 不含 stencil
- [ ] 9.2 [F31] 删除 FragmentOutputState::Init / GeometryShaderState::Init 未定义声明（及无定义调用）
- [ ] 9.3 [F44] ShaderImporter_Vulkan.cpp:414：memcpy 长度与目标缓冲匹配（按字节或 4 倍数校验）

## 10. 帧管理与 staging

- [ ] 10.1 [F22] VulkanFrameContext::Aquire：resetDescriptorPool/resetCommandPool 的 GPU 完成前提以注释/断言固化
- [ ] 10.2 [F18/F19] VulkanLinearMemoryManager：超大分配不保留永久空页；Reset 前提注释明确；分配失败返回失败

## 11. 构建与测试验证

- [ ] 11.1 [P] `python build.py` 编译通过（MSVC，无警告级别新增错误）
- [ ] 11.2 [P] headless 100 帧测试：`GPUBackendTester.exe --backend vulkan --test TestSimpleTriangle --headless N`（N=100+）无崩溃、无验证层错误
- [ ] 11.3 [P] 非 headless 测试：三角形正常渲染、resize 不崩溃（覆盖 F3/F4 的 OutOfDate 路径）

## 12. 审查与对抗验证（Review & Adversarial Verify）

- [ ] 12.1 开 workflow 对抗验证：逐条复查 44 条 finding 的修复（代码实况 + 官方文档 URL 复查，MCP 工具联网），输出 Review Log 写入本文件末尾 `## Review Log` 区域
- [ ] 12.2 验证层/VMA 校验复查：修复后运行带验证层的测试，确认无 VUID 报错（与修复前的 VUID 列表对比）
- [ ] 12.3 崩溃候选根因验证：F16/F20/F36/F39 修复后，`fix-vulkan-test-crash` 的崩溃与 VMA -8 是否消失（若消失，把证据写回该 change 的 tasks.md）

## Review Log

（待审查任务 12.1 完成后填写）
