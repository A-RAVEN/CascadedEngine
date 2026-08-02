## Why

2026-08-02 对 Vulkan 后端做了全量外部正确性审查（57 个 agent，workflow 对抗验证）：47 条 finding 中 **44 条 CONFIRMED**（13 error / 20 warning / 11 info）。审查按 CLAUDE.md 新规逐条联网核对官方文档（docs.vulkan.org / registry.khronos.org / VMA / vkdoc.net），全部引用 URL 经对抗者复查真实。这些是此前 5 轮审查都未发现的真实规范违反：swapchain 错误处理死代码、VMA virtual block 销毁顺序、compute 队列上使用 graphics 阶段 barrier、别名池绑定越界等——其中 4 条是当前测试崩溃（Submit 1444 后 ACCESS_VIOLATION）与 VMA -8 错误的候选根因。

## What Changes

- **swapchain/呈现错误处理**：acquireNextImageKHR/presentKHR 的 throwing 模式成功码白名单不含 OutOfDate/SurfaceLost/DeviceLost → 这些错误先抛异常、现有处理分支是死代码。改为使用非 throwing 重载或捕获对应异常，恢复 resize 路径
- **swapchain 创建校验**：imageUsage 与 supportedUsageFlags、compositeAlpha 与 supportedCompositeAlpha 求交集/校验，不再硬编码
- **队列族选择**：QueueContext 在单通用族设备上 compute/transfer 族索引保持 -1 → 非法 0xFFFFFFFF 传入 vkGetDeviceQueue/vkCreateCommandPool。改为：通用族可同时作为 compute/transfer 回退
- **VMA 别名池**：vmaDestroyVirtualBlock 前必须先释放全部 virtual allocation；AllocateAliasedPool/CommitVirtualAllocations 保留并传播 VmaAllocationInfo.offset；内存类型查询 VkPhysicalDeviceMemoryProperties 而非硬编码 0/1
- **别名池绑定**：late 注册 buffer 的 BindBufferToAliasedPool 在 offset ≥ 池大小时直接绑定 → 校验/拒绝或扩展池
- **纹理上传**：VkBufferImageCopy.aspectMask 必须单 bit（D/S 分离）；barrier newLayout 禁止 UNDEFINED；CUBE view 需要 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT；D16_UNORM bytesPerPixel=2；深度/模板拷贝需 graphics 队列族
- **GPL 管线库**：PreRasterization 缺 pMultisampleState；tess 阶段缺 pTessellationState；VertexInputStateManager 的 GPL 分支缺 VK_PIPELINE_CREATE_LIBRARY_BIT；特性启用前须 getFeatures2 验证 + 运行时支持检查
- **GraphExecutor barrier**：compute cmd buffer 上禁止使用 VERTEX/FRAGMENT 阶段（3 处）；transfer pass layout 状态跟踪与实际转换一致；upload barrier 同步域补齐 vertex/compute 阶段
- **描述符**：descriptorCount=elementCount 的布局只写 bindings[0] → 按元素数写入；depth-stencil sampled 描述符避免 SHADER_READ_ONLY_OPTIMAL 深度布局
- **图像视图**：2D view + layerCount>1 违反 VUID-imageViewType-04973 → 用 2D_ARRAY 或 REMAINING_ARRAY_LAYERS
- **生命周期**：pipeline 双销毁（Cache + m_CreatedPipelines）；window sync semaphore 索引（windowIdx vs imageIndex）统一；descriptor pool 重置前帧级 fence 等待
- **杂项**：移除死扩展 VK_KHR_MAINTENANCE_4；memcpy 非 4 倍数超写；VMA fallback 失败检查；移除未定义 Init 声明

## Capabilities

### New Capabilities

- `vulkan-device-init-correctness`: 设备/实例创建的外部 API 正确性——feature 开启前必须查询、扩展启用必须匹配实际使用

### Modified Capabilities

- `vulkan-window-handle`: swapchain 创建校验与 acquire/present 错误码处理要求变更
- `vulkan-resource-aliasing`: VMA virtual block 销毁顺序、allocation offset 传播、内存类型查询要求变更
- `vulkan-command-buffer-allocation`: 队列族索引合法性校验要求变更
- `vulkan-backend-alignment`: 纹理格式字节数/上传 staging 尺寸正确性要求变更
- `api-field-correctness`: 新增多个 VUID 合规要求（aspectMask 单 bit、layout 转换、image view 类型/兼容 bit、管线状态结构指针）
- `vulkan-descriptor-binding`: descriptorCount 与写入数量匹配、深度-模板采样布局要求变更
- `barrier-pipeline-efficiency`: compute cmd buffer 阶段支持、layout 状态跟踪一致性、同步域覆盖要求变更
- `vulkan-frame-manager`: 帧级 fence 等待后再重置 descriptor pool 的要求变更
- `vulkan-pipeline-layout`: GPL 运行时支持检查与 feature 验证要求变更

## Impact

- 文件（13 个）：RenderBackend_Vulkan.cpp/.h、VulkanWindowHandle.cpp、QueueContext.cpp/.h、VulkanTexture.cpp、VulkanBuffer.cpp、VulkanResourceAliasing.cpp、VulkanLinearMemoryManager.cpp、VulkanCommandListManager.cpp、VulkanFrameManager.cpp、VulkanPipelineLibrary.cpp、PipelineLibraryCache.cpp、VertexInputStateManager.cpp、VulkanGraphExecutor.cpp、VulkanGraphLocalResourceManager.cpp、VulkanResourceBindingInstance.cpp、ShaderImporter_Vulkan.cpp、FragmentOutputStateManager.h、GeometryShaderStateManager.h
- 依赖：无新增依赖；使用现有 Vulkan 1.3 + VK_EXT_graphics_pipeline_library + VMA
- 风险：barrier 阶段/状态机改动可能影响渲染正确性，需要 headless 多帧 + 非 headless 实测验证

### Non-goals

- 不修复 GPUBackendTester 崩溃本身（由 `fix-vulkan-test-crash` 追踪；本 change 修复的是审查发现的候选根因，二者独立推进）
- 不实现 VK_KHR_dynamic_rendering 真正的 renderPass=NULL 路径
- 不做 async frames-in-flight 重构（VulkanFrameManager 的 TODO 保持不变）
