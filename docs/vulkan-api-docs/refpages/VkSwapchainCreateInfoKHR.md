# VkSwapchainCreateInfoKHR — Complete Reference Summary

**Source:** Vulkan Documentation Project, `VkSwapchainCreateInfoKHR(3)` page (KHR_swapchain extension).

**Purpose:** Structure specifying parameters of a newly created swapchain object. Provided by `VK_KHR_swapchain`.

## C Structure Definition

```c
// Provided by VK_KHR_swapchain
typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType                  sType;
    const void*                      pNext;
    VkSwapchainCreateFlagsKHR        flags;
    VkSurfaceKHR                     surface;
    uint32_t                         minImageCount;
    VkFormat                         imageFormat;
    VkColorSpaceKHR                  imageColorSpace;
    VkExtent2D                       imageExtent;
    uint32_t                         imageArrayLayers;
    VkImageUsageFlags                imageUsage;
    VkSharingMode                    imageSharingMode;
    uint32_t                         queueFamilyIndexCount;
    const uint32_t*                  pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR    preTransform;
    VkCompositeAlphaFlagBitsKHR      compositeAlpha;
    VkPresentModeKHR                 presentMode;
    VkBool32                         clipped;
    VkSwapchainKHR                   oldSwapchain;
} VkSwapchainCreateInfoKHR;
```

## Members

| Member | Description |
|---|---|
| `sType` | A `VkStructureType` identifying this structure. |
| `pNext` | `NULL` or pointer to an extending structure. |
| `flags` | Bitmask of `VkSwapchainCreateFlagBitsKHR` for creation parameters. |
| `surface` | Surface the swapchain presents to; on success the swapchain becomes associated with it. |
| `minImageCount` | Minimum number of presentable images the app needs; the implementation creates at least that many or fails. |
| `imageFormat` | `VkFormat` the swapchain images are created with. |
| `imageColorSpace` | `VkColorSpaceKHR` for how image data is interpreted. |
| `imageExtent` | Size in pixels of the swapchain images; platform-dependent if it doesn't match the surface's `currentExtent`. On minimized windows `maxImageExtent` may be `(0,0)`, making creation impossible unless scaling is selected via `VkSwapchainPresentScalingCreateInfoKHR` (if supported). |
| `imageArrayLayers` | Number of views in a multiview/stereo surface; 1 for non-stereoscopic apps. |
| `imageUsage` | Bitmask of `VkImageUsageFlagBits` for intended usage of acquired swapchain images. |
| `imageSharingMode` | Sharing mode for the swapchain image(s). |
| `queueFamilyIndexCount` | Number of queue families with access when sharing mode is `VK_SHARING_MODE_CONCURRENT`. |
| `pQueueFamilyIndices` | Pointer to array of those queue family indices. |
| `preTransform` | Transform relative to the presentation engine's natural orientation; if it doesn't match `currentTransform` from surface capabilities, the engine transforms content during presentation. |
| `compositeAlpha` | Alpha compositing mode used when compositing with other surfaces. |
| `presentMode` | How incoming present requests are processed and queued. |
| `clipped` | `VK_TRUE` allows the implementation to skip rendering of non-visible surface regions (undefined content when read back, fragment shaders may not execute); `VK_FALSE` means images own all pixels. Apps should set `VK_TRUE` if they don't read back content and shaders have no side effects requiring full coverage. |
| `oldSwapchain` | `VK_NULL_HANDLE` or an existing non-retired swapchain for the same surface; may aid resource reuse and allows presenting already-acquired images from it. |

## Description Highlights

- Passing non-null `oldSwapchain` to `vkCreateSwapchainKHR` retires it even if new swapchain creation fails; the new swapchain is created non-retired either way.
- After retirement, unacquired images from `oldSwapchain` may be freed by the implementation, even on failure; the app can destroy `oldSwapchain` to free all its memory.
- Multiple retired swapchains can exist for one `VkSurfaceKHR` when `oldSwapchain` uses outnumber `vkDestroySwapchainKHR` calls.
- After retirement the app may still present already-acquired images from the old swapchain via `vkQueuePresentKHR` (which may return `VK_ERROR_OUT_OF_DATE_KHR`).
- A shared presentable image from the old swapchain stays usable until an image is acquired from the new swapchain (absent out-of-date state).
- `imageUsage` defines the effective usage flags; if `pNext` includes `VkImageUsageFlags2CreateInfoKHR`, `imageUsage` is ignored in favor of that structure's `usage`.

## Valid Usage

