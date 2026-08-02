# vkBindBufferMemory(3) — Vulkan Documentation Project

## Name

「vkBindBufferMemory - Bind device memory to a buffer object」

## C Specification

To attach memory to a buffer object, call:

```c
// Provided by VK_VERSION_1_0
VkResult vkBindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset);
```

## Parameters

- `device` — the logical device that owns the buffer and memory.
- `buffer` — the buffer to be attached to memory.
- `memory` — a `VkDeviceMemory` object describing the device memory to attach.
- `memoryOffset` — the start offset of the region of `memory` bound to the buffer. The bytes returned in `VkMemoryRequirements::size`, starting at `memoryOffset`, will be bound to the buffer.

## Description

This call is equivalent to passing the same parameters through `VkBindBufferMemoryInfo` to `vkBindBufferMemory2`.

### Valid Usage

- **VUID-vkBindBufferMemory-buffer-07459** — "`buffer` **must** not have been bound to a memory object"
- **VUID-vkBindBufferMemory-buffer-01030** — "`buffer` **must** not have been created with any sparse memory binding flags"
- **VUID-vkBindBufferMemory-memoryOffset-01031** — "`memoryOffset` **must** be less than the size of `memory`"
- **VUID-vkBindBufferMemory-memory-01035** — `memory` must have been allocated with one of the memory types allowed in the `memoryTypeBits` member of `VkMemoryRequirements` from `vkGetBufferMemoryRequirements` with `buffer`.
- **VUID-vkBindBufferMemory-None-10739** — For memory from a heap without `VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM`, `memoryOffset` must be an integer multiple of the `alignment` from `vkGetBufferMemoryRequirements` with `buffer`.
- **VUID-vkBindBufferMemory-memory-10740** — For memory from a heap with `VK_MEMORY_HEAP_TILE_MEMORY_BIT_QCOM`, `memoryOffset` must be an integer multiple of the `alignment` in `VkTileMemoryRequirementsQCOM`.
- **VUID-vkBindBufferMemory-None-10741** — Non-tile heap: the `size` from `VkMemoryRequirements` must be ≤ `size(memory) − memoryOffset`.
- **VUID-vkBindBufferMemory-memory-10742** — Tile heap: the `size` from `VkTileMemoryRequirementsQCOM` must be ≤ `size(memory) − memoryOffset`.
- **VUID-vkBindBufferMemory-buffer-01444** — If `buffer` requires a dedicated allocation, `memory` must have been allocated with `VkMemoryDedicatedAllocateInfo::buffer` equal to `buffer`.
- **VUID-vkBindBufferMemory-memory-01508** — If `memory`'s `VkMemoryAllocateInfo` included `VkMemoryDedicatedAllocateInfo` with a non-`VK_NULL_HANDLE` `buffer`, then `buffer` must equal it and `memoryOffset` must be zero.
- **VUID-vkBindBufferMemory-memory-10925** — If `memory`'s allocation included `VkMemoryDedicatedAllocateInfo`, its `image` member must have been `VK_NULL_HANDLE`.
- **VUID-vkBindBufferMemory-None-01898** — A protected `buffer` must be bound to memory allocated with a type reporting `VK_MEMORY_PROPERTY_PROTECTED_BIT`.
- **VUID-vkBindBufferMemory-None-01899** — A non-protected `buffer` must not be bound to memory allocated with a type reporting `VK_MEMORY_PROPERTY_PROTECTED_BIT`.
- **VUID-vkBindBufferMemory-buffer-01038** — With NV dedicated allocation, `memory` must have been allocated with `VkDedicatedAllocationMemoryAllocateInfoNV::buffer` matching a buffer with identical creation parameters, and `memoryOffset` must be zero.
- **VUID-vkBindBufferMemory-apiVersion-07920** — Without `VK_KHR_dedicated_allocation` (and API version < 1.1) and without NV dedicated allocation, `memory` must not have been allocated dedicated for a specific buffer or image.
- **VUID-vkBindBufferMemory-memory-02726** — If `VkExportMemoryAllocateInfo::handleTypes` used to allocate `memory` is nonzero, it must include at least one handle type set in `VkExternalMemoryBufferCreateInfo::handleTypes` when `buffer` was created.
- **VUID-vkBindBufferMemory-memory-02985** — For imported memory (other than `VkImportAndroidHardwareBufferInfoANDROID` with a non-`NULL` `buffer`), the external handle type must also have been set in `buffer`'s `handleTypes`.
- **VUID-vkBindBufferMemory-memory-02986** — For `VkImportAndroidHardwareBufferInfoANDROID` import with non-`NULL` `buffer`, `VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID` must also have been set in `buffer`'s `handleTypes`.
- **VUID-vkBindBufferMemory-bufferDeviceAddress-03339** — With `bufferDeviceAddress` enabled and `buffer` using `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`, `memory` must have been allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`.
- **VUID-vkBindBufferMemory-bufferDeviceAddressCaptureReplay-09200** — With `bufferDeviceAddressCaptureReplay` enabled and `buffer` using `VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT`, `memory` must have been allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT`.
- **VUID-vkBindBufferMemory-buffer-06408** — If `buffer` was created with `VkBufferCollectionBufferCreateInfoFUCHSIA` chained to `VkBufferCreateInfo::pNext`, `memory` must be allocated with `VkImportMemoryBufferCollectionFUCHSIA` chained to `VkMemoryAllocateInfo::pNext`.
- **VUID-vkBindBufferMemory-descriptorBufferCaptureReplay-08112** — If `buffer` used `VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT`, `memory` must have been allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`.
- **VUID-vkBindBufferMemory-buffer-09201** — If `buffer` used `VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT`, `memory` must have been allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT`.
- **VUID-vkBindBufferMemory-buffer-11408** — If `buffer` used `VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT` or `VK_BUFFER_USAGE_2_DESCRIPTOR_HEAP_BIT_EXT`, `memory` must have been allocated with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`.

### Valid Usage (Implicit)

- **VUID-vkBindBufferMemory-device-parameter** — "`device` **must** be a valid [VkDevice](VkDevice.html) handle"
- **VUID-vkBindBufferMemory-buffer-parameter** — "`buffer` **must** be a valid [VkBuffer](VkBuffer.html) handle"
- **VUID-vkBindBufferMemory-memory-parameter** — "`memory` **must** be a valid [VkDeviceMemory](VkDeviceMemory.html) handle"
- **VUID-vkBindBufferMemory-buffer-parent** — "`buffer` **must** have been created, allocated, or retrieved from `device`"
- **VUID-vkBindBufferMemory-memory-parent** — "`memory` **must** have been created, allocated, or retrieved from `device`"

## Host Synchronization

- "Host access to `buffer` **must** be externally synchronized"

## Return Codes

Success:

- `VK_SUCCESS`

Failure:

- `VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS_KHR`
- `VK_ERROR_OUT_OF_DEVICE_MEMORY`
- `VK_ERROR_OUT_OF_HOST_MEMORY`
- `VK_ERROR_UNKNOWN`
- `VK_ERROR_VALIDATION_FAILED`

## See Also

`VK_VERSION_1_0`, `VkBuffer`, `VkDevice`, `VkDeviceMemory`, `VkDeviceSize`

## Document Notes

For more information, see the [Vulkan Specification](https://docs.vulkan.org/spec/latest/chapters/resources.html#vkBindBufferMemory). This page is extracted from the Vulkan Specification; fixes and changes should be made to the Specification, not directly.
