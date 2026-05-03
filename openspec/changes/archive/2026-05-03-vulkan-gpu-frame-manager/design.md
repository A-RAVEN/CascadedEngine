## Context

当前 Vulkan 后端 `VulkanGraphExecutor::CompileAndExecute` 是同步单帧执行模式：每帧创建所有资源 → 录制命令 → 提交 → `waitForFences(UINT64_MAX)` → 销毁所有资源 → 返回。CPU 和 GPU 完全串行。

D3D12 后端通过 `GPUFrameManager` 实现了 N 帧流水线架构（默认 2 帧），CPU 编译帧 N+1 的同时 GPU 执行帧 N。本设计将相同的架构适配到 Vulkan API 上。

### 参考实现

D3D12 在 `D3D12RenderBackend/private/ResourceManagment/FrameBoundResourceManager.h/.cpp` 中的三层结构：

```
GPUFrameManager → FrameContext → FrameBoundResourceManager
```

### 同步模型差异

| 概念 | D3D12 | Vulkan 等效 |
|------|-------|------------|
| CPU 互斥 | `std::binary_semaphore` | `std::binary_semaphore` (相同) |
| GPU 进度跟踪 | `ID3D12Fence` + 递增 uint64 value | 每 FrameContext 独立 `vk::Fence` |
| 命令列表 | `ID3D12CommandList` + `ID3D12CommandAllocator` | `vk::CommandBuffer` + `vk::CommandPool` |

D3D12 使用全局 fence + 递增 counter 跟踪每帧进度。Vulkan 的 `vk::Fence` 是二值的（signaled/unsignaled），无法设置任意值。最简适配方案：每个 `VulkanFrameContext` 拥有独立的 `vk::Fence`。

## Goals / Non-Goals

**Goals:**
- 实现与 D3D12 `GPUFrameManager` 功能等效的 Vulkan 帧管理子系统
- 支持可配置 `maxFrameCount`（默认 2，即双缓冲流水线）
- `CompileAndExecute` 改为异步提交，CPU 不再阻塞等待 GPU
- 每帧资源（CommandPool、DescriptorPool、StagingBuffer）在帧间复用
- 实现 `VulkanLinearMemoryManager` 替代独立 VMA staging buffer 分配/释放

**Non-Goals:**
- 跨队列同步（Compute queue family ownership transfer）
- Sampler 全局缓存（SamplerManager）
- `ApplyExternalResourceStates` 实现
- 多线程并发访问 FrameContext（与 D3D12 一致，单线程 round-robin）

## Decisions

### Decision 1: 使用独立 `vk::Fence` 而非 Timeline Semaphore

**选择**: 每个 `VulkanFrameContext` 拥有一对 `vk::Fence`（direct + compute），不使用 `VK_SEMAPHORE_TYPE_TIMELINE`。

**理由**: D3D12 使用全局 fence + counter 是因为 `ID3D12Fence` 天然支持 `Signal(value)`。Vulkan 的 `vk::Fence` 是二值的，但每 FrameContext 独立 fence 的功能完全等效：`Aquire()` → `waitForFences(frameFence)` → `resetFences()` → 复用资源。Timeline semaphore 虽然也能实现 counter 模型，但引入了不必要的复杂度，且需要 Vulkan 1.2 的 `VK_KHR_timeline_semaphore` 扩展。

**替代方案**: 使用全局 `VK_SEMAPHORE_TYPE_TIMELINE` 模拟 D3D12 的 fence counter 模式。复杂度更高，收益为零。

### Decision 2: FrameContext 资源所有权模型

**选择**: `VulkanFrameBoundResourceManager` **持有**（非引用）以下每帧资源：
- `VulkanCommandListManager` — 从 `VulkanGraphExecutor` 迁移过来
- `vk::DescriptorPool` — 从 `VulkanGraphExecutor::BuildDescriptors` 迁移过来
- `VulkanLinearMemoryManager` — 新建
- `vk::Fence` × 2 — Direct + Compute 队列提交 fence

