## Context

### 当前状态

`VulkanResourceAliasing` 子系统工作正常：
1. `RegisterResource()` 记录资源生命周期和大小
2. `AnalyzeAndPlanAliasing()` 使用贪心区间调度算法计算每个资源的别名偏移（`aliasedAlloc.offset`）
3. `AllocateAliasedPool(totalSize)` 通过 VMA 分配一大块持久映射的 GPU 内存
4. `GetAliasedAllocation(resourceId)` 可查询计算好的偏移

但 `VulkanGraphLocalResourceManager::AllocateAliasedResources()` 中资源创建完全独立：
```cpp
// 当前实现（VulkanGraphLocalResourceManager.cpp:141）
managed.buffer = GetDevice().createBuffer(bufferInfo);  // 独立分配！

// 当前实现（VulkanGraphLocalResourceManager.cpp:170）
managed.image = GetDevice().createImage(imageInfo);      // 独立分配！

// aliasedAlloc 被获取但未使用（行 130）
auto aliasedAlloc = m_AliasingManager.GetAliasedAllocation(id);
```

D3D12 参考实现使用 `AliasedMemoryAllocator::AllocateGPUResource()` 通过 D3D12MA Virtual Block 完成子分配，每个 tile resource 被 `CreatePlacedResource` 放置在 virtual block 的指定偏移上。

### 对抗验证发现的关键缺陷

对抗验证揭示了原设计未考虑的关键缺陷：

| 严重度 | 缺陷 | 影响 |
|--------|------|------|
| **CRITICAL** | `AliasedAllocation` 仅存 `VmaAllocation`，`vkBind*Memory` 需要 `VkDeviceMemory` | 绑定调用根本无法编译/执行 |
| **CRITICAL** | `GetFormatBlockSize`/`IsCompressedFormat` 在代码库中不存在 | 纹理大小估算和格式判断无基础 |
| **CRITICAL** | ImageView `aspectMask` 硬编码 `eColor` | 深度格式触发 VUID 验证错误 |
| **MAJOR** | `AnalyzeAndPlanAliasing` 用固定 256B 对齐 | 实际对齐可达 64KB+，偏移不匹配 |
| **MAJOR** | `AddBuffer()` 独立分配路径绕过 aliasing | 后注册 buffer 无法复用内存 |
| **MAJOR** | `mappedPtr` 未从 aliased pool 传播 | CBuffer CPU 写入路径断裂 |
| **MAJOR** | Image `usage` flags 硬编码 `eSampled\|eColorAttachment` | 深度缓冲/transfer-only 纹理缺少正确 flags |
| **MAJOR** | `VulkanMemoryManager` 缺少 `VmaAllocationInfo` 查询 | 外部无法获取 `deviceMemory` |

### 技术约束

- Vulkan 要求 `vkBindBufferMemory` 的 `memoryOffset` 必须是 `VkMemoryRequirements::alignment` 的整数倍
- Buffer 和 image 可以绑定到同一 `VkDeviceMemory` 的不同偏移，只要不重叠（Vulkan 规范 12.8）
- VMA allocator 已正确初始化，`vmaGetAllocationInfo` 可在外部调用提取 `VkDeviceMemory`
- `VulkanTexture::ConvertFormat()` 已实现，支持所有现有 `ETextureFormat` 值
- `Common.h` 中已有 `IsDepthOnlyFormat`/`FormatHasDepth`/`FormatHasStencil` 等辅助函数

## Goals / Non-Goals

