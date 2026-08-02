# vkCmdCopyBufferToImage — Summary

NOTE: This page could not be reproduced verbatim by the fetch tool; the following is a complete, concise summary covering every VUID entry (identifiers quoted; surrounding text paraphrased).

## Signature

```c
void vkCmdCopyBufferToImage(
    VkCommandBuffer  commandBuffer,
    VkBuffer         srcBuffer,
    VkImage          dstImage,
    VkImageLayout    dstImageLayout,
    uint32_t         regionCount,
    const VkBufferImageCopy* pRegions);
```

## Parameters

- `commandBuffer` — command buffer into which the command is recorded.
- `srcBuffer` — source buffer.
- `dstImage` — destination image.
- `dstImageLayout` — layout of the destination image subresources at copy time.
- `regionCount` — number of regions to copy.
- `pRegions` — pointer to `VkBufferImageCopy` structures.

## Description (paraphrased)

Each source region is copied from buffer to image per the spec's addressing calculations. Depth-aspect data outside [0,1] writes undefined values unless `VK_EXT_depth_range_unrestricted` is enabled. Copy regions must be aligned to texel block extents except at image edges, where extents must match the edge.

## Valid Usage (all VUIDs from the page)

Memory/binding:

- `VUID-vkCmdCopyBufferToImage-dstImage-07966` — non-sparse `dstImage` must be fully bound contiguously.
- `VUID-vkCmdCopyBufferToImage-srcBuffer-00176` — non-sparse `srcBuffer` must be fully bound contiguously.
- `VUID-vkCmdCopyBufferToImage-srcBuffer-00174` — `srcBuffer` needs `VK_BUFFER_USAGE_TRANSFER_SRC_BIT`.
- `VUID-vkCmdCopyBufferToImage-dstImage-00177` — `dstImage` needs `VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
- `VUID-vkCmdCopyBufferToImage-dstImage-01997` — format features must include `VK_FORMAT_FEATURE_TRANSFER_DST_BIT`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07969` — `dstImage` must not have `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07973` — sample count must be `VK_SAMPLE_COUNT_1_BIT`.
- `VUID-vkCmdCopyBufferToImage-pRegions-00171` — `srcBuffer` must be large enough for all accessed locations.
- `VUID-vkCmdCopyBufferToImage-pRegions-00173` — no location accessed by both a source and a destination region.
- `VUID-vkCmdCopyBufferToImage-pRegions-12483` — no location accessed by more than one destination region.

Subresource/extent:

- `VUID-vkCmdCopyBufferToImage-imageSubresource-07967` — `mipLevel` must be less than `mipLevels` at creation.
- `VUID-vkCmdCopyBufferToImage-imageSubresource-07968` — `baseArrayLayer + layerCount` ≤ `arrayLayers` (unless `VK_REMAINING_ARRAY_LAYERS`).
- `VUID-vkCmdCopyBufferToImage-imageSubresource-07971` — `imageOffset.x` and `x+width` within the subresource width.
- `VUID-vkCmdCopyBufferToImage-imageSubresource-07972` — `imageOffset.y` and `y+height` within the subresource height.
- `VUID-vkCmdCopyBufferToImage-imageOffset-09104` — `imageOffset.z` and `z+depth` within the subresource depth.
- `VUID-vkCmdCopyBufferToImage-dstImage-07979` — 1D: `offset.y = 0`, `height = 1`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07980` — 1D/2D: `offset.z = 0`, `depth = 1`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07983` — 3D: `baseArrayLayer = 0`, `layerCount = 1`.
- `VUID-vkCmdCopyBufferToImage-imageSubresource-09105` — `aspectMask` must specify aspects present in `dstImage`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07981` — multi-planar: `aspectMask` must be a single valid multi-planar aspect.
- `VUID-vkCmdCopyBufferToImage-imageOffset-07738` — offsets/extents must respect the queue family's image transfer granularity.

Queue/protection:

- `VUID-vkCmdCopyBufferToImage-commandBuffer-01828` — unprotected command buffer (no `protectedNoFault`): `srcBuffer` not protected.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-01829` — unprotected command buffer (no `protectedNoFault`): `dstImage` not protected.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-01830` — protected command buffer (no `protectedNoFault`): `dstImage` not unprotected.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-07737` — no `maintenance11` and queue lacks GRAPHICS/COMPUTE: `bufferOffset` multiple of 4.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-07739` — no GRAPHICS and no `maintenance10`: aspect not DEPTH or STENCIL.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-11778` — COMPUTE queue, depth aspect: needs `VK_FORMAT_FEATURE_2_DEPTH_COPY_ON_COMPUTE_QUEUE_BIT_KHR`.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-11779` — TRANSFER-only queue, depth aspect: needs `VK_FORMAT_FEATURE_2_DEPTH_COPY_ON_TRANSFER_QUEUE_BIT_KHR`.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-11780` — COMPUTE queue, stencil aspect: needs `VK_FORMAT_FEATURE_2_STENCIL_COPY_ON_COMPUTE_QUEUE_BIT_KHR`.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-11781` — TRANSFER-only queue, stencil aspect: needs `VK_FORMAT_FEATURE_2_STENCIL_COPY_ON_TRANSFER_QUEUE_BIT_KHR`.

Layout/depth:

- `VUID-vkCmdCopyBufferToImage-dstImageLayout-00180` — `dstImageLayout` must match the layout at execution time.
- `VUID-vkCmdCopyBufferToImage-dstImageLayout-01396` — layout must be `VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR`, `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, or `VK_IMAGE_LAYOUT_GENERAL`.
- `VUID-vkCmdCopyBufferToImage-pRegions-07931` — without `VK_EXT_depth_range_unrestricted`, depth data must be in [0,1].

