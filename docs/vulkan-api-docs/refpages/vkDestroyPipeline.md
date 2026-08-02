# vkDestroyPipeline(3)

## Name

vkDestroyPipeline - Destroy a pipeline object

## C Specification

```c
// Provided by VK_VERSION_1_0
void vkDestroyPipeline(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator);
```

## Parameters

| Parameter | Description |
|---|---|
| `device` | 是销毁该管线的逻辑设备。 |
| `pipeline` | 要销毁的管线句柄。 |
| `pAllocator` | 控制主机内存分配，详见 Memory Allocation 章节。 |

## Description

### Valid Usage

- **VUID-vkDestroyPipeline-pipeline-00765** — 所有引用 `pipeline` 的已提交命令必须已完成执行。
- **VUID-vkDestroyPipeline-pipeline-00766** — 若创建 `pipeline` 时提供了 `VkAllocationCallbacks`，此处必须提供一组兼容的回调。
- **VUID-vkDestroyPipeline-pipeline-00767** — 若创建 `pipeline` 时未提供 `VkAllocationCallbacks`，`pAllocator` 必须为 `NULL`。

### Valid Usage (Implicit)

- **VUID-vkDestroyPipeline-device-parameter** — `device` 必须是有效的 `VkDevice` 句柄。
- **VUID-vkDestroyPipeline-pipeline-parameter** — 若 `pipeline` 非 `VK_NULL_HANDLE`，则必须是有效的 `VkPipeline` 句柄。
- **VUID-vkDestroyPipeline-pAllocator-parameter** — 若 `pAllocator` 非 `NULL`，则必须是有效 `VkAllocationCallbacks` 结构的有效指针。
- **VUID-vkDestroyPipeline-pipeline-parent** — 若 `pipeline` 为有效句柄，则必须由 `device` 创建、分配或获取。

### Host Synchronization

- 对 `pipeline` 的主机访问必须进行外部同步。

## Return Value

无 — 函数返回类型为 `void`，不返回错误码（页面无 Error Codes 章节）。

## See Also

- [VK_VERSION_1_0](VK_VERSION_1_0.html)
- [VkAllocationCallbacks](VkAllocationCallbacks.html)
- [VkDevice](VkDevice.html)
- [VkPipeline](VkPipeline.html)

## Document Notes

更多信息参见 [Vulkan Specification](../../../../spec/latest/chapters/pipelines.html#vkDestroyPipeline)。本页面摘自 Vulkan 规范，修改应提交至规范本身而非本页。

---
*Source: https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyPipeline.html (crawled via WebFetch, 2026-07-19)*

全部 4 条显式 VUID + 4 条隐式 VUID 均已列出，无省略。
