## MODIFIED Requirements

### Requirement: 图内资源生命周期由 shader 绑定使用推导且必须被绑定

图内（Internal）资源的生命周期 SHALL 由其在图内的**实际 shader 绑定使用**推导（经 RWState → 生命周期表 head/end），而非硬编码 batch 0；被 shader 绑定的图内资源 SHALL 进入生命周期表并被物理绑定，使其 Handle 可解析到有效句柄。register/绑定层 SHALL 与 D3D12 语义对齐（所有 pass 的 shader SRV/UAV 绑定喂进生命周期表；资源按生命周期出生/释放；绑定遍历所有注册资源）。

#### Scenario: 所有 shader 绑定（RAST+compute）喂进生命周期表

- **WHEN** 一个资源被任何 pass 的 shader（RAST 或 compute）以 SRV/UAV 绑定（`ShaderStruct`=SetBuffer/SetImage）
- **THEN** 该资源 SHALL 被喂进对应 pass 的 RWState（`SetImageRWState`/`SetBufferRWState`），从而进入 `m_ImageLifetimes`/`m_BufferLifetimes`（对齐 D3D12 `addPassShaderInstancesResourcesRWStates` 的 RAST L478 / compute L504 分支，不得仅限 compute；**含 External/Backbuffer 的 shader 绑定**——它们只进生命周期供屏障、不参与别名分配）

#### Scenario: 生命周期来自实际使用而非硬编码

- **WHEN** 一个图内资源被注册进本地资源管理器
- **THEN** 其 `firstUseBatch`/`lastUseBatch` SHALL 由生命周期表（`states` 首/末 `batchID`）扩展而来（经 `MarkResourceUse` Min/Max），而非顶层注册的硬编码 batch 0——使别名规划在资源真实存活区间内不提前释放/复用

#### Scenario: 顶层图资源 Foreach 配对（insert-only）

- **WHEN** `CollectResources` 遍历 `GetBufferManager()/GetImageManager().Foreach`，为图内 `AllocBuffer`/`AllocImage` 资源调用 `RegisterTemporaryBuffer`/`RegisterTemporaryTexture`
- **THEN** 系统仅当该 Handle **尚未映射**时用返回的 `resourceId` 调用 `RegisterBufferHandle`/`RegisterTextureHandle`（insert-only），**不得覆盖**已被 per-pass 路径（attachment/index/vertex/upload/finalize）用正确 usage 登记的映射

### Requirement: 已注册的 Internal 资源可通过 Handle 查询到有效句柄

`GetBuffer(BufferHandle)` 和 `GetTexture(TextureHandle)` SHALL 在 Handle 类型为 Internal 且已正确注册时，返回非空的 VkBuffer/VkImage 句柄。`GetTextureView(ImageHandle)` SHALL 对被 shader 绑定的已注册内建图像返回非空 VkImageView。被 shader 绑定的图内资源 SHALL 在绑定阶段被创建/绑定（遍历所有注册资源，而非仅"事后仍活跃"者）。

#### Scenario: Internal buffer 查询成功

- **WHEN** `GetBuffer(BufferHandle)` 被调用，且该 Handle 已在 `CollectResources` 中注册并在绑定阶段被绑定
- **THEN** 返回对应的有效 VkBuffer（非 VK_NULL_HANDLE）

#### Scenario: 被 shader 绑定的内建图像返回有效 ImageView

- **WHEN** 一个图内 `AllocImage` 被 `SetImage` 绑定为采样纹理，`GetTextureView(ImageHandle)` 被调用
- **THEN** 返回非空 `VkImageView`（该 Handle 已在顶层 Foreach 或 per-pass 路径登记并绑定，不触发 `if(!imageView) continue;`）

#### Scenario: 早期批次资源不被跳过绑定

- **WHEN** 一个图内资源仅在前几个批次使用（其生命周期不在最后一个批次）
- **THEN** 绑定阶段仍会为其创建 VkBuffer/Image 并绑定（遍历所有注册资源按持久 offset），不因 `FreeResourcesUpToBatch` 提前释放而跳过（对齐 D3D12 `CommitAliasedResources` 遍历所有）

#### Scenario: 持久 offset 自 vmaVirtualAllocate 且峰值防越界

- **WHEN** D-C 为图内资源绑定到别名内存块
- **THEN** 所用 offset SHALL 持久化自 `AllocResource` 的 `vmaVirtualAllocate` 输出 `{offset, size, blockIndex}`（不可复用单池 greedy 的 `m_AliasedAllocations`，二者 offset 不匹配），存入不被 `FreeResourcesUpToBatch` 删除、且随帧/`ResetVirtualBlocks` 清除的表
- **AND** 物理块提交大小 SHALL 为按 `blockIndex` 分 pool 的 `max(offset+size)`（buffer pool=0 / image pool=1，`CommitVirtualAllocations` 本就 per-pool），而非 `vmaGetVirtualBlockStatistics.allocationBytes`（= 当前 live 字节，会低估导致早期批次 OOB）；且不得扩到跨 pool 高水位而过度分配较小的 `DEVICE_LOCAL` image pool 上限超支

#### Scenario: 早期与后期资源别名重叠时提供内存依赖

- **WHEN** D-C 后一个早期批次资源与一个后期批次资源被绑到同一 offset（时间不重叠别名）
- **THEN** 二者使用间（至少一方写时）SHALL 有内存依赖（barrier scope 覆盖重叠范围），对 optimal-tiling image 的第二个别名 SHALL 从 `VK_IMAGE_LAYOUT_UNDEFINED` 过渡

