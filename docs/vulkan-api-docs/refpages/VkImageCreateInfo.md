# VkImageCreateInfo(3)

## Name

VkImageCreateInfo - Structure specifying the parameters of a newly created image object

## C Specification

The `VkImageCreateInfo` structure is defined as:

```c
// Provided by VK_VERSION_1_0
typedef struct VkImageCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkImageCreateFlags       flags;
    VkImageType              imageType;
    VkFormat                 format;
    VkExtent3D               extent;
    uint32_t                 mipLevels;
    uint32_t                 arrayLayers;
    VkSampleCountFlagBits    samples;
    VkImageTiling            tiling;
    VkImageUsageFlags        usage;
    VkSharingMode            sharingMode;
    uint32_t                 queueFamilyIndexCount;
    const uint32_t*          pQueueFamilyIndices;
    VkImageLayout            initialLayout;
} VkImageCreateInfo;
```

## Members

* `sType` is a `VkStructureType` value identifying this structure.
* `pNext` is `NULL` or a pointer to a structure extending this structure.
* `flags` is a bitmask of `VkImageCreateFlagBits` describing additional parameters of the image.
* `imageType` is a `VkImageType` value specifying the basic dimensionality of the image. Layers in array textures do not count as a dimension for the purposes of the image type.
* `format` is a `VkFormat` describing the format and type of the texel blocks that will be contained in the image.
* `extent` is a `VkExtent3D` describing the number of texels/pixels in each dimension of the base level.
* `mipLevels` describes the number of levels of detail available for minified sampling of the image.
* `arrayLayers` is the number of layers in the image.
* `samples` is a `VkSampleCountFlagBits` value specifying the number of samples per texel.
* `tiling` is a `VkImageTiling` value specifying the tiling arrangement of the texel blocks in memory.
* `usage` is a bitmask of `VkImageUsageFlagBits` describing the intended usage of the image.
* `sharingMode` is a `VkSharingMode` value specifying the sharing mode of the image when it will be accessed by multiple queue families.
* `queueFamilyIndexCount` is the number of entries in the `pQueueFamilyIndices` array.
* `pQueueFamilyIndices` is a pointer to an array of queue families that will access this image. It is ignored if `sharingMode` is not `VK_SHARING_MODE_CONCURRENT`.
* `initialLayout` is a `VkImageLayout` value specifying the initial `VkImageLayout` of all image subresources of the image. See Image Layouts.

## Description

`flags` defines the effective create flags for the image. If the `pNext` chain includes a `VkImageCreateFlags2CreateInfoKHR` structure, `flags` is ignored, and the effective create flags are defined by `VkImageCreateFlags2CreateInfoKHR::flags`.

`usage` defines the effective usage flags for the image. If the `pNext` chain includes a `VkImageUsageFlags2CreateInfoKHR` structure, `usage` is ignored, and the effective usage flags are defined by `VkImageUsageFlags2CreateInfoKHR::usage`.

Images created with `tiling` equal to `VK_IMAGE_TILING_LINEAR` have further restrictions on their limits and capabilities compared to images created with `tiling` equal to `VK_IMAGE_TILING_OPTIMAL`. Creation of images with tiling `VK_IMAGE_TILING_LINEAR` **may** not be supported unless other parameters meet all of the constraints:

* `imageType` is `VK_IMAGE_TYPE_2D`
* `format` is not a depth/stencil format
* `mipLevels` is 1
* `arrayLayers` is 1
* `samples` is `VK_SAMPLE_COUNT_1_BIT`
* `usage` only includes `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` and/or `VK_IMAGE_USAGE_TRANSFER_DST_BIT`

Images created with one of the formats that require a sampler Y′CBCR conversion, have further restrictions on their limits and capabilities compared to images created with other formats. Creation of images with a format requiring Y′CBCR conversion **may** not be supported unless other parameters meet all of the constraints:

* `imageType` is `VK_IMAGE_TYPE_2D`
* `mipLevels` is 1
* `arrayLayers` is 1, unless the `ycbcrImageArrays` feature is enabled, or otherwise indicated by `VkImageFormatProperties::maxArrayLayers`, as returned by `vkGetPhysicalDeviceImageFormatProperties`
* `samples` is `VK_SAMPLE_COUNT_1_BIT`

Images created with the `VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM` usage flag set have further restrictions on their limits and capabilities compared to images created without this flag. Creation of images with usage including `VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM` **may** not be supported unless parameters meet all of the constraints:

* `flags` is 0 or only includes `VK_IMAGE_CREATE_ALIAS_BIT`
* `imageType` is `VK_IMAGE_TYPE_2D`
* `mipLevels` is 1
* `arrayLayers` is 1
* `samples` is `VK_SAMPLE_COUNT_1_BIT`
* `tiling` is `VK_IMAGE_TILING_OPTIMAL`
* `usage` includes `VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM` and any valid combination of the following `VK_IMAGE_USAGE_SAMPLED_BIT`, `VK_IMAGE_USAGE_STORAGE_BIT`, `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`

Implementations **may** support additional limits and capabilities beyond those listed above.

To determine the set of valid `usage` bits for a given format, call `vkGetPhysicalDeviceFormatProperties`.

If the size of the resultant image would exceed `maxResourceSize`, then `vkCreateImage` **must** fail and return `VK_ERROR_OUT_OF_DEVICE_MEMORY`. This failure **may** occur even when all image creation parameters satisfy their valid usage requirements.

If the implementation reports `VK_TRUE` in `VkPhysicalDeviceHostImageCopyProperties::identicalMemoryTypeRequirements`, usage of `VK_IMAGE_USAGE_HOST_TRANSFER_BIT` **must** not affect the memory type requirements of the image as described in Sparse Resource Memory Requirements and Resource Memory Association.

For images created without the `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` flag set, a `usage` bit is valid if it is supported for the format the image is created with.

For images created with `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` a `usage` bit is valid if it is supported for at least one of the formats a `VkImageView` created from the image **can** have (see Image Views for more detail).

### Image Creation Limits

Valid values for some image creation parameters are limited by a numerical upper bound or by inclusion in a bitset. For example, `VkImageCreateInfo::arrayLayers` is limited by `imageCreateMaxArrayLayers`, defined below; and `VkImageCreateInfo::samples` is limited by `imageCreateSampleCounts`, also defined below.

Several limiting values are defined below, as well as assisting values from which the limiting values are derived. The limiting values are referenced by the relevant valid usage statements of `VkImageCreateInfo`.

