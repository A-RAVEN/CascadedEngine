# vkGetDeviceQueue(3)

## Name

vkGetDeviceQueue - Get a queue handle from a device

## C Specification

To retrieve a handle to a `VkQueue` object, call:

```c
// Provided by VK_VERSION_1_0
void vkGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue);
```

## Parameters

| Parameter | Description |
|---|---|
| `device` | The logical device that owns the queue. |
| `queueFamilyIndex` | Index of the queue family to which the queue belongs. |
| `queueIndex` | Index within this queue family of the queue to retrieve. |
| `pQueue` | Pointer to a `VkQueue` object filled with the handle for the requested queue. |

## Description

`vkGetDeviceQueue` **must** only be used to get queues created with the `flags` parameter of `VkDeviceQueueCreateInfo` set to zero. For queues created with non-zero `flags`, use `vkGetDeviceQueue2`.

## Valid Usage

- **VUID-vkGetDeviceQueue-queueFamilyIndex-00384** — "`queueFamilyIndex` **must** be one of the queue family indices specified when `device` was created, via the `VkDeviceQueueCreateInfo` structure"
- **VUID-vkGetDeviceQueue-queueIndex-00385** — "`queueIndex` **must** be less than the value of `VkDeviceQueueCreateInfo`::`queueCount` for the queue family indicated by `queueFamilyIndex` when `device` was created"
- **VUID-vkGetDeviceQueue-flags-01841** — "`VkDeviceQueueCreateInfo`::`flags` **must** have been zero when `device` was created"

## Valid Usage (Implicit)

- **VUID-vkGetDeviceQueue-device-parameter** — "`device` **must** be a valid `VkDevice` handle"
- **VUID-vkGetDeviceQueue-pQueue-parameter** — "`pQueue` **must** be a valid pointer to a `VkQueue` handle"

## Return Values

This function returns `void`. The retrieved queue handle is written to `pQueue`. The page does not define additional error codes.

## See Also

`VK_VERSION_1_0`, `VkDevice`, `VkQueue`

## Document Notes

For more information, see the [Vulkan Specification](https://registry.khronos.org/vulkan/specs/latest/man/html/vkGetDeviceQueue.html). This page is extracted from the Vulkan Specification; fixes and changes should be made to the Specification, not directly to the page.
