# VkPipelineTessellationStateCreateInfo(3)

## Name

**VkPipelineTessellationStateCreateInfo** — Structure specifying parameters of a newly created pipeline tessellation state

## C Specification

```c
// Provided by VK_VERSION_1_0
typedef struct VkPipelineTessellationStateCreateInfo {
    VkStructureType                           sType;
    const void*                               pNext;
    VkPipelineTessellationStateCreateFlags    flags;
    uint32_t                                  patchControlPoints;
} VkPipelineTessellationStateCreateInfo;
```

## Members

- `sType` — a `VkStructureType` value identifying this structure.
- `pNext` — NULL or a pointer to a structure extending this structure.
- `flags` — reserved for future use.
- `patchControlPoints` — the number of control points per patch.

## Description — Valid Usage

- **VUID-VkPipelineTessellationStateCreateInfo-patchControlPoints-01214**
  "patchControlPoints must be greater than zero and less than or equal to VkPhysicalDeviceLimits::maxTessellationPatchSize"

- **VUID-VkPipelineTessellationStateCreateInfo-sType-sType**
  "sType must be VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO"

- **VUID-VkPipelineTessellationStateCreateInfo-pNext-pNext**
  "pNext must be NULL or a pointer to a valid instance of VkPipelineTessellationDomainOriginStateCreateInfo"

- **VUID-VkPipelineTessellationStateCreateInfo-sType-unique**
  "The sType value of each structure in the pNext chain must be unique"

- **VUID-VkPipelineTessellationStateCreateInfo-flags-zerobitmask**
  "flags must be 0"

## See Also

- VK_VERSION_1_0
- VkGraphicsPipelineCreateInfo
- VkGraphicsShaderGroupCreateInfoNV
- VkPipelineTessellationStateCreateFlags
- VkStructureType

## Document Notes

"For more information, see the Vulkan Specification." "This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly."

---
*Source: https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineTessellationStateCreateInfo.html (crawled via WebFetch, 2026-07-19)*

该页面是结构体定义页而非函数页，因此不涉及返回值（return values）或错误码（error codes）。全部 5 条 VUID 均已列出，无省略。
