# Vulkan后端 vs D3D12后端 对齐分析

> 生成日期: 2026-04-12 | 更新: 2026-05-04 (基于 vulkan-renderpass-format-conversion + vulkan-gpu-frame-manager 变更)
> 目标: 使 VulkanRenderBackendNew 在接口功能上向 D3D12RenderBackend 对齐

---

## 架构总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        CRenderBackend (Interface)                       │
├──────────────────────────────┬──────────────────────────────────────────┤
│   D3D12RenderBackend         │   VulkanRenderBackendNew                 │
├──────────────────────────────┼──────────────────────────────────────────┤
│  RenderBackend_D3D12         │  RenderBackend_Vulkan                    │
│  ├── GPUFrameManager         │  ├── VulkanGPUFrameManager               │
│  │   └── FrameContext        │  │   └── VulkanFrameContext              │
│  │       └── FrameBoundResMgr│  │       └── VulkanFrameBoundResourceMgr │
│  │           ├── Descriptor  │  │           ├── VulkanCommandListMgr   │
│  │           ├── GPU Heaps   │  │           ├── vk::DescriptorPool     │
│  │           ├── CmdListMgr  │  │           ├── VulkanLinearMemoryMgr  │
│  │           ├── StagingMem  │  │           └── Direct/Compute Fences  │
│  │           ├── AliasedMem  │  ├── VulkanGraphExecutor (per-frame)    │
│  │           └── Fences      │  │   ├── VulkanGraphLocalResourceManager│
│  ├── D3D12GPUGraphExecutor   │  │   └── VulkanResourceBindingInstance  │
│  ├── GPUPipelineManager      │  │       (Init/BuildResources/           │
│  ├── GPUComputePipelineMgr   │  │        BuildDescriptors)             │
│  ├── RootSignatureManager    │  ├── VulkanPipelineLibrary              │
│  ├── SamplerManager          │  ├── PipelineLibraryCache               │
│  ├── MemoryManager (D3D12MA) │  ├── ShaderLibrary (Vulkan)             │
│  ├── CommandListManager      │  ├── VulkanConstantBufferManager         │
│  └── ShaderLibrary (D3D12)   │                                        │
│                               │  ✅ 帧管理子系统已完整实现               │
│                               │  ✅ Descriptor 流程已完整实现            │
│                               │  ✅ CBuffer 上传已完整实现               │
│                               │  ✅ Pipeline + Layout 缓存已实现         │
│                               │  ✅ LinearMemoryManager 已实现           │
│                               │  ❌ No SamplerManager                    │
└──────────────────────────────┴──────────────────────────────────────────┘
```

---

## CompileAndExecute 阶段对比 (2026-05-04 更新)

```
D3D12 CompileAndExecute:                     Vulkan CompileAndExecute:
────────────────────────────                 ─────────────────────────
[1] Prepare (CollectResources                [1] Prepare
    + RegisterCBufferUsage)                     ├─ InitArraySizes
                                                ├─ CollectResources
[2] BuildDependencyFreeBatches                  ├─ CollectShaderBindings
                                                └─ RegisterCBufferUsageStates
[3] BuildResourceUsageRanges
                                            [2] BuildDependencyFreeBatches
[4] AllocateAliasedResources
    (+ CBuffer allocation)                   [3] BuildResourceUsageRanges

[5] BuildDescriptors  ← 在 barrier 之前     [4] AllocateAliasedResources

[6] PrepareBatchResourceBarriers             [4.5] BuildResources ←
                                                 ├─ CBuffer GPU buffer 分配
[7] BuildPipelineStates                          ├─ Image/Buffer fallback 注册
                                                 └─ 填充 CBufferResourceIdMap
[8] Execute
    ├─ CBuffer upload (staging→copy→barrier) [5] PrepareBatchResourceBarriers
    ├─ Bind RootSig + DescriptorTable             └─ CBuffer 添加到首帧 batch barriers
    └─ Submit (FrameContext.Signal)
                                             [6] BuildPipelineStates