1. `VUID-VkSwapchainCreateInfoKHR-surface-01270` — surface must be supported by the device per `vkGetPhysicalDeviceSurfaceSupportKHR`.
2. `VUID-VkSwapchainCreateInfoKHR-minImageCount-01272` — minImageCount ≤ `maxImageCount` from surface capabilities, unless that value is zero.
3. `VUID-VkSwapchainCreateInfoKHR-swapchainMaintenance1-10155` — without the `swapchainMaintenance1` feature, `pNext` must not include `VkSwapchainPresentModesCreateInfoKHR`.
4. `VUID-VkSwapchainCreateInfoKHR-presentMode-02839` — for non-shared present modes, minImageCount ≥ the surface's `minImageCount` capability.
5. `VUID-VkSwapchainCreateInfoKHR-minImageCount-01383` — minImageCount must be 1 for `VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR` or `VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR`.
6. `VUID-VkSwapchainCreateInfoKHR-imageFormat-01273` — imageFormat/imageColorSpace must match a `VkSurfaceFormatKHR` from `vkGetPhysicalDeviceSurfaceFormatsKHR`.
7. `VUID-VkSwapchainCreateInfoKHR-pNext-07781` — with no scaling info (or zero scalingBehavior), imageExtent must lie between the surface's min/max image extent.
8. `VUID-VkSwapchainCreateInfoKHR-pNext-07782` — with nonzero scalingBehavior, imageExtent must lie within min/max scaled extent from `vkGetPhysicalDeviceSurfaceCapabilities2KHR`.
9. `VUID-VkSwapchainCreateInfoKHR-swapchainMaintenance1-10157` — without `swapchainMaintenance1`, flags must not include `VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_KHR`.
10. `VUID-VkSwapchainCreateInfoKHR-imageExtent-01689` — imageExtent width and height must both be non-zero.
11. `VUID-VkSwapchainCreateInfoKHR-imageArrayLayers-01275` — imageArrayLayers > 0 and ≤ surface `maxImageArrayLayers`.
12. `VUID-VkSwapchainCreateInfoKHR-presentMode-01427` — for FIFO_LATEST_READY/IMMEDIATE/MAILBOX/FIFO/FIFO_RELAXED modes, imageUsage must be a subset of `supportedUsageFlags`.
13. `VUID-VkSwapchainCreateInfoKHR-imageUsage-01384` — for shared present modes, imageUsage must be a subset of `sharedPresentSupportedUsageFlags`.
14. `VUID-VkSwapchainCreateInfoKHR-imageSharingMode-01277` — concurrent mode requires a valid `pQueueFamilyIndices` array of `queueFamilyIndexCount` values.
15. `VUID-VkSwapchainCreateInfoKHR-imageSharingMode-01278` — concurrent mode requires `queueFamilyIndexCount` > 1.
16. `VUID-VkSwapchainCreateInfoKHR-imageSharingMode-01428` — concurrent mode indices must be unique and less than the queue family property count for the physical device used.
17. `VUID-VkSwapchainCreateInfoKHR-preTransform-01279` — preTransform must be one of the surface's `supportedTransforms`.
18. `VUID-VkSwapchainCreateInfoKHR-compositeAlpha-01280` — compositeAlpha must be one of the surface's `supportedCompositeAlpha` bits.
19. `VUID-VkSwapchainCreateInfoKHR-presentMode-01281` — presentMode must be one of the values from `vkGetPhysicalDeviceSurfacePresentModesKHR`.
20. `VUID-VkSwapchainCreateInfoKHR-presentModeFifoLatestReady-10161` — without the `presentModeFifoLatestReady` feature, presentMode must not be `VK_PRESENT_MODE_FIFO_LATEST_READY_KHR`.
21. `VUID-VkSwapchainCreateInfoKHR-physicalDeviceCount-01429` — with a single-physicalDevice logical device, flags must not contain `VK_SWAPCHAIN_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR`.
22. `VUID-VkSwapchainCreateInfoKHR-oldSwapchain-01933` — oldSwapchain, if not null, must be non-retired and associated with the same native window as `surface`.
23. `VUID-VkSwapchainCreateInfoKHR-imageFormat-01778` — implied image creation parameters must be supported per `vkGetPhysicalDeviceImageFormatProperties`.
24. `VUID-VkSwapchainCreateInfoKHR-flags-03168` — with `VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR`, `pNext` must include `VkImageFormatListCreateInfo` with `viewFormatCount` > 0 and `pViewFormats` containing `imageFormat`.
25. `VUID-VkSwapchainCreateInfoKHR-pNext-04099` — if `VkImageFormatListCreateInfo` has nonzero `viewFormatCount`, all `pViewFormats` formats must be compatible per the compatibility table.
26. `VUID-VkSwapchainCreateInfoKHR-flags-04100` — without the mutable-format flag, `VkImageFormatListCreateInfo::viewFormatCount` must be 0 or 1.
27. `VUID-VkSwapchainCreateInfoKHR-flags-03187` — with `VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR`, `VkSurfaceProtectedCapabilitiesKHR::supportsProtected` must be `VK_TRUE`.
28. `VUID-VkSwapchainCreateInfoKHR-pNext-02679` — for a Win32 surface with application-controlled full-screen exclusive, `VkSurfaceFullScreenExclusiveWin32InfoEXT` must be in `pNext`.
29. `VUID-VkSwapchainCreateInfoKHR-pNext-06752` — without the `imageCompressionControlSwapchain` feature, `pNext` must not include `VkImageCompressionControlEXT`.
30. `VUID-VkSwapchainCreateInfoKHR-multisampledRenderToSwapchain-12447` — without the `multisampledRenderToSwapchain` feature, flags must not contain `VK_SWAPCHAIN_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT`.
31. `VUID-VkSwapchainCreateInfoKHR-flags-12448` — flags must be a subset of `VkSwapchainFlagsSurfaceCapabilitiesEXT::swapchainSupportedFlags` for the given presentMode.
32. `VUID-VkSwapchainCreateInfoKHR-presentTiming-12232` — without `presentTiming`/`presentAtAbsoluteTime`/`presentAtRelativeTime` features, flags must not contain `VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT`.