**Goals:**
1. 通过两阶段方法（先获取真实对齐，再规划绑定偏移）将 buffer/image 绑定到 aliased pool，实现真正的内存别名复用
2. 补充 VmaAllocation → VkDeviceMemory 转换链，使绑定调用可执行
3. 纹理格式从 descriptor 通过 `ConvertFormat` 获取，消除硬编码
4. ImageView aspectMask 根据格式类型动态推导（color/depth/depth-stencil）
5. Image usage flags 根据 `textureAccess` 动态推导
6. 纹理大小估算基于格式 block size 和 mip chain 面积递减求和
7. 实现 `GetFormatBlockSize`/`IsCompressedFormat` 辅助函数
8. Aliased pool 的 persistent mapped pointer 传播到 `ManagedGPUResource.mappedPtr`
9. `AddBuffer()` 纳入 aliasing 系统，不再绕过
10. 资源释放时正确清理（仅 destroy handles，内存由 pool 统一管理）
11. `VulkanMemoryManager` 新增 `GetAllocationInfo()` 查询方法

**Non-Goals:**
- 不改变 `VulkanResourceAliasing::AnalyzeAndPlanAliasing()` 的贪心调度算法逻辑
- 不引入 VMA Virtual Block（`VmaVirtualBlock`）——保持单 VMA allocation pool 模式
- 不处理 buffer/image 跨 pool 异构 memory type 问题（初始版本信任 Vulkan 规范允许混绑）
- 不新增 BCn 压缩格式到 `ETextureFormat` 枚举

## Decisions

### Decision 1: 两阶段资源绑定流程（替代原简易 `vkBind*Memory` 方案）

**问题**: 原方案假设 `AnalyzeAndPlanAliasing` 用 `ResourceLifetime.alignment = 256` 即可规划偏移，但 `vkGetBufferMemoryRequirements` 可返回 64KB+ 的对齐。规划偏移与实际硬件对齐不匹配，会导致覆盖或无效绑定。

**选择**: 采用两阶段方法：

**Phase A -- 获取真实对齐**:
1. 在 `AllocateAliasedResources()` 中先遍历所有 `m_LocalResources`，为每个 buffer 调用 `vkCreateBuffer(cInfo, nullptr, &rawBuffer)` + `vkGetBufferMemoryRequirements(device, rawBuffer, &memReqs)`，为每个 image 调用 `vkCreateImage(cInfo, nullptr, &rawImage)` + `vkGetImageMemoryRequirements(device, rawImage, &memReqs)`
2. 将获取到的 `memReqs.alignment` 和 `memReqs.size` 回传给 `VulkanResourceAliasing`（新增 `RegisterRealRequirements(resourceId, alignment, size)` 或 `ReplanWithRealAlignment()` 方法）
3. 重新运行 aliasing 规划（或就地调整偏移）——对 `AnalyzeAndPlanAliasing` 增加一个 `Replan()` 步骤，使用真实对齐覆盖 `ResourceLifetime.alignment`
4. 销毁 Phase A 创建的临时 buffer/image handle

**Phase B -- 分配 pool 并绑定**:
1. `AllocateAliasedPool()` 使用修正后的 `m_TotalAliasedSize` 分配 VMA pool
2. 通过 `vmaGetAllocationInfo` 从 `m_AliasedPoolAllocation` 提取 `VkDeviceMemory`
3. 再次创建 raw buffer/image，调用 `vkBindBufferMemory(buffer, deviceMemory, alignedOffset)` 绑定

**原因**: 对齐值是硬件决定的，无法预先假定。必须从真实的 buffer/image 对象上查询。跳过 Phase A 直接绑定可能导致 `VK_ERROR_INVALID_OFFSET` 或静默的数据破坏。

**备选方案**: 保守使用硬件报告的最大可能对齐（256MB），过度保守，pool 极大浪费 ===> 不选。

### Decision 2: VmaAllocation → VkDeviceMemory 转换方案

**选择**: 在 `VulkanResourceAliasing::AliasedAllocation` 中新增 `VkDeviceMemory deviceMemory` 字段，在 `AllocateAliasedPool()` 中通过 `vmaGetAllocationInfo(m_AliasedPoolAllocation, &allocInfo)` 提取并存储。

