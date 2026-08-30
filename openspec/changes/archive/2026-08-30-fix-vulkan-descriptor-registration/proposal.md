## Why

vulkan 后端多个测试在渲染带 shader 绑定的图内（graph 分配）资源时命中 `VUID-vkCmdDraw/DrawIndexed/Dispatch-None-08114`（"bound descriptor set 从未 update"）与 `VUID-VkWriteDescriptorSet-descriptorType-00331`（storage 描述符的 buffer 缺 `STORAGE_BUFFER_BIT`），导致 StructuredBufferColor / DoublePass / ComputeBuffer / IMGUI 渲染黑/残缺，而 d3d12 全部正确。

经对抗式逐行审计（D3D12 vs Vulkan 差分），根因不是单一"描述符跳过写"，而是一条**资源生命周期链**上三个独立缺口，它与 D3D12 的正确做法构成直接对照：

**D3D12 的正确模型**（`GPUGraphExecutor.cpp` `addPassShaderInstancesResourcesRWStates`，L354-391，RAST L478 / compute L504 均调用）：每个 pass 的所有 shader 绑定（SRV/UAV，经 `IterateResourceUsages` 遍历 image/buffer/cbuffer）都调 `SetImageRWState`/`SetBufferRWState` 喂进 RWState → 生命周期表（`lifeTime.head()/end()`）。`AllocateAliasedResources` 接收 `imageLifeTimes`/`bufferLifeTimes`，**按生命周期出生批次创建真实资源、结束批次释放**；`CommitAliasedResources` 提交后**遍历所有** Internal 资源挂 `pResource`。所以 D3D12 里每个 shader 绑定的图内资源都有正确生命周期、且真实存在、被绑定。

**Vulkan 的三个缺口（均与上面对照）：**

- **缺口 A（shader 绑定不进生命周期表）**：Vulkan 只有 `RegisterComputeResources`（Compute 专用）喂 shader 绑定进 RWState；**RAST pass 的 shader SRV/UAV 绑定只被 `BuildShaderUsageMaps` 收集 usage，从不调 `SetBufferRWState`/`SetImageRWState`** → 一个仅被 RAST shader 采样的图内资源（如 StructuredBufferColor 的 `colorStructuredBuffer`、DoublePass 采样纹理）根本**不进 `m_BufferLifetimes`/`m_ImageLifetimes`**，生命周期缺失。
- **缺口 B（生命周期硬编码 0）**：顶层 `GetBufferManager()/GetImageManager().Foreach` 注册资源用 `RegisterTemporaryBuffer(desc, usage, 0)`，**batchIndex 硬编码 0**，从不从实际使用推导 → 别名规划把该资源当"只在 batch 0 存活"，而它实际在更晚批次被 draw。
- **缺口 C（VirtualBlock 绑定丢早期批次）**：`AllocateAliasedResources` 的 VirtualBlock 路径逐批次 `vmaVirtualAllocate/free`，`FreeResourcesUpToBatch` 会把 `endBatch < 当前 batch` 的资源从 `m_ActiveVirtualAllocs` 删除；`BindResourcesToPhysicalMemory` 在**所有批次跑完后一次绑定**，只绑到"最后一个批次仍活跃"的资源 → 早期批次（含缺 A/B 导致的一系列）的资源被跳过绑定，**永不创建 VkBuffer**。这与 fallback 单池路径（遍历所有 `m_LocalResources` + 持久 `GetAliasedAllocation(id)`）和 D3D12（`CommitAliasedResources` 遍历所有）都不一致。

**结果**：一个 shader-only 图内资源（如 `colorStructuredBuffer`）在缺口 B 下被记为 `batch 0..0`、在缺口 A 下没被喂进生命周期表、在缺口 C 下被 `FreeResourcesUpToBatch` 提前释放，最终 `GetBuffer(handle)` 返回 null → `VulkanResourceBindingInstance::BuildDescriptors` 命中 `if(!vkBuffer) continue;` 静默跳过写 → set 空槽 → 08114 黑帧。缺 A/B/C 让资源从未绑定，这是 D1/D3 无法治愈的根因。

## What Changes

针对上述对照，按 D3D12 的模型修复（而非绕路）：

