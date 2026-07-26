# Vulkan Resource Upload

**Version**: 1.0
**Created**: 2026-06-27
**Status**: Proposed

---

## ADDED Requirements

### Requirement: Buffer UploadData 支持 Device-Local 内存

`VulkanBuffer::UploadData` SHALL 当目标 buffer 分配在 device-local 内存（`VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`）时，通过 staging buffer 完成数据上传，而非静默失败。

#### Scenario: CPU 可映射 buffer 直接上传

- **WHEN** `VulkanBuffer::UploadData(pData, size, offset)` 被调用且 buffer 的 VMA allocation 可被 `Map()`（host-visible 内存）
- **THEN** 系统直接 `memcpy` 到 mapped pointer 并 `Unmap()`，无需 staging buffer

#### Scenario: Device-local buffer 通过 staging 上传

- **WHEN** `VulkanBuffer::UploadData(pData, size, offset)` 被调用且 buffer 的 VMA allocation 不可被 `Map()`（device-local 内存）
- **THEN** 系统创建 HOST_VISIBLE staging buffer，`memcpy` 数据到 staging buffer，通过 `vkCmdCopyBuffer` 复制到目标 buffer，添加 `TRANSFER → 目标用途` pipeline barrier，并同步等待完成（Submit + WaitIdle）

#### Scenario: 上传后数据立即可用

- **WHEN** `UploadData` 调用返回后
- **THEN** 上传的数据已在 GPU 端可见，后续命令可安全读取

### Requirement: Texture UploadData 实现 Staging 上传

`VulkanTexture::UploadData` SHALL 通过 staging buffer + `vkCmdCopyBufferToImage` 将纹理数据上传到 GPU，而非空实现。

#### Scenario: 纹理数据上传（含 mip 和 array layer）

- **WHEN** `VulkanTexture::UploadData(pData, size)` 被调用
- **THEN** 系统创建 HOST_VISIBLE staging buffer（大小 = 全 mip/layer 紧密打包字节数），`memcpy` 纹理数据，将 image 布局转换为 `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`，为每个 mip level 和 array layer 构建独立的 `VkBufferImageCopy` region（指定正确的 `bufferOffset`、`imageSubresource.mipLevel`、`imageExtent`），执行 `vkCmdCopyBufferToImage`，恢复原始布局，并同步等待完成

#### Scenario: 上传后 layout 恢复

- **WHEN** `UploadData` 调用完成后
- **THEN** image 恢复到调用前的 `VkImageLayout`（即 `m_CurrentLayout` 不变）

#### Scenario: 特殊格式字节对齐

- **WHEN** 纹理格式为 D24_UNORM_S8_UINT 等 depth-stencil 格式
- **THEN** staging buffer 大小计算使用正确的 bytes-per-pixel（4 bytes for D24S8），`VkBufferImageCopy` regions 正确映射到紧密打包的源数据