[9] ApplyExternalResourceStates                  └─ PipelineLayout + DLL 缓存

[10] PresentWindows                           [7] DescriptorPool 创建

[11] Reset                                    [8] BuildDescriptors
                                                   ├─ AllocateDescriptorSets
                                                   ├─ 写 binding (CBuffer/Image/Buffer/Sampler)
                                                   └─ UpdateDescriptorSets

                                              [9] Execute
                                                   ├─ CBuffer staging upload (LinearMemoryManager)
                                                   ├─ RecordRenderPass/ComputePass
                                                   │    └─ bindDescriptorSets (连续区间)
                                                   └─ SubmitBatches (异步, 不再 waitForFences)

                                              [10] ApplyExternalResourceStates (空)
                                              [11] PresentWindows
                                              [12] Reset (FrameContext::Aquire 时等待上一帧 Fence)
```

**关键排序差异:**
- D3D12: `BuildDescriptors` 在 `PrepareBatchResourceBarriers` **之前**
- Vulkan: `BuildDescriptors` 在 `BuildPipelineStates` **之后** (因为 DescriptorSetLayout 必须从 PipelineLayout cache 中获取，而 Vulkan 中 DescriptorSetLayout 由 PipelineLayout 创建时一并生成)
- 这个排序差异是合理的：D3D12 的 Descriptor Heap 独立于 RootSignature，可以先行分配；Vulkan 的 DescriptorSetLayout 内嵌在 PipelineLayout 中

**同步模型差异:**
- D3D12: 每帧通过 `FrameContext->Signal(DirectQ, ComputeQ)` 信号 Fence，`FrameContext->WaitForPreviousFrame()` 等待
- Vulkan: `SubmitBatches()` 异步提交后 CPU 立即返回；GPU 同步推迟到 `FrameContext::Aquire()` 中 `waitForFences`，实现等效的 N 帧重叠流水线

---

## 第一部分: D3D12已实现但Vulkan尚未实现的功能

### 1.1 帧管理子系统 (GPUFrameManager) ✅

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 类名 | `GPUFrameManager` / `FrameContext` / `FrameBoundResourceManager` | `VulkanGPUFrameManager` / `VulkanFrameContext` / `VulkanFrameBoundResourceManager` | ✅ 已对齐 |
| 功能 | 多帧重叠渲染，每帧独立资源分配器 | 环形缓冲区 N 帧上下文（默认 maxFrameCount=2），VulkanGraphExecutor 每帧重建 | ✅ 已对齐 |
| 同步 | 每帧独立 Fence (Direct + Compute)，`FrameContext->Signal()` | VulkanFrameBoundResourceManager 持有 Direct/Compute Fence，SubmitBatches 异步提交，FrameContext::Aquire() 中 waitForFences | ✅ 已对齐 |
| 资源 | 从 FrameContext 获取 DescriptorAllocator / GPUHeap / StagingMem / AliasedMem | VulkanFrameBoundResourceManager 持有 CommandListManager / DescriptorPool / LinearMemoryManager / Fences | ✅ 已对齐 |

**具体实现：**
- `VulkanGPUFrameManager`: 顶层管理器，持有 `maxFrameCount`（默认 2）个 `VulkanFrameContext` 的环形缓冲区。`AquireFrameContext()` 返回 `PFrameContext`（unique_ptr + custom deleter，析构时自动 Reset）
- `VulkanFrameContext`: 每帧独立上下文，包含 `std::binary_semaphore` 进行线程同步，`VulkanFrameBoundResourceManager`，以及 per-window acquire/present semaphore pairs
- `VulkanFrameBoundResourceManager`: 对应 D3D12 `FrameBoundResourceManager`，持有：
  - `VulkanCommandListManager` — 命令列表管理器
  - `vk::DescriptorPool` — 描述符池（动态扩容 `EnsurePoolCapacity`）
  - `VulkanLinearMemoryManager` — 线性上传内存
  - `vk::Fence` (Direct + Compute) — 队列 Fence
- 跨帧缓存（`ShaderModule`/`RenderPass`/`Framebuffer`/`PipelineLayout`/`DescriptorSetLayout`）迁至 `RenderBackend_Vulkan` 持久持有，不随帧上下文回收

**差异细节：**
- D3D12 `FrameBoundResourceManager` 包含 `AliasedMemoryAllocator` 和 `CPUDescriptorAllocatorSet`；Vulkan 的 aliasing 仍在 `VulkanGraphExecutor` 中管理，Descriptor 直接通过 `vkAllocateDescriptorSets` 分配
- Vulkan 额外包含 per-window semaphore pair，修复 acquire/present 同步链

---

### 1.2 GPUConstantBufferManager (Constant Buffer 管理)

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 类名 | `GPUConstantBufferManager` | `VulkanConstantBufferManager` | ✅ 已对齐 |
| 功能 | 收集 CBuffer 请求，统一分配 `BufferHandle`，跨 BindingInstance 复用 | `VulkanConstantBufferManager` 通过 `shared_dic::get_or_create` 统一管理，跨 BindingInstance 复用 | ✅ 已对齐 |
| Handle 管理 | `m_ConstantBufferHandles: shared_dic<ShaderStruct*, BufferHandle>` | `m_CBufferResources: shared_dic<ShaderStruct*, uint64_t>` | ✅ 功能等效 |
| BuildResources | `GPUConstantBufferManager::BuildResources()` 独立方法 | 集成在 `GetOrCreateResourceId` 中，通过 `RegisterTemporaryBuffer` 参与 aliasing | ✅ 功能等效 |
| Aliasing | CBuffer 参与 aliasing 内存复用 | CBuffer 在 `AllocateAliasedResources` 之前注册，参与 aliasing 内存复用 | ✅ 已对齐 |
| IterateResources | `IterateResources(callback)` | `IterateResources(callback)` | ✅ 已对齐 |
| Clear/Release | `Clear()` | `Clear()` + `Release()` | ✅ 已对齐 |

**差异细节：**
- D3D12: CBuffer 通过 `BufferHandle` 统一管理，分两步（GetConstantBufferHandle + BuildResources）
- Vulkan: CBuffer 通过 `uint64_t resourceId` 管理，`GetOrCreateResourceId` 一步完成注册
- 两者均使用 `shared_dic` 保证同一 ShaderStruct 只分配一次资源
- 两者均在 aliasing 前注册 CBuffer 元数据，使 CBuffer 参与 aliasing 内存复用

---

### 1.3 LinearMemoryManager (线性上传内存) ✅

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 类名 | `LinearMemoryManager` | `VulkanLinearMemoryManager` | ✅ 已对齐 |
| 功能 | 从大块 upload heap 中线性分配上传内存 | 从预分配 VMA staging page（默认 64MB）中线性分配，page 耗尽时自动扩容 | ✅ 已对齐 |
| Staging 复用 | 帧内线性分配，帧末整体回收 | 帧内线性分配，`Reset()` 将所有 page offset 归零（内存保留） | ✅ 已对齐 |
| 使用场景 | CBuffer 上传、buffer 数据上传、image 数据上传 | CBuffer staging upload + transfer pass (buffer/image data upload) | ✅ 已对齐 |

**实现细节：**
- 每个 Page 为 VMA 分配的 `vk::Buffer`，创建时带 `VMA_ALLOCATION_CREATE_MAPPED_BIT`，持久 mapped
- `AllocUploadStagingBuffer(size, alignment)` 从当前 page 线性分配（带对齐），必要时分配新 page
- `Reset()` 将所有 page 的 `currentOffset` 归零，page 本身不释放
- 由 `VulkanFrameBoundResourceManager` 持有，帧末通过 `FrameContext::Reset()` 回收

---

### 1.4 SamplerManager

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 类名 | `SamplerManager` (独立管理，全局缓存) | **不存在** | ❌ 未实现 |
| Sampler 创建 | `SamplerManager::GetCPUHandle(desc)` → 全局缓存 + 返回 CPU descriptor | 内联在 `BuildDescriptors()` 中通过 `MakeSamplerCreateInfo()` + `device.createSampler()` 创建 | - |
| 缓存 | 全局 sampler descriptor 到 CPU handle 的映射 | 无缓存，每次 BuildDescriptors 重建 sampler | - |
| 生命周期 | SamplerManager 管理，跨帧复用 | 每帧在 BuildDescriptors 创建，Release() 销毁 | - |

**影响**: Vulkan 每帧重建 sampler 对象（然后在 Release 中 destroy），D3D12 的 sampler 全局缓存只在首次创建。不过 Vulkan 的 sampler 对象很轻量，实际开销不大。

---

### 1.5 描述符管理系统 — 架构差异

2026-05-01 更新: 描述符流程已完整实现。下表重点对比架构差异：

| 维度 | D3D12 | Vulkan |
|------|-------|--------|
| 描述符池 | `GPUDescriptorHeap` (free-list 分配) | `vk::DescriptorPool` (per-frame 创建) |
| 分配器 | `CPUDescriptorAllocatorSet` (CPU端描述符分配器，per-frame) | 无等效物 — 直接 `vkAllocateDescriptorSets` |
| 描述符集 | Descriptor Table (连续 chunk) | `vk::DescriptorSet` (per-set) |
| 写入方式 | `CopyDescriptorsSimple(CPU → GPU Heap)` | `vkUpdateDescriptorSets` (直接写入 DescriptorSet) |
| Sampler 堆 | 独立 `GPUDescriptorHeap` (sampler heap) | Sampler 是独立的 `eSampler` descriptor type（image 使用 `eSampledImage`，分离管理） |
| null descriptor | `ID3D12Device2::CreatePipelineState` 容忍 null | Vulkan 需要有效 Descriptor，当前用 CA_ASSERT_BREAK 防护 |

**架构差异本质**:
- D3D12: 两级分配 — CPU descriptor allocator → GPU descriptor heap → descriptor table chunk
- Vulkan: 单级分配 — DescriptorPool → DescriptorSet → 直接写入

两者功能等效，但 Vulkan 每个 DescriptorSet 占用不同 set index，绑定粒度更细。

---

### 1.6 跨队列同步 (Cross-Queue Synchronization)

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 队列同步 | `BatchCommandExecutionRanges` + `FrameContext->Signal(DirectQ, ComputeQ)` | `SubmitBatches` 异步提交 + `FrameContext::Aquire()` 中 `waitForFences` | ✅ 已对齐 |
| Fence 管理 | 每帧独立 Direct/Compute Fence，精确等待/信号 | `VulkanFrameBoundResourceManager` 持有 Direct/Compute Fence，SubmitBatches 异步 signal，Aquire 时等待 | ✅ 已对齐 |
| Queue Family 转换 | 隐式 (single queue family) | Barrier 中始终 `VK_QUEUE_FAMILY_IGNORED` | ❌ 未实现 |

**差异细节：**
- 异步提交: `SubmitBatches()` 不再调用 `waitForFences(UINT64_MAX)`，CPU 提交所有批次后立即返回。GPU 同步推迟到下一轮该帧上下文被 `Aquire()` 时
- 呈现同步: 最后一个批次使用 window acquire semaphore 作为等待、present semaphore 作为信号，替代 Fence 同步
- Queue Family ownership transfer 仍未实现，barrier 始终使用 `VK_QUEUE_FAMILY_IGNORED`

---

### 1.7 外部资源状态应用 (ApplyExternalResourceStates)

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 实现 | 遍历 `imageRanges` / `bufferRanges`，调用 `ApplyResourceState()` 更新外部资源 | **空函数** | ❌ 未实现 |

---

### 1.8 RunTestCode 测试入口

| 维度 | D3D12 | Vulkan | 状态 |
|------|-------|--------|------|
| 实现 | `RenderBackend_D3D12::RunTestCode()` 已实现 | 使用基类默认空实现 | ❌ 未实现 |

---

## 第二部分: Vulkan与D3D12已对齐的部分 (2026-05-01 更新)

### 2.1 CBuffer 完整流程 ✅

```
D3D12 CBuffer Flow:                          Vulkan CBuffer Flow:
──────────────────                           ──────────────────
Prepare: 注册 CBuffer Handle                 RegisterCBufferUsageStates()
         via IterateResourceUsages               → m_CBufferLifetimes

