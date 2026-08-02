# vkCreateDevice(3)

## Name
vkCreateDevice — Create a new device instance

## C Specification
Provided by `VK_VERSION_1_0`:

```c
VkResult vkCreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice);
```

## Parameters
| Parameter | Description |
|---|---|
| `physicalDevice` | must be one of the device handles returned from a call to `vkEnumeratePhysicalDevices` |
| `pCreateInfo` | pointer to a `VkDeviceCreateInfo` structure containing information about how to create the device |
| `pAllocator` | controls host memory allocation; may be `NULL`, else a valid `VkAllocationCallbacks` pointer |
| `pDevice` | pointer to a handle in which the created `VkDevice` is returned |

## Description
A logical device is created as a connection to a physical device. `vkCreateDevice` verifies that extensions and features requested via `ppEnabledExtensionNames` and `pEnabledFeatures` are supported:
- Unsupported extension → `VK_ERROR_EXTENSION_NOT_PRESENT`
- Unsupported feature → `VK_ERROR_FEATURE_NOT_PRESENT`

Extensions can be pre-checked with `vkEnumerateDeviceExtensionProperties`; features with `vkGetPhysicalDeviceFeatures`. After verifying and enabling the extensions, the `VkDevice` object is created and returned. Multiple logical devices may be created from the same physical device; lack of device-specific resources may cause failure → `VK_ERROR_TOO_MANY_OBJECTS`.

## Valid Usage (explicit)
- **VUID-vkCreateDevice-ppEnabledExtensionNames-01387**: "All required device extensions for each extension in the VkDeviceCreateInfo::ppEnabledExtensionNames list" must also be present in that list

## Valid Usage (implicit)
- **VUID-vkCreateDevice-physicalDevice-parameter**: "physicalDevice must be a valid VkPhysicalDevice handle"
- **VUID-vkCreateDevice-pCreateInfo-parameter**: "pCreateInfo must be a valid pointer to a valid VkDeviceCreateInfo structure"
- **VUID-vkCreateDevice-pAllocator-parameter**: "If pAllocator is not NULL, pAllocator must be a valid pointer to a valid VkAllocationCallbacks structure"
- **VUID-vkCreateDevice-pDevice-parameter**: "pDevice must be a valid pointer to a VkDevice handle"

## Return Codes
**Success:**
- `VK_SUCCESS`

**Failure:**
- `VK_ERROR_DEVICE_LOST`
- `VK_ERROR_EXTENSION_NOT_PRESENT`
- `VK_ERROR_FEATURE_NOT_PRESENT`
- `VK_ERROR_INITIALIZATION_FAILED`
- `VK_ERROR_OUT_OF_DEVICE_MEMORY`
- `VK_ERROR_OUT_OF_HOST_MEMORY`
- `VK_ERROR_TOO_MANY_OBJECTS`
- `VK_ERROR_UNKNOWN`
- `VK_ERROR_VALIDATION_FAILED`

## See Also
`VK_VERSION_1_0`, `VkAllocationCallbacks`, `VkDevice`, `VkDeviceCreateInfo`, `VkPhysicalDevice`

*Spec reference: Vulkan Specification, "vkCreateDevice" (Device Creation chapter).*
