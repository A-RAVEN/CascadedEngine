# VkWriteDescriptorSet(3)

Vulkan Documentation Project — Vulkan API Reference Pages — Structures — W

## Name

`VkWriteDescriptorSet` — Structure specifying the parameters of a descriptor set write operation

## C Specification

The `VkWriteDescriptorSet` structure is defined as:

```c
// Provided by VK_VERSION_1_0
typedef struct VkWriteDescriptorSet {
    VkStructureType                  sType;
    const void*                      pNext;
    VkDescriptorSet                  dstSet;
    uint32_t                         dstBinding;
    uint32_t                         dstArrayElement;
    uint32_t                         descriptorCount;
    VkDescriptorType                 descriptorType;
    const VkDescriptorImageInfo*     pImageInfo;
    const VkDescriptorBufferInfo*    pBufferInfo;
    const VkBufferView*              pTexelBufferView;
} VkWriteDescriptorSet;
```

## Members

- `sType` is a `VkStructureType` value identifying this structure.
- `pNext` is `NULL` or a pointer to a structure extending this structure.
- `dstSet` is the destination descriptor set to update.
- `dstBinding` is the descriptor binding within that set.
- `dstArrayElement` is the starting element in that array. If the descriptor binding identified by `dstSet` and `dstBinding` has a descriptor type of `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, then `dstArrayElement` specifies the starting byte offset within the binding.
- `descriptorCount` is the number of descriptors to update. If the binding has a descriptor type of `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, then `descriptorCount` specifies the number of bytes to update. Otherwise, `descriptorCount` is one of:
  - the number of elements in `pImageInfo`
  - the number of elements in `pBufferInfo`
  - the number of elements in `pTexelBufferView`
  - a value matching the `dataSize` member of a `VkWriteDescriptorSetInlineUniformBlock` structure in the `pNext` chain
  - a value matching the `accelerationStructureCount` of a `VkWriteDescriptorSetAccelerationStructureKHR` or `VkWriteDescriptorSetAccelerationStructureNV` structure in the `pNext` chain
  - a value matching the `descriptorCount` of a `VkWriteDescriptorSetTensorARM` structure in the `pNext` chain
- `descriptorType` is a `VkDescriptorType` specifying the type of each descriptor in `pImageInfo`, `pBufferInfo`, or `pTexelBufferView`. If the `VkDescriptorSetLayoutBinding` for `dstSet` at `dstBinding` is not `VK_DESCRIPTOR_TYPE_MUTABLE_EXT`, `descriptorType` must be the same type as specified in the layout binding. The type also controls which array the descriptors are taken from.
- `pImageInfo` is a pointer to an array of `VkDescriptorImageInfo` structures or is ignored, as described below.
- `pBufferInfo` is a pointer to an array of `VkDescriptorBufferInfo` structures or is ignored, as described below.
- `pTexelBufferView` is a pointer to an array of `VkBufferView` handles or is ignored, as described below.

## Description

Members of `pImageInfo`, `pBufferInfo`, and `pTexelBufferView` are only accessed when they correspond to a descriptor type being defined; otherwise they are ignored. Which members are accessed per descriptor type:

- `VK_DESCRIPTOR_TYPE_SAMPLER` — only the `sampler` member of each `pImageInfo` element.
- `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT` — only the `imageView` and `imageLayout` members of each `pImageInfo` element.
- `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` — all members of each `pImageInfo` element.
- `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC` — all members of each `pBufferInfo` element.
- `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`, `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER` — each element of `pTexelBufferView`.

