# Pipeline Depth-Clamp

## ADDED Requirements

### Requirement: 统一的近平面 depth-clamp 光栅化开关
渲染管线状态 SHALL 暴露一个上层字段 `enableDepthClamp`，Vulkan 与 D3D12 后端 SHALL 读取同一字段并在光栅化时**一致**实现其语义：`true` = 近平面沿 z **clamp**（z 在近平面之外也渲染，深度按 z=0 clamp）；`false`（默认）= **标准近平面裁剪**（z<0 几何被剔除）。两个后端对同一字段值 SHALL 产生**相同**的 z<0 几何可见性结果。

#### Scenario: Vulkan 默认裁剪近平面几何
- **WHEN** `enableDepthClamp` 为默认 `false`，且顶点 clip z < 0
- **THEN** Vulkan 后端在光栅化时裁剪该几何（不产生像素），且无 validation 报错

#### Scenario: D3D12 默认裁剪近平面几何（与 Vulkan 一致）
- **WHEN** `enableDepthClamp` 为默认 `false`，且顶点 clip z < 0
- **THEN** D3D12 后端也同样裁剪该几何，与 Vulkan 结果一致

#### Scenario: Vulkan 启用 clamp 后渲染 z<0
- **WHEN** `enableDepthClamp` 为 `true`（且 Vulkan 设备已启用 `depthClamp` feature）
- **THEN** Vulkan 后端对 z<0 几何沿 z clamp 而非裁剪，几何以 z=0 深度渲染

#### Scenario: D3D12 启用 clamp 后渲染 z<0
- **WHEN** `enableDepthClamp` 为 `true`
- **THEN** D3D12 后端不裁近平面（`DepthClipEnable=false`），z<0 几何渲染

#### Scenario: 双后端对同一字段值行为一致
- **WHEN** 给两个后端设置相同的 `enableDepthClamp` 值（`true` 或 `false`）
- **THEN** 两个后端对同一份 z<0 几何的可见性结果相同（同为渲染或同为裁剪）

#### Scenario: 测试用例在默认 clip 下双端渲染
- **WHEN** `TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` 使用合法 clip z（≥0）并保持默认 `enableDepthClamp=false`
- **THEN** 两个测试在 Vulkan 与 D3D12 后端都渲染出可见三角形，且双端像素输出一致

### Requirement: Vulkan 设备启用 depthClamp feature
使用 `depthClampEnable=true` 的 Vulkan 管线 SHALL 仅在其物理设备 `depthClamp` feature 已启用时创建；否则该管线创建 SHALL 遵循 Vulkan 规范（不得产生因 feature 缺失导致的 validation 错误）。

#### Scenario: 未启用 feature 时不设置 clamp
- **WHEN** Vulkan 设备未启用 `depthClamp` feature，且管线请求 `depthClampEnable=true`
- **THEN** 该状态不得在运行时触发 validation 错误（`VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782`；实现需在设备创建时启用 `depthClamp`，或避免在 feature 缺失时设 true）

### Requirement: D3D12 depth-clip 由上层字段驱动（非硬编码）
D3D12 后端的光栅化状态（`DepthClipEnable`）SHALL 由上层 `enableDepthClamp` 字段驱动（`DepthClipEnable = !enableDepthClamp`），SHALL 不包含任何针对此字段的硬编码常量。所有 D3D12 管线构建路径（`GPUPipelineInstance` 与 `PipelineStatesObject`）SHALL 使用同一映射，结果一致。

#### Scenario: 去除 D3D12 硬编码
- **WHEN** 构建 D3D12 光栅化状态
- **THEN** `DepthClipEnable` 由 `enableDepthClamp` 逆映射而来（`true` → `false`，`false` → `true`），无硬编码常量；两条管线路径结果一致
