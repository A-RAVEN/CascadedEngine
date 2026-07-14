# Vulkan Resource Aliasing

**Version**: 1.0
**Created**: 2026-07-12
**Status**: Proposed

---

## ADDED Requirements

### Requirement: Aliased resources SHALL be bound to shared memory pool at computed offsets using two-phase realignment

`VulkanGraphLocalResourceManager::AllocateAliasedResources()` SHALL 采用两阶段方法实现资源绑定：(Phase A) 先创建临时未绑定资源获取真实 `VkMemoryRequirements`（对齐+大小），用真实值重新规划 aliasing 偏移；(Phase B) 分配 pool 后将资源绑定到真实对齐的偏移位置，而非使用固定 256B 对齐和独立分配。

#### Scenario: Buffer 绑定到 aliased pool 偏移（两阶段方法）

- **WHEN** `AllocateAliasedResources()` 执行 Phase A
- **THEN** 系统为每个注册 buffer 调用 `vkCreateBuffer`（不绑定内存，`VkBuffer` handle）创建临时对象，通过 `vkGetBufferMemoryRequirements` 获取真实 `alignment` 和 `size`
- **AND** 真实对齐值被传入 `VulkanResourceAliasing::ReplanWithRealAlignment()` 重新规划偏移
- **AND** Phase A 临时 buffer handle 被立即 destroy
- **WHEN** Phase B 执行，pool 已用修正后的 `m_TotalAliasedSize` 分配
- **THEN** 系统再次调用 `vkCreateBuffer`（不绑定内存）创建正式 buffer，通过 `vkBindBufferMemory(buffer, deviceMemory, alignedOffset)` 绑定到 aliased pool 的真实对齐偏移

#### Scenario: Image 绑定到 aliased pool 偏移（两阶段方法）

- **WHEN** `AllocateAliasedResources()` 执行 Phase A 对 image 类型
- **THEN** 系统为每个注册 image 调用 `vkCreateImage`（不绑定内存）创建临时对象，通过 `vkGetImageMemoryRequirements` 获取真实 `alignment` 和 `size`
- **AND** 真实对齐值被传入 `ReplanWithRealAlignment()` 重新规划
- **AND** Phase A 临时 image handle 被立即 destroy
- **WHEN** Phase B 执行
- **THEN** 系统再次调用 `vkCreateImage`（不绑定内存）创建正式 image，通过 `vkBindImageMemory(image, deviceMemory, alignedOffset)` 绑定

#### Scenario: 生命周期不重叠的资源共享同一 pool 的不同偏移

- **WHEN** 资源 A（生命周期 [1,3]）和资源 B（生命周期 [4,6]）被注册到 aliasing 分析
- **THEN** `AnalyzeAndPlanAliasing()` 计算出 A 和 B 可使用同一 pool 的不同偏移区域（或同一区域，因为生命周期不重叠），两者绑定到同一个 `VkDeviceMemory`

#### Scenario: 对齐不匹配时自动修正

- **WHEN** `RegisterTemporaryBuffer()` 注册 buffer 时使用估算的 `alignment=256`，但运行时 `vkGetBufferMemoryRequirements` 返回 `alignment=65536`
- **THEN** `ReplanWithRealAlignment()` 使用 65536 重新计算该 buffer 的 `aliasedAlloc.offset`，确保绑定偏移是真实对齐的整数倍
- **AND** `m_TotalAliasedSize` 可能因对齐增大而重新计算

### Requirement: VmaAllocation SHALL be converted to VkDeviceMemory for binding

`VulkanResourceAliasing::AliasedAllocation` SHALL 包含 `VkDeviceMemory deviceMemory` 字段，在 `AllocateAliasedPool()` 中通过 `vmaGetAllocationInfo` 提取并存储。`vkBindBufferMemory` 和 `vkBindImageMemory` 调用 SHALL 使用该 `deviceMemory` 而非 `VmaAllocation` 句柄。

#### Scenario: AliasedAllocation 包含 deviceMemory 字段