AllocateAliasedResources:                    BuildResources()
  分配 aliased CBuffer buffer                    → AddBuffer(desc, eConstantBuffer)
                                                 → m_CBufferResourceIdMap[pStruct] = resourceId
BuildDescriptors:
  EnsureResourceView(Handle, eCBV)           BuildDescriptors():
  → CopyDescriptorsSimple(GPU Heap)              → SetUniformBuffer(set, binding, buffer)
                                                 → vkUpdateDescriptorSets()
PrepareBatchResourceBarriers:
  生成 before/after barrier                  PrepareBatchResourceBarriers:
  → batch.cbufferBarriers                        → batch.cbufferBarriers.AddCBuffer(resourceId, pStruct)

Execute:                                    RecordBatchCommands:
  CbufferInitializeBarriers()                  VulkanLinearMemoryManager::AllocUploadStagingBuffer()
    before barrier (NONE → COPY_DEST)          pStruct->UpdateUniformBuffer() → Map/Write
  CBufferCopyInitialize():                     vkCmdCopyBuffer(staging → gpuBuffer)
    LinearMemoryManager::AllocUpload()         vkCmdPipelineBarrier(transfer → uniformRead)
    Map → UpdateUniformBuffer → Unmap
    CopyBufferRegion(staging → target)
  ExecuteCBufferInitializationBarriers():
    after barrier (COPY_DEST → CONSTANT_BUFFER)
