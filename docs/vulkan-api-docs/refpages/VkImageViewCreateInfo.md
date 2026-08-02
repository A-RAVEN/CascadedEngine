# VkImageViewCreateInfo（结构体参考页整理）

该页面本身属于结构体参考，**不包含返回值或错误码**。使用该结构体的创建函数是 `vkCreateImageView`（返回 `VkResult`），但本页未列出其错误码。

## C 结构体定义

```c
typedef struct VkImageViewCreateInfo {
    VkStructureType            sType;
    const void*                pNext;
    VkImageViewCreateFlags     flags;
    VkImage                    image;
    VkImageViewType            viewType;
    VkFormat                   format;
    VkComponentMapping         components;
    VkImageSubresourceRange    subresourceRange;
} VkImageViewCreateInfo;
```

由 `VK_VERSION_1_0` 提供。

## 成员说明

| 成员 | 含义 |
|---|---|
| `sType` | `VkStructureType`，标识本结构体 |
| `pNext` | `NULL` 或指向扩展本结构体的结构 |
| `flags` | `VkImageViewCreateFlagBits` 位掩码，指定 image view 的附加参数 |
| `image` | 在其上创建 view 的 `VkImage` |
| `viewType` | `VkImageViewType`，指定 image view 的类型 |
| `format` | `VkFormat`，用于解释 image texel block 的格式和类型 |
| `components` | `VkComponentMapping`，颜色分量（或转换后的深度/模板分量）的重映射 |
| `subresourceRange` | `VkImageSubresourceRange`，选择 view 可访问的 mipmap 级别和数组层的集合 |

## 描述要点（概述）

- image view 继承 image 的 `usage` 参数；可通过 `pNext` 链中的 `VkImageViewUsageCreateInfo` 或 `VkImageViewUsage2CreateInfoKHR` 覆盖，但 view usage 必须是 image usage 的子集。
- 深度模板格式且创建 image 时带 `VkImageStencilUsageCreateInfo` 时，usage 按 `aspectMask` 计算（仅 stencil→`stencilUsage`；仅 depth→image `usage`；两者→二者交集）。
- 3D image 可通过 `VkImageViewSlicedCreateInfoEXT` 限制 Z 范围。
- multi-planar 格式的相关规则：plane aspect 对应的 view format 为 plane 的兼容格式，否则为创建 image 的 format。
- `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` 时 `format` 可与 view format 兼容；未设置时 `format` 必须等于 view format。
- block texel view 兼容、YCbCr 转换（`VkSamplerYcbcrConversionInfo`）、external format（Android/QNX）等均有专门约束。
- 非 identity swizzle 的限制：combined image sampler 且启用 Y′CBCR 转换、input attachment、framebuffer attachment、storage image descriptor 时必须为 identity swizzle。

## 表 1：Image 类型与 Image View 类型兼容性

| Image View 类型 | 兼容的 Image 类型 |
|---|---|
| `VK_IMAGE_VIEW_TYPE_1D` | `VK_IMAGE_TYPE_1D` |
| `VK_IMAGE_VIEW_TYPE_1D_ARRAY` | `VK_IMAGE_TYPE_1D` |
| `VK_IMAGE_VIEW_TYPE_2D` | `VK_IMAGE_TYPE_2D`、`VK_IMAGE_TYPE_3D` |
| `VK_IMAGE_VIEW_TYPE_2D_ARRAY` | `VK_IMAGE_TYPE_2D`、`VK_IMAGE_TYPE_3D` |
| `VK_IMAGE_VIEW_TYPE_CUBE` | `VK_IMAGE_TYPE_2D` |
| `VK_IMAGE_VIEW_TYPE_CUBE_ARRAY` | `VK_IMAGE_TYPE_2D` |
| `VK_IMAGE_VIEW_TYPE_3D` | `VK_IMAGE_TYPE_3D` |

## Valid Usage（全部 95 条 VUID，未省略）