* Let `uint64_t imageCreateDrmFormatModifiers[]` be the set of Linux DRM format modifiers that the resultant image **may** have.
  * If `tiling` is not `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then `imageCreateDrmFormatModifiers` is empty.
  * If `VkImageCreateInfo::pNext` contains `VkImageDrmFormatModifierExplicitCreateInfoEXT`, then `imageCreateDrmFormatModifiers` contains exactly one modifier, `VkImageDrmFormatModifierExplicitCreateInfoEXT::drmFormatModifier`.
  * If `VkImageCreateInfo::pNext` contains `VkImageDrmFormatModifierListCreateInfoEXT`, then `imageCreateDrmFormatModifiers` contains the entire array `VkImageDrmFormatModifierListCreateInfoEXT::pDrmFormatModifiers`.
* Let `VkBool32 imageCreateMaybeLinear` indicate if the resultant image may be linear.
  * If `tiling` is `VK_IMAGE_TILING_LINEAR`, then `imageCreateMaybeLinear` is `VK_TRUE`.
  * If `tiling` is `VK_IMAGE_TILING_OPTIMAL`, then `imageCreateMaybeLinear` is `VK_FALSE`.
  * If `tiling` is `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then `imageCreateMaybeLinear` is `VK_TRUE` if and only if `imageCreateDrmFormatModifiers` contains `DRM_FORMAT_MOD_LINEAR`.