```

**实现差异**:
- D3D12 使用 `LinearMemoryManager` 线性分配 staging，Vulkan 同样使用 `VulkanLinearMemoryManager` 从预分配 VMA staging page 线性分配
- D3D12 在 aliasing 时分配 CBuffer buffer，Vulkan 在 `BuildResources()` 中单独分配
- D3D12 的 before/after barrier 是在同一个 batch 中分两阶段执行，Vulkan 只有 after barrier（从 transferWrite → uniformRead）

### 2.2 Descriptor 创建与写入 ✅

```
D3D12 BuildDescriptors:                      Vulkan BuildDescriptors:
────────────────────────                     ───────────────────────
gpuDescriptorHeap.AllocDescriptorChunk()     AllocateDescriptorSets(pool, setLayoutPairs)
samplerDescriptorHeap.AllocDescriptorChunk()     → vkAllocateDescriptorSets

for each ImageBinding:                       for each CBufferBinding:
  EnsureResourceView(image, SRV/UAV)           GetBuffer(resourceId) → vk::Buffer
  CopyDescriptorsSimple(CPU → GPU Heap)        SetUniformBuffer(set, binding, buffer)
                                               → m_PendingWrites.push_back()
for each BufferBinding:
  EnsureResourceView(buffer, SRV/UAV)        for each ImageBinding:
  CopyDescriptorsSimple(CPU → GPU Heap)        GetTextureView(handle) → vk::ImageView
                                               SetSampledImage / SetStorageImage
