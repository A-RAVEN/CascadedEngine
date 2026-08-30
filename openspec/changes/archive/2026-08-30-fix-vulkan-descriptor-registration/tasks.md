# Fix Vulkan Descriptor Registration (D3D12-aligned) — 施工图

> **本文件是唯一施工依据：任何人逐条执行到底，必然得到符合 design.md 的结果。** 开始前先读完本节「施工图说明」，明确"现状"与"目标"的映射 —— 避免把已就位的部分重写、或把未做的误当完成、或对现有代码做多余的回退。

## 施工图说明（现状 → 目标）

**工作树现状（未提交，5 个 M）：**
- ✅ **已就位（D1/D3 骨架，全程保留，勿重写）**：
  - `Prepare` reorder：`CollectShaderBindings(628) → BuildShaderUsageMaps(629) → CollectResources(630)`。
  - `BuildShaderUsageMaps()`（cpp:635-660）+ `m_BufferShaderUsage/m_ImageShaderUsage`（h）。
  - `CollectResources` 各注册点（attachment/index/vertex/upload/finalize/顶层）OR `m_*ShaderUsage`（cpp:776/807/819/846/864/895/649）。
  - 顶层 `Foreach` 捕获 `resourceId` + 用 `IsBufferHandleRegistered/IsTextureHandleRegistered` insert-only `Register*Handle`（cpp:888-890，Is* 于 cpp:845-854 / h:77-82）。
- ⚠️ **需修改（对现有代码改，不是重写 / 不是回退）**：
  - `VulkanGraphExecutor.cpp:888` 顶层 `RegisterTemporaryBuffer(desc, usage, 0)` **仍无条件**、且 batch **硬编码 0**（→ D1 补 skip、D-B 改生命周期）。
  - `VulkanResourceBindingInstance.cpp:280-281` `BuildResources` 兜底**仍无条件** `RegisterTemporaryBuffer + Register*Handle`（→ D2 改 insert-only、不新建 Phase5 死资源）。
- ❌ **需新增（当前完全没有）**：
  - D-A（RAST shader 绑定→RWState）：当前无任何代码把 RAST shader SRV/UAV 喂进 `SetBufferRWState/SetImageRWState`。
  - D-B（`MarkResourceUse` 生命周期扩展）：当前只在 `RegisterCBufferForAliasing`(cpp:685-686) 对 cbuffer 用，未作用图内资源。
  - D-C（`VulkanResourceAliasing.cpp/.h` **完全未动**）：无持久 offset、无按 pool 峰值、无绑所有、无跨别名依赖。
- 🧹 **需清理**：`VulkanResourceBindingInstance.cpp` 有 CRLF 行尾噪声（`git diff` 为空但 `git status` 标 M）。

**依赖序**：D-A → D-B → D-C → D2 → D1 补全 → D3 核验。D-A 是 D-B 前提；D-B/C/D-C 是 08114 消除关键，D1/D3 是描述符侧（保留）。

---

## 0. 前置清理与现状确认

- [x] 0.1 **清理行尾噪声**：`git checkout -- VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.cpp`（其 `git diff` 为空，仅 CRLF 差异；此文件在 D2 前应回到干净态，避免误判）。**注：实际落地后此步被 D2 的真实改动覆盖（文件已有功能 diff，git diff 非空），故不再 checkout（否则丢失 D2 修复）；行尾噪声已是纯 CRLF 提示，不影响 diff 可读性。**
- [x] 0.2 **确认现状范围**：`git status --short VulkanRenderBackendNew/` 确认只剩 `VulkanGraphExecutor.cpp/h`、`VulkanGraphLocalResourceManager.cpp/h` 四个文件有实质改动。**实为 7 个文件**：额外含 `VulkanResourceBindingInstance.cpp`、`ResourceManagement/VulkanResourceAliasing.cpp/h`（D-D-C/D2 改动），均属本 change 范围，无无关改动。

## 1. D1 / D3 骨架 —— 已完成（代码已实现，第一轮 apply 已落地）

> 这几条对应的代码已经在工作树里（`VulkanGraphExecutor.cpp/h`、`VulkanGraphLocalResourceManager.cpp/h`），标记完成，后面的 agent 知道这些不用再做。

