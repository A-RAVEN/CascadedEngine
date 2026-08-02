# vkBindVertexBuffers

> **NOTE**: The assigned URL `https://docs.vulkan.org/refpages/latest/refpages/source/vkBindVertexBuffers.html` returns HTTP 404 (both docs.vulkan.org and the khronos.org registry redirect to it). The canonical Vulkan refpage for binding vertex buffers is `vkCmdBindVertexBuffers.html`. This file contains the full canonical content from `https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindVertexBuffers.html`. All VUIDs use the `VUID-vkCmdBindVertexBuffers-*` naming.


# vkCmdBindVertexBuffers(3)

## Name

vkCmdBindVertexBuffers - Bind vertex buffers to a command buffer

## C Specification

To bind vertex buffers to a command buffer for use in subsequent drawing commands, call:

```c
// Provided by VK_VERSION_1_0
void vkCmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets);
```

## Parameters

* commandBuffer is the command buffer into which the command is recorded.
* firstBinding is the index of the first vertex input binding whose state is updated by the command.
* bindingCount is the number of vertex input bindings whose state is updated by the command.
* pBuffers is a pointer to an array of buffer handles.
* pOffsets is a pointer to an array of buffer offsets.

## Description

The values taken from elements i of pBuffers and pOffsets replace the current state for the vertex input binding firstBinding + i , for i in [0, bindingCount ) . The vertex input binding is updated to start at the offset indicated by pOffsets [i] from the start of the buffer pBuffers [i]. All vertex input attributes that use each of these bindings will use these updated addresses in their address calculations for subsequent drawing commands. If the nullDescriptor feature is enabled, elements of pBuffers can be VK_NULL_HANDLE , and can be used by the vertex shader. If a vertex input attribute is bound to a vertex input binding that is VK_NULL_HANDLE , the values taken from memory are considered to be zero, and missing G, B, or A components are filled with (0 .

### Valid Usage

* VUID-vkCmdBindVertexBuffers-firstBinding-00624 firstBinding must be less than VkPhysicalDeviceLimits :: maxVertexInputBindings
* VUID-vkCmdBindVertexBuffers-firstBinding-00625 The sum of firstBinding and bindingCount must be less than or equal to VkPhysicalDeviceLimits :: maxVertexInputBindings
* VUID-vkCmdBindVertexBuffers-pOffsets-00626 All elements of pOffsets must be less than the size of the corresponding element in pBuffers
* VUID-vkCmdBindVertexBuffers-pBuffers-00627 All elements of pBuffers must have been created with the VK_BUFFER_USAGE_VERTEX_BUFFER_BIT flag
* VUID-vkCmdBindVertexBuffers-pBuffers-00628 Each element of pBuffers that is non-sparse must be bound completely and contiguously to a single VkDeviceMemory object
* VUID-vkCmdBindVertexBuffers-pBuffers-04001 If the nullDescriptor feature is not enabled, all elements of pBuffers must not be VK_NULL_HANDLE
* VUID-vkCmdBindVertexBuffers-pBuffers-04002 If an element of pBuffers is VK_NULL_HANDLE , then the corresponding element of pOffsets must be zero

### Valid Usage (Implicit)

* VUID-vkCmdBindVertexBuffers-commandBuffer-parameter commandBuffer must be a valid VkCommandBuffer handle
* VUID-vkCmdBindVertexBuffers-pBuffers-parameter pBuffers must be a valid pointer to an array of bindingCount valid or VK_NULL_HANDLE VkBuffer handles
* VUID-vkCmdBindVertexBuffers-pOffsets-parameter pOffsets must be a valid pointer to an array of bindingCount VkDeviceSize values
* VUID-vkCmdBindVertexBuffers-commandBuffer-recording commandBuffer must be in the recording state
* VUID-vkCmdBindVertexBuffers-commandBuffer-cmdpool The VkCommandPool that commandBuffer was allocated from must support VK_QUEUE_GRAPHICS_BIT operations
* VUID-vkCmdBindVertexBuffers-videocoding This command must only be called outside of a video coding scope
* VUID-vkCmdBindVertexBuffers-bindingCount-arraylength bindingCount must be greater than 0
* VUID-vkCmdBindVertexBuffers-commonparent Both of commandBuffer , and the elements of pBuffers that are valid handles of non-ignored parameters must have been created, allocated, or retrieved from the same VkDevice

### Host Synchronization

* Host access to commandBuffer must be externally synchronized
* Host access to the VkCommandPool that commandBuffer was allocated from must be externally synchronized

### Command Properties

| Command Buffer Levels | Render Pass Scope | Video Coding Scope | Supported Queue Types | Command Type |
|---|---|---|---|---|
| Primary Secondary | Both | Outside | VK_QUEUE_GRAPHICS_BIT | State |

### Conditional Rendering

vkCmdBindVertexBuffers is not affected by conditional rendering

## See Also

VK_VERSION_1_0 , VkBuffer , VkCommandBuffer , VkDeviceSize

## Document Notes

For more information, see the Vulkan Specification .

This page is extracted from the Vulkan Specification. Fixes and changes should be made to the Specification, not directly.
