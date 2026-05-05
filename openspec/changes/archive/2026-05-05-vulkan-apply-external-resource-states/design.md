## Context

当前 `VulkanGraphExecutor::ApplyExternalResourceStates()` 为空实现。D3D12 后端在 `GPUGraphExecutor.cpp:951-1006` 有完整参考实现。

Vulkan 侧已具备全部前置条件：
- `m_ImageLifetimes` / `m_BufferLifetimes` 在 `BuildResourceUsageRanges()` 中已填充（类型 `VulkanResourceUsageRangeData`）
- `VulkanResourceUsageRangeData` 持有 `castl::vector<BatchAndState> states`，其中最后一个元素即为帧末状态
- `CollectResources` 已正确区分 `ImageHandle::ImageType::External` / `Backbuffer` / `Internal`
- VulkanTexture 已有 `m_CurrentLayout`（vk::ImageLayout），VulkanBuffer 已有 `m_PipelineStageFlags` + `m_AccessFlags`

缺失的是：
1. VulkanTexture / VulkanBuffer 没有存储完整的 `VulkanResourceState`（含 stageFlags + accessFlags + imageLayout + queueType）
2. `ApplyExternalResourceStates()` 函数体为空

## Goals / Non-Goals

**Goals:**
- 实现帧末外部资源状态写回，功能对齐 D3D12 的 `ApplyExternalResourceStates`
- VulkanTexture / VulkanBuffer 可以记录并查询上一次的资源状态

**Non-Goals:**
- 不实现 D3D12 的 `EnsureResourceView()` 完整功能（那是 descriptor 创建时的事情，不在本次 scope）
- 不修改 barrier 生成逻辑（那属于 Queue Family 相关的独立变更）
- 不创建全局 sampler 管理器

## Decisions

### Decision 1: 资源状态存储类型

**选择**: 在 VulkanTexture / VulkanBuffer 中存储 `VulkanResourceState`（而非 D3D12 的 `ResourceState`）

**理由**: D3D12 的 `ResourceState` 只含 enum state，而 Vulkan 侧需要完整的 vk 状态三件套（accessFlags + stageFlags + imageLayout）才能正确生成 barrier。`VulkanResourceState` 结构已定义（`VulkanGraphExecutor.h:27`），包含了所有 Vulkan 特有字段。

**替代方案**: 只存最后 imageLayout — 但 buffer 没有 imageLayout，且 barrier 生成需要 stageFlags 信息，不如存完整结构。

### Decision 2: ApplyExternalResourceStates 实现结构

**选择**: 直接复制 D3D12 控制流 — 遍历 imageRanges → 筛选 External/Backbuffer → 取最后一帧状态 → 写回对象

**理由**: 逻辑一致，易于将来对照维护。D3D12 的实现简单直接（~56行），没有抽象复杂度。

### Decision 3: VulkanResourceState 与外部结构的关系

**选择**: 不在 `VulkanResourceState` 中引入对 `ResourceState` 的依赖。当前存储纯 vk 字段，足够使用。

**理由**: `ResourceState`（D3D12 侧或公共接口侧）在当前 Vulkan 后端不使用。Vulkan 的全部资源状态转换都通过 `VulkanResourceState` 完成。

### Decision 4: Backbuffer 状态存储模型

**选择**: 在 `VulkanWindowHandle` 中使用 `castl::vector<VulkanResourceState> m_BackBufferResourceStates` 按 `m_CurrentImageIndex` 索引存储每个 swapchain image 的状态（而非单一状态变量）

**理由**: Swapchain 通常有 2~3 张 image 轮换使用。第 N 帧的 backbuffer image 到第 N+2 帧才会再次使用。若只存单数状态，轮换后会丢失之前写入的状态。D3D12 的 `WindowContext` 也是 per-backbuffer 存储：`m_BackBuffers[m_BackBufferIndex].resourceState`。

**实现**: `ApplyCurrentBackBufferResourceState(state)` 执行 `m_BackBufferResourceStates[m_CurrentImageIndex] = state`；初始化时 `resize` 为 swapchain image 数量。

### Decision 5: 空状态防御

**选择**: 在 `ApplyExternalResourceStates()` 中对每个 External/Backbuffer 资源断言 `!resourceUsageRange.states.empty()`，与 D3D12 参考实现保持一致。

**理由**: 虽然正常情况下 `states` 不为空，但断言可以在资源生命周期计算出问题时快速定位异常。

### Decision 6: 资源对象初始状态

**选择**: `VulkanTexture::m_LastResourceState` 初始化为 `{ eNone, eTopOfPipe, eUndefined, EGPUQueueType::eDirect, true }`；`VulkanBuffer::m_LastResourceState` 初始化为 `{ eNone, eTopOfPipe, eUndefined, EGPUQueueType::eDirect, false }`。

**理由**: 与现有字段的初始化值保持一致 — `VulkanTexture::m_CurrentLayout` 初始为 `eUndefined`，`VulkanBuffer::m_PipelineStageFlags` 初始为 `eTopOfPipe`，`m_AccessFlags` 初始为 `eNone`。对于 Buffer，`imageLayout` 字段无意义（`isImage=false`），设为 `eUndefined` 作为哨兵值。

## Risks / Trade-offs

- **风险**: OpenSpec 依赖的 `projects` 查询当前在 render pipeline 基础上可能出现精度的 issue。但本变更不触及 barrier/pipeline 流程，只做状态存储，风险可控
- **影响面**: 仅新增接口（Add），不修改现有逻辑，回归风险低