QCOM transform alignment (texel block extents):

- `VUID-vkCmdCopyBufferToImage-dstImage-07274`, `-imageOffset-10051` — `offset.x` alignment rules for IDENTITY/ROTATE_270 vs ROTATE_180/90 (edge exceptions).
- `VUID-vkCmdCopyBufferToImage-dstImage-07275`, `-imageOffset-10052` — `offset.y` alignment rules for IDENTITY/ROTATE_90 vs ROTATE_270/180 (edge exceptions).
- `VUID-vkCmdCopyBufferToImage-dstImage-07276` — `offset.z` multiple of block depth.
- `VUID-vkCmdCopyBufferToImage-dstImage-00207`, `-imageOffset-10053/10054/10055` — extent.width rules per transform (edge exceptions).
- `VUID-vkCmdCopyBufferToImage-dstImage-00208`, `-imageOffset-10056/10057/10058` — extent.height rules per transform (edge exceptions).
- `VUID-vkCmdCopyBufferToImage-dstImage-00209` — `extent.depth` multiple of block depth unless at edge.

Buffer layout/offset:

- `VUID-vkCmdCopyBufferToImage-bufferRowLength-09106` — `bufferRowLength` multiple of block width.
- `VUID-vkCmdCopyBufferToImage-bufferImageHeight-09107` — `bufferImageHeight` multiple of block height.
- `VUID-vkCmdCopyBufferToImage-bufferRowLength-09108` — `(bufferRowLength / blockWidth) * texelBlockSize ≤ 2³¹−1`.
- `VUID-vkCmdCopyBufferToImage-dstImage-07975` — `bufferOffset` multiple of texel block size (color, non-multi-planar).
- `VUID-vkCmdCopyBufferToImage-dstImage-07976` — multi-planar: `bufferOffset` multiple of the compatible plane's element size.
- `VUID-vkCmdCopyBufferToImage-dstImage-07978` — depth/stencil: `bufferOffset` multiple of 4.

## Valid Usage (Implicit)

- `VUID-vkCmdCopyBufferToImage-commandBuffer-parameter`, `-srcBuffer-parameter`, `-dstImage-parameter`, `-dstImageLayout-parameter`, `-pRegions-parameter` — valid handles/pointers.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-recording` — command buffer in recording state.
- `VUID-vkCmdCopyBufferToImage-commandBuffer-cmdpool` — pool supports COMPUTE, GRAPHICS, or TRANSFER.
- `VUID-vkCmdCopyBufferToImage-renderpass` — must be outside a render pass instance.
- `VUID-vkCmdCopyBufferToImage-suspended` — not between suspended render pass instances.
- `VUID-vkCmdCopyBufferToImage-videocoding` — outside a video coding scope.
- `VUID-vkCmdCopyBufferToImage-regionCount-arraylength` — `regionCount > 0`.
- `VUID-vkCmdCopyBufferToImage-commonparent` — all handles from the same `VkDevice`.

## Host Synchronization

- Host access to `commandBuffer` must be externally synchronized.
- Host access to the `VkCommandPool` must be externally synchronized.

## Command Properties

- Levels: Primary, Secondary; Render Pass Scope: Outside; Video Coding Scope: Outside.
- Queue types: `VK_QUEUE_COMPUTE_BIT`, `VK_QUEUE_GRAPHICS_BIT`, `VK_QUEUE_TRANSFER_BIT`; Command Type: Action.
- Not affected by conditional rendering.

## Return Values / Error Codes

None — like all `vkCmd*` functions it returns `void` and reports errors via validation layers rather than return codes. See Also: `VkBuffer`, `VkBufferImageCopy`, `VkCommandBuffer`, `VkImage`, `VkImageLayout`.
