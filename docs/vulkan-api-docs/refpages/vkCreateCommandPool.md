# vkCreateCommandPool(3)

## Name

vkCreateCommandPool - Create a new command pool object

## C Specification

To create a command pool, call:

```c
// Provided by VK_VERSION_1_0
VkResult vkCreateCommandPool(
    VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool);
```

## Parameters

- `device` — the logical device that creates the command pool.
- `pCreateInfo` — pointer to a `VkCommandPoolCreateInfo` structure specifying the state of the command pool object.
- `pAllocator` — controls host memory allocation as described in the Memory Allocation chapter.
- `pCommandPool` — pointer to a `VkCommandPool` handle in which the created pool is returned.

## Description

### Valid Usage

- **VUID-vkCreateCommandPool-queueFamilyIndex-01937**  
  `pCreateInfo->queueFamilyIndex` **must** be the index of a queue family available in the logical device `device`

### Valid Usage (Implicit)

- **VUID-vkCreateCommandPool-device-parameter**  
  `device` **must** be a valid `VkDevice` handle
- **VUID-vkCreateCommandPool-pCreateInfo-parameter**  
  `pCreateInfo` **must** be a valid pointer to a valid `VkCommandPoolCreateInfo` structure
- **VUID-vkCreateCommandPool-pAllocator-parameter**  
  If `pAllocator` is not `NULL`, `pAllocator` **must** be a valid pointer to a valid `VkAllocationCallbacks` structure
- **VUID-vkCreateCommandPool-pCommandPool-parameter**  
  `pCommandPool` **must** be a valid pointer to a `VkCommandPool` handle
- **VUID-vkCreateCommandPool-device-queuecount**  
  The device **must** have been created with at least `1` queue

### Return Codes

Success:

- `VK_SUCCESS`

Failure:

- `VK_ERROR_OUT_OF_DEVICE_MEMORY`
- `VK_ERROR_OUT_OF_HOST_MEMORY`
- `VK_ERROR_UNKNOWN`
- `VK_ERROR_VALIDATION_FAILED`

## See Also

`VK_VERSION_1_0`, `VkAllocationCallbacks`, `VkCommandPool`, `VkCommandPoolCreateInfo`, `VkDevice`

## Document Notes

For more information, see the [Vulkan Specification](https://registry.khronos.org/vulkan/specs/latest/html/chapters/cmdbuffers.html#vkCreateCommandPool). This page is extracted from the Vulkan Specification; fixes and changes should be made to the Specification, not directly.