* Let `VkFormatFeatureFlags imageCreateFormatFeatures` be the set of valid _format features_ available during image creation.
  * If `tiling` is `VK_IMAGE_TILING_LINEAR`, then `imageCreateFormatFeatures` is the value of `VkFormatProperties::linearTilingFeatures` found by calling `vkGetPhysicalDeviceFormatProperties` with parameter `format` equal to `VkImageCreateInfo::format`.
  * If `tiling` is `VK_IMAGE_TILING_OPTIMAL`, and if the `pNext` chain includes no `VkExternalFormatANDROID` or `VkExternalFormatQNX` structure with non-zero `externalFormat`, then `imageCreateFormatFeatures` is the value of `VkFormatProperties::optimalTilingFeatures` found by calling `vkGetPhysicalDeviceFormatProperties` with parameter `format` equal to `VkImageCreateInfo::format`.
  * If `tiling` is `VK_IMAGE_TILING_OPTIMAL`, and if the `pNext` chain includes a `VkExternalFormatANDROID` structure with non-zero `externalFormat`, then `imageCreateFormatFeatures` is the value of `VkAndroidHardwareBufferFormatPropertiesANDROID::formatFeatures` obtained by `vkGetAndroidHardwareBufferPropertiesANDROID` with a matching `externalFormat` value.
  * If `tiling` is `VK_IMAGE_TILING_OPTIMAL`, and if the `pNext` chain includes a `VkExternalFormatQNX` structure with non-zero `externalFormat`, then `imageCreateFormatFeatures` is the value of `VkScreenBufferFormatPropertiesQNX::formatFeatures` obtained by `vkGetScreenBufferPropertiesQNX` with a matching `externalFormat` value.
  * If the `pNext` chain includes a `VkBufferCollectionImageCreateInfoFUCHSIA` structure, then `imageCreateFormatFeatures` is the value of `VkBufferCollectionPropertiesFUCHSIA::formatFeatures` found by calling `vkGetBufferCollectionPropertiesFUCHSIA` with a parameter `collection` equal to `VkBufferCollectionImageCreateInfoFUCHSIA::collection`
  * If `tiling` is `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then the value of `imageCreateFormatFeatures` is found by calling `vkGetPhysicalDeviceFormatProperties2` with `VkImageFormatProperties::format` equal to `VkImageCreateInfo::format` and with `VkDrmFormatModifierPropertiesListEXT` chained into `VkFormatProperties2`; by collecting all members of the returned array `VkDrmFormatModifierPropertiesListEXT::pDrmFormatModifierProperties` whose `drmFormatModifier` belongs to `imageCreateDrmFormatModifiers`; and by taking the bitwise intersection, over the collected array members, of `drmFormatModifierTilingFeatures`. (The resultant `imageCreateFormatFeatures` **may** be empty).
* Let `VkImageFormatProperties2 imageCreateImageFormatPropertiesList[]` be defined as follows.
  * If `VkImageCreateInfo::pNext` contains no `VkExternalFormatANDROID` or `VkExternalFormatQNX` structure with non-zero `externalFormat`, then `imageCreateImageFormatPropertiesList` is the list of structures obtained by calling `vkGetPhysicalDeviceImageFormatProperties2`, possibly multiple times, as follows:
    * The parameters `VkPhysicalDeviceImageFormatInfo2::format`, `imageType`, `tiling`, `usage`, and `flags` **must** be equal to those in `VkImageCreateInfo`.
    * If `VkImageCreateInfo::pNext` contains a `VkExternalMemoryImageCreateInfo` structure whose `handleTypes` is not `0`, then `VkPhysicalDeviceImageFormatInfo2::pNext` **must** contain a `VkPhysicalDeviceExternalImageFormatInfo` structure whose `handleType` is not `0`; and `vkGetPhysicalDeviceImageFormatProperties2` **must** be called for each handle type in `VkExternalMemoryImageCreateInfo::handleTypes`, successively setting `VkPhysicalDeviceExternalImageFormatInfo::handleType` on each call.
    * If `VkImageCreateInfo::pNext` contains no `VkExternalMemoryImageCreateInfo` structure, or contains a structure whose `handleTypes` is `0`, then `VkPhysicalDeviceImageFormatInfo2::pNext` **must** either contain no `VkPhysicalDeviceExternalImageFormatInfo` structure, or contain a structure whose `handleType` is `0`.
    * If `VkImageCreateInfo::pNext` contains a `VkVideoProfileListInfoKHR` structure then `VkPhysicalDeviceImageFormatInfo2::pNext` **must** also contain the same `VkVideoProfileListInfoKHR` structure on each call.
    * If `tiling` is `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then:
      * `VkPhysicalDeviceImageFormatInfo2::pNext` **must** contain a `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` structure where `sharingMode` is equal to `VkImageCreateInfo::sharingMode`;
      * if `sharingMode` is `VK_SHARING_MODE_CONCURRENT`, then `queueFamilyIndexCount` and `pQueueFamilyIndices` **must** be equal to those in `VkImageCreateInfo`;
      * if `flags` contains `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`, then the `VkImageFormatListCreateInfo` structure included in the `pNext` chain of `VkPhysicalDeviceImageFormatInfo2` **must** be equivalent to the one included in the `pNext` chain of `VkImageCreateInfo`;
      * if `VkImageCreateInfo::pNext` contains a `VkImageCompressionControlEXT` structure, then the `VkPhysicalDeviceImageFormatInfo2::pNext` chain **must** contain an equivalent structure;
      * `vkGetPhysicalDeviceImageFormatProperties2` **must** be called for each modifier in `imageCreateDrmFormatModifiers`, successively setting `VkPhysicalDeviceImageDrmFormatModifierInfoEXT::drmFormatModifier` on each call.
    * If `tiling` is not `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then `VkPhysicalDeviceImageFormatInfo2::pNext` **must** contain no `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` structure.
    * If any call to `vkGetPhysicalDeviceImageFormatProperties2` returns an error, then `imageCreateImageFormatPropertiesList` is defined to be the empty list.
  * If `VkImageCreateInfo::pNext` contains a `VkExternalFormatANDROID` structure with non-zero `externalFormat`, then `imageCreateImageFormatPropertiesList` contains a single element where:
    * `VkImageFormatProperties::maxMipLevels` is ⌊log2(max(`extent.width`, `extent.height`, `extent.depth`))⌋ + 1.
    * `VkImageFormatProperties::maxArrayLayers` is `VkPhysicalDeviceLimits::maxImageArrayLayers`.
    * Each component of `VkImageFormatProperties::maxExtent` is `VkPhysicalDeviceLimits::maxImageDimension2D`.
    * `VkImageFormatProperties::sampleCounts` contains exactly `VK_SAMPLE_COUNT_1_BIT`.
* Let `uint32_t imageCreateMaxMipLevels` be the minimum value of `VkImageFormatProperties::maxMipLevels` in `imageCreateImageFormatPropertiesList`. The value is **undefined** if `imageCreateImageFormatPropertiesList` is empty.
* Let `uint32_t imageCreateMaxArrayLayers` be the minimum value of `VkImageFormatProperties::maxArrayLayers` in `imageCreateImageFormatPropertiesList`. The value is **undefined** if `imageCreateImageFormatPropertiesList` is empty.
* Let `VkExtent3D imageCreateMaxExtent` be the component-wise minimum over all `VkImageFormatProperties::maxExtent` values in `imageCreateImageFormatPropertiesList`. The value is **undefined** if `imageCreateImageFormatPropertiesList` is empty.
* Let `VkSampleCountFlags imageCreateSampleCounts` be the intersection of each `VkImageFormatProperties::sampleCounts` in `imageCreateImageFormatPropertiesList`. The value is **undefined** if `imageCreateImageFormatPropertiesList` is empty.
* Let `VkVideoFormatPropertiesKHR videoFormatProperties[]` be defined as follows.
  * If `VkImageCreateInfo::pNext` contains a `VkVideoProfileListInfoKHR` structure, then `videoFormatProperties` is the list of structures obtained by calling `vkGetPhysicalDeviceVideoFormatPropertiesKHR` with `VkPhysicalDeviceVideoFormatInfoKHR::imageUsage` equal to the `usage` member of `VkImageCreateInfo` and `VkPhysicalDeviceVideoFormatInfoKHR::pNext` containing the same `VkVideoProfileListInfoKHR` structure chained to `VkImageCreateInfo`.
  * If `VkImageCreateInfo::pNext` contains no `VkVideoProfileListInfoKHR` structure, then `videoFormatProperties` is an empty list.
* Let `VkBool32 supportedVideoFormat` indicate if the image parameters are supported by the specified video profiles.
  * `supportedVideoFormat` is `VK_TRUE` if there exists an element in the `videoFormatProperties` list for which all of the following conditions are true:
    * `VkImageCreateInfo::format` equals `VkVideoFormatPropertiesKHR::format`.
    * `VkImageCreateInfo::flags` only contains `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR` and/or bits also set in `VkVideoFormatPropertiesKHR::imageCreateFlags`.
    * `VkImageCreateInfo::imageType` equals `VkVideoFormatPropertiesKHR::imageType`.
    * `VkImageCreateInfo::tiling` equals `VkVideoFormatPropertiesKHR::imageTiling`.
    * `VkImageCreateInfo::usage` only contains bits also set in `VkVideoFormatPropertiesKHR::imageUsageFlags`, or `VkImageCreateInfo::flags` includes `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT`.
  * Otherwise `supportedVideoFormat` is `VK_FALSE`.

## Valid Usage

- **VUID-VkImageCreateInfo-imageCreateMaxMipLevels-02251**  
  Each of the following values (as described in Image Creation Limits) **must** not be **undefined**: `imageCreateMaxMipLevels`, `imageCreateMaxArrayLayers`, `imageCreateMaxExtent`, and `imageCreateSampleCounts`

- **VUID-VkImageCreateInfo-sharingMode-00941**  
  If `sharingMode` is `VK_SHARING_MODE_CONCURRENT`, `pQueueFamilyIndices` **must** be a valid pointer to an array of `queueFamilyIndexCount` `uint32_t` values

- **VUID-VkImageCreateInfo-maintenance11-13354**  
  If the `maintenance11` feature is enabled and `sharingMode` is `VK_SHARING_MODE_CONCURRENT`, then `queueFamilyIndexCount` **must** be greater than `0`

- **VUID-VkImageCreateInfo-sharingMode-00942**  
  If the `maintenance11` feature is not enabled and `sharingMode` is `VK_SHARING_MODE_CONCURRENT`, then `queueFamilyIndexCount` **must** be greater than `1`

- **VUID-VkImageCreateInfo-sharingMode-01420**  
  If `sharingMode` is `VK_SHARING_MODE_CONCURRENT`, each element of `pQueueFamilyIndices` **must** be unique and **must** be less than `pQueueFamilyPropertyCount` returned by either `vkGetPhysicalDeviceQueueFamilyProperties` or `vkGetPhysicalDeviceQueueFamilyProperties2` for the `physicalDevice` that was used to create `device`

- **VUID-VkImageCreateInfo-pNext-01974**  
  If the `pNext` chain includes a `VkExternalFormatANDROID` structure, and its `externalFormat` member is non-zero the `format` **must** be `VK_FORMAT_UNDEFINED`

- **VUID-VkImageCreateInfo-pNext-01975**  
  If the `pNext` chain does not include a `VkExternalFormatANDROID` structure, or does and its `externalFormat` member is `0`, the `format` **must** not be `VK_FORMAT_UNDEFINED`

- **VUID-VkImageCreateInfo-extent-00944**  
  `extent.width` **must** be greater than `0`

- **VUID-VkImageCreateInfo-extent-00945**  
  `extent.height` **must** be greater than `0`

- **VUID-VkImageCreateInfo-extent-00946**  
  `extent.depth` **must** be greater than `0`

- **VUID-VkImageCreateInfo-mipLevels-00947**  
  `mipLevels` **must** be greater than `0`

- **VUID-VkImageCreateInfo-arrayLayers-00948**  
  `arrayLayers` **must** be greater than `0`

- **VUID-VkImageCreateInfo-flags-00949**  
  If `flags` contains `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-flags-08865**  
  If `flags` contains `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `extent.width` and `extent.height` **must** be equal

- **VUID-VkImageCreateInfo-flags-08866**  
  If `flags` contains `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `arrayLayers` **must** be greater than or equal to 6

- **VUID-VkImageCreateInfo-initialLayout-10765**  
  If the `zeroInitializeDeviceMemory` feature is not enabled, `initialLayout` **must** not be `VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT`

- **VUID-VkImageCreateInfo-flags-02557**  
  If `flags` contains `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-flags-00950**  
  If `flags` contains `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`, `imageType` **must** be `VK_IMAGE_TYPE_3D`

- **VUID-VkImageCreateInfo-flags-09403**  
  If `flags` contains `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`, `flags` **must** not include `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT`, `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`, or `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-flags-07755**  
  If `flags` contains `VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT`, `imageType` **must** be `VK_IMAGE_TYPE_3D`