**同时**: 在 `VulkanMemoryManager` 中新增便捷方法：
```cpp
void GetAllocationInfo(VmaAllocation allocation, VmaAllocationInfo* outInfo) const
{
    vmaGetAllocationInfo(m_Allocator, allocation, outInfo);
}
```

**原因**: 集中管理 `deviceMemory` 避免每次绑定都查询 VMA。`AliasedAllocation` 已传递给绑定逻辑，扩展该结构是最小改动。

**备选**: 在绑定前调用 `vmaGetAllocationInfo` 提取 `deviceMemory`，每次绑定都查询。缺点：VMA API 调用过多。

### Decision 3: 纹理格式使用 `VulkanTexture::ConvertFormat()`

```cpp
imageInfo.format = VulkanTexture::ConvertFormat(localResource.textureDesc.format);
viewInfo.format = imageInfo.format;  // image view format 与 image 一致
```

`ConvertFormat` 已在 `VulkanTexture.cpp` 中实现，支持所有现有 `ETextureFormat` 枚举值到 `vk::Format` 的映射。

### Decision 4: ImageView aspectMask 动态推导

**选择**: 根据 `textureDesc.format` 派生 aspectMask：
```cpp
vk::ImageAspectFlags GetImageAspectMask(ETextureFormat format)
{
    if (FormatHasStencil(format))
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    if (FormatHasDepth(format))
        return vk::ImageAspectFlagBits::eDepth;
    return vk::ImageAspectFlagBits::eColor;
}
```

`Common.h` 中已有 `FormatHasDepth`/`FormatHasStencil` 等辅助函数，可直接复用。

### Decision 5: Image usage flags 动态推导

**选择**: 根据 `localResource.textureAccess` 动态构建 `vk::ImageUsageFlags`：
```cpp
vk::ImageUsageFlags GetTextureImageUsage(ETextureAccessTypeFlags access)
{
    vk::ImageUsageFlags usage{};
    if (access & ETextureAccessType::eSampled)
        usage |= vk::ImageUsageFlagBits::eSampled;
    if (access & ETextureAccessType::eRT)
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (access & ETextureAccessType::eDepthStencil)
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (access & ETextureAccessType::eTransferSrc)
        usage |= vk::ImageUsageFlagBits::eTransferSrc;
    if (access & ETextureAccessType::eTransferDst)
        usage |= vk::ImageUsageFlagBits::eTransferDst;
    if (access & ETextureAccessType::eUnorderedAccess)
        usage |= vk::ImageUsageFlagBits::eStorage;
    return usage;
}
```

### Decision 6: 纹理大小正确计算（含辅助函数实现）

**问题**: 当前 `width * height * 4 * mipLevels * layers` 假设：
- 固定 4 bytes/pixel（仅 `RGBA8` 适用）
- 每个 mip level 面积与 base 相同
- 忽略 alignment 要求

**修复**: 先实现两个辅助函数，再使用它们进行估算：

**`GetFormatBlockSize(ETextureFormat format)`** -- 静态查表返回每像素/block 字节数：
```cpp
constexpr uint32_t GetFormatBlockSize(ETextureFormat format)
{
    switch (format)
    {
    case ETextureFormat::E_R8_UNORM:       return 1;
    case ETextureFormat::E_R8G8_UNORM:     return 2;
    case ETextureFormat::E_R8G8B8A8_UNORM: return 4;
    case ETextureFormat::E_B8G8R8A8_UNORM: return 4;
    case ETextureFormat::E_R16_UNORM:
    case ETextureFormat::E_R16_SFLOAT:     return 2;
    case ETextureFormat::E_R16G16_SFLOAT:  return 4;
    case ETextureFormat::E_R16G16B16A16_UNORM:
    case ETextureFormat::E_R16G16B16A16_SFLOAT: return 8;
    case ETextureFormat::E_R32_SFLOAT:     return 4;
    case ETextureFormat::E_R32G32_SFLOAT:  return 8;
    case ETextureFormat::E_R32G32B32A32_SFLOAT: return 16;
    case ETextureFormat::E_D16_UNORM:      return 2;
    case ETextureFormat::E_D32_SFLOAT:     return 4;
    case ETextureFormat::E_D24_UNORM_S8_UINT: return 4;
    case ETextureFormat::E_D32_SFLOAT_S8_UINT: return 8;
    default: return 4; // fallback to RGBA8
    }
}
```

