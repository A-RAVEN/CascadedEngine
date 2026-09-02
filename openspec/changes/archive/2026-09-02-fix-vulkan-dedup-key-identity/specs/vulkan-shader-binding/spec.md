## ADDED Requirements

### Requirement: Vulkan shader binding instance dedup key SHALL include per-pass shader-struct identity

`VulkanResourceBindingInstance` 的去重身份（`VulkanShaderResourceSet`）SHALL 包含 per-pass 的 `VulkanShaderStruct*` 指针（连同 shader path 与各绑定名），使相同 shader path + 相同绑定名但不同 per-pass 常量（struct）的 pass 各自得到独立绑定实例及其常量绑定，而非被合并到首个实例。

#### Scenario: 同 shader 同绑定名、不同 per-pass struct 的 raster pass 各自绑定
- **WHEN** 两个 raster pass 使用相同 shader path 与相同绑定名（如 "imguiCommon"/"imguiTextureBinding"），但各自携带不同的 per-pass `VulkanShaderStruct*`（其常量内容不同）
- **THEN** 两个 pass 得到不同的 `VulkanShaderResourceSet` 去重键（hash/等值不同）
- **AND** `createOrGetBindingInstance` 为每个 pass 返回独立的 `VulkanResourceBindingInstance`
- **AND** 每个实例各自绑定对应 per-pass 的常量（uniform buffer），而非绑定首个 pass 的常量

#### Scenario: IMGUI 多窗口副窗不再偏移
- **WHEN** IMGUI 多窗口（主窗 + 副窗）以不同 `IMGUIScale_Pos`（不同 viewport pos）渲染同一 "Shaders/Imgui"
- **THEN** 每个 viewport 的 pass 绑定各自的 imguiCommon 常量（`.zw` = 各自 viewport pos）
- **AND** 副窗回读 PNG 内容不再偏移 `(副窗pos − 主窗pos)`，与 D3D12 一致

#### Scenario: compute dispatch 同 shader 同绑定名、不同 per-dispatch struct 各自绑定
- **WHEN** 两个 compute dispatch 使用相同 shader path 与绑定名，但各自携带不同的 per-dispatch `VulkanShaderStruct*`
- **THEN** 两个 dispatch 各自绑定各自的 cbuffer/UAV
- **AND** 首个 dispatch 的常量不泄漏到第二个 dispatch

#### Scenario: 同 shader 同绑定名、不同 per-pass struct 的 pass 不合并
- **WHEN** 两个 pass 使用相同 shader path 与相同绑定名，但各自携带不同的 per-pass `VulkanShaderStruct*`
- **THEN** 两个 pass 去重后得到**不同**的 `VulkanResourceBindingInstance`（各自绑定各自常量），而非合并到首个
- **AND** 该判定依赖"每 pass struct 指针"参与去重身份（hash/等值），且两个不同 struct 的 pass 绝不因"同 path 同绑定名"被合并
