## Why

`VulkanGraphLocalResourceManager::AllocateAliasedResources()` 对每个临时资源调用 `device.createBuffer()/createImage()` 独立分配 GPU 内存，完全忽略 `VulkanResourceAliasing::AnalyzeAndPlanAliasing()` 计算出的别名偏移量（`aliasedAlloc.offset`）。VMA aliased pool（`AllocateAliasedPool` 已正确分配）大块内存闲置，每个资源独享独立显存，内存别名复用（resource aliasing）形同虚设。

对抗验证进一步揭示以下关键缺陷链：

- **VmaAllocation → VkDeviceMemory 转换链缺失**：`AliasedAllocation` 仅存储 `VmaAllocation` 句柄，但 `vkBindBufferMemory`/`vkBindImageMemory` 需要 `VkDeviceMemory`。需通过 `vmaGetAllocationInfo` 提取底层 `deviceMemory` 方可绑定。
- **对齐不匹配**：`AnalyzeAndPlanAliasing` 以固定 256B 对齐规划偏移，但实际 `vkGetBufferMemoryRequirements` 可能返回 64KB+ 的对齐——规划偏移与实际绑定偏移不一致，导致覆盖或无效绑定。
- **辅助函数缺失**：`GetFormatBlockSize()` 和 `IsCompressedFormat()` 在代码库中不存在，需从零实现。
- **aspectMask 硬编码**：ImageView `subresourceRange.aspectMask` 硬编码为 `eColor`，深度/深度-模板格式会触发 Vulkan Validation Layer 错误（`VUID-VkImageViewCreateInfo-format-parameter`）。
- **image usage flags 硬编码**：`vk::ImageUsageFlagBits::eSampled | eColorAttachment` 未根据 `textureAccess` 动态设置，深度纹理缺少 `eDepthStencilAttachment`。
- **AddBuffer() 绕过 aliasing**：`AddBuffer()` 在 aliased pool 分配后独立创建 buffer，不参与内存共享。
- **mappedPtr 未传播**：`ManagedGPUResource.mappedPtr` 始终为空/不关联 aliased pool 的 persistent map，CBuffer CPU 端更新无法工作。
- **`VulkanMemoryManager` 缺少 `VmaAllocationInfo` 查询接口**：无法在外部获取 `VmaAllocationInfo::deviceMemory` 用于手动绑定。

此外纹理格式硬编码为 `vk::Format::eR8G8B8A8Unorm`，纹理大小估算使用粗糙的 `width * height * 4 * mipLevels * layers` 公式，不区分格式 block size 和 mip chain 面积递减。

## What Changes

- **两阶段资源绑定流程**（新，替换决策1简易绑定方案）:
  - Phase A -- 先创建临时未绑定 buffer/image（`vkCreateBuffer/vkCreateImage`，不绑定内存），调用 `vkGetBufferMemoryRequirements/vkGetImageMemoryRequirements` 获取真实的 `alignment` 和 `size`，重新运行 `AnalyzeAndPlanAliasing`（或就地调整偏移），确保规划偏移与实际对齐一致
  - Phase B -- 按修正后的真实对齐偏移调用 `vkBindBufferMemory(buffer, aliasedPoolDeviceMemory, alignedOffset)` 和 `vkBindImageMemory(image, aliasedPoolDeviceMemory, alignedOffset)` 绑定到 aliased pool 的正确位置
- **VmaAllocation → VkDeviceMemory 提取**: 在 `VulkanResourceAliasing` 中通过 `vmaGetAllocationInfo` 从 `m_AliasedPoolAllocation` 提取 `VkDeviceMemory`，保存到 `AliasedAllocation` 结构（新增 `deviceMemory` 字段），或通过 `VulkanMemoryManager::GetAllocationInfo()` 查询
- **VulkanMemoryManager 新增查询接口**: 添加 `GetAllocationInfo(VmaAllocation, VmaAllocationInfo*)` 方法，供 aliasing 绑定逻辑使用
- **辅助函数实现**: 新增 `GetFormatBlockSize(ETextureFormat)` 和 `IsCompressedFormat(ETextureFormat)` 辅助函数，在 `VulkanGraphLocalResourceManager.cpp`（或 `Common.h`）中实现，用于纹理大小估算和格式判断
- **纹理大小修正重写**: 修正 `RegisterTemporaryTexture()` 中的临时资源尺寸估算，基于格式的 block size（通过 `GetFormatBlockSize`），按 mip level 面积递减求和，支持压缩格式（4x4 block）和非压缩格式
- **ImageView aspectMask 动态推导**: 根据 `textureDesc.format` 动态设置 `viewInfo.subresourceRange.aspectMask`——深度格式用 `eDepth`、深度模板格式用 `eDepth|eStencil`、其他用 `eColor`，消除硬编码
- **Image usage flags 动态推导**: 根据 `localResource.textureAccess` 设置 `imageInfo.usage`——包含 `eSampled` 时加 `eSampled`、包含 `eRT` 时加 `eColorAttachment`、包含 `eDepthStencil` 时加 `eDepthStencilAttachment`、包含 `eTransferSrc/eTransferDst` 时加对应 transfer flag
- **AddBuffer() 纳入 aliasing 系统**: 修改 `AddBuffer()` 逻辑——若 aliased pool 已分配（后于 `AllocateAliasedResources` 调用），则将 buffer 绑定到 aliased pool 的指定偏移；若尚未分配，则先注册后统一在 `AllocateAliasedResources` 中创建和绑定
- **ManagedGPUResource.mappedPtr 传播**: 将 aliased pool 的 persistent mapped pointer 按 `aliasedOffset` 偏移后赋值给 `ManagedGPUResource.mappedPtr`，使得 CBuffer 等需要 CPU 端写入的资源可直接通过 mapped pointer 更新
- **纹理格式动态转换**: 使用 `localResource.textureDesc.format` 通过 `VulkanTexture::ConvertFormat()` 获取实际 `vk::Format`，替换硬编码的 `eR8G8B8A8Unorm`
- **资源销毁一致性**: 修改 `ReleaseAllResources()`——仅 destroy buffer/image/imageView handles，不做独立 `freeMemory`（内存由 aliased pool 统一释放），避免 double-free 和泄漏
- **ManagedGPUResource 扩展**: 在 `ManagedGPUResource` 中添加 `aliasedOffset` 字段，记录资源的别名偏移

