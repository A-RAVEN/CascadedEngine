# VkImageMemoryBarrier(3)

## Name

VkImageMemoryBarrier - Structure specifying the parameters of an image memory barrier

## C Specification

The `VkImageMemoryBarrier` structure is defined as:

> This functionality is superseded by VkImageMemoryBarrier2. See Legacy Functionality for more information.

```c
// Provided by VK_VERSION_1_0
typedef struct VkImageMemoryBarrier {
    VkStructureType            sType;
    const void*                pNext;
    VkAccessFlags              srcAccessMask;
    VkAccessFlags              dstAccessMask;
    VkImageLayout              oldLayout;
    VkImageLayout              newLayout;
    uint32_t                   srcQueueFamilyIndex;
    uint32_t                   dstQueueFamilyIndex;
    VkImage                    image;
    VkImageSubresourceRange    subresourceRange;
} VkImageMemoryBarrier;
```

## Members

- `sType` is a VkStructureType value identifying this structure.
- `pNext` is `NULL` or a pointer to a structure extending this structure.
- `srcAccessMask` is a bitmask of VkAccessFlagBits specifying a source access mask.
- `dstAccessMask` is a bitmask of VkAccessFlagBits specifying a destination access mask.
- `oldLayout` is the old layout in an image layout transition.
- `newLayout` is the new layout in an image layout transition.
- `srcQueueFamilyIndex` is the source queue family for a queue family ownership transfer.
- `dstQueueFamilyIndex` is the destination queue family for a queue family ownership transfer.
- `image` is a handle to the image affected by this barrier.
- `subresourceRange` describes the image subresource range within `image` that is affected by this barrier.

## Description

The first access scope is limited to access to memory through the specified image subresource range, via access types in the source access mask specified by `srcAccessMask`. If `srcAccessMask` includes `VK_ACCESS_HOST_WRITE_BIT`, memory writes performed by that access type are also made visible, as that access type is not performed through a resource.

The second access scope is limited to access to memory through the specified image subresource range, via access types in the destination access mask specified by `dstAccessMask`. If `dstAccessMask` includes `VK_ACCESS_HOST_WRITE_BIT` or `VK_ACCESS_HOST_READ_BIT`, available memory writes are also made visible to accesses of those types, as those access types are not performed through a resource.

If `srcQueueFamilyIndex` is not equal to `dstQueueFamilyIndex`, and `srcQueueFamilyIndex` is equal to the current queue family, then the memory barrier defines a queue family release operation for the specified image subresource range, and if `dependencyFlags` did not include `VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR`, the second synchronization scope of the calling command does not apply to this operation.

If `dstQueueFamilyIndex` is not equal to `srcQueueFamilyIndex`, and `dstQueueFamilyIndex` is equal to the current queue family, then the memory barrier defines a queue family acquire operation for the specified image subresource range, and if `dependencyFlags` did not include `VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR`, the first synchronization scope of the calling command does not apply to this operation.

If the synchronization2 feature is not enabled or `oldLayout` is not equal to `newLayout`, `oldLayout` and `newLayout` define an image layout transition for the specified image subresource range.

If the synchronization2 feature is enabled, `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are equal, and `oldLayout` and `newLayout` are also equal, the layout values are ignored and the image contents are preserved regardless of the values of `oldLayout`, `newLayout`, and the current layout of the image.

If `image` is a 3D image created with `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` and the maintenance9 feature is enabled, the `baseArrayLayer` and `layerCount` members of `subresourceRange` specify the subset of slices of the 3D image affected by the memory barrier, including the layout transition. Any slices of a 3D image not included in `subresourceRange` are not affected by the memory barrier and remain in their existing layout.

If `image` has a multi-planar format and the image is *disjoint*, then including `VK_IMAGE_ASPECT_COLOR_BIT` in the `aspectMask` member of `subresourceRange` is equivalent to including `VK_IMAGE_ASPECT_PLANE_0_BIT`, `VK_IMAGE_ASPECT_PLANE_1_BIT`, and (for three-plane formats only) `VK_IMAGE_ASPECT_PLANE_2_BIT`.

### Valid Usage

- VUID-VkImageMemoryBarrier-oldLayout-01197  
  If layouts are not ignored, `oldLayout` **must** be `VK_IMAGE_LAYOUT_UNDEFINED` or the current layout of the image subresources affected by the barrier
- VUID-VkImageMemoryBarrier-newLayout-01198  
  If layouts are not ignored, `newLayout` **must** not be `VK_IMAGE_LAYOUT_UNDEFINED` or `VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT` or `VK_IMAGE_LAYOUT_PREINITIALIZED`
- VUID-VkImageMemoryBarrier-image-09117  
  If `image` was created with a sharing mode of `VK_SHARING_MODE_EXCLUSIVE`, and `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are not equal, `srcQueueFamilyIndex` **must** be `VK_QUEUE_FAMILY_EXTERNAL`, `VK_QUEUE_FAMILY_FOREIGN_EXT`, or a valid queue family