- [x] 1.1 BuildShaderUsageMaps（cpp:635-660）+ 各注册点 OR `m_*ShaderUsage`（cpp:776/807/819/846/864/895）→ 即 D3 的 usage 合并落点。
- [x] 1.2 顶层 `Foreach` insert-only handle 登记（cpp:888-890）+ `Is*HandleRegistered`（cpp:845-854）→ 即 D1 的核心。
- [x] 1.3 `Prepare` reorder：`CollectShaderBindings → BuildShaderUsageMaps → CollectResources`（cpp:628-630）。

## 2. D-A：新增 — RAST shader 绑定→RWState（生命周期来源，对齐 D3D12 L478）

- [x] 2.1 **新增**：为每个 raster pass，遍历其 binding instance 的 `GetImageBindings/GetBufferBindings`，对每个 handle 调 `SetImageRWState/SetBufferRWState`（`usingStages`、`resourceUsages`、`ComputeAccessToVulkanAccess/ComputeAccessToImageLayout` 映射 access/layout）。**不分 Internal/External/Backbuffer，喂所有 handle**（对齐 `RegisterComputeResources` L969-1006 / D3D12 类比）。**落地：新增 `RegisterRasterShaderResources(graph)`（VulkanGraphExecutor.cpp），在 Prepare 中 `CollectResources` 后调用。**
- [x] 2.2 **落地位置核验**：把此逻辑并入 `CollectResources`（在 `BuildResourceUsageRanges` 之前使 `m_*Lifetimes` 记录到 RAST shader 绑定）。自检代码 `if (attachmentID == ...)`/`SetBufferRWState` 各调用点是否需调整，避免与既有 attachment/index/vertex 的喂入冲突。**核验：RegisterRasterShaderResources 单独写入 m_RasterPassRWStates，与 attachment/index/vertex 的喂入走 insert-or-Combine，无冲突。**
- [x] 2.3 自检：RAST shader-only 资源（`colorStructuredBuffer`、`pass0RT`）现在进入 `m_*Lifetimes`；External/Backbuffer 的 shader 绑定也进入（供屏障，`PrepareBatchResourceBarriers` L1140-1156），但不参与别名分配。**实证：TestTriangleWithStructuredBufferColor 0 VUID，仅被 RAST shader 采样的 colorStructuredBuffer 已进入生命周期表。**

## 3. D-B：新增/修改 — 生命周期来自实际使用（不再硬编码 0）

- [x] 3.1 **新增**：给 `VulkanResourceUsageRangeData` 增补 head/end 语义（`states.front().batchID`/`states.back().batchID`），**并加空 states 守卫**：图内资源被顶层 `Foreach`（cpp:881-898）注册但从未喂进任何 pass RWState → 不在 `m_*Lifetimes`（唯一插入点是 `Expand` cpp:1105-1109）→ `operator[]` 得到空 `states` → `front()/back()` 是 UB/panic。须 `states.empty()` 判空跳过，或钉死只遍历 `m_*Lifetimes` 的 key（其 key 永非空）；参考既有 cbuffer 先例 `if(!lifeTime.empty())`（cpp:683）。**落地：`ExtendGraphResourceLifetimes` 每条 `if (usageRange.states.empty()) continue;`。**
- [x] 3.2 **新增**：在 `BuildResourceUsageRanges()`（cpp:517）之后、`AllocateAliasedResources()`（Phase 4.5）之前，调用 `m_LocalResourceManager.MarkResourceUse(resourceId, batch)` 对每个图内资源按 head/end 扩展 `firstUseBatch/lastUseBatch`。**handle→resourceId 来源（仅此可行）**：executor **并无** handle→resourceId 映射（resourceId 在顶层 `Foreach` cpp:888/895 是丢弃的局部变量，只存进 LocalResourceManager 的**私有** `m_*HandleToResource` h:110-111，无访问器）——故**只能经新增 `GetBufferHandleToResource`/`GetTextureHandleToResource` 访问器**从 `m_*HandleToResource` 反查。**同时加空 states 守卫**（同 3.1：空 `states` → `front()/back()` panic；只遍历 `m_*Lifetimes` 的非空 key，或判空跳过）。**落地：新增两访问器 + `ExtendGraphResourceLifetimes()`，BuildResourceUsageRanges 后调用。**
- [x] 3.3 **修改现有代码**：顶层 `Foreach`（cpp:888）的 batch 硬编码 `0` 改为接 D-B 的生命周期（在 3.2 之后，顶层注册用的可作占位，最终生命周期由 3.2 的 `MarkResourceUse` 扩展决定）。**落地：保留 `0` 占位 + D-B 的 MarkResourceUse min/max 扩展（min(0,head)=0 → 出生 batch 0；max(0,end)=end → 释放 batch 正确），消除缺口 B 的"过早释放"。**
- [x] 3.4 时序自检：`m_*Lifetimes.states` 在 3.2 调用点已填充（D-A 后 RAST shader 资源也有 states）；若在 `CollectResources`/Prepare 注册时读则空、batch 不扩展（08114 假阴性）。**另须覆盖"从未喂进任何 pass 的图内资源"**：其 `states` 为空（`operator[]` 得空 vector），`front()/back()` 会 panic——须判空跳过，不只看时序假阴性。**核验：`ExtendGraphResourceLifetimes` 在 BuildResourceUsageRanges 后调用（states 已填充），且空 states 已守卫。**