## Capabilities

### New Capabilities
- `vulkan-resource-aliasing`: Vulkan 后端 render graph 临时资源的别名内存复用——两阶段方法获取真实对齐后，资源绑定到共享 VMA aliased pool 的指定偏移，多个生命周期不重叠的 buffer/image 共享同一块物理显存，支持 mappedPtr CPU 端写入
- `vulkan-format-block-size`: 新增 `GetFormatBlockSize(ETextureFormat)` 和 `IsCompressedFormat(ETextureFormat)` 辅助函数，为纹理大小估算和 aspectMask 推导提供格式元信息
- `vulkan-imageview-aspectmask`: ImageView 的 aspectMask 根据纹理格式动态推导，支持 color/depth/depth-stencil

### Modified Capabilities
- `vulkan-backend-alignment`: 新增 Phase 3 差距项 #4 和 #5 —— 别名资源未绑定到 aliased pool 计算偏移、image view aspectMask 硬编码

## Impact

- **核心代码**:
  - `VulkanGraphLocalResourceManager.cpp::AllocateAliasedResources()` — 两阶段 buffer/image 创建和绑定流程重写（行 109-206）
  - `VulkanGraphLocalResourceManager.cpp::AllocateAliasedResources()` — ImageView aspectMask 动态推导、image usage flags 动态设置（行 153-198）
  - `VulkanGraphLocalResourceManager.cpp::RegisterTemporaryTexture()` — 纹理大小估算重写，使用新增辅助函数（行 89-96）
  - `VulkanGraphLocalResourceManager.cpp::AddBuffer()` — 纳入 aliasing 系统（行 44-76）
  - `VulkanGraphLocalResourceManager.cpp::ReleaseAllResources()` — 资源销毁逻辑调整（行 236-263）
  - `VulkanGraphLocalResourceManager.h::ManagedGPUResource` — 新增 `aliasedOffset` 字段（行 28-36）
  - `VulkanGraphLocalResourceManager.cpp` — 新增 `GetFormatBlockSize()`/`IsCompressedFormat()` 辅助函数，新增 `GetTextureImageUsage()`/`GetImageAspectMask()` 派生函数
  - `VulkanMemoryManager.h/cpp` — 新增 `GetAllocationInfo()` 查询方法
  - `VulkanResourceAliasing.h::AliasedAllocation` — 新增 `deviceMemory` 字段
  - `VulkanResourceAliasing.cpp::AllocateAliasedPool()` — 提取并存储 `VkDeviceMemory`
  - `VulkanResourceAliasing.cpp::AnalyzeAndPlanAliasing()` — 扩展为两阶段支持（接受外部传入的真实对齐）
- **依赖**: `VulkanTexture::ConvertFormat`（已就绪），`vmaGetAllocationInfo`（VMA API，已可调用），`vkGetBufferMemoryRequirements`/`vkGetImageMemoryRequirements`（Vulkan C API）
- **参考实现**: `D3D12RenderBackend/private/ResourceManagment/MemoryManager.h` 的 `AliasedMemoryAllocator::AllocateGPUResource()`——D3D12MA Virtual Block + `CreatePlacedResource` 子分配模式
- **非影响**: 不改动 greedy interval scheduling 别名分析核心算法（但扩展接口），不涉及 RenderInterface 抽象层

## Non-Goals

- 不改动 greedy interval scheduling 别名分析核心算法逻辑
- 不引入 D3D12 Virtual Block 等效的 VMA Virtual Block 机制（保持单 pool 模式）
- 不处理 buffer 和 image 的异构 memory type 兼容性问题（初始版本信任 Vulkan 规范允许混绑，实践中若遇到 `VK_ERROR_OUT_OF_DEVICE_MEMORY` 再拆分 pool）
- 不新增 BCn 压缩格式到 `ETextureFormat` 枚举（当前枚举不含压缩格式，使用场景未出现）