## Valid Usage (Implicit)

33. `VUID-VkSwapchainCreateInfoKHR-sType-sType` — sType must be `VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR`.
34. `VUID-VkSwapchainCreateInfoKHR-pNext-pNext` — each `pNext` entry must be NULL or a valid instance of: `VkDeviceGroupSwapchainCreateInfoKHR`, `VkImageCompressionControlEXT`, `VkImageFormatListCreateInfo`, `VkImageUsageFlags2CreateInfoKHR`, `VkSurfaceFullScreenExclusiveInfoEXT`, `VkSurfaceFullScreenExclusiveWin32InfoEXT`, `VkSwapchainCounterCreateInfoEXT`, `VkSwapchainDisplayNativeHdrCreateInfoAMD`, `VkSwapchainLatencyCreateInfoNV`, `VkSwapchainPresentBarrierCreateInfoNV`, `VkSwapchainPresentModesCreateInfoKHR`, or `VkSwapchainPresentScalingCreateInfoKHR`.
35. `VUID-VkSwapchainCreateInfoKHR-sType-unique` — each `sType` in the `pNext` chain must be unique.
36. `VUID-VkSwapchainCreateInfoKHR-flags-parameter` — flags must be a valid combination of `VkSwapchainCreateFlagBitsKHR`.
37. `VUID-VkSwapchainCreateInfoKHR-surface-parameter` — surface must be a valid `VkSurfaceKHR` handle.
38. `VUID-VkSwapchainCreateInfoKHR-imageFormat-parameter` — imageFormat must be a valid `VkFormat`.
39. `VUID-VkSwapchainCreateInfoKHR-imageColorSpace-parameter` — imageColorSpace must be a valid `VkColorSpaceKHR`.
40. `VUID-VkSwapchainCreateInfoKHR-imageUsage-parameter` — imageUsage must be a valid combination of `VkImageUsageFlagBits`.
41. `VUID-VkSwapchainCreateInfoKHR-imageUsage-requiredbitmask` — imageUsage must not be 0.
42. `VUID-VkSwapchainCreateInfoKHR-imageSharingMode-parameter` — imageSharingMode must be a valid `VkSharingMode`.
43. `VUID-VkSwapchainCreateInfoKHR-preTransform-parameter` — preTransform must be a valid `VkSurfaceTransformFlagBitsKHR`.
44. `VUID-VkSwapchainCreateInfoKHR-compositeAlpha-parameter` — compositeAlpha must be a valid `VkCompositeAlphaFlagBitsKHR`.
45. `VUID-VkSwapchainCreateInfoKHR-presentMode-parameter` — presentMode must be a valid `VkPresentModeKHR`.
46. `VUID-VkSwapchainCreateInfoKHR-oldSwapchain-parameter` — oldSwapchain, if not null, must be a valid `VkSwapchainKHR` handle.
47. `VUID-VkSwapchainCreateInfoKHR-commonparent` — valid `oldSwapchain` and `surface` handles must come from the same `VkInstance`.

## Host Synchronization

- Host access to `surface` must be externally synchronized.
- Host access to `oldSwapchain` must be externally synchronized.

## Return Values / Error Codes

**None on this page.** This is a structure reference page, not a function page; no return values or error codes are defined here. The related creation functions are `vkCreateSwapchainKHR` and `vkCreateSharedSwapchainsKHR`.

## See Also

`VK_KHR_swapchain`, `VkBool32`, `VkColorSpaceKHR`, `VkCompositeAlphaFlagBitsKHR`, `VkExtent2D`, `VkFormat`, `VkImageUsageFlags`, `VkPresentModeKHR`, `VkSharingMode`, `VkStructureType`, `VkSurfaceKHR`, `VkSurfaceTransformFlagBitsKHR`, `VkSwapchainCreateFlagsKHR`, `VkSwapchainKHR`, `vkCreateSharedSwapchainsKHR`, `vkCreateSwapchainKHR`.