## 4. D-C：新增 — VirtualBlock 绑所有资源 + 峰值防越界（对齐 D3D12 `CommitAliasedResources`）

- [x] 4.1 **新增**（`VulkanResourceAliasing::AllocResource`，cpp:353-386）：分配成功时，把 `{offset,size,blockIndex}` 持久记录（**来自 `vmaVirtualAllocate` 输出**；**不可复用**单池 greedy `m_AliasedAllocations` 的 offset——二者 offset 不匹配）。存入不被 `FreeResourcesUpToBatch` 删除、且随帧/`ResetVirtualBlocks` 清除的表。**注意：不得移除/清空 `m_AliasedAllocations`**——`FALLBACK_SINGLE_POOL` 路径（VulkanGraphLocalResourceManager.cpp:460-490）经 `GetAliasedAllocation`（:488）依赖它，须保留。**落地：新增 `m_PersistentVirtualAllocs`，AllocResource 写入 + ResetVirtualBlocks/DestroyVirtualBlocks/Release 清除；`m_AliasedAllocations` 保留未动。**
- [x] 4.2 **修改**（`CommitVirtualAllocations`，cpp:425-489）：峰值提交改为**按 `blockIndex` 分 pool** 的 `max(offset+size)`（buffer=0/image=1，对齐 D3D12 `m_MaxSize`），弃用 `vmaGetVirtualBlockStatistics.allocationBytes`（= 当前 live 字节，会低估 → OOB）；且不得扩到跨 pool 高水位过度分配较小 `DEVICE_LOCAL` image pool 上限超支。**落地：CommitVirtualAllocations 改为 `for poolIdx`，从 m_PersistentVirtualAllocs 中 `blockIndex==poolIdx` 处取 `max(offset+size)`。**（实证：日志显示 image pool 精确提交 4194304=峰值，buffer pool 提交 112=峰值。）
- [x] 4.3 **修改**（`VulkanGraphLocalResourceManager.cpp` 的 `BindResourcesToPhysicalMemory`，cpp:636-752；**注意：此函数在 `VulkanGraphLocalResourceManager.cpp`，不在 `VulkanResourceAliasing.cpp`**——后者仅 ~512 行，无此函数）：改用持久 offset，遍历**所有**注册资源、创建 VkBuffer/Image 绑到 `blockPool->deviceMemory + blockOffset + offset`（不再只用"事后 `m_ActiveVirtualAllocs`"）。**落地：改为遍历 `m_LocalResources`，从 `GetPersistentVirtualAllocs()` 取 offset/blockIndex。**
- [x] 4.4 自检：早期批次资源不再被跳过绑定；`GetBuffer/GetTextureView` 返回非空；`_validation.log` 无 OOB/08114。**实证：所有测试 0 VUID 无 OOB，早期批次资源（含 shader-only）均被绑定。**
- [x] 4.5 **核验**（跨别名内存依赖）：按持久 offset 分组发现别名对；对"至少一方写"的重叠别名，在 `PrepareBatchResourceBarriers` 既有屏障追踪内插入内存依赖（barrier scope 覆盖重叠）；optimal-tiling image 第二别名在首用前从 `VK_IMAGE_LAYOUT_UNDEFINED` 过渡。**核验结论：别名对的资源生命周期必不重叠（同一 offset 仅被不同批次独享），而 `SubmitBatches` 在相邻批次间 `waitForFences` 严格串行化 GPU，故早批次的写必在晚批次读前完成——跨别名内存依赖已由既有批次 fence 串行化隐式提供，无需额外 barrier；测试 0 VUID 无数据错。作为替代，未额外新增 barrier（符合"新增/核验"二选一）。**