- **VUID-VkImageCreateInfo-imageType-10197**  
  If `flags` contains `VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT` and either the `maintenance9` feature is not enabled on the device or `image2DViewOf3DSparse` is `VK_FALSE`, `flags` **must** not include `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT`, `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`, or `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-extent-02252**  
  `extent.width` **must** be less than or equal to `imageCreateMaxExtent.width` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-extent-02253**  
  `extent.height` **must** be less than or equal to `imageCreateMaxExtent.height` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-extent-02254**  
  `extent.depth` **must** be less than or equal to `imageCreateMaxExtent.depth` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-imageType-00956**  
  If `imageType` is `VK_IMAGE_TYPE_1D`, both `extent.height` and `extent.depth` **must** be `1`

- **VUID-VkImageCreateInfo-imageType-00957**  
  If `imageType` is `VK_IMAGE_TYPE_2D`, `extent.depth` **must** be `1`

- **VUID-VkImageCreateInfo-mipLevels-00958**  
  `mipLevels` **must** be less than or equal to the number of levels in the complete mipmap chain based on `extent.width`, `extent.height`, and `extent.depth`

- **VUID-VkImageCreateInfo-mipLevels-02255**  
  `mipLevels` **must** be less than or equal to `imageCreateMaxMipLevels` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-arrayLayers-02256**  
  `arrayLayers` **must** be less than or equal to `imageCreateMaxArrayLayers` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-imageType-00961**  
  If `imageType` is `VK_IMAGE_TYPE_3D`, `arrayLayers` **must** be `1`

- **VUID-VkImageCreateInfo-samples-02257**  
  If `samples` is not `VK_SAMPLE_COUNT_1_BIT`, then `imageType` **must** be `VK_IMAGE_TYPE_2D`, `flags` **must** not contain `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `mipLevels` **must** be equal to `1`, and `imageCreateMaybeLinear` (as defined in Image Creation Limits) **must** be `VK_FALSE`

- **VUID-VkImageCreateInfo-samples-02558**  
  If `samples` is not `VK_SAMPLE_COUNT_1_BIT`, `usage` **must** not contain `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT`

- **VUID-VkImageCreateInfo-usage-00963**  
  If `usage` includes `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, then bits other than `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, and `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` **must** not be set

- **VUID-VkImageCreateInfo-usage-00964**  
  If `usage` includes `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, `extent.width` **must** be less than or equal to `VkPhysicalDeviceLimits::maxFramebufferWidth`

- **VUID-VkImageCreateInfo-usage-00965**  
  If `usage` includes `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, `extent.height` **must** be less than or equal to `VkPhysicalDeviceLimits::maxFramebufferHeight`

- **VUID-VkImageCreateInfo-fragmentDensityMapOffset-06514**  
  If the `fragmentDensityMapOffset` feature is not enabled and `usage` includes `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT`, `extent.width` **must** be less than or equal to `VkPhysicalDeviceFragmentDensityMapPropertiesEXT::maxFragmentDensityMapTexelSize.width`

- **VUID-VkImageCreateInfo-fragmentDensityMapOffset-06515**  
  If the `fragmentDensityMapOffset` feature is not enabled and `usage` includes `VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT`, `extent.height` **must** be less than or equal to `VkPhysicalDeviceFragmentDensityMapPropertiesEXT::maxFragmentDensityMapTexelSize.height`

- **VUID-VkImageCreateInfo-usage-00966**  
  If `usage` includes `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, `usage` **must** also contain at least one of `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, or `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-samples-02258**  
  `samples` **must** be a valid `VkSampleCountFlagBits` value that is set in `imageCreateSampleCounts` (as defined in Image Creation Limits)

- **VUID-VkImageCreateInfo-usage-00968**  
  If the `shaderStorageImageMultisample` feature is not enabled, and `usage` contains `VK_IMAGE_USAGE_STORAGE_BIT`, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-flags-00969**  
  If the `sparseBinding` feature is not enabled, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`

- **VUID-VkImageCreateInfo-flags-01924**  
  If the `sparseResidencyAliased` feature is not enabled, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT`

- **VUID-VkImageCreateInfo-tiling-04121**  
  If `tiling` is `VK_IMAGE_TILING_LINEAR`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00970**  
  If `imageType` is `VK_IMAGE_TYPE_1D`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00971**  
  If the `sparseResidencyImage2D` feature is not enabled, and `imageType` is `VK_IMAGE_TYPE_2D`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00972**  
  If the `sparseResidencyImage3D` feature is not enabled, and `imageType` is `VK_IMAGE_TYPE_3D`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00973**  
  If the `sparseResidency2Samples` feature is not enabled, `imageType` is `VK_IMAGE_TYPE_2D`, and `samples` is `VK_SAMPLE_COUNT_2_BIT`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00974**  
  If the `sparseResidency4Samples` feature is not enabled, `imageType` is `VK_IMAGE_TYPE_2D`, and `samples` is `VK_SAMPLE_COUNT_4_BIT`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00975**  
  If the `sparseResidency8Samples` feature is not enabled, `imageType` is `VK_IMAGE_TYPE_2D`, and `samples` is `VK_SAMPLE_COUNT_8_BIT`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-imageType-00976**  
  If the `sparseResidency16Samples` feature is not enabled, `imageType` is `VK_IMAGE_TYPE_2D`, and `samples` is `VK_SAMPLE_COUNT_16_BIT`, `flags` **must** not contain `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`

- **VUID-VkImageCreateInfo-flags-00987**  
  If `flags` contains `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` or `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT`, it **must** also contain `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`

- **VUID-VkImageCreateInfo-None-01925**  
  If any of the bits `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`, `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`, or `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT` are set, `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` **must** not also be set

- **VUID-VkImageCreateInfo-flags-01890**  
  If the `protectedMemory` feature is not enabled, `flags` **must** not contain `VK_IMAGE_CREATE_PROTECTED_BIT`

- **VUID-VkImageCreateInfo-None-01891**  
  If any of the bits `VK_IMAGE_CREATE_SPARSE_BINDING_BIT`, `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`, or `VK_IMAGE_CREATE_SPARSE_ALIASED_BIT` are set, `VK_IMAGE_CREATE_PROTECTED_BIT` **must** not also be set

- **VUID-VkImageCreateInfo-pNext-00988**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfoNV` structure, it **must** not contain a `VkExternalMemoryImageCreateInfo` structure

- **VUID-VkImageCreateInfo-pNext-00990**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` structure, its `handleTypes` member **must** only contain bits that are also in `VkExternalImageFormatProperties::externalMemoryProperties.compatibleHandleTypes`, as returned by `vkGetPhysicalDeviceImageFormatProperties2` with `format`, `imageType`, `tiling`, `usage`, and `flags` equal to those in this structure, and with a `VkPhysicalDeviceExternalImageFormatInfo` structure included in the `pNext` chain, with a `handleType` equal to any one of the handle types specified in `VkExternalMemoryImageCreateInfo::handleTypes`

- **VUID-VkImageCreateInfo-pNext-00991**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfoNV` structure, its `handleTypes` member **must** only contain bits that are also in `VkExternalImageFormatPropertiesNV::externalMemoryFeatures.compatibleHandleTypes`, as returned by `vkGetPhysicalDeviceExternalImageFormatPropertiesNV` with `format`, `imageType`, `tiling`, `usage`, and `flags` equal to those in this structure, and with `externalHandleType` equal to any one of the handle types specified in `VkExternalMemoryImageCreateInfoNV::handleTypes`