- **D-A（对齐 D3D12 生命周期来源）**：为 RAST pass 补"shader 绑定→RWState"。在 `CollectResources`/Prepare 中遍历每个 raster pass 的 binding instance，对其 `GetImageBindings`/`GetBufferBindings` 的**每个 handle**（不分 Internal/External/Backbuffer，与 `RegisterComputeResources`/D3D12 类比对称；external/backbuffer 只进生命周期供屏障、不参与别名分配）调 `SetImageRWState`/`SetBufferRWState`（`usingStages`、`resourceUsages`、`ComputeAccessTo*` 映射 access/layout），使**所有** shader 绑定资源（RAST + compute）进入 `m_ImageLifetimes`/`m_BufferLifetimes`。复刻 `addPassShaderInstancesResourcesRWStates` 的 raster 分支。
- **D-B（生命周期来自实际使用）**：图内资源注册后，用生命周期表（`VulkanResourceUsageRangeData` 的 `states` 首/末 `batchID`，或新增 head/end 辅助）经 `MarkResourceUse(resourceId, batch)` 把 `firstUseBatch/lastUseBatch` 扩展到真实使用区间，不再硬编码 0。使别名规划在资源真实存活区间内不提前释放/复用。
- **D-C（VirtualBlock 绑定所有资源，对齐 D3D12 `CommitAliasedResources`，且防越界）**：`BindResourcesToPhysicalMemory` 不再用"事后 `m_ActiveVirtualAllocs`"（只剩最后批次），改为**持久记录每个资源在 `AllocResource`（`vmaVirtualAllocate`）分配时的 `{offset,size,blockIndex}`**（不被 `FreeResourcesUpToBatch` 删除、随帧清除），遍历所有注册资源、按持久 offset 创建 VkBuffer/Image 并绑到 `blockPool->deviceMemory + blockOffset + offset`。与 fallback 单池路径和 D3D12 `CommitAliasedResources` 遍历所有保持一致。**峰值提交改为按 `blockIndex` 分 pool 的 `max(offset+size)`**（buffer pool=0 / image pool=1，对齐 D3D12 `m_MaxSize`），避免 `vmaGetVirtualBlockStatistics.allocationBytes`（= 当前 live 字节，非历史峰值）低估物理块导致早期批次资源 OOB，且不得扩到跨 pool 高水位过度分配较小的 `DEVICE_LOCAL` image pool 上限超支。
- **D2（BuildResources 兜底安全通道，防 08114 复活）**：`VulkanResourceBindingInstance::BuildResources` 3.3/3.4 兜底在 `GetTextureView/GetBuffer` 返回 null 时**无条件 `Register*Handle` 覆盖 D1 映射**、且新资源 batch-0 在 `AllocateAliasedResources`（Phase 4.5）之后创建 → 永不绑定 → 08114 复活。**改为仅当 handle 尚未映射才登记（insert-only），且绝不在 Phase 5 新建死资源**——此类资源应已在 Phase 4.5 前由 D-A/D-B/D-C/D1 预绑定。
- **D1（保留，描述符侧，且防重复）**：顶层 `Foreach` 捕获 `resourceId`，**insert-only** 登记 `Register*Handle`（不覆盖 per-pass 正确 usage 登记），使 handle 解析到已绑定资源；**同时跳过已被 per-pass 登记的 handle 的资源注册**（不重复创建 orphan 资源，防 D-C "bind all" 下绑成额外 VkObject / 碎片化 plan）。
- **D3（保留，描述符侧）**：被 shader 绑定的图内资源携带正确 usage——buffer `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`、采样 image `VK_IMAGE_USAGE_SAMPLED_BIT`（经 `BuildShaderUsageMaps` OR 进每个注册点）。
- 无接口变更：纯后端实现，`CRenderBackend` 等不动。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `resource-handle-registration`：补充"图内资源生命周期由**实际 shader 绑定使用**推导（而非硬编码 batch 0）"与"shader 绑定资源必须进入生命周期表并被绑定"要求。
- `vulkan-descriptor-binding`：补充"shader 绑定资源必须解析到**已绑定**句柄并写入（不得 skip）"与"shader 绑定资源须含正确 usage（buffer storage / image sampled）"。

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` —— D-A 补 RAST shader 绑定→RWState；D-B 生命周期扩展；D1/D3 顶层 insert-only 登记（+防重复）+ usage 合并。
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.cpp` —— D2 BuildResources 兜底安全通道（insert-only、不新建 Phase 5 死资源）。
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp` / `ResourceManagement/VulkanResourceAliasing.cpp(.h)` —— D-C 持久 `vmaVirtualAllocate` offset 记录 + 峰值 `max(offset+size)` + 绑所有。
- 测试：StructuredBufferColor / DoublePass / ComputeBuffer 恢复非黑且无 `08114`/`00331`；**IMGUI 单独隔离判定**（其 08114 若属 D-A/B/C 覆盖则消除，若属外部/另案则如实记录，不作"全部修复" overclaim）；ImageBuffer / SimpleTriangle / readback 保持字节一致（回归门禁）。

## Non-goals

- **latent 描述符池计数分叉**：池按"每 binding 元素 1 个"计、set 分配按 `elementCount`；当前测试均单元素，不触发，另案。
- **IMGUI 的 `04007/07312`**（vertex/index no-buffer-bound）：非描述符/生命周期问题，另案。IMGUI 的 08114 是否由本 change 消除，须经任务 1.4 隔离核实（其只 shader 绑定采样资源是外部 `m_Fontimage`，未证实在本 change 覆盖内）。
- **ConstantColor / D32_SFLOAT depth usage（02251）**：`docs/TODO.md` C 条另案。
- **D3D12 未覆盖的别名复用边界**：本 change 只对齐 D3D12 已有语义（资源按生命周期出生/释放 + 绑所有），不引入 D3D12 也缺乏的更细别名复用。

## Acceptance

- `python build.py --config Debug` 通过。
- vulkan `--test`+`--capture`：按 `test_output/*_validation.log`（**非 stdout**）核查——StructuredBufferColor / DoublePass / ComputeBuffer **不再出现 `VUID-*-08114`**；ComputeBuffer / StructuredBufferColor **不再出现 `00331`**；截图恢复非黑/非空白（真实渲染内容）。**IMGUI 如实记录**：隔离判定其 08114 是否在本 change 覆盖内；若否，如实标注（不纳入"修复完成" claim）。
- 回归门禁：ImageBuffer / SimpleTriangle / readback **字节一致**（对照 d3d12，按修复后产物核查）、validation 干净。
