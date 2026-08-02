# VkDescriptorImageInfo(3)

## Name

VkDescriptorImageInfo — Structure specifying descriptor image information

## C Specification

The `VkDescriptorImageInfo` structure is defined as:

```c
// Provided by VK_VERSION_1_0
typedef struct VkDescriptorImageInfo {
    VkSampler        sampler;
    VkImageView      imageView;
    VkImageLayout    imageLayout;
} VkDescriptorImageInfo;
```

## Members

| Member | Description |
|---|---|
| `sampler` | A sampler handle, used in descriptor updates for `VK_DESCRIPTOR_TYPE_SAMPLER` and `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` when the binding being updated does not use immutable samplers. |
| `imageView` | `VK_NULL_HANDLE` or an image view handle, used in descriptor updates for `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`. |
| `imageLayout` | The layout the image subresources reachable from `imageView` will be in when the descriptor is accessed. Used for the same descriptor types as `imageView`. |

## Description

Members of `VkDescriptorImageInfo` that are not used in a given update (as described above) are ignored.

### Valid Usage

- **VUID-VkDescriptorImageInfo-imageView-06712** — `imageView` must not be a 2D array image view created from a 3D image.
- **VUID-VkDescriptorImageInfo-imageView-07795** — If `imageView` is a 2D view created from a 3D image, `descriptorType` must be `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, or `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`.
- **VUID-VkDescriptorImageInfo-imageView-07796** — If `imageView` is a 2D view created from a 3D image, the image must have been created with `VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT` set.
- **VUID-VkDescriptorImageInfo-descriptorType-06713** — If the `image2DViewOf3D` feature is not enabled, or `descriptorType` is not `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `imageView` must not be a 2D view created from a 3D image.
- **VUID-VkDescriptorImageInfo-descriptorType-06714** — If the `sampler2DViewOf3D` feature is not enabled, or `descriptorType` is not `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` or `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `imageView` must not be a 2D view created from a 3D image.
- **VUID-VkDescriptorImageInfo-imageView-01976** — If `imageView` is created from a depth/stencil image, the `aspectMask` used must include either `VK_IMAGE_ASPECT_DEPTH_BIT` or `VK_IMAGE_ASPECT_STENCIL_BIT`, but not both.
- **VUID-VkDescriptorImageInfo-imageLayout-09425** — If `imageLayout` is `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`, the `aspectMask` used to create `imageView` must not include `VK_IMAGE_ASPECT_DEPTH_BIT` or `VK_IMAGE_ASPECT_STENCIL_BIT`.
- **VUID-VkDescriptorImageInfo-imageLayout-09426** — If `imageLayout` is any of the depth/stencil layouts (`VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL`, `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL`, `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL`, `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL`, `VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL`, `VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL`, `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`, or `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`), the `aspectMask` used to create `imageView` must not include `VK_IMAGE_ASPECT_COLOR_BIT`.
- **VUID-VkDescriptorImageInfo-sampler-01564** — If `sampler` is used and the image format is multi-planar, the image must have been created with `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`, and the `aspectMask` of the `imageView` must be a valid multi-planar aspect mask bit.
- **VUID-VkDescriptorImageInfo-mutableComparisonSamplers-04450** — If the `VK_KHR_portability_subset` extension is enabled and `VkPhysicalDevicePortabilitySubsetFeaturesKHR::mutableComparisonSamplers` is `VK_FALSE`, `sampler` must have been created with `VkSamplerCreateInfo::compareEnable` set to `VK_FALSE`.

### Valid Usage (Implicit)

- **VUID-VkDescriptorImageInfo-commonparent** — Both `imageView` and `sampler`, when they are valid handles of non-ignored parameters, must have been created, allocated, or retrieved from the same `VkDevice`.

## See Also

`VK_VERSION_1_0`, `VkDescriptorDataEXT`, `VkImageLayout`, `VkImageView`, `VkSampler`, `VkWriteDescriptorSet`

## Document Notes

For more information, see the Vulkan Specification (descriptor sets chapter, `VkDescriptorImageInfo` section).

This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly.

---

说明：此页面描述的是结构体而非函数，因此不包含返回值或错误码。以上已包含全部 11 条 VUID（10 条 Valid Usage + 1 条隐式 Valid Usage），未省略任何条目。