- **VUID-VkImageCreateInfo-physicalDeviceCount-01421**  
  If the logical device was created with `VkDeviceGroupDeviceCreateInfo::physicalDeviceCount` equal to 1, `flags` **must** not contain `VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT`

- **VUID-VkImageCreateInfo-flags-02259**  
  If `flags` contains `VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT`, then `mipLevels` **must** be one, `arrayLayers` **must** be one, `imageType` **must** be `VK_IMAGE_TYPE_2D`, and `imageCreateMaybeLinear` (as defined in Image Creation Limits) **must** be `VK_FALSE`

- **VUID-VkImageCreateInfo-flags-01572**  
  If `flags` contains `VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT`, then `format` **must** be a compressed image format

- **VUID-VkImageCreateInfo-flags-01573**  
  If `flags` contains `VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT`, then `flags` **must** also contain `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`

- **VUID-VkImageCreateInfo-initialLayout-00993**  
  `initialLayout` **must** be `VK_IMAGE_LAYOUT_UNDEFINED`, `VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT`, or `VK_IMAGE_LAYOUT_PREINITIALIZED`

- **VUID-VkImageCreateInfo-initialLayout-12478**  
  If `initialLayout` is `VK_IMAGE_LAYOUT_PREINITIALIZED`, `tiling` **must** be `VK_IMAGE_TILING_LINEAR`

- **VUID-VkImageCreateInfo-pNext-01443**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` or `VkExternalMemoryImageCreateInfoNV` structure whose `handleTypes` member is not `0`, `initialLayout` **must** be `VK_IMAGE_LAYOUT_UNDEFINED`

- **VUID-VkImageCreateInfo-format-06410**  
  If the image `format` is one of the formats that require a sampler Y′CBCR conversion, `mipLevels` **must** be 1

- **VUID-VkImageCreateInfo-format-06411**  
  If the image `format` is one of the formats that require a sampler Y′CBCR conversion, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-format-06412**  
  If the image `format` is one of the formats that require a sampler Y′CBCR conversion, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-imageCreateFormatFeatures-02260**  
  If `format` is a _multi-planar_ format, and if `imageCreateFormatFeatures` (as defined in Image Creation Limits) does not contain `VK_FORMAT_FEATURE_DISJOINT_BIT`, then `flags` **must** not contain `VK_IMAGE_CREATE_DISJOINT_BIT`

- **VUID-VkImageCreateInfo-format-01577**  
  If `format` is not a _multi-planar_ format, and `flags` does not include `VK_IMAGE_CREATE_ALIAS_BIT`, `flags` **must** not contain `VK_IMAGE_CREATE_DISJOINT_BIT`

- **VUID-VkImageCreateInfo-format-04712**  
  If `format` has a `_422` or `_420` suffix, `extent.width` **must** be a multiple of 2

- **VUID-VkImageCreateInfo-format-04713**  
  If `format` has a `_420` suffix, `extent.height` **must** be a multiple of 2

- **VUID-VkImageCreateInfo-format-09583**  
  If `format` is one of the `VK_FORMAT_PVRTC1_*_IMG` formats, `extent.width` **must** be a power of 2

- **VUID-VkImageCreateInfo-format-09584**  
  If `format` is one of the `VK_FORMAT_PVRTC1_*_IMG` formats, `extent.height` **must** be a power of 2

- **VUID-VkImageCreateInfo-tiling-02261**  
  If `tiling` is `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`, then the `pNext` chain **must** include exactly one of `VkImageDrmFormatModifierListCreateInfoEXT` or `VkImageDrmFormatModifierExplicitCreateInfoEXT` structures

- **VUID-VkImageCreateInfo-pNext-02262**  
  If the `pNext` chain includes a `VkImageDrmFormatModifierListCreateInfoEXT` or `VkImageDrmFormatModifierExplicitCreateInfoEXT` structure, then `tiling` **must** be `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT`

- **VUID-VkImageCreateInfo-tiling-02353**  
  If `tiling` is `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT` and `flags` contains `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`, then the `pNext` chain **must** include a `VkImageFormatListCreateInfo` structure with non-zero `viewFormatCount`

- **VUID-VkImageCreateInfo-flags-01533**  
  If `flags` contains `VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT` `format` **must** be a depth or depth/stencil format

- **VUID-VkImageCreateInfo-pNext-02393**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` structure whose `handleTypes` member includes `VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-pNext-02394**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` structure whose `handleTypes` member includes `VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID`, `mipLevels` **must** either be `1` or equal to the number of levels in the complete mipmap chain based on `extent.width`, `extent.height`, and `extent.depth`

- **VUID-VkImageCreateInfo-pNext-02396**  
  If the `pNext` chain includes a `VkExternalFormatANDROID` structure whose `externalFormat` member is not `0`, `flags` **must** not include `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`

- **VUID-VkImageCreateInfo-pNext-02397**  
  If the `pNext` chain includes a `VkExternalFormatANDROID` structure whose `externalFormat` member is not `0`, `usage` **must** not include any usages except `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, or `VK_IMAGE_USAGE_SAMPLED_BIT`

- **VUID-VkImageCreateInfo-pNext-09457**  
  If the `pNext` chain includes a `VkExternalFormatANDROID` structure whose `externalFormat` member is not `0`, and `externalFormatResolve` feature is not enabled, `usage` **must** not include `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` or `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-pNext-02398**  
  If the `pNext` chain includes a `VkExternalFormatANDROID` structure whose `externalFormat` member is not `0`, `tiling` **must** be `VK_IMAGE_TILING_OPTIMAL`