**理由**: 与 D3D12 `FrameBoundResourceManager` 一致，每帧资源完全独立，`Reset()` 时快速重置而非销毁重建。Vulkan 的 `vkResetCommandPool` 和 `vkResetDescriptorPool` 支持高效重置。

### Decision 3: VulkanGraphExecutor 改造策略 — 对齐 D3D12 生命周期

**选择**: `VulkanGraphExecutor` **保持每帧创建/销毁**（与 D3D12 `D3D12GPUGraphExecutor` 一致），跨帧缓存（ShaderModule、PipelineLayout、DescriptorSetLayout、RenderPass、Framebuffer）**移到 `RenderBackend_Vulkan` 作为持久成员**。`CompileAndExecute` 签名变为：

```cpp
void CompileAndExecute(castl::shared_ptr<GPUGraph> const& graph, 
                       VulkanGPUFrameManager::PFrameContext&& frameContext);
```

**理由**: D3D12 executor 每帧创建销毁，只持有帧级临时数据（`m_LocalResourceManager`、`m_ConstantBufferManager`、`m_ShaderResourceInstances`），跨帧缓存（Pipeline/RootSignature/Sampler Manager）全在 `RenderBackend_D3D12` 上。Vulkan 采用完全相同的模式：

```
RenderBackend_Vulkan 持久成员 (新增缓存):
├── m_ShaderModuleCache        ← 从 executor 搬出
├── m_PipelineLayoutCache      ← 合并到已有 m_PipelineLayoutContainer
├── m_DescriptorSetLayoutCache ← 合并到已有 m_DescriptorSetLayoutContainer
├── m_RenderPassCache          ← 从 executor 搬出
├── m_FramebufferCache         ← 从 executor 搬出
└── m_GPUFrameManager          ← 新增 (本 Change)

VulkanGraphExecutor 每帧成员 (仅帧级):
├── m_LocalResourceManager     ← 帧级资源分配
├── m_ConstantBufferManager    ← 帧级 CB 管理
├── m_CurrentFrameContext      ← 当前帧引用
├── m_ExecutionBatches / passRWStates / lifetimes  ← 帧级临时数据
└── m_ShaderResourceInstances  ← 帧级 binding
```

**CBuffer staging 实现细节**: 当前 CBuffer staging 逻辑在 `VulkanGraphExecutor::RecordBatchCommands()` 中直接创建独立 VMA staging buffer。改造后 executor 通过 `frameContext->GetResourceManager().GetStagingMemoryManager().AllocUploadStagingBuffer()` 获取持久映射的 staging 内存，无需经过 `VulkanConstantBufferManager`。

### Decision 4: LinearMemoryManager 实现策略

**选择**: 从 VMA 分配大块 `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` buffer（如 64MB），使用 `VMA_ALLOCATION_CREATE_MAPPED_BIT` 持久映射。帧内线性分配子范围：`AllocUploadStagingBuffer(size, alignment)` 将 offset 按 alignment 向上取整后分配，返回 `(alignedOffset, basePtr + alignedOffset)`。`Reset()` 简单地将 offset 归零，不释放 VMA 内存也不 unmap。需要新块时分配第二个大 buffer，帧末统一回收。

```
┌──────────────────────────────────────────┐
│  VMA Staging Buffer (64MB)               │
│  ├── Allocation 1 (CBuffer A upload)     │
│  ├── Allocation 2 (CBuffer B upload)     │
│  ├── Allocation 3 (texture upload)       │
│  └── ... free space ...                  │
│  offset: 已用字节数, Reset() → 0         │
└──────────────────────────────────────────┘
```

**理由**: 减少每帧数十次 `vmaAllocateMemory` / `vmaFreeMemory` 调用，显著降低分配开销。与 D3D12 `LinearMemoryManager` 功能等效。

### Decision 5: maxFrameCount 默认值

**选择**: 默认 `maxFrameCount = 2`（双缓冲）。

