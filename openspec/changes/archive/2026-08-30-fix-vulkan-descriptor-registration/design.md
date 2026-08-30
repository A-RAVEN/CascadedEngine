## Context

vulkan 图内（`AllocBuffer`/`AllocImage`）资源被 shader 绑定时命中 `VUID-*-08114`（描述符从未 update）与 `00331`（storage usage）。经 D3D12 差分 + 对抗验证，根因是资源**生命周期链**的三个缺口，与 D3D12 构成直接对照：

- **D3D12**：所有 pass（RAST L478 / compute L504）的 shader SRV/UAV 绑定经 `addPassShaderInstancesResourcesRWStates` 喂进 RWState → 生命周期表（`lifeTime.head()/end()`）；`AllocateAliasedResources(imageLifeTimes, bufferLifeTimes, ...)` 按生命周期**出生批次预留、结束批次释放**（`AllocateGPUResource` 只做虚拟预留→`CommitAllocations` 才 `CreatePlacedResource` 建真实资源）；`CommitAliasedResources` 提交后**遍历所有** Internal 资源挂 `pResource`。因此 D3D12 里每个 shader 绑定图内资源都有正确生命周期、全部被绑定、被写入描述符。
- **Vulkan 缺口 A**：仅 `RegisterComputeResources`（compute 专用）喂 shader 绑定进 RWState；**RAST shader SRV/UAV 绑定只进 `BuildShaderUsageMaps`（usage 并集），从不调 `SetBufferRWState`/`SetImageRWState`** → RAST shader-only 资源不进 `m_*Lifetimes`。
- **Vulkan 缺口 B**：顶层 `Foreach` 注册资源用 `RegisterTemporaryBuffer(desc, usage, 0)`，**batchIndex 硬编码 0**。
- **Vulkan 缺口 C**：VirtualBlock 路径逐批次 `vmaVirtualAllocate/free`，`FreeResourcesUpToBatch` 删除 `endBatch < 当前 batch` 的 `m_ActiveVirtualAllocs`；`BindResourcesToPhysicalMemory` 在**所有批次后一次绑定**，只绑到"最后批次仍活跃"资源 → 早期批次资源（含受 A/B 影响的 shader-only 资源）被跳过绑定，永不创建 VkBuffer。fallback 单池（遍历所有 + 持久 `GetAliasedAllocation(id)`）与 D3D12（`CommitAliasedResources` 遍历所有）都无此问题。

**实证**：诊断日志显示 shader-only 资源被顶层 insert-only 登记、但 `BindResourcesToPhysicalMemory` 报 `NOT in activeAllocs`（batch 0..0），故 `GetBuffer(handle)` 返回 null → `BuildDescriptors` 命中 `if(!vkBuffer) continue;` → 08114。

## Goals / Non-Goals

**Goals:**
- 让**所有** shader 绑定图内资源（RAST + compute，SRV/UAV）进入生命周期表、生命周期正确（对齐 D3D12），并被物理绑定、描述符写入 → 消除 `08114`。
- 被 shader 绑定的图内资源携带正确 usage：buffer `STORAGE_BUFFER`、采样 image `SAMPLED` → 消除 `00331`/image-`08114`。
- 回归不破坏 ImageBuffer / SimpleTriangle / readback 字节一致、validation 干净。

**Non-Goals:**
- 描述符池"每元素 1 个" vs `elementCount` 计数分叉——当前测试单元素，latent，另案（以任务显式验证）。
- **IMGUI `04007/07312`（vertex/index no-buffer-bound）——另案。IMGUI 的 08114 是否由本 change 消除，需任务 1.4 先行隔离核实（其只 shader 绑定采样资源是外部 `m_Fontimage`，未证实也非本 change 覆盖）。本 change 不声称 IMGUI 达到"真实渲染内容"，只承诺其描述符侧 08114（若有）经 D-A/B/C+D1/D3 可能消除，须实证。**
- ConstantColor / D32 depth usage `02251` —— `docs/TODO.md` C 条另案。

## Decisions