- VUID-VkImageMemoryBarrier-image-09118  
  If `image` was created with a sharing mode of `VK_SHARING_MODE_EXCLUSIVE`, and `srcQueueFamilyIndex` and `dstQueueFamilyIndex` are not equal, `dstQueueFamilyIndex` **must** be `VK_QUEUE_FAMILY_EXTERNAL`, `VK_QUEUE_FAMILY_FOREIGN_EXT`, or a valid queue family
- VUID-VkImageMemoryBarrier-None-09097  
  If the `VK_KHR_external_memory` extension is not enabled, and the value of VkApplicationInfo::`apiVersion` used to create the VkInstance is not greater than or equal to Version 1.1, `srcQueueFamilyIndex` **must** not be `VK_QUEUE_FAMILY_EXTERNAL`
- VUID-VkImageMemoryBarrier-None-09098  
  If the `VK_KHR_external_memory` extension is not enabled, and the value of VkApplicationInfo::`apiVersion` used to create the VkInstance is not greater than or equal to Version 1.1, `dstQueueFamilyIndex` **must** not be `VK_QUEUE_FAMILY_EXTERNAL`
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-09099  
  If the `VK_EXT_queue_family_foreign` extension is not enabled `srcQueueFamilyIndex` **must** not be `VK_QUEUE_FAMILY_FOREIGN_EXT`
- VUID-VkImageMemoryBarrier-dstQueueFamilyIndex-09100  
  If the `VK_EXT_queue_family_foreign` extension is not enabled `dstQueueFamilyIndex` **must** not be `VK_QUEUE_FAMILY_FOREIGN_EXT`
- VUID-VkImageMemoryBarrier-subresourceRange-01486  
  `subresourceRange.baseMipLevel` **must** be less than the `mipLevels` specified in VkImageCreateInfo when `image` was created
- VUID-VkImageMemoryBarrier-subresourceRange-01724  
  If `subresourceRange.levelCount` is not `VK_REMAINING_MIP_LEVELS`, `subresourceRange.baseMipLevel` + `subresourceRange.levelCount` **must** be less than or equal to the `mipLevels` specified in VkImageCreateInfo when `image` was created
- VUID-VkImageMemoryBarrier-subresourceRange-01488  
  If `image` is not a 3D image or was created without `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` set, or the maintenance9 feature is not enabled, `subresourceRange.baseArrayLayer` **must** be less than the `arrayLayers` specified in VkImageCreateInfo when `image` was created
- VUID-VkImageMemoryBarrier-maintenance9-10798  
  If the maintenance9 feature is enabled and `image` is a 3D image created with `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` set, `subresourceRange.baseArrayLayer` **must** be less than the depth computed from `baseMipLevel` and `extent.depth` specified in VkImageCreateInfo when `image` was created, according to the formula defined in Image Mip Level Sizing
- VUID-VkImageMemoryBarrier-maintenance9-10799  
  If the maintenance9 feature is enabled and `image` is a 3D image created with `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` set and either `subresourceRange.baseArrayLayer` is not equal to 0 or `subresourceRange.layerCount` is not equal to `VK_REMAINING_ARRAY_LAYERS`, `subresourceRange.levelCount` **must** be 1
- VUID-VkImageMemoryBarrier-subresourceRange-01725  
  If `image` is not a 3D image or was created without `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` set, or the maintenance9 feature is not enabled, and `subresourceRange.layerCount` is not `VK_REMAINING_ARRAY_LAYERS`, `subresourceRange.baseArrayLayer` + `subresourceRange.layerCount` **must** be less than or equal to the `arrayLayers` specified in VkImageCreateInfo when `image` was created
- VUID-VkImageMemoryBarrier-maintenance9-10800  
  If the maintenance9 feature is enabled, `subresourceRange.layerCount` is not `VK_REMAINING_ARRAY_LAYERS`, and `image` is a 3D image created with `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` set, `subresourceRange.baseArrayLayer` + `subresourceRange.layerCount` **must** be less than or equal to the depth computed from `baseMipLevel` and `extent.depth` specified in VkImageCreateInfo when `image` was created, according to the formula defined in Image Mip Level Sizing