**`IsCompressedFormat(ETextureFormat format)`** -- 当前 `ETextureFormat` 枚举不含 BCn 格式，直接返回 false：
```cpp
constexpr bool IsCompressedFormat(ETextureFormat format)
{
    // BCn 等压缩格式暂不在 ETextureFormat 枚举中
    return false;
}
```

**`CalculateTextureSize()`** 使用上述辅助函数按 mip chain 求和。

**放置位置**: `GetFormatBlockSize` 和 `IsCompressedFormat` 放在 `Common.h` 中（与其他 `IsDepthOnlyFormat` 等同类的格式辅助函数一起），`CalculateTextureSize` 放在 `VulkanGraphLocalResourceManager.cpp` 中作为匿名 namespace 函数。

### Decision 7: AddBuffer() 纳入 aliasing 系统

**选择**: 修改 `AddBuffer()`——若 aliased pool 已分配（`m_AliasingManager` 的 pool 非空），执行完整的 Phase A + Phase B 流程将 buffer 绑定到 pool；若尚未分配，仅调用 `RegisterTemporaryBuffer()` 注册并在 `AllocateAliasedResources()` 中统一创建和绑定。

```cpp
uint64_t AddBuffer(GPUBufferDescriptor const& desc, EBufferUsageFlags usage, uint32_t batchIndex)
{
    uint64_t id = RegisterTemporaryBuffer(desc, usage, batchIndex);
    if (m_AliasingManager.IsPoolAllocated()) // 新增方法
    {
        // 已分配: 直接绑定到 pool
        CreateAndBindBufferToAliasedPool(id);
    }
    // 否则: AllocateAliasedResources() 会统一处理
    return id;
}
```

### Decision 8: ManagedGPUResource.mappedPtr 传播

**选择**: 在 `AllocateAliasedResources()` 中将 aliased pool 的持久映射指针按偏移传播到每个 `ManagedGPUResource`：
```cpp
void* poolMappedPtr = m_AliasingManager.GetMappedPtr();
managed.mappedPtr = poolMappedPtr
    ? static_cast<uint8_t*>(poolMappedPtr) + aliasedAlloc.offset
    : nullptr;
```

**原因**: 当前 `ManagedGPUResource` 有 `mappedPtr` 字段但始终为空（`device.createBuffer()` 不返回 mapped pointer）。对于需 CPU 端写入的 buffer（如 CBuffer），调用者期望通过 `managed.mappedPtr` 直接写入 aliased pool 映射内存。

## Risks / Trade-offs

- **[Risk] Buffer 和 image 混在同一 pool 可能导致 memory type 兼容性冲突**
  - Mitigation: Vulkan 规范允许在同一 `VkDeviceMemory` 上混用 buffer 和 image binding，只要每个 binding 满足各自的 alignment 和 memory type bits
  - 实际风险: 某些 GPU（尤其是移动端）buffer 和 image 可能使用不同的 `memoryTypeBits`，导致 pool allocation 时的 `memoryTypeBits = 0xFFFFFFFF` 无法同时满足两者
  - 降级方案: 如果实践中遇到 `VK_ERROR_OUT_OF_DEVICE_MEMORY`，改为分别分配 buffer pool 和 image pool（`AllocateAliasedPool` 接受 `memoryTypeBits` 参数）

- **[Risk] Phase A 临时资源创建/销毁增加帧开销**
  - Mitigation: Phase A 创建的资源无绑定内存，创建/销毁成本极低（≈us 级）。仅在 `AllocateAliasedResources()` 调用一次，每帧开销可忽略