**D-A（生命周期来源：补 RAST shader 绑定→RWState，对齐 D3D12 L478）。**
在 `CollectResources`/Prepare 中，为每个 raster pass 遍历其 binding instance，对 `GetImageBindings`/`GetBufferBindings` 的每个 handle 调 `SetImageRWState`/`SetBufferRWState`（`usingStages`、`resourceUsages`、`ComputeAccessToVulkanAccess`/`ComputeAccessToImageLayout` 映射 access/layout），使 shader SRV/UAV 资源进入对应 `passRWState`，经 `BuildResourceUsageRanges` 进入 `m_*Lifetimes`。**与 compute 对称：不分 Internal/External/Backbuffer，喂所有 handle**（`RegisterComputeResources` L969-1006 与 D3D12 类比 `addPassShaderInstancesResourcesRWStates` 均无过滤；external/backbuffer 进 `m_*Lifetimes` 只作屏障/生命周期，不参与别名分配，安全；`PrepareBatchResourceBarriers` L1140-1156 对 External/Backbuffer 有显式 case）。`RegisterComputeResources` 保留，变成"compute + raster 都喂"。
- 作用与边界：D-A 只喂 RWState（屏障/生命周期），**不负责注册图内资源**（注册是 D1 顶层 Foreach）。08114 消除依赖 D-B + D-C。

**D-B（生命周期来自实际使用，不再硬编码 0）。**
图内资源注册后，用生命周期表把头/尾批次扩展进 `firstUseBatch/lastUseBatch`。实现：给 `VulkanResourceUsageRangeData` 增补 head/end（`states.front().batchID`/`states.back().batchID`）；在注册后、`AllocateAliasedResources`（Phase 4.5）前，用 `m_LocalResourceManager.MarkResourceUse(resourceId, batch)`（Min/Max 扩展）对每个图内资源按其 head/end 扩展。
- **跨对象映射（关键）**：`MarkResourceUse(resourceId, ...)` 需要 resourceId，但 executor 的 `m_*Lifetimes` 按 `ImageHandle/BufferHandle` key。需从 `m_LocalResourceManager` 的 `GetBufferHandleToResource` 等（`m_*HandleToResource`）反查 handle→resourceId，或让注册点保存 handle→resourceId（D1 顶层 Foreach 已捕获 `resourceId`，应留存）。**不得假设依赖 D-A 之外的映射**。
- **依赖序**：D-A 是 D-B 的前提（RAST shader 资源先进生命周期表才有 head/end）。**D-B 不可在 D-A 前独立验证。**

**D-C（VirtualBlock 绑所有资源，对齐 D3D12 `CommitAliasedResources`，且防越界）。**
`BindResourcesToPhysicalMemory` 不再用"事后 `m_ActiveVirtualAllocs`"（只剩最后一批），改为遍历**所有**注册资源、按持久 offset 创建 VkBuffer/Image 绑到 `blockPool->deviceMemory + blockOffset + offset`。**offset 必须持久化自 `AllocResource` 的 `vmaVirtualAllocate` 输出 `{offset, size, blockIndex}`**（不可复用单池 greedy 的 `m_AliasedAllocations`，二者 offset 不匹配），存入一个**不被 `FreeResourcesUpToBatch` 删除、且随帧/`ResetVirtualBlocks` 清除**的表。
- **峰值提交（关键时刻）**：`CommitVirtualAllocations` 现用 `vmaGetVirtualBlockStatistics.allocationBytes`（= 当前 **live** 字节，非历史峰值）作为物理块大小，在 `FreeResourcesUpToBatch` 已释放早期批次后只剩最后一批 → 物理块不足、早期批次资源绑到 offset 0/1000/2000 会 **OOB**。**改为按 `blockIndex` 分 pool：`peakSize[pool] = max(offset + size) over 持久记录中 blockIndex == 该 pool`**（对齐 D3D12 `m_MaxSize`，MemoryManager.cpp L330/L361；buffer pool=0 / image pool=1，`CommitVirtualAllocations` L429-489 本就 per-pool，故须按 pool 取峰值，避免把两端都扩到跨 pool 高水位、过度分配较小的 `DEVICE_LOCAL` image pool 而上限超支）。这是 D-C 的前提。
- **跨别名内存依赖**：D-C 后早期/晚期资源可能绑到同一 offset（时间不重叠别名）。别名共享合法，但**异别名的重叠范围内若至少一方写，二者使用间必须有内存依赖**（barrier scope 覆盖整个重叠范围）；对 optimal-tiling image 的第二个别名须从 `VK_IMAGE_LAYOUT_UNDEFINED` 过渡。D-A/D-B 提供的屏障须覆盖此依赖（现有 barrier 追踪有 access/stage，需核对 layout 过渡）。
- 与 D3D12 对齐：D3D12 `AllocateGPUResource` 在出生批次**虚拟预留** offset，`CommitAllocations` 对**每条记录** `CreatePlacedResource`——即"出生预留 + commit 绑定每条"。D-C 的"持久 offset + 绑所有"精确镜像此语义（实现因 VMA VirtualBlock 模型分两步，语义一致）。