- **WHEN** `AllocateAliasedPool()` 成功分配 VMA allocation
- **THEN** 系统调用 `vmaGetAllocationInfo(m_AliasedPoolAllocation, &allocInfo)` 提取 `allocInfo.deviceMemory`
- **AND** `m_AliasedAllocations` 中每个 entry 的 `deviceMemory` 字段被设置为 `allocInfo.deviceMemory`
- **AND** `GetPoolDeviceMemory()` 方法返回该值

#### Scenario: VulkanMemoryManager 提供 VmaAllocationInfo 查询

- **WHEN** 外部需要查询 VMA allocation 的底层 `VkDeviceMemory` 和 offset
- **THEN** `VulkanMemoryManager::GetAllocationInfo(VmaAllocation, VmaAllocationInfo*)` 方法可用，内部调用 `vmaGetAllocationInfo`

### Requirement: ManagedGPUResource SHALL track the aliased offset

`ManagedGPUResource` 结构 SHALL 包含 `aliasedOffset` 字段，记录资源在 aliased pool 中的绑定偏移，用于调试和帧内数据更新。

#### Scenario: Buffer 资源存储 aliased offset

- **WHEN** buffer 成功绑定到 aliased pool 的偏移 4096
- **THEN** `ManagedGPUResource.aliasedOffset == 4096`

#### Scenario: Image 资源存储 aliased offset

- **WHEN** image 成功绑定到 aliased pool 的偏移 262144 且 alignment 向下对齐后
- **THEN** `ManagedGPUResource.aliasedOffset == 262144`

### Requirement: ReleaseAllResources SHALL destroy handles without freeing pool memory

`ReleaseAllResources()` SHALL 仅调用 `vkDestroyBuffer`/`vkDestroyImage`/`vkDestroyImageView` 销毁资源句柄，SHALL NOT 调用 `vmaFreeMemory` 或 `vmaDestroyBuffer`（内存由 aliased pool 统一通过 `FreeAliasedPool()` 释放）。

#### Scenario: 正常释放 aliased 资源

- **WHEN** `ReleaseAllResources()` 被调用
- **THEN** 每个 `ManagedGPUResource` 的 `buffer`/`image`/`imageView` handle 被 `vkDestroy*` 销毁，但不调用 VMA free 函数
- **AND** `m_AliasingManager.FreeAliasedPool()` 在此之后（或之前）统一释放 pool 内存

#### Scenario: 避免 double-free

- **WHEN** aliased pool 已被 `FreeAliasedPool()` 释放
- **THEN** 后续对已绑定到该 pool 的 buffer/image handle 调用 `vkDestroyBuffer/vkDestroyImage` 不会触发 double-free（因为 `vkDestroy` 不释放已释放的内存）

### Requirement: Texture format SHALL be dynamically resolved from descriptor

`AllocateAliasedResources()` SHALL 使用 `localResource.textureDesc.format` 通过 `VulkanTexture::ConvertFormat()` 转换为实际的 `vk::Format`，SHALL NOT 硬编码为 `eR8G8B8A8Unorm`。

#### Scenario: RGBA8 纹理使用正确格式

- **WHEN** `localResource.textureDesc.format == ETextureFormat::eRGBA8` 或 `eBGRA8`
- **THEN** `imageInfo.format == VulkanTexture::ConvertFormat(ETextureFormat::eRGBA8)` 或对应的 `vk::Format`
- **AND** `viewInfo.format == imageInfo.format`

#### Scenario: Depth-Stencil 纹理使用正确格式

- **WHEN** `localResource.textureDesc.format == ETextureFormat::eD24_UNORM_S8_UINT`
- **THEN** `imageInfo.format == vk::Format::eD24UnormS8Uint`
- **AND** `viewInfo.subresourceRange.aspectMask` 包含 depth 和 stencil

#### Scenario: 压缩格式纹理使用 BCn 格式

- **WHEN** `localResource.textureDesc.format == ETextureFormat::eBC1`
- **THEN** `imageInfo.format == vk::Format::eBc1RgbaUnormBlock`

### Requirement: Texture size estimation SHALL account for format block size and mip chain

`RegisterTemporaryTexture()` 中的纹理大小估算 SHALL 基于格式的每像素字节数（block size）和 mip chain 的递进尺寸计算总字节数，SHALL NOT 使用 `width * height * 4 * mipLevels * layers` 的简化公式。