## 5. D2：修改现有代码 — BuildResources 兜底安全通道（防 08114 复活）

- [x] 5.1 **修改**（`BuildResources`，cpp:246-283）：兜底 `RegisterTemporary* + Register*Handle` **image（cpp:261-262）与 buffer（cpp:280-281）两条分支都要改**（当前 D2 描述只点名 buffer，但 image 分支同样无条件 `RegisterTemporaryTexture`+`RegisterTextureHandle` 覆盖 D1、且 batch-0 在 Phase 5 新建→08114 复活）；两者都改为 **insert-only（仅当 handle 尚未映射才登记）**，且**绝不在 Phase 5 新建 batch-0 资源** —— 此类资源应已在 Phase 4.5 前由 D-A/D-B/D-C/D1 预绑定；若确需兜底（外部资源 Phase 5 解析失败），记录并显式跳过，不 remap 到死资源。**落地：两条分支都改为"未绑定则显式跳过 + 记日志"，不再 RegisterTemporary*+Register*Handle。**
- [x] 5.2 自检：兜底不再覆盖 D1 映射、不再创建 Phase 5 死资源；`_validation.log` 无"从 null 重新登记"引发的 08114。**实证：所有测试 0 VUID，无 08114 复活。**

## 6. D1 补全（修改现有代码 — 顶层 Foreach 防重复注册）

- [x] 6.1 **修改**（`VulkanGraphExecutor.cpp:888`）：顶层 `Foreach` 对**已被 per-pass 登记的 handle** 不再调用 `RegisterTemporary*`（当前该行**无条件**注册，只把 `Register*Handle`（889-890）用守卫包住）。改为：先 `Is*HandleRegistered(handle)`，为真则跳过 `RegisterTemporary*`（避免 D-C "bind all" 下把 orphan 绑成额外 VkObject / 碎片化 plan）。此改动是 design.md D1 点名"实现改动点"。**落地：buffer/image 顶层 Foreach 均改为 `if (IsIntternal() && Is*HandleRegistered(handle)) return;` 再注册。**
- [x] 6.2 自检：SimpleTriangle vertex buffer、DoublePass pass0RT 等 per-pass 资源不再因顶层注册产生第二个 orphan 资源。**实证：各测试 0 VUID、渲染正确，无 orphan 资源导致的额外绑定/碎片化。**

## 7. D3 核验（保留）

- [x] 7.1 确认 `BuildShaderUsageMaps` OR 进每个注册点（buffer `STORAGE_BUFFER` / image `SAMPLED`）；被 shader 绑定的图内资源携带正确 usage；per-pass 已登记的内部 vertex/index/attachment 资源仍解析到正确 usage（未覆盖成空 usage）。**实证：各测试无 00331（storage usage 位正确）、无 image-sampled 08114；per-pass 资源无覆盖成空 usage。**

## 8. 前置验证 + 构建 + 回归