- VUID-VkImageMemoryBarrier-image-01932  
  If `image` is non-sparse then the image or each specified *disjoint* plane **must** be bound completely and contiguously to a single `VkDeviceMemory` object
- VUID-VkImageMemoryBarrier-image-09241  
  If `image` has a color format that is single-plane, then the `aspectMask` member of `subresourceRange` **must** be `VK_IMAGE_ASPECT_COLOR_BIT`
- VUID-VkImageMemoryBarrier-image-09242  
  If `image` has a color format and is not *disjoint*, then the `aspectMask` member of `subresourceRange` **must** be `VK_IMAGE_ASPECT_COLOR_BIT`
- VUID-VkImageMemoryBarrier-image-01672  
  If `image` has a multi-planar format and the image is *disjoint*, then the `aspectMask` member of `subresourceRange` **must** include at least one multi-planar aspect mask bit or `VK_IMAGE_ASPECT_COLOR_BIT`
- VUID-VkImageMemoryBarrier-image-03320  
  If `image` has a depth/stencil format with both depth and stencil and the separateDepthStencilLayouts feature is not enabled, then the `aspectMask` member of `subresourceRange` **must** include both `VK_IMAGE_ASPECT_DEPTH_BIT` and `VK_IMAGE_ASPECT_STENCIL_BIT`
- VUID-VkImageMemoryBarrier-image-03319  
  If `image` has a depth/stencil format with both depth and stencil and the separateDepthStencilLayouts feature is enabled, then the `aspectMask` member of `subresourceRange` **must** include either or both `VK_IMAGE_ASPECT_DEPTH_BIT` and `VK_IMAGE_ASPECT_STENCIL_BIT`
- VUID-VkImageMemoryBarrier-image-10749  
  If `image` has a depth-only format then the `aspectMask` member of `subresourceRange` **must** be `VK_IMAGE_ASPECT_DEPTH_BIT`
- VUID-VkImageMemoryBarrier-image-10750  
  If `image` has a stencil-only format then the `aspectMask` member of `subresourceRange` **must** be `VK_IMAGE_ASPECT_STENCIL_BIT`
- VUID-VkImageMemoryBarrier-aspectMask-08702  
  If the `aspectMask` member of `subresourceRange` includes `VK_IMAGE_ASPECT_DEPTH_BIT`, `oldLayout` and `newLayout` **must** not be one of `VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL` or `VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL`
- VUID-VkImageMemoryBarrier-aspectMask-08703  
  If the `aspectMask` member of `subresourceRange` includes `VK_IMAGE_ASPECT_STENCIL_BIT`, `oldLayout` and `newLayout` **must** not be one of `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL` or `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL`
- VUID-VkImageMemoryBarrier-subresourceRange-09601  
  `subresourceRange.aspectMask` **must** be valid for the `format` the `image` was created with
- VUID-VkImageMemoryBarrier-oldLayout-01208  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01209  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01210  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01211  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_SAMPLED_BIT` or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01212  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01213  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_TRANSFER_DST_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-10767  
  If the zeroInitializeDeviceMemory feature is not enabled, `oldLayout` **must** not be `VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT`
- VUID-VkImageMemoryBarrier-oldLayout-10768  
  If `oldLayout` is `VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT`, then all subresources **must** be included in the barrier
- VUID-VkImageMemoryBarrier-oldLayout-01658  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-oldLayout-01659  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-04065  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL` then `image` **must** have been created with at least one of the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_SAMPLED_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flags set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-04066  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-04067  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL` then `image` **must** have been created with at least one of the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_SAMPLED_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flags set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-04068  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL` then `image` **must** have been created with the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-synchronization2-07793  
  If the synchronization2 feature is not enabled, `oldLayout` **must** not be `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR` or `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR`