- **[Risk] 两阶段方法增加实现复杂度**
  - Mitigation: 将 Phase A 逻辑封装为 `CreateAliasedResourcesPhaseA()` 私有方法，Phase B 逻辑封装为 `CreateAliasedResourcesPhaseB()`，保持 `AllocateAliasedResources()` 的主流程清晰

- **[Risk] raw C API 与 HPP wrappers 混用可能导致类型不匹配**
  - Mitigation: 使用 `vk::Buffer(VkBuffer)` 和 `vk::Image(VkImage)` 隐式构造进行包装，`VulkanMemoryManager` 已有 `reinterpret_cast` 转化（如 `AllocateBuffer` 行 43-44）

- **[Trade-off] 不引入 VmaVirtualBlock 意味着无法按需扩展 aliased pool**
  - 影响: pool 固定大小，如果 `AnalyzeAndPlanAliasing` 计算的 `m_TotalAliasedSize` 过大可能 over-allocate
  - 可接受: 当前贪心算法给出的 total 是有界的，后续可优化

- **[Risk] Phase A 重新规划后 total aliased size 可能增大，pool 需重新分配**
  - Mitigation: `AnalyzeAndPlanAliasing` 使用真实对齐 (`alignment`) 和真实大小（`memReqs.size`）重新计算 `m_TotalAliasedSize`，然后用新值调用 `AllocateAliasedPool()`。Phase A 过程中旧 pool 尚未分配

## Migration Plan

1. 在 `Common.h` 中实现 `GetFormatBlockSize()` 和 `IsCompressedFormat()` 辅助函数
2. 在 `VulkanMemoryManager` 中新增 `GetAllocationInfo()` 查询方法
3. 在 `VulkanResourceAliasing::AliasedAllocation` 中新增 `deviceMemory` 字段
4. 在 `VulkanResourceAliasing` 中新增 `ReplanWithRealAlignment()` 和 `IsPoolAllocated()`/`GetMappedPtr()` 方法
5. 修改 `VulkanGraphLocalResourceManager::ManagedGPUResource` -- 添加 `aliasedOffset` 字段
6. 实现 `CalculateTextureSize()`、`GetImageAspectMask()`、`GetTextureImageUsage()` 辅助函数
7. 重写 `AllocateAliasedResources()` -- 两阶段流程 + aspectMask/usage 动态推导
8. 修改 `RegisterTemporaryTexture()` -- 使用 `CalculateTextureSize()`
9. 修改 `AddBuffer()` -- 纳入 aliasing 系统
10. 修改 `ReleaseAllResources()` -- 仅 destroy handles
11. 编译验证（使用 `build.py`）

无破坏性变更（内部实现修改，不影响外部接口），无需数据迁移。

## Open Questions

- **Buffer usage flags**: 当前 `AllocateAliasedResources()` 中 buffer `usage` 硬编码为 `eTransferDst | eVertexBuffer`。是否需要从 `localResource.bufferUsage` 动态推导？（Decision: 是，与 image usage 动态推导一并处理，基于 `EBufferUsageFlags` 映射到 `vk::BufferUsageFlags`）
- **Phase A 创建的资源应在何时销毁?** 两个选项：(a) 获取 `memReqs` 后立即 destroy，Phase B 重新创建；(b) 保留并复用 Phase A 的 handle，Phase B 仅绑定内存。选项 (a) 更安全（避免 handle 复用时的状态问题），(b) 更高效。（Decision: 选项 (a)，handle 创建成本极低，不绑定内存时尤其轻量）
- **`AnalyzeAndPlanAliasing` 是否应改名?** 原方法一次完成分析和规划，现扩展为支持 re-plan。可新增 `ReplanWithRealAlignment(resourceIdToMemReqs)` 方法，保留原 `AnalyzeAndPlanAliasing` 用于初次注册资源的估算阶段。（Decision: 新增方法，不重命名）
