## Context

Vulkan 后端当前的 sampler 生命周期是 per-frame 的 — `BuildDescriptors()` 中每次调用 `device.createSampler()`，返回的 `vk::Sampler` 存入 `m_CreatedSamplers` vector，在两个时机销毁：

1. 下次 `BuildDescriptors()` 开头 — 清理上一帧的 sampler
2. `Release()` — 绑定实例销毁时

D3D12 后端的 `SamplerManager` 是全局缓存：`unordered_map<TextureSamplerDescriptor, DescriptorAllocation>`，sampler 只创建一次，跨帧复用至 backend 销毁。

Vulkan sampler 是轻量对象（无 heap 分配），但重复创建/销毁仍然是浪费。更重要的是架构对齐 — 所有其他 D3D12 后端子系统（ConstantBufferManager、FrameManager、LinearMemoryManager 等）Vulkan 端都有对应，唯独 SamplerManager 缺失。

## Goals / Non-Goals

**Goals:**
- 创建 `VulkanSamplerManager` 全局 sampler 缓存，对每个唯一 `TextureSamplerDescriptor` 只创建一次 `vk::Sampler`
- Sampler 生命周期与 `RenderBackend_Vulkan` 一致，跨帧复用
- 移除 `VulkanResourceBindingInstance::m_CreatedSamplers` 及其双路径销毁逻辑
- `MakeSamplerCreateInfo()` 复用现有实现，不修改其映射逻辑
- 对齐文档更新

**Non-Goals:**
- 不修改 `TextureSamplerDescriptor` 结构体
- 不修改 `MakeSamplerCreateInfo()` 的字段映射（当前仅映射 filter/addressMode/mipmapMode，borderColor、integerFormat、maxAnisotropy、minLod/maxLod 等其他字段不在本次范围）
- 不以 Vulkan 的 sampler 管理去反向修改 D3D12 的 sampler 管理方式
- 不涉及 CombinedImageSampler 的 descriptor write 逻辑修改

## Decisions

### Decision 1: 缓存容器使用 `castl::shared_dic`

**选择**: `castl::shared_dic<TextureSamplerDescriptor, vk::Sampler>`，key 为 `TextureSamplerDescriptor` value 本身

**理由**:
- `shared_dic` 模式已在 `VulkanConstantBufferManager` 中验证（`shared_dic<VulkanShaderStruct const*, uint64_t>`）
- `TextureSamplerDescriptor` 已有完整 `operator<=>()`（C++20 defaulted comparison），可直接做 shared_dic key
- `get_or_create(key, lambda)` 语义天然匹配 "查缓存 → 未命中则创建" 的流程
- 对比 `unordered_map`，`shared_dic` 内部支持引用计数/弱引用，为未来优化留空间

**备选**: `std::unordered_map` + mutex — 当前单线程 + single device 无需并发保护，`shared_dic` 更简洁

### Decision 2: 接口设计 — 仅 `GetOrCreateSampler` + `Release`

```
VulkanSamplerManager
├── GetOrCreateSampler(TextureSamplerDescriptor) → vk::Sampler
└── Release()  // destroy all cached samplers
```

不需要 D3D12 的其他接口（`GetCPUHandle`、`m_Sampler_Allocator`），因为 Vulkan sampler 是独立对象无需 descriptor 堆。

`VulkanSamplerManager` 继承 `VulkanSubobjectBase`，通过 `GetDevice()` 获取 device、`GetApp()` 获取 backend 引用。

### Decision 3: Manager 由 `RenderBackend_Vulkan` 持有

与 D3D12 一致 — `SamplerManager` 是 `RenderBackend_D3D12` 的成员，`VulkanSamplerManager` 也作为 `RenderBackend_Vulkan` 的成员。

通过 `GetApp()->GetSamplerManager()` 在 `VulkanResourceBindingInstance` 中访问（与 D3D12 的 `GetApp()->GetSamplerManager()` 完全相同）。

### Decision 4: Sampler 创建时机

`VulkanResourceBindingInstance::BuildDescriptors()` 中调用 `GetSamplerManager().GetOrCreateSampler(samplerDesc)` 获取 `vk::Sampler`，传入 `SetSampler()`。Sampler 可能在首次使用某 `TextureSamplerDescriptor` 时创建（lazy），后续帧直接命中缓存。

与 CBuffer 的 `VulkanConstantBufferManager` 不同 — CBuffer 需要在 aliasing 之前注册（影响内存分配），Sampler 没有内存占用依赖，lazy 创建即可。

## Risks / Trade-offs

- **[低风险] Sampler 缓存无限增长**: `TextureSamplerDescriptor` 的取值空间有限（filter × 2, addressMode × 4, mipmapMode × 2 ≈ 64 种），即使全部创建也是微不足道的。无风险。
- **[低风险] 多线程访问**: 当前 `BuildDescriptors` 在单线程内运行，后续若引入多线程需加锁保护 `shared_dic`。预留 `shared_dic` 的内部线程安全性（取决于实现）。
- **[Trade-off] 放弃 D3D12 的 descriptor heap allocator 层**: Vulkan 不需要。这是 API 差异，不是缺陷。