**理由**: D3D12 当前配置为 1（`GPUFrameManager(app, 1)`），但从代码结构看支持 >1。Vulkan 默认 2 可获得 CPU/GPU 一帧重叠的流水线效果。代价是额外一帧的 VRAM 开销（额外的 CommandPool、DescriptorPool、64MB staging buffer × N-1）。用户可通过构造函数参数调整，且始终 `≥ 1`。调回 1 即可恢复"同步模式"行为。

### Decision 6: 跨队列 fence 策略

**选择**: 本 Change 中每个 FrameContext 仍只有一对 fence（direct + compute），但 compute fence 在当前阶段与 direct fence 使用相同的 queue submit。跨队列同步（Phase 2 第 4 项）留待后续 Change。

**理由**: 当前 Vulkan 后端实际只使用 graphics queue family，compute queue 的高级用法尚未启用。保留 compute fence 字段以保持 API 兼容，后续 Change 实现真正的跨队列 Signal/等待。

### Decision 7: Image Upload Staging 也纳入 LinearMemoryManager

**选择**: Image upload（Transfer pass 中的 `m_ImageDataUploads`）也使用 `VulkanLinearMemoryManager` 分配 staging buffer，与 CBuffer staging 保持一致。

**理由**: 当前 `RecordTransferPass` 中 image upload 也是每张图独立 `createBuffer + vmaAllocateMemory + map/memcpy/unmap + vmaFreeMemory`。统一使用 LinearMemoryManager 可消除此开销。Image upload 的 staging 用量通常远小于 64MB 上限，与 CBuffer staging 复用同一 buffer 池不会造成容量问题。

### FrameContext 构造与 Move 语义

`VulkanFrameContext` 无需 move 构造。`PFrameContext`（`castl::unique_ptr<FrameContext, deleter>`）持有 `m_FrameContexts` vector 中元素的裸指针 + custom deleter（与 D3D12 一致）。`binary_semaphore` 默认构造计数为 1（"可用"），`Aquire()` 中 `acquire()` 减为 0（"被占用"），`Reset()` 中 `release()` 恢复为 1。

### Decision 9: DescriptorPool 大小策略 — 预检 + 按需重建

**选择**: `CompileAndExecute` 的 Prepare 阶段统计当前帧 descriptor 需求，然后调用 `FrameBoundResourceManager::EnsurePoolCapacity(maxSets, poolSizes)` 检查当前 pool 是否满足需求。够则 `vkResetDescriptorPool`（零分配），不够则销毁重建更大 pool。

```
Prepare():
  for each ShaderResourceInstance:
    统计 maxSets, uniformBufferCount, sampledImageCount, storageBufferCount, ...
  frameContext.GetResourceManager().EnsurePoolCapacity(maxSets, poolSizes)

EnsurePoolCapacity(maxSets, poolSizes):
  if (currentPool != null && currentPool足够):
      return  // 稳态：走 vkResetDescriptorPool（Aquire 中已调用）
  else:
      vkDestroyDescriptorPool(oldPool)      // 安全：Aquire 已 waitForFences
      vkCreateDescriptorPool(maxSets, ...)   // 按当前帧需求创建
```

Pool 初始值为当前帧统计值；多帧运行后自然收敛到峰值，此后每帧走快速 Reset 路径。

**理由**: 不同帧的 shader/dispatch 组合可能不同，固定大小的 pool 不可行。Aquire 时的 fence 等待保证了旧 pool 可以安全销毁。预检确保 `vkAllocateDescriptorSets` 在 `BuildDescriptors` 阶段必定成功。

### Decision 10: Present 同步 — Per-Window Semaphore Pair

**选择**: `VulkanFrameContext` 持有 `castl::vector<WindowSync>`，其中每窗口包含一对 `vk::Semaphore`（acquire + present）。`AcquireNextImage` **在** `CompileAndExecute` 中执行（不在 `AquireFrameContext`），因为只有这里知道有哪些窗口。

```
同步链:
AcquireNextImage ──[signal]──▶ acquireSemaphore
                                  │
queue.submit ──[wait]─────────────┘
queue.submit ──[signal]──▶ presentSemaphore
                                  │
vkQueuePresentKHR ──[wait]───────┘
```