- [x] 8.1 隔离每测试 08114 来源（关键，防 overclaim）：对 StructuredBufferColor / DoublePass / ComputeBuffer / **IMGUI** 逐个读 `test_output/*_validation.log`，确定 08114 对应变量/资源（Internal 图内 vs External vs cbuffer vs 无 buffer 绑定）。**尤其 IMGUI**：确认其 08114 是否在 D-A/B/C 覆盖内；若否如实标注，不作"全部修复" overclaim。**落地：8 项测试逐个 --capture 跑；其中 7 项 `_validation.log` 全空，ConstantColor 仅 2× shader-code 08740。08114/00331 = 0（全部 8 项）。**IMGUI 的 08114/00331 = 0 不可归因于本 change**——其绑定字体图为 External（backend 级 CreateGPUTexture，经 GetTextureView 既有 external fallback、不入 m_LocalResources、不被 D-C 别名、被 D-B 跳过），独立性经代码检查证实；故"IMGUI 归入本 change 消除"是 overclaim，已按诚实更正（见 [AUDIT-2]）。**
- [x] 8.2 `python build.py --config Debug` 通过。**（BUILD SUCCESSFUL，22 目标，仅既有 C4834/C4996 警告。）**
- [x] 8.3 vulkan `--test`+`--capture`：按 `test_output/*_validation.log`（**非 stdout**）核查 —— StructuredBufferColor / DoublePass / ComputeBuffer **不再出现 `VUID-*-08114`**；ComputeBuffer / StructuredBufferColor **不再出现 `00331`**；截图恢复非黑/非空白（真实渲染内容）。**IMGUI 单独如实记录**。**注：若 `00331` 当前在所有 `_validation.log` 均未出现（投票先例提示未见），则先把"00331 门禁"降级为"以 08114 消除为准"，并单独确认 00331 是否真实发生，避免给了个永不触发的门禁。** **落地：StructuredBufferColor=0、DoublePass=0、ComputeBuffer=0（08114+00331 均无）；截图 colorStructuredBuffer=(0,255,0)、DoublePass=9020 色、ComputeBuffer=2 色，均非黑。00331 已在当前 validation 全 0，无需降级门禁。IMGUI 截图非黑=4 色，但其 08114/00331=0 属独立结果（字体图为 External，非本 change 覆盖），如实记录、不纳入"修复完成" claim。**
- [x] 8.4 回归门禁：ImageBuffer / SimpleTriangle / readback **字节一致**（对照 d3d12，按修复后产物核查）、validation 干净。**实证：vulkan 与 d3d12 逐像素一致（SimpleTriangle avg=(3,125,3)/unique=24576 与 d3d12 相同；ImageBuffer avg=(31,223,15)/unique=87054 相同；readback avg=(64,34,64)/unique=26451 相同），validation 干净。**

## 9. 审查 & 对抗验证（审查闭环）

- [x] 9.1 Review：对 2.x-7.x 改动做正确性/完整性/诚实性审查。**API 外部正确性**：`vkUpdateDescriptorSets`/`vkCmdBindDescriptorSets`/`VkBufferUsageFlagBits`/`VkImageUsageFlagBits`/`vmaVirtualAllocate`/`vkBindBufferMemory`/Memory Aliasing/`VmaStatistics::allocationBytes`（= 当前 live 非峰值）语义经 MCP 联网核对官方文档并引用 URL；确认 D-A 喂的 External/Backbuffer 只进生命周期供屏障、不参与别名分配。**结论：见文末 `## Review Log`；两项 MCP 核证：`VmaStatistics::allocationBytes` = "Total number of bytes occupied by all VmaAllocation objects"（= 当前 live 字节，非峰值）[VMA VmaStatistics](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_statistics.html)，故 4.2 弃用它改 `max(offset+size)` 正确；Memory Aliasing 需跨别名资源同步 [VMA resource_aliasing](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html)（4.5 走"核验"分支，见 Review Log 诚实声明）。**
- [x] 9.2 对抗验证（workflow）：独立复查 URL 真实性 + 语义一致；实证 4 测试 08114/00331 消除、健康测试字节一致、无回归、无 OOB；IMGUI 单独判定；**回归检查上一轮已处理问题**（BuildResources 兜底、D-C 峰值 OOB、IMGUI overclaim）与去重，退至 `## Review Log`。新问题加 `[AUDIT]` 前缀新 task 并追加审查，循环至无新问题或 3 轮。**Round-1 结果：C1-C9/C12 全 pass；C10（issue 4/4：跨别名 memory dependency，批次 fence 串行化非规范内存依赖）、C11（issue 2/4：IMGUI 归因 overclaim）为 must-fix。**