- **VUID-VkImageCreateInfo-pNext-08951**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` structure whose `handleTypes` member includes `VK_EXTERNAL_MEMORY_HANDLE_TYPE_SCREEN_BUFFER_BIT_QNX`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-pNext-08952**  
  If the `pNext` chain includes a `VkExternalMemoryImageCreateInfo` structure whose `handleTypes` member includes `VK_EXTERNAL_MEMORY_HANDLE_TYPE_SCREEN_BUFFER_BIT_QNX`, `mipLevels` **must** either be `1` or equal to the number of levels in the complete mipmap chain based on `extent.width`, `extent.height`, and `extent.depth`

- **VUID-VkImageCreateInfo-pNext-08953**  
  If the `pNext` chain includes a `VkExternalFormatQNX` structure whose `externalFormat` member is not `0`, `flags` **must** not include `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`

- **VUID-VkImageCreateInfo-pNext-08954**  
  If the `pNext` chain includes a `VkExternalFormatQNX` structure whose `externalFormat` member is not `0`, `usage` **must** not include any usages except `VK_IMAGE_USAGE_SAMPLED_BIT`

- **VUID-VkImageCreateInfo-pNext-08955**  
  If the `pNext` chain includes a `VkExternalFormatQNX` structure whose `externalFormat` member is not `0`, `tiling` **must** be `VK_IMAGE_TILING_OPTIMAL`

- **VUID-VkImageCreateInfo-format-02795**  
  If `format` is a depth-stencil format, `usage` includes `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure, then its `VkImageStencilUsageCreateInfo::stencilUsage` member **must** also include `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-format-02796**  
  If `format` is a depth-stencil format, `usage` does not include `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure, then its `VkImageStencilUsageCreateInfo::stencilUsage` member **must** also not include `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-format-02797**  
  If `format` is a depth-stencil format, `usage` includes `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure, then its `VkImageStencilUsageCreateInfo::stencilUsage` member **must** also include `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-format-02798**  
  If `format` is a depth-stencil format, `usage` does not include `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`, and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure, then its `VkImageStencilUsageCreateInfo::stencilUsage` member **must** also not include `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`

- **VUID-VkImageCreateInfo-Format-02536**  
  If `Format` is a depth-stencil format and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure with its `stencilUsage` member including `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, `extent.width` **must** be less than or equal to `VkPhysicalDeviceLimits::maxFramebufferWidth`

- **VUID-VkImageCreateInfo-format-02537**  
  If `format` is a depth-stencil format and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure with its `stencilUsage` member including `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, `extent.height` **must** be less than or equal to `VkPhysicalDeviceLimits::maxFramebufferHeight`

- **VUID-VkImageCreateInfo-format-02538**  
  If the `shaderStorageImageMultisample` feature is not enabled, `format` is a depth-stencil format and the `pNext` chain includes a `VkImageStencilUsageCreateInfo` structure with its `stencilUsage` including `VK_IMAGE_USAGE_STORAGE_BIT`, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-flags-02050**  
  If `flags` contains `VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV`, `imageType` **must** be `VK_IMAGE_TYPE_2D` or `VK_IMAGE_TYPE_3D`

- **VUID-VkImageCreateInfo-flags-02051**  
  If `flags` contains `VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV`, it **must** not contain `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` and the `format` **must** not be a depth/stencil format

- **VUID-VkImageCreateInfo-flags-02052**  
  If `flags` contains `VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV` and `imageType` is `VK_IMAGE_TYPE_2D`, `extent.width` and `extent.height` **must** be greater than `1`

- **VUID-VkImageCreateInfo-flags-02053**  
  If `flags` contains `VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV` and `imageType` is `VK_IMAGE_TYPE_3D`, `extent.width`, `extent.height`, and `extent.depth` **must** be greater than `1`

- **VUID-VkImageCreateInfo-imageType-02082**  
  If `usage` includes `VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-samples-02083**  
  If `usage` includes `VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR`, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-shadingRateImage-07727**  
  If the `shadingRateImage` feature is enabled and `usage` includes `VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV`, `tiling` **must** be `VK_IMAGE_TILING_OPTIMAL`

- **VUID-VkImageCreateInfo-flags-02565**  
  If `flags` contains `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT`, `tiling` **must** be `VK_IMAGE_TILING_OPTIMAL`

- **VUID-VkImageCreateInfo-flags-02566**  
  If `flags` contains `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-flags-02567**  
  If `flags` contains `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT`, `flags` **must** not contain `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`

- **VUID-VkImageCreateInfo-flags-02568**  
  If `flags` contains `VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT`, `mipLevels` **must** be `1`

- **VUID-VkImageCreateInfo-usage-04992**  
  If `usage` includes `VK_IMAGE_USAGE_INVOCATION_MASK_BIT_HUAWEI`, `tiling` **must** be `VK_IMAGE_TILING_LINEAR`

- **VUID-VkImageCreateInfo-imageView2DOn3DImage-04459**  
  If the `VK_KHR_portability_subset` extension is enabled, and `VkPhysicalDevicePortabilitySubsetFeaturesKHR::imageView2DOn3DImage` is `VK_FALSE`, `flags` **must** not contain `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`

- **VUID-VkImageCreateInfo-multisampleArrayImage-04460**  
  If the `VK_KHR_portability_subset` extension is enabled, and `VkPhysicalDevicePortabilitySubsetFeaturesKHR::multisampleArrayImage` is `VK_FALSE`, and `samples` is not `VK_SAMPLE_COUNT_1_BIT`, then `arrayLayers` **must** be `1`

- **VUID-VkImageCreateInfo-pNext-06722**  
  If a `VkImageFormatListCreateInfo` structure was included in the `pNext` chain and `format` is not a multi-planar format and `VkImageFormatListCreateInfo::viewFormatCount` is not zero, then each format in `VkImageFormatListCreateInfo::pViewFormats` **must** either be compatible with the `format` as described in the compatibility table or, if `flags` contains `VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT`, be an uncompressed format that is size-compatible with `format`

- **VUID-VkImageCreateInfo-pNext-10062**  
  If a `VkImageFormatListCreateInfo` structure was included in the `pNext` chain and `format` is a multi-planar format and `flags` contains `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and `VkImageFormatListCreateInfo::viewFormatCount` is not zero, then each format in `VkImageFormatListCreateInfo::pViewFormats` **must** be compatible with the `VkFormat` for the plane of the image format

- **VUID-VkImageCreateInfo-flags-04738**  
  If `flags` does not contain `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and the `pNext` chain includes a `VkImageFormatListCreateInfo` structure, then `VkImageFormatListCreateInfo::viewFormatCount` **must** be `0` or `1`

- **VUID-VkImageCreateInfo-usage-04815**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR`, `VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR`, or `VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR`, and `flags` does not include `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR`, then the `pNext` chain **must** include a `VkVideoProfileListInfoKHR` structure with `profileCount` greater than `0` and `pProfiles` including at least one `VkVideoProfileInfoKHR` structure with a `videoCodecOperation` member specifying a decode operation

- **VUID-VkImageCreateInfo-usage-04816**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR`, `VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR`, or `VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR`, and `flags` does not include `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR`, then the `pNext` chain **must** include a `VkVideoProfileListInfoKHR` structure with `profileCount` greater than `0` and `pProfiles` including at least one `VkVideoProfileInfoKHR` structure with a `videoCodecOperation` member specifying an encode operation

- **VUID-VkImageCreateInfo-flags-08328**  
  If `flags` includes `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR`, then `videoMaintenance1` **must** be enabled

- **VUID-VkImageCreateInfo-flags-08329**  
  If `flags` includes `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR` and `usage` does not include `VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR`, then `usage` **must** not include `VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR`

- **VUID-VkImageCreateInfo-flags-08331**  
  If `flags` includes `VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR`, then `usage` **must** not include `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR`, `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, or `VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR`

- **VUID-VkImageCreateInfo-pNext-06811**  
  If the `pNext` chain includes a `VkVideoProfileListInfoKHR` structure with `profileCount` greater than `0`, then `supportedVideoFormat` **must** be `VK_TRUE`