#### Scenario: RGBA8 纹理每个 mip level 面积递减

- **WHEN** 纹理为 1024x1024 RGBA8（4 bytes/pixel），mipLevels=10，layers=1
- **THEN** 估算大小为 `(1024*1024 + 512*512 + 256*256 + ... + 1*1) * 4 ≈ 5.46 MB`，而非 `1024*1024*4*10 = 40 MB`

#### Scenario: BC1 压缩纹理使用 4x4 block 对齐

- **WHEN** 纹理为 512x512 BC1（8 bytes per 4x4 block），mipLevels=1
- **THEN** 估算大小为 `ceil(512/4) * ceil(512/4) * 8 = 128*128*8 = 131072 bytes`

#### Scenario: D24S8 纹理使用 4 bytes/pixel

- **WHEN** 纹理为 800x600 D24_UNORM_S8_UINT（4 bytes/pixel），mipLevels=1
- **THEN** 估算大小为 `800 * 600 * 4 = 1.92 MB`

#### Scenario: Multi-layer 纹理考虑 layer 乘数

- **WHEN** 纹理为 256x256 RGBA8，mipLevels=1，layers=6
- **THEN** 估算大小为 `256*256*4*6 = 1.5 MB`

### Requirement: ImageView aspectMask SHALL be derived from format type

`AllocateAliasedResources()` 中 ImageView 创建 SHALL 根据 `textureDesc.format` 动态设置 `subresourceRange.aspectMask`，SHALL NOT 硬编码为 `eColor`。

#### Scenario: Color 格式使用 eColor aspectMask

- **WHEN** `localResource.textureDesc.format` 是一个 color 格式（如 `E_R8G8B8A8_UNORM`）
- **THEN** `viewInfo.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eColor`

#### Scenario: Depth-only 格式使用 eDepth aspectMask

- **WHEN** `localResource.textureDesc.format == E_D32_SFLOAT`（纯深度格式）
- **THEN** `viewInfo.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eDepth`

#### Scenario: Depth-Stencil 格式使用 eDepth|eStencil aspectMask

- **WHEN** `localResource.textureDesc.format == E_D24_UNORM_S8_UINT`（深度模板格式）
- **THEN** `viewInfo.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil`

### Requirement: Image usage flags SHALL be derived from textureAccess

`AllocateAliasedResources()` 中 image 创建 SHALL 根据 `localResource.textureAccess` 动态设置 `imageInfo.usage`，SHALL NOT 硬编码为 `eSampled | eColorAttachment`。

#### Scenario: Sampled texture 仅包含 eSampled usage flag

- **WHEN** `textureAccess == eSampled`
- **THEN** `imageInfo.usage` 包含 `eSampled`，不包含 `eColorAttachment`

#### Scenario: Render target texture 包含 eColorAttachment usage flag

- **WHEN** `textureAccess` 包含 `eRT`
- **THEN** `imageInfo.usage` 包含 `eColorAttachment`

#### Scenario: Depth-Stencil texture 包含 eDepthStencilAttachment usage flag

- **WHEN** `textureAccess` 包含 `eDepthStencil`
- **THEN** `imageInfo.usage` 包含 `eDepthStencilAttachment`

#### Scenario: Transfer texture 包含 transfer src/dst usage flags

- **WHEN** `textureAccess` 包含 `eTransferSrc`
- **THEN** `imageInfo.usage` 包含 `eTransferSrc`
- **WHEN** `textureAccess` 包含 `eTransferDst`
- **THEN** `imageInfo.usage` 包含 `eTransferDst`

#### Scenario: 空 textureAccess 默认包含 eSampled|eColorAttachment

- **WHEN** `textureAccess` 为空（无访问类型标记）
- **THEN** `imageInfo.usage` 默认包含 `eSampled | eColorAttachment` 作为兜底

### Requirement: AddBuffer SHALL integrate with aliasing system

`AddBuffer()` SHALL 在 aliased pool 已分配时将 buffer 绑定到 pool 的指定偏移，SHALL NOT 总是独立分配 GPU 内存。

#### Scenario: AddBuffer after AllocateAliasedResources 绑定到已分配 pool