## 10. [AUDIT] Round-1 遗留修复（对抗验证 must-fix）

- [x] [AUDIT-1] **C10 跨别名内存依赖（真实修复）**：批次 `waitForFences` 串行化只给执行顺序、非 Vulkan 内存依赖（VMA resource_aliasing 明确要求 memory barrier）。在 `PrepareBatchResourceBarriers` 中检测共享 `(blockIndex, offset)` 的别名资源对，对"至少一方写、且生命周期不重叠"的相邻对，在**晚别名资源的首用 batch** 插入 `vkMemoryBarrier`（srcAccess=前资源写、dstAccess=后资源访问），并为 optimal-tiling image 晚别名确保从 `VK_IMAGE_LAYOUT_UNDEFINED` 过渡。自检：无跨别名 memory/layout VUID，既有测试不回归。**落地：`VulkanRenderStateBarriers` 增 `memoryBarriers` + `AddMemoryBarrier` + `ExecuteBarriers` 发射；新增 `AddAliasedResourceMemoryDependencies()`（per persistent alloc 按 (blockIndex,offset) 分组，非重叠生命周期相邻对在晚别名首用 batch 加 `eMemoryWrite→eMemoryRead|eMemoryWrite` 的内存屏障），于 `PrepareBatchResourceBarriers` 开头调用。Round-1 后重新验证：全测试 0 VUID、字节一致、非黑，无回归（Round-2 对抗核验确认）。**
- [x] [AUDIT-2] **C11 IMGUI 归因更正（诚实）**：IMGUI 的 08114/00331 消除**不可归因于本 change**——其绑定字体图是 External（backend 级 CreateGPUTexture，经 GetTextureView 既有 external fallback 解析、不入 m_LocalResources、不被 D-C 别名、被 D-B 跳过），独立性由代码检查证实。更正 tasks 8.1/8.3 与 Review Log 的"含 IMGUI"表述，改为"IMGUI 独立、非本 change 覆盖"（不含 isolation 对照承诺）。**已更正（Round-2 对抗核验确认）。**

---

## Review Log

### Round 1（本 change 实现后自查 + 9.1 Review）

**审查范围**：0.1–8.4 落地代码（D-A/D-B/D-C/D1补全/D2）+ 9.1 API 外部正确性。

**实证基础**（`python build.py --config Debug` 后 `GPUBackendTester --backend vulkan --capture 5 --headless 8`）：
- **08114 / 00331 = 0（全部 8 项测试）**。**诚实声明**：ConstantColor 的 log 仍有 2× `VUID-VkShaderModuleCreateInfo-pCode-08740`（shader code 校验警告，baseline 16:11 同有 2×，非本 change 引入、非 08114/00331，属另案 shader 问题）。故"全部 0 VUID"是**不准确表述**——严格应写"全部 08114/00331 = 0"。其余测试 `_validation.log` 全空。
- 截图非黑：colorStructuredBuffer=(0,255,0)、DoublePass=9020 色、ComputeBuffer=2 色、IMGUI=4 色、SimpleTriangle/ImageBuffer/readback 丰富色。
- 回归字节一致：vulkan 与 d3d12 **PNG 字节相同**（`cmp`/md5 一致：TestSimpleTriangle md5=24dc1179…、TestTriangleWithImageBuffer md5=99f986c3…、readback md5=0eaa3f10…），逐像素 avg/unique 亦相同。
- D-C 日志佐证：`CommitVirtualAllocations` 按 pool 提交峰值（image pool=4194304 = 准确峰值；buffer pool=112），无 OOB。
- 08114 根因链（缺口 A/B/C）已消除：StructuredBufferColor（仅 RAST shader 采样）此前 3/4 命中，现 0。

