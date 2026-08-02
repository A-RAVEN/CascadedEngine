# vkAcquireNextImageKHR(3)

## Name

vkAcquireNextImageKHR - Retrieve the index of the next available presentable image

## C Specification

To acquire an available presentable image to use, and retrieve the index of that image, call:

```c
// Provided by VK_KHR_swapchain
VkResult vkAcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex);
```

## Parameters

- `device` is the device associated with `swapchain`.
- `swapchain` is the non-retired swapchain from which an image is being acquired.
- `timeout` specifies how long the function waits, in nanoseconds, if no image is available.
- `semaphore` is `VK_NULL_HANDLE` or a semaphore defining a semaphore signal operation.
- `fence` is `VK_NULL_HANDLE` or a fence to signal.
- `pImageIndex` is a pointer to a `uint32_t` in which the index of the next image to use (i.e. an index into the array of images returned by `vkGetSwapchainImagesKHR`) is returned.

## Description

If the `swapchain` has been created with the `VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR` flag, the image whose index is returned in `pImageIndex` will be fully backed by memory before this call returns to the application, as if it is bound completely and contiguously to a single `VkDeviceMemory` object.

If `semaphore` defines a semaphore signal operation, its first synchronization scope includes acquisition of the image.

## Valid Usage

- **VUID-vkAcquireNextImageKHR-swapchain-01285**
  `swapchain` **must** not be in the retired state

- **VUID-vkAcquireNextImageKHR-semaphore-01286**
  If `semaphore` is not `VK_NULL_HANDLE`, it **must** be unsignaled

- **VUID-vkAcquireNextImageKHR-semaphore-01779**
  If `semaphore` is not `VK_NULL_HANDLE`, it **must** not have any uncompleted signal or wait operations pending

- **VUID-vkAcquireNextImageKHR-fence-01287**
  If `fence` is not `VK_NULL_HANDLE`, `fence` **must** be unsignaled

- **VUID-vkAcquireNextImageKHR-fence-10066**
  If `fence` is not `VK_NULL_HANDLE`, `fence` **must** not be associated with any other queue command that has not yet completed execution on that queue

- **VUID-vkAcquireNextImageKHR-semaphore-01780**
  `semaphore` and `fence` **must** not both be equal to `VK_NULL_HANDLE`

- **VUID-vkAcquireNextImageKHR-surface-07783**
  If forward progress cannot be guaranteed for the `surface` used to create the `swapchain` member of `pAcquireInfo`, `timeout` **must** not be `UINT64_MAX`

- **VUID-vkAcquireNextImageKHR-semaphore-03265**
  `semaphore` **must** have a `VkSemaphoreType` of `VK_SEMAPHORE_TYPE_BINARY`

## Valid Usage (Implicit)

- **VUID-vkAcquireNextImageKHR-device-parameter**
  `device` **must** be a valid `VkDevice` handle

- **VUID-vkAcquireNextImageKHR-swapchain-parameter**
  `swapchain` **must** be a valid `VkSwapchainKHR` handle

- **VUID-vkAcquireNextImageKHR-semaphore-parameter**
  If `semaphore` is not `VK_NULL_HANDLE`, `semaphore` **must** be a valid `VkSemaphore` handle

- **VUID-vkAcquireNextImageKHR-fence-parameter**
  If `fence` is not `VK_NULL_HANDLE`, `fence` **must** be a valid `VkFence` handle

- **VUID-vkAcquireNextImageKHR-pImageIndex-parameter**
  `pImageIndex` **must** be a valid pointer to a `uint32_t` value

- **VUID-vkAcquireNextImageKHR-swapchain-parent**
  `swapchain` **must** have been created, allocated, or retrieved from `device`

- **VUID-vkAcquireNextImageKHR-semaphore-parent**
  If `semaphore` is a valid handle, it **must** have been created, allocated, or retrieved from `device`

- **VUID-vkAcquireNextImageKHR-fence-parent**
  If `fence` is a valid handle, it **must** have been created, allocated, or retrieved from `device`

## Host Synchronization

- Host access to `swapchain` **must** be externally synchronized
- Host access to `semaphore` **must** be externally synchronized
- Host access to `fence` **must** be externally synchronized

## Return Codes

Success:

- `VK_NOT_READY`
- `VK_SUBOPTIMAL_KHR`
- `VK_SUCCESS`
- `VK_TIMEOUT`

Failure:

- `VK_ERROR_DEVICE_LOST`
- `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`
- `VK_ERROR_OUT_OF_DATE_KHR`
- `VK_ERROR_OUT_OF_DEVICE_MEMORY`
- `VK_ERROR_OUT_OF_HOST_MEMORY`
- `VK_ERROR_SURFACE_LOST_KHR`
- `VK_ERROR_UNKNOWN`
- `VK_ERROR_VALIDATION_FAILED`

## See Also

`VK_KHR_swapchain`, `VkDevice`, `VkFence`, `VkSemaphore`, `VkSwapchainKHR`

## Document Notes

For more information, see the Vulkan Specification.

This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly.