For `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, the `pImageInfo`/`pBufferInfo`/`pTexelBufferView` members are not accessed; source data comes from `VkWriteDescriptorSetInlineUniformBlock` in the `pNext` chain. For `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`, source data comes from `VkWriteDescriptorSetAccelerationStructureKHR`; for `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV`, from `VkWriteDescriptorSetAccelerationStructureNV`; for `VK_DESCRIPTOR_TYPE_TENSOR_ARM`, from `VkWriteDescriptorSetTensorARM`.

If the `nullDescriptor` feature is enabled, the buffer, acceleration structure, tensor, imageView, or bufferView can be `VK_NULL_HANDLE`. Loads from a null descriptor return zero values; stores and atomics to a null descriptor are discarded. A null acceleration structure descriptor results in the miss shader being invoked.

If the destination descriptor is a mutable descriptor, the active descriptor type becomes `descriptorType`.

### Consecutive Binding Updates

If `dstBinding` has fewer than `descriptorCount` array elements remaining from `dstArrayElement`, the remainder updates binding `dstBinding`+1 starting at array element zero. Bindings with a `descriptorCount` of zero are skipped. This applies recursively. Consecutive bindings must have identical `VkDescriptorType`, `VkShaderStageFlags`, `VkDescriptorBindingFlagBits`, and immutable samplers references. For `VK_DESCRIPTOR_TYPE_MUTABLE_EXT`, the supported descriptor types in `VkMutableDescriptorTypeCreateInfoEXT` must be equally defined.

The same applies to `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK` where `descriptorCount` is bytes and `dstArrayElement` is a byte offset; overflow continues into `dstBinding`+1 starting at offset zero.

## Valid Usage

- **VUID-VkWriteDescriptorSet-dstBinding-00315** — `dstBinding` must be less than or equal to the maximum value of `binding` of all `VkDescriptorSetLayoutBinding` structures specified when `dstSet`'s descriptor set layout was created
- **VUID-VkWriteDescriptorSet-dstBinding-00316** — `dstBinding` must be a binding with a non-zero `descriptorCount`
- **VUID-VkWriteDescriptorSet-dstBinding-10009** — `dstBinding` must be a binding with a non-zero `VkDescriptorSetLayoutCreateInfo::bindingCount`
- **VUID-VkWriteDescriptorSet-descriptorCount-00317** — All consecutive bindings updated via a single `VkWriteDescriptorSet` structure, except those with a `descriptorCount` of zero, must have identical `descriptorType`
- **VUID-VkWriteDescriptorSet-descriptorCount-10776** — All consecutive bindings updated via a single `VkWriteDescriptorSet` structure, except those with a `descriptorCount` of zero, must have identical `stageFlags`
- **VUID-VkWriteDescriptorSet-descriptorCount-00318** — All consecutive bindings updated via a single `VkWriteDescriptorSet` structure, except those with a `descriptorCount` of zero, must all either use immutable samplers or must all not use immutable samplers
- **VUID-VkWriteDescriptorSet-descriptorCount-10777** — All consecutive bindings updated via a single `VkWriteDescriptorSet` structure, except those with a `descriptorCount` of zero, must have identical `VkDescriptorBindingFlagBits`
- **VUID-VkWriteDescriptorSet-descriptorType-00319** — `descriptorType` must match the type of `dstBinding` within `dstSet`
- **VUID-VkWriteDescriptorSet-dstSet-00320** — `dstSet` must be a valid `VkDescriptorSet` handle
- **VUID-VkWriteDescriptorSet-dstArrayElement-00321** — The sum of `dstArrayElement` and `descriptorCount` must be less than or equal to the number of array elements in the descriptor set binding specified by `dstBinding`, and all applicable consecutive bindings
- **VUID-VkWriteDescriptorSet-descriptorType-02219** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, `dstArrayElement` must be an integer multiple of `4`
- **VUID-VkWriteDescriptorSet-descriptorType-02220** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, `descriptorCount` must be an integer multiple of `4`
- **VUID-VkWriteDescriptorSet-descriptorType-02994** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER`, each element of `pTexelBufferView` must be either a valid `VkBufferView` handle or `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-02995** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER` and the `nullDescriptor` feature is not enabled, each element of `pTexelBufferView` must not be `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-00324** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, or `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC`, `pBufferInfo` must be a valid pointer to an array of `descriptorCount` valid `VkDescriptorBufferInfo` structures
- **VUID-VkWriteDescriptorSet-descriptorType-00325** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLER` or `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and `dstSet` was not allocated with a layout that included immutable samplers for `dstBinding` with `descriptorType`, the `sampler` member of each element of `pImageInfo` must be a valid `VkSampler` object
- **VUID-VkWriteDescriptorSet-descriptorType-02996** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, or `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, the `imageView` member of each element of `pImageInfo` must be either a valid `VkImageView` handle or `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-02997** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, or `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, and the `nullDescriptor` feature is not enabled, the `imageView` member of each element of `pImageInfo` must not be `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-07683** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM`, `VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM`, or `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, the `imageView` member of each element of `pImageInfo` must not be `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-02221** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, the `pNext` chain must include a `VkWriteDescriptorSetInlineUniformBlock` structure whose `dataSize` member equals `descriptorCount`
- **VUID-VkWriteDescriptorSet-descriptorType-02382** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`, the `pNext` chain must include a `VkWriteDescriptorSetAccelerationStructureKHR` structure whose `accelerationStructureCount` member equals `descriptorCount`
- **VUID-VkWriteDescriptorSet-descriptorType-03817** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV`, the `pNext` chain must include a `VkWriteDescriptorSetAccelerationStructureNV` structure whose `accelerationStructureCount` member equals `descriptorCount`
- **VUID-VkWriteDescriptorSet-descriptorType-09945** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_TENSOR_ARM`, the `pNext` chain must include a `VkWriteDescriptorSetTensorARM` structure whose `tensorViewCount` member equals `descriptorCount`
- **VUID-VkWriteDescriptorSet-descriptorType-01946** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, the `imageView` member of each `pImageInfo` element must have been created without a `VkSamplerYcbcrConversionInfo` structure in its `pNext` chain
- **VUID-VkWriteDescriptorSet-descriptorType-02738** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and any element of `pImageInfo` has an `imageView` created with a `VkSamplerYcbcrConversionInfo` in its `pNext` chain, then `dstSet` must have been allocated with a layout that included immutable samplers for `dstBinding`, and the corresponding immutable sampler must have been created with an identically defined `VkSamplerYcbcrConversionInfo` object
- **VUID-VkWriteDescriptorSet-descriptorType-01948** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, and `dstSet` was allocated with a layout that included immutable samplers for `dstBinding`, the `imageView` member of each `pImageInfo` element which corresponds to an immutable sampler that enables sampler Y′CBCR conversion must have been created with a `VkSamplerYcbcrConversionInfo` in its `pNext` chain with an identically defined `VkSamplerYcbcrConversionInfo` to the corresponding immutable sampler
- **VUID-VkWriteDescriptorSet-descriptorType-09506** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `dstSet` was allocated with a layout that included immutable samplers for `dstBinding` enabling sampler Y′CBCR conversion, then `imageView` must not be `VK_NULL_HANDLE`
- **VUID-VkWriteDescriptorSet-descriptorType-00327** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` or `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, the `offset` member of each element of `pBufferInfo` must be a multiple of `VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment`
- **VUID-VkWriteDescriptorSet-descriptorType-00328** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC`, the `offset` member of each element of `pBufferInfo` must be a multiple of `VkPhysicalDeviceLimits::minStorageBufferOffsetAlignment`
- **VUID-VkWriteDescriptorSet-descriptorType-00329** — If `descriptorType` is a buffer descriptor type and the `buffer` member of any element of `pBufferInfo` is the handle of a non-sparse buffer, that buffer must be bound completely and contiguously to a single `VkDeviceMemory` object
- **VUID-VkWriteDescriptorSet-descriptorType-00330** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` or `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, the `buffer` member of each element of `pBufferInfo` must have been created with the `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-00331** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC`, the `buffer` member of each element of `pBufferInfo` must have been created with the `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-00332** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` or `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, the `range` member of each element of `pBufferInfo`, or the effective range if `range` is `VK_WHOLE_SIZE`, must be less than or equal to `VkPhysicalDeviceLimits::maxUniformBufferRange`
- **VUID-VkWriteDescriptorSet-descriptorType-00333** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` or `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC`, and the `shader64BitIndexing` feature is not enabled, the `range` member of each element of `pBufferInfo`, or the effective range if `range` is `VK_WHOLE_SIZE`, must be less than or equal to `VkPhysicalDeviceLimits::maxStorageBufferRange`
- **VUID-VkWriteDescriptorSet-descriptorType-08765** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`, the `pTexelBufferView` buffer view usage must include `VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT`
- **VUID-VkWriteDescriptorSet-descriptorType-08766** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER`, the `pTexelBufferView` buffer view usage must include `VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT`
- **VUID-VkWriteDescriptorSet-descriptorType-00336** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` or `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, the `imageView` member of each element of `pImageInfo` must have been created with the identity swizzle
- **VUID-VkWriteDescriptorSet-descriptorType-00337** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` or `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, the `imageView` member of each element of `pImageInfo` must have been created with the `VK_IMAGE_USAGE_SAMPLED_BIT` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-04149** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`, the `imageLayout` member of each element of `pImageInfo` must be in the list given in Sampled Image
- **VUID-VkWriteDescriptorSet-descriptorType-04150** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, the `imageLayout` member of each element of `pImageInfo` must be in the list given in Combined Image Sampler
- **VUID-VkWriteDescriptorSet-descriptorType-04151** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, the `imageLayout` member of each element of `pImageInfo` must be in the list given in Input Attachment
- **VUID-VkWriteDescriptorSet-descriptorType-04152** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, the `imageLayout` member of each element of `pImageInfo` must be in the list given in Storage Image
- **VUID-VkWriteDescriptorSet-descriptorType-00338** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, the `imageView` member of each element of `pImageInfo` must have been created with the `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-00339** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, the `imageView` member of each element of `pImageInfo` must have been created with the `VK_IMAGE_USAGE_STORAGE_BIT` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-02752** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLER`, then `dstSet` must not have been allocated with a layout that included immutable samplers for `dstBinding`
- **VUID-VkWriteDescriptorSet-dstSet-04611** — If the `VkDescriptorSetLayoutBinding` for `dstSet` at `dstBinding` is `VK_DESCRIPTOR_TYPE_MUTABLE_EXT`, the new active descriptor type `descriptorType` must exist in the corresponding `pMutableDescriptorTypeLists` list for `dstBinding`
- **VUID-VkWriteDescriptorSet-descriptorType-06450** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, the `imageView` member of each element of `pImageInfo` must have been created either without a `VkImageViewMinLodCreateInfoEXT` in the `pNext` chain or with a `VkImageViewMinLodCreateInfoEXT::minLod` of `0.0`
- **VUID-VkWriteDescriptorSet-descriptorType-06942** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM`, the `imageView` member of each element of `pImageInfo` must have been created with a view created with an `image` created with the `VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM` usage flag set
- **VUID-VkWriteDescriptorSet-descriptorType-06943** — If `descriptorType` is `VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM`, the `imageView` member of each element of `pImageInfo` must have been created with a view created with an `image` created with the `VK_IMAGE_USAGE_SAMPLE_BLOCK_MATCH_BIT_QCOM` usage flag set