for each CBufferBinding:                        → m_PendingWrites.push_back()
  EnsureResourceView(handle, CBV)
  CopyDescriptorsSimple(CPU → GPU Heap)      for each BufferBinding:
                                               GetBuffer(handle) → vk::Buffer
for each SamplerBinding:                        SetStorageBuffer()
  GetCPUHandle(desc)                             → m_PendingWrites.push_back()
  CopyDescriptorsSimple(CPU → Sampler Heap)
                                             for each SamplerBinding:
                                               MakeSamplerCreateInfo(desc)
                                               device.createSampler()
                                               SetSampler()
                                                 → m_PendingWrites.push_back()

                                             UpdateDescriptorSets()
                                               → vkUpdateDescriptorSets
```

### 2.3 Shader Module + Pipeline 创建 ✅

| 维度 | D3D12 | Vulkan |
|------|-------|--------|
| Shader 加载 | 类似机制 | `GetOrCreateShaderModule(programHash)` → SPIR-V → `vkCreateShaderModule` |
| Pipeline Layout | RootSignature (独立创建) | `GetOrCreatePipelineLayout(bindingInfo)` → 收集 DLL → `vkCreatePipelineLayout` |
| DLL 缓存 | RootSignatureManager | `m_DescriptorSetLayoutCache` + `m_PipelineLayoutCache` |
| Graphics Pipeline | `GPUPipelineManager::GetPipelineState(key)` | `PipelineLibrary.LinkPipeline(parts)` 或 `CreateMonolithicPipeline` |
| Compute Pipeline | `GPUComputePipelineManager::GetPipelineState(shader)` | `device.createComputePipeline(nullptr, info)` |

### 2.4 RenderPass / Framebuffer 缓存 ✅

| 维度 | D3D12 | Vulkan |
|------|-------|--------|
| RenderPass | N/A (D3D12 无此概念) | `GetOrCreateRenderPass(key)` → `vkCreateRenderPass` (cached) |
| Framebuffer | N/A | `GetOrCreateFramebuffer(renderPass, views)` → `vkCreateFramebuffer` (cached) |

---

## 第三部分: Vulkan 独有优势

| 特性 | 说明 |
|------|------|
| Pipeline Library | `VK_EXT_graphics_pipeline_library` 支持管线部件独立编译，可加速热重载和增量编译 |
| Vulkan HPP | 使用 C++ 封装的 `vk::*` 类型，更现代的 API 风格 |
| VulkanResourceAliasing | 基于生命周期的别名算法更通用，不依赖 Virtual Block |
| Explicit Descriptor Sets | 每个 set 独立的 DescriptorSetLayout + PipelineLayout，比 D3D12 RootSignature 的 flat binding 更具表现力 |

---

## 第四部分: 已完成 vs 未完成 TODO 清单

### 已完成的 TODO (vulkan-renderpass-format-conversion + vulkan-gpu-frame-manager 变更, 2026-05-04)

| 原位置 | 内容 | 新位置/实现 |
|--------|------|-----------|
| `VulkanGraphExecutor.cpp` | RenderPass 格式硬编码 (`eD32Sfloat`/`eR8G8B8A8Unorm`) | 从 `GPUTextureDescriptor` 通过 `VulkanTexture::ConvertFormat()` 动态转换，关闭 Phase 1 项目 7 |
| `VulkanGraphExecutor.cpp` | `SubmitBatches` 同步等待 (`waitForFences(UINT64_MAX)`) | 异步提交: CPU 提交后立即返回，Fence 等待移至 `VulkanFrameContext::Aquire()` |
| Staging Buffer | 每次 transfer/CBuffer 上传独立 VMA 分配/释放 | 新增 `VulkanLinearMemoryManager`: 预分配 64MB VMA staging page，线性分配 + 帧末 offset 归零 |
| `RenderBackend_Vulkan` | 无帧管理子系统，无法多帧重叠 | 新增 `VulkanGPUFrameManager` + `VulkanFrameContext` + `VulkanFrameBoundResourceManager` 三层架构 |
| `VulkanGraphExecutor.h` | 跨帧缓存 (ShaderModule/RenderPass/Framebuffer/PipelineLayout/DLL) 内嵌在 Executor | 迁移至 `RenderBackend_Vulkan` 持久持有，不随帧上下文回收 |
| 交换链同步 | 无 per-window semaphore | 新增 `WindowSync` (acquire + present semaphore pair)，最后一个 batch 使用 semaphore 同步 |

### 已完成 TODO (vulkan-descriptor-cbuffer-upload 变更, 2026-05-01)

| 原位置 | 内容 | 新位置/实现 |
|--------|------|-----------|
| `VulkanResourceBindingInstance.cpp` | `BuildResources()` 空函数 | 完整实现: CBuffer GPU buffer 分配 + Image/Buffer fallback 注册 + usingStages 设置 |
| `VulkanResourceBindingInstance.cpp` | `BuildDescriptors()` 不完整 | 完整实现: DescriptorSet 分配 + 所有 binding 类型写入 + UpdateDescriptorSets |
| `VulkanGraphExecutor.cpp` | CBuffer 初始化 barriers | 实现: staging buffer → vkCmdCopyBuffer → pipeline barrier |
| `VulkanGraphExecutor.cpp` | DescriptorPool 创建 | 实现: 统计各 descriptor 数量 → vkCreateDescriptorPool → flags=eFreeDescriptorSet |
| `VulkanGraphExecutor.cpp` | DescriptorSet 绑定 | 实现: 连续 set index 区间分组 → vkCmdBindDescriptorSets |
| `VulkanGraphExecutor.cpp` | `GetOrCreateShaderModule()` 返回 nullptr | 实现: 从 ShaderLibrary 加载 SPIR-V → vkCreateShaderModule |
| `VulkanGraphExecutor.cpp` | Pipeline Layout 获取 | 实现: GetOrCreatePipelineLayout + GetOrCreateDescriptorSetLayout (cached) |
| `VulkanGraphExecutor.cpp` | 完整管线创建 | 实现: PipelineLibrary GPL 路径 + 单体 fallback |
| `VulkanGraphExecutor.cpp` | Compute Pass 资源注册 (CollectResources 中) | 实现: `RegisterComputeResources()` 遍历 dispatches → image/buffer bindings → SetImageRWState/SetBufferRWState + access→vk映射helper |

### 剩余 TODO / 未完成项

| 位置 | TODO 内容 | 优先级 |
|------|----------|--------|
| `VulkanGraphExecutor.cpp` | `ApplyExternalResourceStates()` 空函数 | 中 |
| Barrier 中 Queue Family | 始终 `VK_QUEUE_FAMILY_IGNORED`，跨队列 ownership transfer 缺失 | 中 |
| SamplerManager | 无独立 sampler 管理器，每帧在 BuildDescriptors 中重建 sampler | 低 |
| RunTestCode | 测试入口未实现 | 低 |

---

## 第五部分: 建议的对齐路径 (2026-05-04 更新)

### Phase 1: 核心功能补全 (使后端可用) — 全部完成 ✅
1. ~~Shader Module 创建~~ ✅ (2026-05-01)
2. ~~Pipeline Layout 正确获取~~ ✅ (2026-05-01)
3. ~~完整管线创建流程~~ ✅ (2026-05-01)
4. ~~Descriptor 完整流程~~ ✅ (2026-05-01)
5. ~~CBuffer 初始化~~ ✅ (2026-05-01)
6. ~~Compute Pass 资源注册~~ ✅ (2026-05-01)
7. ~~RenderPass/Framebuffer 格式转换 — 消除硬编码~~ ✅ (2026-05-03)

### Phase 2: 性能与同步 — 大部分完成 ✅
1. **跨队列 Queue Family ownership transfer** — 正确处理 Queue Family ownership transfer（barrier 中 `VK_QUEUE_FAMILY_IGNORED` → 实际 queue family index）
2. ~~异步 CommandBuffer 提交~~ ✅ (2026-05-03)
3. ~~帧管理子系统~~ ✅ (2026-05-03)
4. ~~LinearMemoryManager~~ ✅ (2026-05-03)

### Phase 3: 架构完善
1. **SamplerManager** — 独立管理 sampler 创建和缓存（优先级低）
2. **ApplyExternalResourceStates** — 更新外部资源状态
3. **RunTestCode** — 实现测试入口
