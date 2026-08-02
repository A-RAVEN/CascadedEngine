# vkCmdPipelineBarrier(3)

## Name

`vkCmdPipelineBarrier` - Insert a memory dependency

## C Specification

To record a pipeline barrier, call:

> This functionality is superseded by vkCmdPipelineBarrier2. See Legacy Functionality for more information.

```c
// Provided by VK_VERSION_1_0
void vkCmdPipelineBarrier(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers);
```

## Parameters

- `commandBuffer` — 命令被记录到的 command buffer。
- `srcStageMask` — `VkPipelineStageFlagBits` 位掩码，指定 source stages。
- `dstStageMask` — `VkPipelineStageFlagBits` 位掩码，指定 destination stages。
- `dependencyFlags` — `VkDependencyFlagBits` 位掩码，指定 execution 与 memory dependencies 的形成方式。
- `memoryBarrierCount` — `pMemoryBarriers` 数组的长度。
- `pMemoryBarriers` — 指向 `VkMemoryBarrier` 结构数组的指针。
- `bufferMemoryBarrierCount` — `pBufferMemoryBarriers` 数组的长度。
- `pBufferMemoryBarriers` — 指向 `VkBufferMemoryBarrier` 结构数组的指针。
- `imageMemoryBarrierCount` — `pImageMemoryBarriers` 数组的长度。
- `pImageMemoryBarriers` — 指向 `VkImageMemoryBarrier` 结构数组的指针。

## Description

`vkCmdPipelineBarrier` 与 `vkCmdPipelineBarrier2` 行为几乎相同，区别在于 scopes 和 barriers 直接作为参数传入，而非由 `VkDependencyInfo` 定义。

当 `vkCmdPipelineBarrier` 被提交到队列时，它在同一队列中先于它提交的命令与后于它提交的命令之间定义一个 memory dependency。

- 若在 render pass instance 之外记录：第一个 synchronization scope 包含 submission order 中更早出现的所有命令；若在 render pass instance 内记录：仅包含同一 subpass 内更早出现的命令。两种情况下的 scope 均限于 `srcStageMask` 指定的 pipeline stages。
- 若在 render pass instance 之外记录：第二个 synchronization scope 包含 submission order 中更晚出现的所有命令；若在 render pass instance 内记录：仅包含同一 subpass 内更晚出现的命令。两种情况下的 scope 均限于 `dstStageMask` 指定的 pipeline stages。
- 第一个 access scope 限于 `srcStageMask` 指定的 stages，且仅包括 `pMemoryBarriers`、`pBufferMemoryBarriers`、`pImageMemoryBarriers` 各元素定义的第一 access scopes。若未指定任何 memory barrier，则第一 access scope 不含任何访问。
- 第二个 access scope 限于 `dstStageMask` 指定的 stages，且仅包括各 barrier 元素定义的第二 access scopes。若未指定任何 memory barrier，则第二 access scope 不含任何访问。
- 若 `dependencyFlags` 含 `VK_DEPENDENCY_BY_REGION_BIT`，framebuffer-space pipeline stages 之间的依赖为 framebuffer-local，否则为 framebuffer-global。

## Valid Usage

