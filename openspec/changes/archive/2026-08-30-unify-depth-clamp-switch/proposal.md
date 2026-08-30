# Unify Depth-Clamp Pipeline Switch

## Why

Vulkan 后端对"近平面 z 裁剪"的默认行为与 D3D12 不一致，导致 `TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` 的三角形（顶点 z=-0.25/0，近平面之外）在 **Vulkan 上消失**、在 **D3D12 上渲染**。根因：光栅化的"近平面裁剪/clamp"开关**没有统一的上层字段**，两个后端各自局部决策——Vulkan 忽略已有 `enableDepthClamp` 字段、`depthClampEnable` 用默认 `VK_FALSE`（裁剪）；D3D12 在 `GPUPipelineInstance.cpp:159` 硬编码 `DepthClipEnable = FALSE`（不裁）。同一语义，两端跑出不同结果，属于后端错配（非测试数据错、也非绑定 bug）。

## What Changes

- 把"近平面 z-clip/z-clamp"统一成**一个上层 GPUGraph / PipelineState 字段** `enableDepthClamp`（`RenderInterface` 的 `RasterizerStates` 已有此字段，需确认/打通到上层暴露面），**两个后端都忠实读取同一值**，值由上层一次性决定、两端一致执行。
- **默认 `false` = 标准近平面裁剪**（用户拍板）：无论哪个后端，z 在近平面外的几何默认被裁掉。
- Vulkan：设备**启用 `depthClamp` feature**；`VulkanGraphExecutor` 光栅化状态把 `rasterizationStates.enableDepthClamp` 映射到 `depthClampEnable`（true=沿 z clamp 而非 clip）。
- D3D12：**去掉 `GPUPipelineInstance.cpp:159` 硬编码 `DepthClipEnable = FALSE`**，改由 `enableDepthClamp` 驱动，并注意与 Vulkan 的**反相关**映射（`DepthClipEnable = !enableDepthClamp`）；`PipelineStatesObject.cpp` 路径也拉齐。
- 对齐测试数据：把 `ConstantColor` / `StructuredBufferColor` 两个测试的顶点 **z 改为 ≥0**（`0.25/0.5`，对齐其它渲染测试），使其在默认 clip 语义下**双端都正确渲染**——否则默认裁剪会把这俩三角形的 z=-0.25 裁掉。
- **BREAKING**：D3D12 当前"不裁近平面（渲染 z<0）"的默认行为会被改为"裁剪"；这两个测试的输出会随测试数据调整而变化。属预期。

## Capabilities

### New Capabilities
- `pipeline-depth-clamp`: 统一的近平面 z-clip/z-clamp 光栅化开关，暴露在 RenderInterface 的 PipelineState 上，Vulkan+ D3D12 双后端按同一字段一致实现（默认=标准近平面裁剪）。

### Modified Capabilities
<!-- 无：本 change 引入单列 capability；如后续需在 vulkan-backend-alignment 追加"光栅化一致性"要求，可在 delta 阶段补 -->

## Impact

- `Interface/RenderInterface/header/CPipelineStateObject.h`（`RasterizerStates::enableDepthClamp`，打通到上层暴露面）
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`（光栅化状态映射 `depthClampEnable`）
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`（启用 `depthClamp` device feature）
- `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp`（去硬编码，改字段驱动）
- `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp`（逆映射拉齐）
- `Test/GPUBackendTester/Main.cpp`（两测试顶点 z ≥0）

## Non-goals

- 不改 Z 坐标投影/坐标系的其它约定（仅控制"近平面 clip vs clamp"这一开关）。
- 不处理 IMGUI 字体白块（另案，见 TODO 2026-08-30）。
- 不改变深度测试/深度值比对语义（`depthClampEnable` 只影响 clip/clamp，不改变 depth test 的 compare op）。