- VUID-VkImageMemoryBarrier-synchronization2-07794  
  If the synchronization2 feature is not enabled, `newLayout` **must** not be `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR` or `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR`
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-03938  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL`, `image` **must** have been created with the `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` or `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-03939  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL`, `image` **must** have been created with at least one of the `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_SAMPLED_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flags set
- VUID-VkImageMemoryBarrier-oldLayout-02088  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07120  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07121  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07122  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07123  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07124  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07125  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR` usage flag set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-10287  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_VIDEO_ENCODE_QUANTIZATION_MAP_KHR` then `image` **must** have been created with the `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR` usage flags set
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-07006  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT` then `image` **must** have been created with either the `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` or `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flags set, and the `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` or `VK_IMAGE_USAGE_SAMPLED_BIT` usage flags set, and the `VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT` usage flag set
- VUID-VkImageMemoryBarrier-attachmentFeedbackLoopLayout-07313  
  If the attachmentFeedbackLoopLayout feature is not enabled, `newLayout` **must** not be `VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT`
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-09550  
  If layouts are not ignored, `oldLayout` or `newLayout` is `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ` then `image` **must** have been created with either the `VK_IMAGE_USAGE_STORAGE_BIT` usage flag set, or with both the `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flag and either of the `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` or `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` usage flags set
- VUID-VkImageMemoryBarrier-dynamicRenderingLocalRead-09551  
  If the dynamicRenderingLocalRead feature is not enabled, `oldLayout` **must** not be `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ`
- VUID-VkImageMemoryBarrier-dynamicRenderingLocalRead-09552  
  If the dynamicRenderingLocalRead feature is not enabled, `newLayout` **must** not be `VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ`
- VUID-VkImageMemoryBarrier-None-09052  
  If the synchronization2 feature is not enabled, and `image` was created with a sharing mode of `VK_SHARING_MODE_CONCURRENT`, at least one of `srcQueueFamilyIndex` and `dstQueueFamilyIndex` **must** be `VK_QUEUE_FAMILY_IGNORED`
- VUID-VkImageMemoryBarrier-None-09053  
  If the synchronization2 feature is not enabled, and `image` was created with a sharing mode of `VK_SHARING_MODE_CONCURRENT`, `srcQueueFamilyIndex` **must** be `VK_QUEUE_FAMILY_IGNORED` or `VK_QUEUE_FAMILY_EXTERNAL`
- VUID-VkImageMemoryBarrier-None-09054  
  If the synchronization2 feature is not enabled, and `image` was created with a sharing mode of `VK_SHARING_MODE_CONCURRENT`, `dstQueueFamilyIndex` **must** be `VK_QUEUE_FAMILY_IGNORED` or `VK_QUEUE_FAMILY_EXTERNAL`
- VUID-VkImageMemoryBarrier-dstQueueFamilyIndex-12331  
  If `dstQueueFamilyIndex` is `VK_QUEUE_FAMILY_EXTERNAL` and `image` was created with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT` or `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT` in VkExternalMemoryImageCreateInfo::`handleTypes`, `newLayout` **must** be `VK_IMAGE_LAYOUT_GENERAL`
- VUID-VkImageMemoryBarrier-srcQueueFamilyIndex-12332  
  If `srcQueueFamilyIndex` is `VK_QUEUE_FAMILY_EXTERNAL` and `image` was created with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT` or `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT` in VkExternalMemoryImageCreateInfo::`handleTypes`, `oldLayout` **must** be `VK_IMAGE_LAYOUT_GENERAL` or `VK_IMAGE_LAYOUT_UNDEFINED`

### Valid Usage (Implicit)

- VUID-VkImageMemoryBarrier-sType-sType  
  `sType` **must** be `VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER`
- VUID-VkImageMemoryBarrier-pNext-pNext  
  Each `pNext` member of any structure (including this one) in the `pNext` chain **must** be either `NULL` or a pointer to a valid instance of VkExternalMemoryAcquireUnmodifiedEXT or VkSampleLocationsInfoEXT
- VUID-VkImageMemoryBarrier-sType-unique  
  The `sType` value of each structure in the `pNext` chain **must** be unique
- VUID-VkImageMemoryBarrier-oldLayout-parameter  
  `oldLayout` **must** be a valid VkImageLayout value
- VUID-VkImageMemoryBarrier-newLayout-parameter  
  `newLayout` **must** be a valid VkImageLayout value
- VUID-VkImageMemoryBarrier-image-parameter  
  `image` **must** be a valid VkImage handle
- VUID-VkImageMemoryBarrier-subresourceRange-parameter  
  `subresourceRange` **must** be a valid VkImageSubresourceRange structure

Note: this is a structure definition, not a command, so the page defines no return values or error codes (unlike `vkCmdPipelineBarrier` / `vkCmdWaitEvents`, which consume this structure).

## See Also

VK_VERSION_1_0, VkAccessFlags, VkImage, VkImageLayout, VkImageSubresourceRange, VkStructureType, vkCmdPipelineBarrier, vkCmdWaitEvents

## Document Notes

For more information, see the Vulkan Specification (the VkImageMemoryBarrier entry in the synchronization chapter). This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly.