## Valid Usage (Implicit)

- **VUID-VkWriteDescriptorSet-sType-sType** — `sType` must be `VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET`
- **VUID-VkWriteDescriptorSet-pNext-pNext** — Each `pNext` member of any structure (including this one) in the `pNext` chain must be either `NULL` or a pointer to a valid instance of `VkWriteDescriptorSetAccelerationStructureKHR`, `VkWriteDescriptorSetAccelerationStructureNV`, `VkWriteDescriptorSetInlineUniformBlock`, `VkWriteDescriptorSetPartitionedAccelerationStructureNV`, or `VkWriteDescriptorSetTensorARM`
- **VUID-VkWriteDescriptorSet-sType-unique** — The `sType` value of each structure in the `pNext` chain must be unique
- **VUID-VkWriteDescriptorSet-descriptorType-parameter** — `descriptorType` must be a valid `VkDescriptorType` value
- **VUID-VkWriteDescriptorSet-descriptorCount-arraylength** — `descriptorCount` must be greater than `0`
- **VUID-VkWriteDescriptorSet-commonparent** — Both of `dstSet`, and the elements of `pTexelBufferView` that are valid handles of non-ignored parameters must have been created, allocated, or retrieved from the same `VkDevice`

## See Also

`VK_VERSION_1_0`, `VkBufferView`, `VkDescriptorBufferInfo`, `VkDescriptorImageInfo`, `VkDescriptorSet`, `VkDescriptorType`, `VkPushDescriptorSetInfo`, `VkStructureType`, `vkCmdPushDescriptorSet`, `vkUpdateDescriptorSets`

## Document Notes

For more information, see the Vulkan Specification (descriptor sets chapter, `VkWriteDescriptorSet` section). This page is extracted from the Vulkan Specification; fixes and changes should be made to the Specification, not directly.

*Source: Khronos Vulkan Documentation Project — Vulkan API Reference Pages (Vulkan Specification, licensed by Khronos Group under the Apache 2.0 / MIT-style spec license).*
