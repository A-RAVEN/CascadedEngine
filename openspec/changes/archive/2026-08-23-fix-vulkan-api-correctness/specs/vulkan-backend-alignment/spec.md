## ADDED Requirements

### Requirement: 纹理格式字节数与上传尺寸正确

`bytesPerPixel`（或等价函数）SHALL 按 Vulkan 格式表返回真实字节宽度：`VK_FORMAT_D16_UNORM` 为 2 字节、`VK_FORMAT_D24_UNORM_S8_UINT` 为 4 字节、`VK_FORMAT_D32_SFLOAT` 为 4 字节等。staging buffer 大小、`VkBufferImageCopy.bufferOffset` 与 memcpy 长度 SHALL 使用该真实字节数，SHALL NOT 按 4 字节高估导致 staging 尺寸翻倍并从调用方数据源超读。

#### Scenario: 上传 D16_UNORM 纹理

- **GIVEN** `UploadData` 上传 `E_D16_UNORM` 格式纹理，调用方提供恰好 2 字节/像素的数据
- **WHEN** 计算 staging 尺寸与 `bufferOffset`
- **THEN** 每个像素按 2 字节计
- **AND** memcpy 不超读调用方数据源

#### Scenario: 上传 D24S8 纹理

- **GIVEN** `UploadData` 上传 `E_D24_UNORM_S8_UINT` 格式纹理
- **WHEN** 计算 staging 尺寸
- **THEN** 每个像素按 4 字节计（depth 24bit + stencil 8bit）