**D1（保留，描述符侧，且防重复）。**
顶层 `Foreach` 捕获 `RegisterTemporaryBuffer/Texture` 返回的 `resourceId`，仅当 handle 尚未映射时 `Register*Handle`（**insert-only**，绝不覆盖 per-pass 正确 usage 登记）。**实现时须"跳过已被 per-pass 登记的 handle 的资源注册"**（对 `IsXxxHandleRegistered` 的 handle 不再调用 `RegisterTemporary*`，不重复创建 orphan 资源），否则 D-C "bind all" 下会把重复资源绑成额外 VkObject、碎片化 VirtualBlock plan——注意当前顶层 `Foreach` 无条件 `RegisterTemporary*`（只 handle 映射是 insert-only），此 skip 是实现改动点。

**D3（保留，描述符侧）。**
被 shader 绑定的图内资源携带正确 usage：buffer `STORAGE_BUFFER_BIT`（`BuildShaderUsageMaps` OR 进 `eUnorderedAccess`/`eStructuredBuffer`）、采样 image `SAMPLED_BIT`（or eSampled），并 OR 进每个注册点。修 `00331` 与 image 采样 `08114`。

**D2（新增处理 BuildResources 兜底，防 08114 复活）。**
`VulkanResourceBindingInstance::BuildResources` 的 3.3/3.4 兜底（`GetTextureView`/`GetBuffer` 返回 null 时无条件 `RegisterTemporary*` + `Register*Handle`）会**覆盖 D1 映射**、且新资源 batch-0 在 `AllocateAliasedResources`（Phase 4.5）之后创建 → 永不绑定 → 08114 复活。**改为**：兜底不再无条件覆盖——仅当 handle 尚未映射时登记（对齐 D1 insert-only），且绝不为"Phase 5 才创建"的资源新建（此类资源应已在 Phase 4.5 前由 D1/D-A/D-B/D-C 预绑定）。若确需兜底（如外部资源 Phase 5 解析失败），应记录并显式跳过而不是 remap 到死资源。

## Risks / Trade-offs

- [D-C 峰值 OOB] → 以**按 `blockIndex` 分 pool** 的 `max(offset+size)` 提交物理块（buffer=0/image=1，对齐 D3D12 m_MaxSize），用 `_validation.log` + 截图验证无 OOB/无 08114，且不得扩到跨 pool 高水位过度分配较小的 `DEVICE_LOCAL` image pool 上限超支。
- [D-C 跨别名依赖] → D-A/D-B 提供的屏障须覆盖重叠范围内存依赖 + image UNDEFINED 过渡；以 validation 干净做门禁。
- [D-A double-feed] → `SetImageRWState/SetBufferRWState` 是 insert-or-Combine（OR access+stages，无重复屏障）；但 `Combine` **不合并 imageLayout**，资源在同一 pass 既是 attachment 又 sampled（feedback）可能 layout VUID——4 测试均无此（pass0RT 跨 pass、vbuffer 跨 compute/rast），记录即可。
- [D-B 窗口过短] → 若 D-A/D-B 给过短 head/end，重叠生命周期资源会撞同一 offset → 读错数据（非 08114）。以生命周期表 head/end 为准，验证无数据错。
- [D1 覆盖既有回归] → insert-only + 跳过已注册是硬约束；以 ImageBuffer/SimpleTriangle/readback 字节一致做回归门禁。

## Migration Plan

无接口变更，直接修复落地；经 `python build.py --config Debug` + vulkan/d3d12 全量截图对比验证。落地顺序 D-A → D-B → D-C → D2，但**不以"可独立验证"为假设**（D-A 是 D-B 前提、D-B/C 是 08114 消除关键）。每个决策后跑对应测试自检。

## Open Questions

- D-B 的 handle→resourceId 来源（钉死）：由 **D1 顶层 Foreach 注册时留存 handle→resourceId 映射**（注册点已捕获 `resourceId`），或经 `m_*HandleToResource` 反查（实现时须新增 `GetBufferHandleToResource`/`GetTextureHandleToResource` 访问器）。二选一，不得悬空；且 `MarkResourceUse` 须在 `BuildResourceUsageRanges()` 之后、`AllocateAliasedResources()` 之前调用（`m_*Lifetimes.states` 此时已填充）。
- D-C 的 peak 提交：`CommitVirtualAllocations` 改为 `max(offset+size)` 的落点（VulkanResourceAliasing 还是 GraphLocalResourceManager）；需确保不破坏单池 fallback。实现时按"最小改动且与 fallback 不冲突"选取。
- D-A 的 RAST shader 绑定 access/layout 映射：复用 `ComputeAccessToVulkanAccess`/`ComputeAccessToImageLayout`（语义通用，命名带 Compute）还是新增 RAST 专用映射。实现时核对。