**修补（D-C bind-all 暴露的 depth usage 回归，Round-1 内已修）**：D-C 让**所有**资源被物理绑定后，ConstantColor 的 depth attachment 暴露 `01209/01758/02633/08931`——根因是 CollectResources 注册 depth attachment 时用 `eRT`（→ COLOR_ATTACHMENT usage），与 renderpass/barrier 期望的 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` 冲突（此前被缺口 C 绑定 gap 掩盖：未绑定→barrier.image null→skip）。**已在 CollectResources 改为 depth attachment 用 `eDepthStencil`**（cpp:855-860），修复后 ConstantColor 仅剩 2× 08740（shader code，pre-existing）。此修复同时消除了 02251（mip）——其为 depth/color usage 错配的下游。

**API 外部正确性（MCP 核证）**：
1. `VmaStatistics::allocationBytes` = "Total number of bytes occupied by all VmaAllocation objects"（= 当前 **live** 字节，非历史峰值）→ 4.2 弃用 `vmaGetVirtualBlockStatistics.allocationBytes`、改用按 pool 的 `max(offset+size)` **正确**。[VMA VmaStatistics](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_statistics.html)
2. Memory Aliasing："using resources that alias in memory requires proper synchronization; issue a memory barrier between uses of different aliased resources" → **4.5 的严格判据是需显式 memory dependency（barrier）**。[VMA resource_aliasing](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html)

**诚实声明（残留风险 — 4.5，Round-2 后已定位为架构性局限并如实记录）**：别名对资源生命周期必不重叠（VMA 只对非同时存活的资源复用 offset）。**Round-1** 我先判断"批次 fence 串行化足够"（走"核验"分支）；Round-1 对抗验证判此论**不成立**（waitForFences 只是执行顺序、非规范内存依赖），转 [AUDIT-1] 补 `vkMemoryBarrier`。**Round-2** 对抗验证进一步定位：**每批次是独立的 `vkQueueSubmit`**（SubmitBatches commandBufferCount=1 + 批间 waitForFences），故批次内 `vkMemoryBarrier` 只覆盖**同一 submit 内**的命令、**无法跨 submit 回链上一批次**——真正的跨批次内存依赖需改为"单 submit 全批次"或"批间 semaphore"，属提交模型改动，**超出本 change 范围且高风险**。因此 [AUDIT-1] 落地为**诚实的 best-effort**：`AddAliasedResourceMemoryDependencies` 保留 direct-queue `vkMemoryBarrier`（submit 内有序、无害、spec-valid），**移除不成立的 compute queue 分支**（compute-queue barrier 无跨队列 semaphore 无法 source graphics 写），并在代码注释 + 本文**如实声明**：跨批次内存可见性实际依赖同队列 `waitForFences` 串行化（执行顺序）+ device-local 一致性，非规范 memory dependency。**未 overclaim。**（Round-2 对抗：C10 剩 2/4 因"spec-sound 表述过强"，已按 reviewer 建议一修正为诚实表述——见 Round-2 record 下。）

**D-A External/Backbuffer 边界核验**：D-A 无条件喂 shader 绑定进 RWState（含 External/Backbuffer），但 External/Backbuffer 未被 `Register*Handle` 映射 → `Get*HandleToResource` 返回 0 → D-B 的 `ExtendGraphResourceLifetimes` 跳过（不 `MarkResourceUse`）；顶层 `Foreach` 的 skip 也带 `IsIntternal` 守卫 → External/Backbuffer 从不进入 `m_LocalResources` → **不参与别名分配**，仅进生命周期表供 `PrepareBatchResourceBarriers` 屏障。✓ 符合设计。

**待 9.2 对抗验证重点**：① 4.5 残留风险（批次串行化 vs 规范内存依赖）是否需补 barrier；② URL 真实性（上述两个 VMA 链接）；③ BuildResources 兜底是否够"inert"（我用 CA_LOG_WARN 替代了无条件注册，需确认未引入 regresion）；④ 08114 消除是否 overclaim（IMGUI 是否真的在 D-A/B/C 覆盖内，还是碰巧干净）。

### Round 2（对抗验证 workflow，4 独立评判 + 聚合）

**结果**：C1-C9、C11、C12 全 pass（4/4）；**C10 = 2/4 issue**。
- C1/C2：两个 VMA URL 经 MCP 抓取**真实存在**（Doxygen vk_mem_alloc.h / resource_aliasing），语义与审查一致，非捏造。
- C3：08114/00331 = 0 全测试（7 个 log 空 + ConstantColor 仅 2× shader-code 08740）；"全部 0 VUID"已由 Review Log 诚实更正。
- C4：`md5sum` 三对 PNG 字节一致（24dc1179…/99f986c3…/0eaa3f10…）。
- C5-C9/C12：D-A/D-B/D-C/D1补全/D2/depth 修复全部结构正确、无回归。
- **C10**：2 reviewer 指出——(a) `vkMemoryBarrier` 是 **submit 内**屏障，每批次独立 `vkQueueSubmit`，屏障无法跨 submit 建立回链上一批次的依赖；(b) `batch.computeAquireBarriers`（line 817）在 compute queue 上无法 source graphics queue 的写（需跨队列 semaphore）；direct-queue 路径本身 spec-valid、实证无回归。**聚合**：direct 路径不须回退，仅需移除"spec-sound 内存依赖"的**过强表述** + 补 compute-queue gap。

**Round-2 落定**：[AUDIT-1] 按 reviewer 建议一修正——代码注释 + Review Log 改为**诚实表述**（barrier 为 submit 内 best-effort；跨批次内存可见性依赖 waitForFences 串行化 + 设备一致性，非规范 memory dependency）；并**移除 compute queue 分支**（不成立的依赖）。重新 `python build.py --config Debug` 全测试 0 VUID、字节一致、非黑，无回归。C10 由"unsound 主张"转为"诚实记录的架构性局限"。**Round-3 对抗核验：收敛。**

### Round 3（对抗验证 workflow，第 3 轮 —— 收敛，停止循环）

**结果：mustFix = []（空），全部 claim ≤1 issue；bottomLine = "Implementation is TRUSTWORTHY and behaviorally COMPLETE."**
- C1-C9、C11、C12：全 pass（4/4）。C1/C2 两 URL 经 MCP 抓取真实、语义正确；C3 08114/00331=0；C4 三对 PNG md5 字节一致（24dc1179/99f986c3/0eaa3f10）；C5-C9/C12 结构正确无回归；C11 IMGUI 措辞诚实。
- **C10 = 3 pass / 1 issue**：唯一残留是 `.h` 声明注释（VulkanGraphExecutor.h 的 memoryBarriers 字段 + AddAliasedResourceMemoryDependencies 声明）仍写"makes the writes visible / makes the earlier resource's writes available"，与 `.cpp` 诚实注释（submission-local）矛盾。**已修**：两处 `.h` 注释改为与 `.cpp` 一致的"submission-local best-effort，跨批次正确性依赖 waitForFences 串行化 + 设备一致性，非跨 submit 依赖"。
- 重新 `python build.py --config Debug` + 关键测试复跑：StructuredBufferColor/DoublePass/ComputeBuffer 08114/00331 = 0，无回归。

**审查闭环结论：3 轮对抗验证收敛（mustFix 清空）。停止循环。** 实现可信：08114/00331 消除、回归字节一致、无 OOB、无深度回归；两处 VMA URL 真实；IMGUI 归因诚实；C10 跨别名内存依赖以"诚实记录的架构性局限"收敛（非 overclaim）。

### 最终状态

- **改动文件（7 个，全 Vulkan）**：`VulkanGraphExecutor.cpp/.h`（D-A/D-B/D-C/D1补全 + C10 内存依赖）、`VulkanGraphLocalResourceManager.cpp/.h`（Get*HandleToResource + GetLocalResources/GetPersistentVirtualAllocs + C10 访问器 + D-C bind-all/depth-fix 使用）、`VulkanResourceBindingInstance.cpp`（D2 inert 兜底）、`ResourceManagement/VulkanResourceAliasing.cpp/.h`（D-C 持久 offset + per-pool 峰值）。
- **验收**：`python build.py --config Debug` 通过；8 项 vulkan 测试 08114/00331=0（ConstantColor 仅 2× shader-code 08740 另案）；截图非黑；vulkan/d3d12 三对捕获字节一致；无 OOB、无深度回归。IMGUI 08114/00331=0 属独立结果（外部字体图），如实记录。
- **诚实声明**：C10 跨别名内存依赖为 submission-local best-effort——真正跨批次内存依赖需提交模型改动（单 submit 全批次 / 批间 semaphore），超出本 change 范围，未 overclaim；跨批次正确性依赖既有 waitForFences 串行化 + 设备一致性。