`SubmitBatches` 中最后一个 batch 的 `SubmitInfo` 设置 `waitSemaphoreCount = windowCount`（wait 所有 acquire semaphore）和 `signalSemaphoreCount = windowCount`（signal 所有 present semaphore）。非最后 batch 不涉及 semaphore。

首帧 `EnsureWindowSync(idx)` 按需创建 semaphore pair；后续帧复用，仅窗口数变化时重建。

**理由**: 修复当前代码的 bug（acquireSemaphore 无人 wait）。与 D3D12 语义等效（D3D12 通过 fence + resource barrier 隐式处理，Vulkan 需要显式 semaphore 链）。

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    RenderBackend_Vulkan                           │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  跨帧缓存 (从 VulkanGraphExecutor 搬出)                     │  │
│  │  - m_ShaderModuleCache  - m_DescriptorSetLayoutContainer    │  │
│  │  - m_PipelineLayoutContainer  - m_RenderPassCache           │  │
│  │  - m_FramebufferCache                                      │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              VulkanGPUFrameManager                          │  │
│  │  m_FrameIndex = 0,1,2,...                                  │  │
│  │  m_MaxFrameContexts = 2                                     │  │
│  │  ┌─────────────────────┐  ┌─────────────────────┐          │  │
│  │  │ VulkanFrameContext  │  │ VulkanFrameContext  │          │  │
│  │  │ [0]                 │  │ [1]                 │          │  │
│  │  │ - binary_semaphore  │  │ - binary_semaphore  │          │  │
│  │  │ - vk::Fence(Dir)    │  │ - vk::Fence(Dir)    │          │  │
│  │  │ - vk::Fence(Comp)   │  │ - vk::Fence(Comp)   │          │  │
│  │  │ - WindowSync[]      │  │ - WindowSync[]      │          │  │
│  │  │   (acquire+present  │  │   (acquire+present  │          │  │
│  │  │    semaphore pair   │  │    semaphore pair   │          │  │
│  │  │    per window)      │  │    per window)      │          │  │
│  │  │ ┌─────────────────┐ │  │ ┌─────────────────┐ │          │  │
│  │  │ │ FrameBoundRes   │ │  │ │ FrameBoundRes   │ │          │  │
│  │  │ │ - CmdListMgr    │ │  │ │ - CmdListMgr    │ │          │  │
│  │  │ │ - DescPool      │ │  │ │ - DescPool      │ │          │  │
│  │  │ │ - LinearMemMgr  │ │  │ │ - LinearMemMgr  │ │          │  │
│  │  │ └─────────────────┘ │  │ └─────────────────┘ │          │  │
│  │  └─────────────────────┘  └─────────────────────┘          │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              VulkanGraphExecutor (每帧创建/销毁)             │  │
│  │  - m_LocalResourceManager      - m_ConstantBufferManager    │  │
│  │  - m_CurrentFrameContext       - m_ExecutionBatches         │  │
│  │  - m_ShaderResourceInstances   - passRWStates/lifetimes     │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### 执行流程