- **VUID-VkImageCreateInfo-pNext-10784**  
  If the `pNext` chain includes a `VkVideoProfileListInfoKHR` structure and for any element of its `pProfiles` member `videoCodecOperation` is `VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR`, then the `videoDecodeVP9` feature **must** be enabled

- **VUID-VkImageCreateInfo-pNext-10250**  
  If the `pNext` chain includes a `VkVideoProfileListInfoKHR` structure and for any element of its `pProfiles` member `videoCodecOperation` is `VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR`, then the `videoEncodeAV1` feature **must** be enabled

- **VUID-VkImageCreateInfo-pNext-10920**  
  If the `pNext` chain includes a `VkVideoEncodeProfileRgbConversionInfoVALVE` structure, then the `videoEncodeRgbConversion` feature **must** be enabled

- **VUID-VkImageCreateInfo-usage-10251**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, then the `videoEncodeQuantizationMap` feature **must** be enabled

- **VUID-VkImageCreateInfo-usage-10252**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, `imageType` **must** be `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-usage-10253**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-usage-10254**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, then the `pNext` chain **must** include a `VkVideoProfileListInfoKHR` structure with `profileCount` equal to `1` and `pProfiles` pointing to a `VkVideoProfileInfoKHR` structure with a `videoCodecOperation` member specifying an encode operation

- **VUID-VkImageCreateInfo-usage-10255**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR`, then `VkVideoEncodeCapabilitiesKHR::flags` **must** include `VK_VIDEO_ENCODE_CAPABILITY_QUANTIZATION_DELTA_MAP_BIT_KHR`, as returned by `vkGetPhysicalDeviceVideoCapabilitiesKHR` for the video profile specified in the `pProfiles` member of the `VkVideoProfileListInfoKHR` structure included in the `pNext` chain

- **VUID-VkImageCreateInfo-usage-10256**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, then `VkVideoEncodeCapabilitiesKHR::flags` **must** include `VK_VIDEO_ENCODE_CAPABILITY_EMPHASIS_MAP_BIT_KHR`, as returned by `vkGetPhysicalDeviceVideoCapabilitiesKHR` for the video profile specified in the `pProfiles` member of the `VkVideoProfileListInfoKHR` structure included in the `pNext` chain

- **VUID-VkImageCreateInfo-usage-10257**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, `extent.width` **must** be less than or equal to `VkVideoEncodeQuantizationMapCapabilitiesKHR::maxQuantizationMapExtent.width`, as returned by `vkGetPhysicalDeviceVideoCapabilitiesKHR` for the video profile specified in the `pProfiles` member of the `VkVideoProfileListInfoKHR` structure included in the `pNext` chain

- **VUID-VkImageCreateInfo-usage-10258**  
  If `usage` includes `VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR` or `VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR`, `extent.height` **must** be less than or equal to `VkVideoEncodeQuantizationMapCapabilitiesKHR::maxQuantizationMapExtent.height`, as returned by `vkGetPhysicalDeviceVideoCapabilitiesKHR` for the video profile specified in the `pProfiles` member of the `VkVideoProfileListInfoKHR` structure included in the `pNext` chain

- **VUID-VkImageCreateInfo-pNext-06390**  
  If the `VkImage` is to be used to import memory from a `VkBufferCollectionFUCHSIA`, a `VkBufferCollectionImageCreateInfoFUCHSIA` structure **must** be chained to `pNext`

- **VUID-VkImageCreateInfo-multisampledRenderToSingleSampled-06882**  
  If the `multisampledRenderToSingleSampled` feature is not enabled, `flags` **must** not contain `VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT`

- **VUID-VkImageCreateInfo-flags-06883**  
  If `flags` contains `VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT`, `samples` **must** be `VK_SAMPLE_COUNT_1_BIT`

- **VUID-VkImageCreateInfo-pNext-06743**  
  If the `pNext` chain includes a `VkImageCompressionControlEXT` structure, `format` is a multi-planar format, and `VkImageCompressionControlEXT::flags` includes `VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT`, then `VkImageCompressionControlEXT::compressionControlPlaneCount` **must** be equal to the number of planes in `format`

- **VUID-VkImageCreateInfo-pNext-06744**  
  If the `pNext` chain includes a `VkImageCompressionControlEXT` structure, `format` is not a multi-planar format, and `VkImageCompressionControlEXT::flags` includes `VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT`, then `VkImageCompressionControlEXT::compressionControlPlaneCount` **must** be 1

- **VUID-VkImageCreateInfo-pNext-06746**  
  If the `pNext` chain includes a `VkImageCompressionControlEXT` structure, it **must** not contain a `VkImageDrmFormatModifierExplicitCreateInfoEXT` structure

- **VUID-VkImageCreateInfo-flags-08104**  
  If `flags` includes `VK_IMAGE_CREATE_DESCRIPTOR_HEAP_CAPTURE_REPLAY_BIT_EXT`, the `descriptorBufferCaptureReplay` or `descriptorHeapCaptureReplay` feature **must** be enabled

- **VUID-VkImageCreateInfo-pNext-08105**  
  If the `pNext` chain includes a `VkOpaqueCaptureDescriptorDataCreateInfoEXT` structure, `flags` **must** contain `VK_IMAGE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT`

- **VUID-VkImageCreateInfo-pNext-06783**  
  If the `pNext` chain includes a `VkExportMetalObjectCreateInfoEXT` structure, its `exportObjectType` member **must** be either `VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT` or `VK_EXPORT_METAL_OBJECT_TYPE_METAL_IOSURFACE_BIT_EXT`

- **VUID-VkImageCreateInfo-pNext-06784**  
  If the `pNext` chain includes a `VkImportMetalTextureInfoEXT` structure its `plane` member **must** be `VK_IMAGE_ASPECT_PLANE_0_BIT`, `VK_IMAGE_ASPECT_PLANE_1_BIT`, or `VK_IMAGE_ASPECT_PLANE_2_BIT`

- **VUID-VkImageCreateInfo-pNext-06785**  
  If the `pNext` chain includes a `VkImportMetalTextureInfoEXT` structure and the image does not have a multi-planar format, then `VkImportMetalTextureInfoEXT::plane` **must** be `VK_IMAGE_ASPECT_PLANE_0_BIT`

- **VUID-VkImageCreateInfo-pNext-06786**  
  If the `pNext` chain includes a `VkImportMetalTextureInfoEXT` structure and the image has a multi-planar format with only two planes, then `VkImportMetalTextureInfoEXT::plane` **must** not be `VK_IMAGE_ASPECT_PLANE_2_BIT`

- **VUID-VkImageCreateInfo-imageCreateFormatFeatures-09048**  
  If `imageCreateFormatFeatures` (as defined in Image Creation Limits) does not contain `VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT`, then `usage` **must** not contain `VK_IMAGE_USAGE_HOST_TRANSFER_BIT`

- **VUID-VkImageCreateInfo-usage-10245**  
  If `usage` includes `VK_IMAGE_USAGE_HOST_TRANSFER_BIT`, then the `hostImageCopy` feature **must** be enabled