- **VUID-vkCmdPipelineBarrier-srcStageMask-04090** — If the `geometryShader` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04091** — If the `tessellationShader` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT` or `VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04092** — If the `conditionalRendering` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04093** — If the `fragmentDensityMap` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04094** — If the `transformFeedback` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04095** — If the `meshShader` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-04096** — If the `taskShader` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-srcStageMask-07318** — If neither of the `shadingRateImage` or the `attachmentFragmentShadingRate` features are enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-srcStageMask-03937** — If the `synchronization2` feature is not enabled, `srcStageMask` **must** not be `0`
- **VUID-vkCmdPipelineBarrier-srcStageMask-07949** — If neither the `VK_NV_ray_tracing` extension or the `rayTracingPipeline` feature are enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-srcStageMask-10754** — If the `accelerationStructure` feature is not enabled, `srcStageMask` **must** not contain `VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-srcAccessMask-06257** — If the `rayQuery` feature is not enabled and a memory barrier `srcAccessMask` includes `VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR`, `srcStageMask` **must** include `VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`, `VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`, or `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04090** — If the `geometryShader` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04091** — If the `tessellationShader` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT` or `VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04092** — If the `conditionalRendering` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04093** — If the `fragmentDensityMap` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04094** — If the `transformFeedback` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04095** — If the `meshShader` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-04096** — If the `taskShader` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT`
- **VUID-vkCmdPipelineBarrier-dstStageMask-07318** — If neither of the `shadingRateImage` or the `attachmentFragmentShadingRate` features are enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-dstStageMask-03937** — If the `synchronization2` feature is not enabled, `dstStageMask` **must** not be `0`
- **VUID-vkCmdPipelineBarrier-dstStageMask-07949** — If neither the `VK_NV_ray_tracing` extension or the `rayTracingPipeline` feature are enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-dstStageMask-10754** — If the `accelerationStructure` feature is not enabled, `dstStageMask` **must** not contain `VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`
- **VUID-vkCmdPipelineBarrier-dstAccessMask-06257** — If the `rayQuery` feature is not enabled and a memory barrier `dstAccessMask` includes `VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR`, `dstStageMask` **must** include `VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`, `VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`, or `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`
- **VUID-vkCmdPipelineBarrier-srcAccessMask-02815** — The `srcAccessMask` member of each element of `pMemoryBarriers` **must** only include access flags supported by one or more pipeline stages in `srcStageMask`, per the table of supported access types
- **VUID-vkCmdPipelineBarrier-dstAccessMask-02816** — The `dstAccessMask` member of each element of `pMemoryBarriers` **must** only include access flags supported by one or more pipeline stages in `dstStageMask`, per the table of supported access types
- **VUID-vkCmdPipelineBarrier-pBufferMemoryBarriers-02817** — For each `pBufferMemoryBarriers` element, if its `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are equal, or if `srcQueueFamilyIndex` equals the queue family index used to create the command pool that `commandBuffer` was allocated from, then its `srcAccessMask` **must** only contain access flags supported by one or more pipeline stages in `srcStageMask`
- **VUID-vkCmdPipelineBarrier-pBufferMemoryBarriers-02818** — For each `pBufferMemoryBarriers` element, if its `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are equal, or if `dstQueueFamilyIndex` equals the queue family index used to create the command pool that `commandBuffer` was allocated from, then its `dstAccessMask` **must** only contain access flags supported by one or more pipeline stages in `dstStageMask`
- **VUID-vkCmdPipelineBarrier-pImageMemoryBarriers-02819** — For each `pImageMemoryBarriers` element, if its `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are equal, or if `srcQueueFamilyIndex` equals the queue family index used to create the command pool that `commandBuffer` was allocated from, then its `srcAccessMask` **must** only contain access flags supported by one or more pipeline stages in `srcStageMask`
- **VUID-vkCmdPipelineBarrier-pImageMemoryBarriers-02820** — For each `pImageMemoryBarriers` element, if its `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are equal, or if `dstQueueFamilyIndex` equals the queue family index used to create the command pool that `commandBuffer` was allocated from, then its `dstAccessMask` **must** only contain access flags supported by one or more pipeline stages in `dstStageMask`
- **VUID-vkCmdPipelineBarrier-image-09373** — If called within a render pass instance using a `VkRenderPass` object, and the `image` member of any image memory barrier is a color resolve attachment, the corresponding color attachment **must** be `VK_ATTACHMENT_UNUSED`
- **VUID-vkCmdPipelineBarrier-image-09374** — If called within a render pass instance using a `VkRenderPass` object, and the `image` member of any image memory barrier is a color resolve attachment, it **must** have been created with a non-zero `VkExternalFormatANDROID::externalFormat` value
- **VUID-vkCmdPipelineBarrier-oldLayout-01181** — If called within a render pass instance, the `oldLayout` and `newLayout` members of any image memory barrier included in this command **must** be equal
- **VUID-vkCmdPipelineBarrier-srcQueueFamilyIndex-01182** — If called within a render pass instance, the `srcQueueFamilyIndex` and `dstQueueFamilyIndex` members of any memory barrier included in this command **must** be equal
- **VUID-vkCmdPipelineBarrier-None-07889** — If called within a render pass instance using a `VkRenderPass` object, the render pass **must** have been created with at least one subpass dependency expressing a dependency from the current subpass to itself, not including `VK_DEPENDENCY_BY_REGION_BIT` if this command does not, not including `VK_DEPENDENCY_VIEW_LOCAL_BIT` if this command does not, and with synchronization and access scopes that are all supersets of the scopes defined in this command
- **VUID-vkCmdPipelineBarrier-bufferMemoryBarrierCount-01178** — If called within a render pass instance using a `VkRenderPass` object, it **must** not include any buffer memory barriers
- **VUID-vkCmdPipelineBarrier-image-04073** — If called within a render pass instance using a `VkRenderPass` object, the `image` member of any image memory barrier included in this command **must** be an attachment used in the current subpass both as an input attachment, and as either a color, color resolve, or depth/stencil attachment
- **VUID-vkCmdPipelineBarrier-None-07890** — If called within a render pass instance, and the source stage masks of any memory barriers include framebuffer-space stages, destination stage masks of all memory barriers **must** only include framebuffer-space stages
- **VUID-vkCmdPipelineBarrier-dependencyFlags-07891** — If called within a render pass instance, and the source stage masks of any memory barriers include framebuffer-space stages, then `dependencyFlags` **must** include `VK_DEPENDENCY_BY_REGION_BIT`
- **VUID-vkCmdPipelineBarrier-None-07892** — If called within a render pass instance, the source and destination stage masks of any memory barriers **must** only include graphics pipeline stages
- **VUID-vkCmdPipelineBarrier-dependencyFlags-01186** — If called outside of a render pass instance, the dependency flags **must** not include `VK_DEPENDENCY_VIEW_LOCAL_BIT`
- **VUID-vkCmdPipelineBarrier-None-07893** — If called inside a render pass instance, and there is more than one view in the current subpass, dependency flags **must** include `VK_DEPENDENCY_VIEW_LOCAL_BIT`
- **VUID-vkCmdPipelineBarrier-None-09553** — If none of the `shaderTileImageColorReadAccess`, `shaderTileImageStencilReadAccess`, or `shaderTileImageDepthReadAccess` features are enabled, and the `dynamicRenderingLocalRead` feature is not enabled, `vkCmdPipelineBarrier` **must** not be called within a render pass instance started with `vkCmdBeginRendering`
- **VUID-vkCmdPipelineBarrier-None-09554** — If the `dynamicRenderingLocalRead` feature is not enabled, and `vkCmdPipelineBarrier` is called within a render pass instance started with `vkCmdBeginRendering`, there **must** be no buffer or image memory barriers specified by this command
- **VUID-vkCmdPipelineBarrier-None-09586** — If the `dynamicRenderingLocalRead` feature is not enabled, and `vkCmdPipelineBarrier` is called within a render pass instance started with `vkCmdBeginRendering`, memory barriers specified by this command **must** only include `VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT`, `VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT`, `VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT`, or `VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` in their access masks
- **VUID-vkCmdPipelineBarrier-image-09555** — If called within a render pass instance started with `vkCmdBeginRendering`, and the `image` member of any image memory barrier is used as an attachment in the current render pass instance, it **must** be in the `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ` or `VK_IMAGE_LAYOUT_GENERAL` layout
- **VUID-vkCmdPipelineBarrier-srcStageMask-09556** — If called within a render pass instance started with `vkCmdBeginRendering`, this command **must** only specify framebuffer-space stages in `srcStageMask` and `dstStageMask`
- **VUID-vkCmdPipelineBarrier-oldLayout-10758** — If called within a render pass instance using a `VkRenderPass` object, the `oldLayout` member of any image memory barrier included in this command **must** be equal to the layout that the corresponding attachment uses during the subpass
- **VUID-vkCmdPipelineBarrier-oldLayout-10759** — If called within a render pass instance started with `vkCmdBeginRendering`, the `oldLayout` member of any image memory barrier included in this command **must** be equal to the layout that the corresponding attachment uses during the render pass instance
- **VUID-vkCmdPipelineBarrier-srcStageMask-06461** — Any pipeline stage included in `srcStageMask` **must** be supported by the capabilities of the queue family specified by the `queueFamilyIndex` member of the `VkCommandPoolCreateInfo` structure used to create the `VkCommandPool` that `commandBuffer` was allocated from
- **VUID-vkCmdPipelineBarrier-dstStageMask-06462** — Any pipeline stage included in `dstStageMask` **must** be supported by the capabilities of the queue family specified by the `queueFamilyIndex` member of the `VkCommandPoolCreateInfo` structure used to create the `VkCommandPool` that `commandBuffer` was allocated from
- **VUID-vkCmdPipelineBarrier-srcStageMask-09633** — If either `srcStageMask` or `dstStageMask` includes `VK_PIPELINE_STAGE_HOST_BIT`, for each element of `pImageMemoryBarriers`, `srcQueueFamilyIndex` and `dstQueueFamilyIndex` **must** be equal
- **VUID-vkCmdPipelineBarrier-srcStageMask-09634** — If either `srcStageMask` or `dstStageMask` includes `VK_PIPELINE_STAGE_HOST_BIT`, for each element of `pBufferMemoryBarriers`, `srcQueueFamilyIndex` and `dstQueueFamilyIndex` **must** be equal
- **VUID-vkCmdPipelineBarrier-srcQueueFamilyIndex-10388** — If a buffer or image memory barrier specifies a queue family ownership transfer operation, either the `srcQueueFamilyIndex` or `dstQueueFamilyIndex` member and the queue family index used to create the command pool that `commandBuffer` was allocated from **must** be equal
- **VUID-vkCmdPipelineBarrier-maintenance8-10206** — If the `maintenance8` feature is not enabled, `dependencyFlags` **must** not include `VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR`

## Valid Usage (Implicit)

- **VUID-vkCmdPipelineBarrier-commandBuffer-parameter** — `commandBuffer` **must** be a valid `VkCommandBuffer` handle
- **VUID-vkCmdPipelineBarrier-srcStageMask-parameter** — `srcStageMask` **must** be a valid combination of `VkPipelineStageFlagBits` values
- **VUID-vkCmdPipelineBarrier-dstStageMask-parameter** — `dstStageMask` **must** be a valid combination of `VkPipelineStageFlagBits` values
- **VUID-vkCmdPipelineBarrier-dependencyFlags-parameter** — `dependencyFlags` **must** be a valid combination of `VkDependencyFlagBits` values
- **VUID-vkCmdPipelineBarrier-pMemoryBarriers-parameter** — If `memoryBarrierCount` is not `0`, `pMemoryBarriers` **must** be a valid pointer to an array of `memoryBarrierCount` valid `VkMemoryBarrier` structures
- **VUID-vkCmdPipelineBarrier-pBufferMemoryBarriers-parameter** — If `bufferMemoryBarrierCount` is not `0`, `pBufferMemoryBarriers` **must** be a valid pointer to an array of `bufferMemoryBarrierCount` valid `VkBufferMemoryBarrier` structures
- **VUID-vkCmdPipelineBarrier-pImageMemoryBarriers-parameter** — If `imageMemoryBarrierCount` is not `0`, `pImageMemoryBarriers` **must** be a valid pointer to an array of `imageMemoryBarrierCount` valid `VkImageMemoryBarrier` structures
- **VUID-vkCmdPipelineBarrier-commandBuffer-recording** — `commandBuffer` **must** be in the recording state
- **VUID-vkCmdPipelineBarrier-commandBuffer-cmdpool** — The `VkCommandPool` that `commandBuffer` was allocated from **must** support `VK_QUEUE_COMPUTE_BIT`, `VK_QUEUE_GRAPHICS_BIT`, `VK_QUEUE_TRANSFER_BIT`, `VK_QUEUE_VIDEO_DECODE_BIT_KHR`, or `VK_QUEUE_VIDEO_ENCODE_BIT_KHR` operations
- **VUID-vkCmdPipelineBarrier-suspended** — This command **must** not be called between suspended render pass instances

## Host Synchronization

- Host access to `commandBuffer` **must** be externally synchronized
- Host access to the `VkCommandPool` that `commandBuffer` was allocated from **must** be externally synchronized

## Command Properties

- Command Buffer Levels: Primary, Secondary
- Render Pass Scope: Both
- Video Coding Scope: Both
- Supported Queue Types: `VK_QUEUE_COMPUTE_BIT`, `VK_QUEUE_GRAPHICS_BIT`, `VK_QUEUE_TRANSFER_BIT`, `VK_QUEUE_VIDEO_DECODE_BIT_KHR`, `VK_QUEUE_VIDEO_ENCODE_BIT_KHR`
- Command Type: Synchronization

**Conditional Rendering**: `vkCmdPipelineBarrier` is not affected by conditional rendering.

## See Also

`VK_VERSION_1_0`, `VkBufferMemoryBarrier`, `VkCommandBuffer`, `VkDependencyFlags`, `VkImageMemoryBarrier`, `VkMemoryBarrier`, `VkPipelineStageFlags`

## Document Notes

更多信息见 [Vulkan Specification](https://registry.khronos.org/vulkan/specs/latest/chapters/synchronization.html#vkCmdPipelineBarrier)。此页面摘自 Vulkan Specification，修改应直接提交至 Specification 而非此处。

---

说明：原页面未提供独立的 Return Values 或 Error Codes 章节（该命令返回 `void`，页面中也没有列出错误码），因此上述转换中不包含这些章节。所有 55 条 Valid Usage VUID 与 10 条 Implicit VUID 均已完整列出，未作省略。
