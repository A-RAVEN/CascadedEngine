# Design: Unify Depth-Clamp Pipeline Switch

## Context

`TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` 用顶点 z=-0.25/0（近平面之外）画三角形而在 **Vulkan 上消失**、**D3D12 上渲染**。根因已定位为**光栅化"近平面 clip/clamp"开关双后端不一致**：

- **Vulkan**：`VulkanGraphExecutor.cpp:1823-1831` 建 `VkPipelineRasterizationStateCreateInfo` 时从未读接口 `enableDepthClamp`，`depthClampEnable` 保持默认 `VK_FALSE` → **裁 z 面**（z<0 剔除）。且设备未启用 `depthClamp` feature。
- **D3D12**：`GPUPipelineInstance.cpp:159` **硬编码 `DepthClipEnable = FALSE`** → **不裁 z 面**（z<0 渲染）；`PipelineStatesObject.cpp:31` 虽有 `enableDepthClamp → DepthClipEnable` 映射，但两条路径不一致。

关键 API 语义（MCP 已核实 [VkPipelineRasterizationStateCreateInfo](https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VkPipelineRasterizationStateCreateInfo.html)、[gpuweb#2100](https://github.com/gpuweb/gpuweb/issues/2100)、[vkguide depth.adoc](https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/depth.adoc)）：
- Vulkan `depthClampEnable=TRUE` → 沿 z **clamp**（替代 clip，z<0 渲染为 z=0）；`FALSE`（默认）→ clip。
- `depthClampEnable=TRUE` **必须**启用 `VkPhysicalDeviceFeatures::depthClamp`，否则 `VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782`。
- Vulkan 的 "depth clamp" 是 "depth clip" 的**反面**；D3D12 的 `DepthClipEnable=FALSE`（不 clip）≈ Vulkan `depthClampEnable=TRUE`（clamp）。**映射需反相**。

## Goals / Non-Goals

**Goals:**
- 把"近平面 clip/clamp"统一为**一个**上层 `GPUGraph` / `PipelineState` 字段 `enableDepthClamp`，两个后端都忠实读取同一值 → 同一值 → 双后端一致。
- **默认 `false` = 标准近平面裁剪**（用户拍板）。
- Vulkan：补 `depthClamp` feature 启用 + 光栅化状态映射 `depthClampEnable`。
- D3D12：去 `GPUPipelineInstance.cpp:159` 硬编码，改由字段驱动（逆映射），两条 D3D12 路径拉齐。
- 测试数据对齐：两测试顶点 z 改 `≥0`，使其在默认 clip 下双端正常渲染。

**Non-Goals:**
- 不改 Z 投影/坐标系其它约定。
- 不处理 IMGUI 字体白块（另案）。
- 不改变深度测试的 compare op / 深度值语义。

## Decisions

### D1: 单一上层字段 `enableDepthClamp`，默认 `false`
- 语义：`true` = 近平面沿 z **clamp**（不裁，z<0 渲染为 z=0）；`false` = 标准**裁剪**（z<0 剔除）。
- 字段已在 `Interface/RenderInterface/header/CPipelineStateObject.h:19`（`RasterizerStates::enableDepthClamp = false`），作为**每 draw 的 pipeline state** 一部分。**打通**：确保它从 GPUGraph 的 pipeline state 一路传到两个后端的 rasterization-state 构建点（现在 Vulkan 没读它）。
- 备选：单独起一个新字段/枚举。否——复用已有 `enableDepthClamp`，避免接口分叉；它语义已明确。

### D2: 后端映射（反相关）
- Vulkan：`rasterizationState.depthClampEnable = enableDepthClamp ? VK_TRUE : VK_FALSE`；并启用 `depthClamp` feature。
- D3D12：`DepthClipEnable = enableDepthClamp ? FALSE : TRUE`（**反相**，因为 D3D "enable clip"与 Vulkan "enable clamp"相反），在 `GPUPipelineInstance` 与 `PipelineStatesObject` 两条路径统一。

### D3: 默认=false → 两测试的 z=-0.25 会被裁
- 一致性优先（用户选择标准近平面裁剪）。因此两测试顶点 z 改为 `0.25/0.5`（对齐 `SimpleTriangle` 等合法 clip z），使默认 clip 下双端都渲染。
- 改为"设置 switch=true"不是首选：会改变测试默认语义，且掩盖"z<0 本就非法"的事实。

## Risks / Trade-offs

- [D3D12 行为变化（BREAKING）] D3D12 从"不裁 z<0"改为"裁" → 影响所有曾依赖"近平面不裁"的 D3D12 几何 → **Mitigation**: 这是预期的一致性修正；若上层需要"不裁"，显式设 `enableDepthClamp=true`。
- [Vulkan 需启用 `depthClamp` feature] 若只设 `depthClampEnable=TRUE` 而未启用 feature → `VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782` → **Mitigation**: 在设备创建时启用 `depthClamp`，且仅在字段为 true 时设 VK_TRUE。
- [全局语义影响] `depthClampEnable=true` 会让 Vulkan 对所有几何"clamp 而非裁"（非默认路径） → **Mitigation**: 默认 false；仅显式用到 clamp 的 draw 设 true。
- [两条 D3D12 管线路径分叉] `GPUPipelineInstance` 与 `PipelineStatesObject` 若只改一条 → 不一致 → **Mitigation**: 两处都改，用同一 `enableDepthClamp` 驱动。

## Migration Plan
1. 设备层：Vulkan 启用 `depthClamp` feature。
2. Vulkan 光栅化状态映射 `enableDepthClamp → depthClampEnable`。
3. D3D12 两条路径去硬编码、改字段驱动（逆映射）。
4. 测试数据：两测试 z → ≥0。
5. `python build.py --config Debug` + 双后端 8 测试回归 + 截图像素一致核验。

## Open Questions
- 是否需要把 `enableDepthClamp` 暴露到更上层的 graph API（除 internal pipeline state 外）？proposal 目标是不暴露则两端仍可能发散——当前字段在 internal pipeline state，打通到两端即可；同时保持其可设（非硬编码）。
