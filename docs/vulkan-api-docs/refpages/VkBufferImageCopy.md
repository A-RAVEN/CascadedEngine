# VkBufferImageCopy(3)

## Name

VkBufferImageCopy — Structure specifying a buffer image copy operation

## C Specification

For both `vkCmdCopyBufferToImage` and `vkCmdCopyImageToBuffer`, each element of `pRegions` is a structure defined as:

```c
// Provided by VK_VERSION_1_0
typedef struct VkBufferImageCopy {
    VkDeviceSize                bufferOffset;
    uint32_t                    bufferRowLength;
    uint32_t                    bufferImageHeight;
    VkImageSubresourceLayers    imageSubresource;
    VkOffset3D                  imageOffset;
    VkExtent3D                  imageExtent;
} VkBufferImageCopy;
```

## Members

- `bufferOffset` — the offset in bytes from the start of the buffer object where the image data is copied from or to.
- `bufferRowLength`, `bufferImageHeight` — Specify, in texels, a subregion of a larger 2D or 3D image in buffer memory and control the addressing calculations. If either value is zero, that buffer aspect is treated as tightly packed according to `imageExtent`.
- `imageSubresource` — A `VkImageSubresourceLayers` used to specify the specific image subresources of the image used for the source or destination image data.
- `imageOffset` — Selects the initial `x`, `y`, `z` offsets in texels of the sub-region of the source or destination image data.
- `imageExtent` — The size in texels of the image to copy in `width`, `height` and `depth`.

## Valid Usage

- VUID-VkBufferImageCopy-bufferRowLength-09101 — `bufferRowLength` **must** be `0`, or greater than or equal to the `width` member of `imageExtent`
- VUID-VkBufferImageCopy-bufferImageHeight-09102 — `bufferImageHeight` **must** be `0`, or greater than or equal to the `height` member of `imageExtent`
- VUID-VkBufferImageCopy-aspectMask-09103 — The `aspectMask` member of `imageSubresource` **must** only have a single bit set
- VUID-VkBufferImageCopy-imageExtent-06659 — `imageExtent.width` **must** not be 0
- VUID-VkBufferImageCopy-imageExtent-06660 — `imageExtent.height` **must** not be 0
- VUID-VkBufferImageCopy-imageExtent-06661 — `imageExtent.depth` **must** not be 0

## Valid Usage (Implicit)

- VUID-VkBufferImageCopy-imageSubresource-parameter — `imageSubresource` **must** be a valid `VkImageSubresourceLayers` structure

## See Also

`VK_VERSION_1_0`, `VkDeviceSize`, `VkExtent3D`, `VkImageSubresourceLayers`, `VkOffset3D`, `vkCmdCopyBufferToImage`, `vkCmdCopyImageToBuffer`

## Document Notes

For more information, see the Vulkan Specification (the `VkBufferImageCopy` entry in the copies chapter). This page is extracted from the Vulkan Specification; fixes and changes should be made to the Specification, not directly.

## Return Values / Error Codes

None — this page documents a structure type, not a callable command, so no return values or error codes are defined here.
