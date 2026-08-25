## MODIFIED Requirements

### Requirement: Compute queue family 有效性检查

`CompileAndExecute` SHALL 在任何 batch 使用 asyncCompute 前验证 compute queue family 非 -1。若为 -1，SHALL 输出警告并将所有 asyncCompute 的 batch 退化为同步 direct queue。`QueueContext` SHALL 在单通用族设备上回退 compute/transfer 族索引到 graphics 族，使 -1 状态在正常情况下不出现。

#### Scenario: 无 compute queue 时退化

- **GIVEN** `QueueContext` 回退后仍无法提供 compute queue family（索引 -1），且 graph 标记了 asyncCompute
- **THEN** asyncCompute 被禁用，batch 在 direct queue 上同步执行

#### Scenario: 单通用族设备正常路径

- **GIVEN** 设备仅有一个通用队列族
- **WHEN** `QueueContext` 初始化
- **THEN** compute/transfer 族索引回退为 graphics 族索引（≥ 0）
- **AND** 无需退化，batch 照常执行

## ADDED Requirements

### Requirement: compute 命令缓冲上的 barrier 阶段必须为 compute 族支持阶段

在 compute 命令缓冲（compute queue family）上记录的 `vkCmdPipelineBarrier`，其 `srcStageMask`/`dstStageMask` SHALL 仅包含该队列族支持的阶段（如 `COMPUTE_SHADER`、`TRANSFER`、`HOST`），SHALL NOT 包含 `VERTEX_SHADER`/`FRAGMENT_SHADER`（compute-only 队列族不支持 graphics 阶段）。涉及 `VulkanGraphExecutor` 的 3 处路径 SHALL 遵守：`ExecuteBarriers` 的 compute acquire/release 屏障、`uploadCBufferBarriers`、compute UAV 写入屏障。

#### Scenario: compute 队列上执行 compute acquire 屏障

- **GIVEN** asyncCompute batch 在 compute 命令缓冲上记录 acquire/release 屏障，且资源访问含 shader read/write
- **WHEN** `ExecuteBarriers` 组装 stage mask
- **THEN** `srcStageMask`/`dstStageMask` 仅含 compute 族支持阶段（`COMPUTE_SHADER` 等）
- **AND** 验证层不报告 stage-support 相关 VUID

#### Scenario: compute 队列上执行 CBuffer 上传屏障

- **GIVEN** `batch.computeCBufferBarriers` 非空且记录于 compute 命令缓冲
- **WHEN** 组装 `dstStageMask`
- **THEN** 不包含 `VERTEX_SHADER`/`FRAGMENT_SHADER`
- **AND** 上传后数据对后续 compute 消费可见

#### Scenario: compute UAV 写入屏障

- **GIVEN** compute pass 写入 UAV 后执行内存屏障
- **WHEN** 组装 `dstStageMask`
- **THEN** 为 compute 族支持阶段
- **AND** 不包含 graphics-only 阶段

### Requirement: transfer pass 布局状态跟踪与实际转换一致

GPU Graph 的状态跟踪（`CollectResources` 等）对 transfer pass 处理过的图像记录的目标布局 SHALL 与实际记录的 barrier/布局转换一致。两者不一致时 SHALL 以实际转换为准修正状态跟踪，使后续 barrier 的 `oldLayout` 与真实布局相符。

#### Scenario: transfer pass 图像后续被采样

- **GIVEN** transfer pass 将图像 inline 转换到 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- **WHEN** 后续 pass 对该图像执行 barrier
- **THEN** 状态跟踪记录的布局与 `SHADER_READ_ONLY_OPTIMAL` 一致
- **AND** 验证层不报告 layout mismatch

### Requirement: 上传 barrier 同步域覆盖所有消费阶段

buffer/纹理上传后的 `vkCmdPipelineBarrier`，其 `dstAccessMask` 中的访问类型 SHALL 与 `dstStageMask` 中的阶段匹配，且 `dstStageMask` SHALL 覆盖所有会消费该数据的阶段（vertex/compute shader 读取 uniform 或 buffer 时 SHALL 包含 `VERTEX_SHADER`/`COMPUTE_SHADER`，而不仅是 `VERTEX_INPUT`/`FRAGMENT_SHADER`）。使用默认 mask（如 `eNone`/`TOP_OF_PIPE`）作上传后同步 SHALL NOT 被允许，SHALL 显式指定。

#### Scenario: 上传的 buffer 被 vertex/compute shader 读取

- **GIVEN** 上传后 buffer 后续被 vertex shader 或 compute shader 读取（uniform/SSBO）
- **WHEN** 记录上传 barrier
- **THEN** `dstStageMask` 含 `VERTEX_SHADER`（或 `COMPUTE_SHADER`）
- **AND** `dstAccessMask` 含对应访问位（`SHADER_READ_BIT` 等）
