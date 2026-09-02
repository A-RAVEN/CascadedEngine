## Requirements

### Requirement: Vulkan pipeline cache key SHALL include the rasterization state that drives VkPipeline construction

`RenderStateCombination` / `GenerateHashKey`（`PipelineLibraryCache`）SHALL 将真正影响 `VkPipeline` 光栅化行为的字段（polygonMode/cullMode/frontFace/depthClampEnable）纳入缓存键，使相同 shader 但光栅化状态不同的 pass 得到不同缓存键，而非共享首个建立的 `VkPipeline`。`lineWidth` 为后端硬编码常量（恒 1.0f，不随 per-pass 变化），不纳入键；`discardRasterization`/depth-bias 后端尚未映射到 Vulkan，同样不纳入（若将来映射需扩展键）。

#### Scenario: 同 shader 不同 cullMode/frontFace 的 pass 各自管线
- **WHEN** 两个 pass 共享 shader path 与除光栅化外的所有状态，但 rasterization 的 cullMode/frontFace 不同
- **THEN** 两个 pass 的管道缓存键（`GenerateHashKey` 结果）不同
- **AND** 各自得到独立的 `VkPipeline`（正确剔除/前向面），而非绑定首个建立的管线

#### Scenario: 同 shader 不同 depthClampEnable 的 pass 各自管线
- **WHEN** 两个 pass 共享 shader path 但 rasterization 的 depthClampEnable 不同（设备已启用 depthClamp feature，`RenderBackend_Vulkan.cpp:305-307`）
- **THEN** 管道缓存键不同，各自独立管线（近平面裁剪/钳制行为各自正确）
- **注**：该场景与 rasterization 键扩展共用同一机制；depthClamp 为 feature 门控（硬件/设备相关），不单独作为运行时断言，由同机制覆盖。

#### Scenario: 不影响既有无光栅化差异的 pass
- **WHEN** 两个 pass 共享 shader path 且光栅化状态也相同
- **THEN** 管道缓存键相同，可共享同一 `VkPipeline`
- **AND** 既有去重/缓存行为不回归