```
RenderBackend_Vulkan::ExecuteGraph(graph):
  frameContext = m_GPUFrameManager.AquireFrameContext()
    ├── contextID = m_FrameIndex % m_MaxFrameContexts
    ├── m_FrameIndex++
    ├── context.Aquire()
    │   ├── m_Semaphore.acquire()        // CPU 互斥
    │   ├── waitForFences(m_DirectFence)  // 等待此 context 上一帧 GPU 完成
    │   ├── resetFences(m_DirectFence)
    │   └── m_FrameBoundResources.Reset()
    │       ├── m_CommandListManager.Reset()     // vkResetCommandPool
    │       ├── m_DescriptorPool.reset()         // vkResetDescriptorPool
    │       └── m_LinearMemoryManager.Reset()    // offset = 0
    └── return PFrameContext(&context, deleter)
  
  executor.CompileAndExecute(graph, move(frameContext))
    ├── Prepare(): 统计 descriptor 需求 → EnsurePoolCapacity()
    ├── ... BuildBatches + Barriers + PipelineStates ...
    ├── 对每个窗口 Acquire swapchain image:
    │   for each backbuffer:
    │     pWindow->AcquireNextImage(sync.acquireSemaphore)
    ├── 从 frameContext 获取 CommandBuffer/DescriptorPool/StagingMemory
    ├── RecordBatchCommands(...)
    ├── SubmitBatches():
    │   for each batch:
    │     if (isLastBatch):
    │       wait = {acquire_0, acquire_1, ...}, signal = {present_0, present_1, ...}
    │     queue.submit(info, context.m_DirectFence)   // 异步！不等待
    ├── PresentWindows():
    │   for each window:
    │     pWindow->Present(queue, sync.presentSemaphore)
    └── Reset(): 清空帧级临时数据 + descriptor set 引用

  executor.Release()
  // frameContext 析构 → deleter → context.Reset() → m_Semaphore.release()
```

## Risks / Trade-offs

- **[复杂度] FrameContext 资源迁移可能遗漏**: 当前 `VulkanGraphExecutor::Reset()` 销毁很多资源，需要仔细识别哪些是"帧级"vs"跨帧" → 查看所有 `Release*` / `Cleanup*` 调用，逐项分类
- **[正确性] Fence 等待逻辑**: 首帧执行时 fence 尚未创建，需要处理 `m_DirectFence == nullptr` 的情况 → `Aquire()` 中检查，首帧跳过 wait
- **[内存] VMA staging buffer 预分配大小**: 64MB 可能不够或浪费 → 可配置，不够时自动分配第二块（与 D3D12 行为一致）
- **[性能] DescriptorPool 重置**: `vkResetDescriptorPool` 要求所有已分配的 descriptor set 不再被使用。GPU fence 已保证这一点。**方案已确定**: Decision 9 预检+按需重建，稳态零分配
- **[正确性] Framebuffer 缓存 key 可能跨帧失效**: Framebuffer 缓存 key 使用 `vk::ImageView` 指针参与 hash。如果 image view 在下帧从 `VulkanGraphLocalResourceManager` 重新分配，指针可能变化导致缓存 miss → 需验证 image view 生命周期是否跨帧稳定，或改用逻辑 key（resource handle + subresource range）
- **[正确性] DescriptorSet handle 在 Reset 后失效**: `vkResetDescriptorPool` 隐式 free 所有 descriptor set。executor 持有的 `VulkanResourceBindingInstance` 其 `vk::DescriptorSet` handle 在 FrameContext Reset 后无效 → `CompileAndExecute` 末尾需清理 binding instance 的 descriptor set 引用，下帧重新分配

## Migration Plan

1. 创建 `VulkanFrameManager.h/.cpp`（GPUFrameManager + FrameContext + FrameBoundResourceManager）
2. 创建 `VulkanLinearMemoryManager.h/.cpp`
3. **将跨帧缓存从 `VulkanGraphExecutor` 搬到 `RenderBackend_Vulkan`**：`m_ShaderModuleCache`、`m_RenderPassCache`、`m_FramebufferCache` 作为新成员；`m_PipelineLayoutCache` 合并到已有 `m_PipelineLayoutContainer`；`m_DescriptorSetLayoutCache` 合并到已有 `m_DescriptorSetLayoutContainer`
4. 改造 `VulkanGraphExecutor`：移除缓存成员，改为通过 `GetApp()` 访问；接受 `PFrameContext&&`；移除 `m_Fences`/`m_Semaphores`/`m_DescriptorPool`/`m_PendingStagingBuffers`
5. 改造 `RenderBackend_Vulkan::ExecuteGraph` 使用 `GPUFrameManager`（executor 仍每帧创建销毁，与 D3D12 一致）
6. 现有 `VulkanCommandListManager` 可复用，只需确保其 `Reset()` 接口正确
7. 不需要回滚策略 — 新旧代码路径在 `ExecuteGraph` 中直接替换