- **VUID-VkImageCreateInfo-tileMemoryHeap-10766**  
  If the `tileMemoryHeap` feature is not enabled, `usage` **must** not include `VK_IMAGE_USAGE_TILE_MEMORY_BIT_QCOM`

- **VUID-VkImageCreateInfo-pNext-09653**  
  If the `pNext` chain contains a `VkImageAlignmentControlCreateInfoMESA` structure, `tiling` **must** be `VK_IMAGE_TILING_OPTIMAL`

- **VUID-VkImageCreateInfo-pNext-09654**  
  If the `pNext` chain contains a `VkImageAlignmentControlCreateInfoMESA` structure, it **must** not contain a `VkExternalMemoryImageCreateInfo` structure

- **VUID-VkImageCreateInfo-tiling-09711**  
  If `tiling` is VK_IMAGE_TILING_LINEAR then `VK_IMAGE_USAGE_TENSOR_ALIASING_BIT_ARM` **must** not be set in `usage`

- **VUID-VkImageCreateInfo-flags-13355**  
  If `flags` contains `VK_IMAGE_CREATE_ALIAS_SINGLE_LAYER_DESCRIPTOR_BIT_KHR`, the `maintenance11` feature **must** be enabled

- **VUID-VkImageCreateInfo-flags-13356**  
  If `flags` contains `VK_IMAGE_CREATE_ALIAS_SINGLE_LAYER_DESCRIPTOR_BIT_KHR`, `imageType` **must** be `VK_IMAGE_TYPE_1D` or `VK_IMAGE_TYPE_2D`

- **VUID-VkImageCreateInfo-None-12279**  
  If Vulkan 1.3 is not supported and the `ycbcr2plane444Formats` feature is not enabled, `format` **must** not be `VK_FORMAT_G8_B8R8_2PLANE_444_UNORM`, `VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16`, `VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16`, or `VK_FORMAT_G16_B16R16_2PLANE_444_UNORM`

- **VUID-VkImageCreateInfo-flags-11281**  
  If `VkOpaqueCaptureDataCreateInfoEXT::pData` is not `NULL`, `flags` **must** contain `VK_IMAGE_CREATE_DESCRIPTOR_HEAP_CAPTURE_REPLAY_BIT_EXT`

- **VUID-VkImageCreateInfo-pData-11286**  
  If `flags` contains `VK_IMAGE_CREATE_DESCRIPTOR_HEAP_CAPTURE_REPLAY_BIT_EXT`, and `VkOpaqueCaptureDataCreateInfoEXT::pData` is not `NULL`, `VkOpaqueCaptureDataCreateInfoEXT::pData->size` **must** be equal to `imageCaptureReplayOpaqueDataSize`

- **VUID-VkImageCreateInfo-pNext-12479**  
  If the `pNext` chain contains a `VkImageTilingControlCreateInfoEXT` structure, `tiling` **must** not be `VK_IMAGE_TILING_LINEAR`

- **VUID-VkImageCreateInfo-pNext-12480**  
  If the `pNext` chain includes a `VkImageTilingControlCreateInfoEXT` structure, then the `pNext` chain **must** not include a `VkImageDrmFormatModifierExplicitCreateInfoEXT` structure

- **VUID-VkImageCreateInfo-pNext-12442**  
  `pNext` **must** contain at most one of the structures `VkImageStencilUsageCreateInfo` and `VkImageStencilUsage2CreateInfoKHR`

## Valid Usage (Implicit)

- **VUID-VkImageCreateInfo-sType-sType**  
  `sType` **must** be `VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO`

- **VUID-VkImageCreateInfo-pNext-pNext**  
  Each `pNext` member of any structure (including this one) in the `pNext` chain **must** be either `NULL` or a pointer to a valid instance of `VkBufferCollectionImageCreateInfoFUCHSIA`, `VkDataGraphOpticalFlowImageFormatInfoARM`, `VkDedicatedAllocationImageCreateInfoNV`, `VkExportMetalObjectCreateInfoEXT`, `VkExternalFormatANDROID`, `VkExternalFormatOHOS`, `VkExternalFormatQNX`, `VkExternalMemoryImageCreateInfo`, `VkExternalMemoryImageCreateInfoNV`, `VkImageAlignmentControlCreateInfoMESA`, `VkImageCompressionControlEXT`, `VkImageCreateFlags2CreateInfoKHR`, `VkImageDrmFormatModifierExplicitCreateInfoEXT`, `VkImageDrmFormatModifierListCreateInfoEXT`, `VkImageFormatListCreateInfo`, `VkImageStencilUsage2CreateInfoKHR`, `VkImageStencilUsageCreateInfo`, `VkImageSwapchainCreateInfoKHR`, `VkImageTilingControlCreateInfoEXT`, `VkImageUsageFlags2CreateInfoKHR`, `VkImportMetalIOSurfaceInfoEXT`, `VkImportMetalTextureInfoEXT`, `VkOpaqueCaptureDataCreateInfoEXT`, `VkOpaqueCaptureDescriptorDataCreateInfoEXT`, `VkOpticalFlowImageFormatInfoNV`, or `VkVideoProfileListInfoKHR`

- **VUID-VkImageCreateInfo-sType-unique**  
  The `sType` value of each structure in the `pNext` chain **must** be unique, with the exception of structures of type `VkExportMetalObjectCreateInfoEXT` or `VkImportMetalTextureInfoEXT`

- **VUID-VkImageCreateInfo-flags-parameter**  
  `flags` **must** be a valid combination of `VkImageCreateFlagBits` values

- **VUID-VkImageCreateInfo-imageType-parameter**  
  `imageType` **must** be a valid `VkImageType` value

- **VUID-VkImageCreateInfo-format-parameter**  
  `format` **must** be a valid `VkFormat` value

- **VUID-VkImageCreateInfo-samples-parameter**  
  `samples` **must** be a valid `VkSampleCountFlagBits` value

- **VUID-VkImageCreateInfo-tiling-parameter**  
  `tiling` **must** be a valid `VkImageTiling` value

- **VUID-VkImageCreateInfo-usage-parameter**  
  `usage` **must** be a valid combination of `VkImageUsageFlagBits` values

- **VUID-VkImageCreateInfo-usage-requiredbitmask**  
  `usage` **must** not be `0`

- **VUID-VkImageCreateInfo-sharingMode-parameter**  
  `sharingMode` **must** be a valid `VkSharingMode` value

- **VUID-VkImageCreateInfo-initialLayout-parameter**  
  `initialLayout` **must** be a valid `VkImageLayout` value

## See Also

`VK_VERSION_1_0`, `VkDeviceImageMemoryRequirements`, `VkDeviceImageSubresourceInfo`, `VkExtent3D`, `VkFormat`, `VkImageCreateFlags`, `VkImageFormatConstraintsInfoFUCHSIA`, `VkImageLayout`, `VkImageTiling`, `VkImageType`, `VkImageUsageFlags`, `VkSampleCountFlagBits`, `VkSharingMode`, `VkStructureType`, `vkCreateImage`

## Document Notes

For more information, see the Vulkan Specification.

This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly.