1. `VUID-VkImageViewCreateInfo-image-01003`：image 未以 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` 创建时，viewType 不得为 CUBE 或 CUBE_ARRAY。
2. `VUID-VkImageViewCreateInfo-viewType-01004`：未启用 `imageCubeArray` 特性时，viewType 不得为 CUBE_ARRAY。
3. `VUID-VkImageViewCreateInfo-image-06723`：3D image 未设置 `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` 时，viewType 不得为 2D_ARRAY。
4. `VUID-VkImageViewCreateInfo-image-06728`：3D image 未设置 `2D_ARRAY_COMPATIBLE_BIT` 或 `2D_VIEW_COMPATIBLE_BIT_EXT` 时，viewType 不得为 2D。
5. `VUID-VkImageViewCreateInfo-image-04970`：3D image 且 viewType 为 2D/2D_ARRAY 时，`subresourceRange.levelCount` 必须为 1。
6. `VUID-VkImageViewCreateInfo-image-04972`：image 的 samples 非 `VK_SAMPLE_COUNT_1_BIT` 时，viewType 必须为 2D 或 2D_ARRAY。
7. `VUID-VkImageViewCreateInfo-image-04441`：image 的 usage 必须至少包含以下之一：SAMPLED、STORAGE、COLOR_ATTACHMENT、DEPTH_STENCIL_ATTACHMENT、INPUT_ATTACHMENT、TRANSIENT_ATTACHMENT、FRAGMENT_SHADING_RATE_ATTACHMENT_KHR、FRAGMENT_DENSITY_MAP_EXT、VIDEO_DECODE_DST_KHR、VIDEO_DECODE_DPB_KHR、VIDEO_ENCODE_SRC_KHR、VIDEO_ENCODE_DPB_KHR、SAMPLE_WEIGHT_QCOM、SAMPLE_BLOCK_MATCH_QCOM、VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_KHR、VIDEO_ENCODE_EMPHASIS_MAP_KHR。
8. `VUID-VkImageViewCreateInfo-None-02273`：所得 image view 的 format features 必须至少包含一个位。
9. `VUID-VkImageViewCreateInfo-usage-02274`：usage 含 SAMPLED_BIT 时，format features 须含 `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`。
10. `VUID-VkImageViewCreateInfo-usage-02275`：usage 含 STORAGE_BIT 时，须含 `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`。
11. `VUID-VkImageViewCreateInfo-usage-08931`：usage 含 COLOR_ATTACHMENT_BIT 时，须含 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` 或 `VK_FORMAT_FEATURE_2_LINEAR_COLOR_ATTACHMENT_BIT_NV`。
12. `VUID-VkImageViewCreateInfo-usage-02277`：usage 含 DEPTH_STENCIL_ATTACHMENT_BIT 时，须含 `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`。
13. `VUID-VkImageViewCreateInfo-image-08333`：usage 含 VIDEO_DECODE_DST_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_VIDEO_DECODE_OUTPUT_BIT_KHR`。
14. `VUID-VkImageViewCreateInfo-image-08334`：usage 含 VIDEO_DECODE_DPB_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_VIDEO_DECODE_DPB_BIT_KHR`。
15. `VUID-VkImageViewCreateInfo-image-08335`：usage 不得包含 `VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR`。
16. `VUID-VkImageViewCreateInfo-image-08336`：usage 含 VIDEO_ENCODE_SRC_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_VIDEO_ENCODE_INPUT_BIT_KHR`。
17. `VUID-VkImageViewCreateInfo-image-08337`：usage 含 VIDEO_ENCODE_DPB_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_VIDEO_ENCODE_DPB_BIT_KHR`。
18. `VUID-VkImageViewCreateInfo-image-08338`：usage 不得包含 `VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR`。
19. `VUID-VkImageViewCreateInfo-usage-10259`：usage 含 VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_2_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR`。
20. `VUID-VkImageViewCreateInfo-usage-10260`：usage 含 VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR 时，须含 `VK_FORMAT_FEATURE_2_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`。
21. `VUID-VkImageViewCreateInfo-usage-08932`：usage 含 INPUT_ATTACHMENT_BIT，且（`externalFormatResolve` 特性未启用，或 `nullColorAttachmentWithExternalFormatResolve` 为 VK_FALSE，或 image 的 `VkExternalFormatANDROID::externalFormat` 为 0）时，format features 须含 COLOR_ATTACHMENT_BIT、DEPTH_STENCIL_ATTACHMENT_BIT、`2_LINEAR_COLOR_ATTACHMENT_BIT_NV` 之一。
22. `VUID-VkImageViewCreateInfo-subresourceRange-01478`：`baseMipLevel` 必须小于创建 image 时的 `mipLevels`。
23. `VUID-VkImageViewCreateInfo-subresourceRange-01718`：`levelCount` 非 `VK_REMAINING_MIP_LEVELS` 时，`baseMipLevel + levelCount` 必须 ≤ image 的 `mipLevels`。
24. `VUID-VkImageViewCreateInfo-image-02571`：image 带 `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT` 时，`levelCount` 必须为 1。
25. `VUID-VkImageViewCreateInfo-image-06724`：非（3D image 且设置了 2D_ARRAY/2D_VIEW_COMPATIBLE）或 viewType 非 2D/2D_ARRAY 时，`baseArrayLayer` 必须小于 image 的 `arrayLayers`。
26. `VUID-VkImageViewCreateInfo-subresourceRange-06725`：`layerCount` 非 `VK_REMAINING_ARRAY_LAYERS` 且（同上条件）时，`layerCount` 非零且 `baseArrayLayer + layerCount` ≤ image 的 `arrayLayers`。
27. `VUID-VkImageViewCreateInfo-image-02724`：带 2D_ARRAY_COMPATIBLE 的 3D image、viewType 为 2D/2D_ARRAY 时，`baseArrayLayer` 必须小于按 mip level sizing 公式计算的 depth。
28. `VUID-VkImageViewCreateInfo-subresourceRange-02725`：同上条件下（且 layerCount 非 REMAINING），`layerCount` 非零且 `baseArrayLayer + layerCount` ≤ 该 depth。
29. `VUID-VkImageViewCreateInfo-image-01761`：设置了 MUTABLE_FORMAT_BIT 但未设置 BLOCK_TEXEL_VIEW_COMPATIBLE_BIT、且 image format 非 multi-planar 时，`format` 必须与 image format 兼容（格式兼容类）。
30. `VUID-VkImageViewCreateInfo-image-01583`：设置了 BLOCK_TEXEL_VIEW_COMPATIBLE_BIT 时，`format` 必须兼容，或为与 image format size-compatible 的非压缩格式。
31. `VUID-VkImageViewCreateInfo-image-07072`：设置了 BLOCK_TEXEL_VIEW_COMPATIBLE_BIT 且 `format` 为非压缩格式时，`levelCount` 必须为 1。
32. `VUID-VkImageViewCreateInfo-image-09487`：同上且 `blockTexelViewCompatibleMultipleLayers` 属性非 VK_TRUE 时，`layerCount` 必须为 1。
33. `VUID-VkImageViewCreateInfo-pNext-01585`：创建 image 时带 `VkImageFormatListCreateInfo` 且 `viewFormatCount` 非零时，`format` 必须在其 `pViewFormats` 列表中。
34. `VUID-VkImageViewCreateInfo-image-01586`：MUTABLE_FORMAT、image format 为 multi-planar、且 aspectMask 为某个 plane 位时，`format` 必须与该 plane 的兼容格式一致。
35. `VUID-VkImageViewCreateInfo-subresourceRange-07818`：`aspectMask` 最多只能有 1 个有效的 multi-planar aspect 位。
36. `VUID-VkImageViewCreateInfo-image-12397`：未设置 MUTABLE_FORMAT_BIT 时，`format` 必须与创建 image 的 format 完全相同。
37. `VUID-VkImageViewCreateInfo-format-12398`：image format 为 multi-planar 且 aspectMask 为 COLOR_BIT 时，`format` 必须与创建 image 的 format 相同。
38. `VUID-VkImageViewCreateInfo-format-06415`：view format 需要 sampler Y′CBCR 转换且 usage 含 SAMPLED_BIT 时，pNext 必须含 `VkSamplerYcbcrConversionInfo` 且 conversion 非 `VK_NULL_HANDLE`。
39. `VUID-VkImageViewCreateInfo-format-04714`：`format` 带 `_422` 或 `_420` 后缀时，image 宽度必须为 2 的倍数。
40. `VUID-VkImageViewCreateInfo-format-04715`：`format` 带 `_420` 后缀时，image 高度必须为 2 的倍数。
41. `VUID-VkImageViewCreateInfo-pNext-01970`：pNext 含非空 conversion 的 `VkSamplerYcbcrConversionInfo` 时，`components` 必须全部为 identity swizzle。
42. `VUID-VkImageViewCreateInfo-pNext-06658`：conversion 非空时，`format` 必须与 `VkSamplerYcbcrConversionCreateInfo::format` 相同。
43. `VUID-VkImageViewCreateInfo-image-01020`：非 sparse image（或每个 disjoint plane）必须完整、连续绑定到单个 `VkDeviceMemory`。
44. `VUID-VkImageViewCreateInfo-subResourceRange-01021`：viewType 必须与 image 类型兼容（见上表）。
45. `VUID-VkImageViewCreateInfo-image-02399`：Android external format 时，`format` 必须为 `VK_FORMAT_UNDEFINED`。
46. `VUID-VkImageViewCreateInfo-image-02400`：Android external format 时，pNext 必须含与 image 相同 external format 创建的 conversion。
47. `VUID-VkImageViewCreateInfo-image-02401`：Android external format 时，`components` 必须全为 identity swizzle。
48. `VUID-VkImageViewCreateInfo-image-08957`：QNX Screen external format 时，`format` 必须为 `VK_FORMAT_UNDEFINED`。
49. `VUID-VkImageViewCreateInfo-image-08958`：QNX Screen external format 时，pNext 必须含同 external format 的 conversion。
50. `VUID-VkImageViewCreateInfo-image-08959`：QNX Screen external format 时，`components` 必须全为 identity swizzle。
51. `VUID-VkImageViewCreateInfo-image-02086`：image 带 `VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR` 时，viewType 必须为 2D 或 2D_ARRAY。
52. `VUID-VkImageViewCreateInfo-image-02087`：`shadingRateImage` 特性启用且 image 带 NV 的 `VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV` 时，`format` 必须为 `VK_FORMAT_R8_UINT`。
53. `VUID-VkImageViewCreateInfo-attachmentFragmentShadingRate-12386`：`attachmentFragmentShadingRate` 特性未启用时，view usage 不得含 `VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`。
54. `VUID-VkImageViewCreateInfo-usage-04550`：usage 含该 KHR 位时，format features 须含 `VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`。
55. `VUID-VkImageViewCreateInfo-usage-04551`：usage 含该位且 `layeredShadingRateAttachments` 为 VK_FALSE 时，`layerCount` 必须为 1。
56. `VUID-VkImageViewCreateInfo-flags-02572`：`fragmentDensityMapDynamic` 特性未启用时，flags 不得含 `VK_IMAGE_VIEW_CREATE_FRAGMENT_DENSITY_MAP_DYNAMIC_BIT_EXT`。
57. `VUID-VkImageViewCreateInfo-flags-03567`：`fragmentDensityMapDeferred` 特性未启用时，flags 不得含 `VK_IMAGE_VIEW_CREATE_FRAGMENT_DENSITY_MAP_DEFERRED_BIT_EXT`。
58. `VUID-VkImageViewCreateInfo-flags-03568`：flags 含 DEFERRED 位时不得同时含 DYNAMIC 位。
59. `VUID-VkImageViewCreateInfo-image-03569`：image 带 `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT` 且含 SAMPLED_BIT 时，`layerCount` 必须 ≤ `maxSubsampledArrayLayers`。
60. `VUID-VkImageViewCreateInfo-invocationMask-04993`：`invocationMask` 特性启用且 image 带 `VK_IMAGE_USAGE_INVOCATION_MASK_BIT_HUAWEI` 时，`format` 必须为 `VK_FORMAT_R8_UINT`。
61. `VUID-VkImageViewCreateInfo-flags-04116`：flags 不含 DYNAMIC 位且 image 带 `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT` 时，image 的 flags 不得含 PROTECTED、SPARSE_BINDING、SPARSE_RESIDENCY、SPARSE_ALIASED。
62. `VUID-VkImageViewCreateInfo-pNext-02662`：pNext 含 `VkImageViewUsageCreateInfo` 且 image 创建时未带 `VkImageStencilUsageCreateInfo` 时，其 usage 不得含 image `VkImageCreateInfo::usage` 之外的位。
63. `VUID-VkImageViewCreateInfo-pNext-02663`：pNext 含该结构、image 带 `VkImageStencilUsageCreateInfo`、且 aspectMask 含 STENCIL_BIT 时，usage 不得含 `stencilUsage` 之外的位。
64. `VUID-VkImageViewCreateInfo-pNext-02664`：同上但 aspectMask 含 STENCIL 之外的位时，usage 不得含 `VkImageCreateInfo::usage` 之外的位。
65. `VUID-VkImageViewCreateInfo-imageViewType-04973`：viewType 为 1D/2D/3D 且 layerCount 非 REMAINING 时，`layerCount` 必须为 1。
66. `VUID-VkImageViewCreateInfo-imageViewType-04974`：viewType 为 1D/2D/3D 且 layerCount 为 REMAINING 时，剩余层数必须为 1。
67. `VUID-VkImageViewCreateInfo-viewType-02960`：viewType 为 CUBE 且 layerCount 非 REMAINING 时，`layerCount` 必须为 6。
68. `VUID-VkImageViewCreateInfo-viewType-02961`：viewType 为 CUBE_ARRAY 且 layerCount 非 REMAINING 时，`layerCount` 必须为 6 的倍数。
69. `VUID-VkImageViewCreateInfo-viewType-02962`：viewType 为 CUBE 且 layerCount 为 REMAINING 时，剩余层数必须为 6。
70. `VUID-VkImageViewCreateInfo-viewType-02963`：viewType 为 CUBE_ARRAY 且 layerCount 为 REMAINING 时，剩余层数必须为 6 的倍数。
71. `VUID-VkImageViewCreateInfo-imageViewFormatSwizzle-04465`：启用 `VK_KHR_portability_subset` 且 `imageViewFormatSwizzle` 为 VK_FALSE 时，`components` 必须全为 identity swizzle。
72. `VUID-VkImageViewCreateInfo-imageViewFormatReinterpretation-04466`：portability subset 的 `imageViewFormatReinterpretation` 为 VK_FALSE 时，`format` 的分量数量及每分量位数必须与 image format 相同。
73. `VUID-VkImageViewCreateInfo-image-04817`：image 带 VIDEO_DECODE_DST/SRC/DPB 任一 usage 时，viewType 必须为 2D 或 2D_ARRAY。
74. `VUID-VkImageViewCreateInfo-image-04818`：image 带 VIDEO_ENCODE_DST/SRC/DPB 任一 usage 时，viewType 必须为 2D 或 2D_ARRAY。
75. `VUID-VkImageViewCreateInfo-image-10261`：image 带 VIDEO_ENCODE_QUANTIZATION_DELTA_MAP 或 EMPHASIS_MAP usage 时，viewType 必须为 2D 或 2D_ARRAY。
76. `VUID-VkImageViewCreateInfo-flags-08106`：flags 含 `VK_IMAGE_VIEW_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT` 时，须启用 `descriptorBufferCaptureReplay` 特性。
77. `VUID-VkImageViewCreateInfo-pNext-08107`：pNext 含 `VkOpaqueCaptureDescriptorDataCreateInfoEXT` 时，flags 必须含上述 CAPTURE_REPLAY 位。
78. `VUID-VkImageViewCreateInfo-pNext-06787`：pNext 含 `VkExportMetalObjectCreateInfoEXT` 时，其 `exportObjectType` 必须为 `VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT`。
79. `VUID-VkImageViewCreateInfo-pNext-06944`：pNext 含 `VkImageViewSampleWeightCreateInfoQCOM` 时，须启用 `textureSampleWeighted` 特性。
80. `VUID-VkImageViewCreateInfo-pNext-06945`：pNext 含该 QCOM 结构时，image 必须以 `VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM` 创建。
81. `VUID-VkImageViewCreateInfo-pNext-06946`：pNext 含该结构时，`components` 必须全为 `VK_COMPONENT_SWIZZLE_IDENTITY`。
82. `VUID-VkImageViewCreateInfo-pNext-06947`：pNext 含该结构时，`aspectMask` 必须为 `VK_IMAGE_ASPECT_COLOR_BIT`。
83. `VUID-VkImageViewCreateInfo-pNext-06948`：pNext 含该结构时，`levelCount` 必须为 1。
84. `VUID-VkImageViewCreateInfo-pNext-06949`：pNext 含该结构时，viewType 必须为 1D_ARRAY 或 2D_ARRAY。
85. `VUID-VkImageViewCreateInfo-pNext-06950`：pNext 含该结构且 viewType 为 1D_ARRAY 时，image 的 imageType 必须为 `VK_IMAGE_TYPE_1D`。
86. `VUID-VkImageViewCreateInfo-pNext-06951`：pNext 含该结构且 viewType 为 1D_ARRAY 时，`layerCount` 必须等于 2。
87. `VUID-VkImageViewCreateInfo-pNext-06952`：pNext 含该结构且 viewType 为 1D_ARRAY 时，image 宽度必须 ≥ `numPhases × max(align(filterSize.width,4), filterSize.height)`。
88. `VUID-VkImageViewCreateInfo-pNext-06953`：pNext 含该结构且 viewType 为 2D_ARRAY 时，image 的 imageType 必须为 `VK_IMAGE_TYPE_2D`。
89. `VUID-VkImageViewCreateInfo-pNext-06954`：pNext 含该结构且 viewType 为 2D_ARRAY 时，`layerCount` 必须 ≥ numPhases。
90. `VUID-VkImageViewCreateInfo-pNext-06955`：pNext 含该结构且 viewType 为 2D_ARRAY 时，image 宽度必须 ≥ `filterSize.width`。
91. `VUID-VkImageViewCreateInfo-pNext-06956`：pNext 含该结构且 viewType 为 2D_ARRAY 时，image 高度必须 ≥ `filterSize.height`。
92. `VUID-VkImageViewCreateInfo-pNext-06957`：pNext 含该结构时，`filterSize.height` 必须 ≤ `maxWeightFilterDimension.height`。
93. `VUID-VkImageViewCreateInfo-subresourceRange-09594`：`aspectMask` 必须对创建 image 的 format 有效。
94. `VUID-VkImageViewCreateInfo-image-13357`：image 以 `VK_IMAGE_CREATE_ALIAS_SINGLE_LAYER_DESCRIPTOR_BIT_KHR` 创建时，`format` 不得为 multi-planar 格式。
95. `VUID-VkImageViewCreateInfo-None-12280`：不支持 Vulkan 1.3 且未启用 `ycbcr2plane444Formats` 特性时，`format` 不得为 `VK_FORMAT_G8_B8R8_2PLANE_444_UNORM`、`VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16`、`VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16` 或 `VK_FORMAT_G16_B16R16_2PLANE_444_UNORM`。

## Valid Usage (Implicit)（9 条）

96. `VUID-VkImageViewCreateInfo-sType-sType`：`sType` 必须为 `VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO`。
97. `VUID-VkImageViewCreateInfo-pNext-pNext`：pNext 链中每个结构必须为 NULL 或以下类型的合法实例：`VkExportMetalObjectCreateInfoEXT`、`VkImageViewASTCDecodeModeEXT`、`VkImageViewMinLodCreateInfoEXT`、`VkImageViewSampleWeightCreateInfoQCOM`、`VkImageViewSlicedCreateInfoEXT`、`VkImageViewUsage2CreateInfoKHR`、`VkImageViewUsageCreateInfo`、`VkOpaqueCaptureDescriptorDataCreateInfoEXT`、`VkSamplerYcbcrConversionInfo`。
98. `VUID-VkImageViewCreateInfo-sType-unique`：pNext 链中各结构的 `sType` 必须唯一（`VkExportMetalObjectCreateInfoEXT` 除外）。
99. `VUID-VkImageViewCreateInfo-flags-parameter`：`flags` 必须是 `VkImageViewCreateFlagBits` 值的合法组合。
100. `VUID-VkImageViewCreateInfo-image-parameter`：`image` 必须是合法的 `VkImage` 句柄。
101. `VUID-VkImageViewCreateInfo-viewType-parameter`：`viewType` 必须是合法的 `VkImageViewType` 值。
102. `VUID-VkImageViewCreateInfo-format-parameter`：`format` 必须是合法的 `VkFormat` 值。
103. `VUID-VkImageViewCreateInfo-components-parameter`：`components` 必须是合法的 `VkComponentMapping` 结构。
104. `VUID-VkImageViewCreateInfo-subresourceRange-parameter`：`subresourceRange` 必须是合法的 `VkImageSubresourceRange` 结构。

## See Also

`VK_VERSION_1_0`、`VkComponentMapping`、`VkFormat`、`VkImage`、`VkImageDescriptorInfoEXT`、`VkImageSubresourceRange`、`VkImageViewCreateFlags`、`VkImageViewType`、`VkStructureType`、`vkCreateImageView`。

说明：本页内容提取自 Vulkan Specification（该页面注明修改应提交到 Specification 而非直接修改本页）。此结构体页面本身无返回值/错误码字段；`vkCreateImageView` 的返回值和错误码在其函数参考页中。