- **WHEN** `AddBuffer()` 在 `AllocateAliasedResources()` 之后被调用，且 aliased pool 已分配
- **THEN** 系统执行 Phase A（单资源）获取真实对齐 → 调用 `ReplanWithRealAlignment` 更新偏移 → 将 buffer 绑定到 aliased pool
- **AND** buffer 不进行独立 `device.createBuffer()` 分配

#### Scenario: AddBuffer before AllocateAliasedResources 仅注册

- **WHEN** `AddBuffer()` 在 `AllocateAliasedResources()` 之前被调用，aliased pool 尚未分配
- **THEN** 系统仅调用 `RegisterTemporaryBuffer()` 注册资源
- **AND** buffer 在后续 `AllocateAliasedResources()` 中统一创建和绑定

### Requirement: Buffer usage flags SHALL be derived from bufferUsage

`AllocateAliasedResources()` 中 buffer 创建 SHALL 根据 `localResource.bufferUsage` 动态设置 `bufferInfo.usage`，SHALL NOT 硬编码为 `eTransferDst | eVertexBuffer`。

#### Scenario: Vertex buffer 包含 eVertexBuffer usage flag

- **WHEN** `bufferUsage` 包含 `eVertexBuffer`
- **THEN** `bufferInfo.usage` 包含 `vk::BufferUsageFlagBits::eVertexBuffer`

#### Scenario: Index buffer 包含 eIndexBuffer usage flag

- **WHEN** `bufferUsage` 包含 `eIndexBuffer`
- **THEN** `bufferInfo.usage` 包含 `vk::BufferUsageFlagBits::eIndexBuffer`

#### Scenario: Constant buffer 包含 eUniformBuffer usage flag

- **WHEN** `bufferUsage` 包含 `eConstantBuffer`
- **THEN** `bufferInfo.usage` 包含 `vk::BufferUsageFlagBits::eUniformBuffer`

#### Scenario: All buffers always include transfer src/dst

- **WHEN** 任意 `bufferUsage` 值
- **THEN** `bufferInfo.usage` 始终包含 `eTransferDst | eTransferSrc` 用于数据上传

### Requirement: ManagedGPUResource mappedPtr SHALL be propagated from aliased pool

`AllocateAliasedResources()` SHALL 将 aliased pool 的持久映射指针按 `aliasedOffset` 偏移后赋值给每个 `ManagedGPUResource.mappedPtr`。

#### Scenario: Buffer mappedPtr 指向 aliased pool 中的正确偏移

- **WHEN** buffer 绑定到 aliased pool 偏移 `alignedOffset`，pool mapped pointer 为 `P`
- **THEN** `ManagedGPUResource.mappedPtr == P + alignedOffset`
- **AND** 调用者可通过 `managed.mappedPtr` 直接写入 CPU 端数据到 GPU 可见内存

#### Scenario: Image mappedPtr 也正确设置

- **WHEN** image 绑定到 aliased pool 偏移 `alignedOffset`
- **THEN** `ManagedGPUResource.mappedPtr == P + alignedOffset`（即使 image 通常不通过 mappedPtr 写入，字段保持一致便于调试）

### Requirement: GetFormatBlockSize and IsCompressedFormat SHALL be implemented

代码库 SHALL 提供 `GetFormatBlockSize(ETextureFormat)` 和 `IsCompressedFormat(ETextureFormat)` 辅助函数，供纹理大小估算使用。

#### Scenario: GetFormatBlockSize 返回正确的 block size

- **WHEN** `format == E_R8G8B8A8_UNORM`
- **THEN** `GetFormatBlockSize(format) == 4`

- **WHEN** `format == E_R32G32B32A32_SFLOAT`
- **THEN** `GetFormatBlockSize(format) == 16`

- **WHEN** `format == E_D16_UNORM`
- **THEN** `GetFormatBlockSize(format) == 2`

- **WHEN** `format` 是未处理的枚举值
- **THEN** `GetFormatBlockSize(format)` 返回 4 作为 fallback

#### Scenario: IsCompressedFormat 在当前枚举范围内返回 false

- **WHEN** 任意当前 `ETextureFormat` 值
- **THEN** `IsCompressedFormat(format) == false`（BCn 格式未在枚举中定义）
