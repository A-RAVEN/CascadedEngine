# Tasks: Unify Depth-Clamp Pipeline Switch

## 1. 接口打通（上层字段暴露）

- [ ] 1.1 确认 `RasterizerStates::enableDepthClamp`（`CPipelineStateObject.h:19`）已存在且语义为「true=近平面沿 z clamp 不裁，false=标准近平面裁剪」；打通从 GPUGraph pipeline state 到两个后端 rasterization-state 构建点的传递，确保字段每 draw 可设、非硬编码。
- [ ] 1.2 核对两后端组装 rasterization state 时读取 `enableDepthClamp` 的入口（Vulkan `VulkanGraphExecutor` / D3D12 `GPUPipelineInstance` + `PipelineStatesObject`），确认字段无中途丢失。

## 2. Vulkan 后端

- [ ] 2.1 设备创建时启用 `VkPhysicalDeviceFeatures::depthClamp`（`RenderBackend_Vulkan.cpp`），满足 `depthClampEnable=TRUE` 的 feature 前提（否则 `VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782`）。
- [ ] 2.2 `VulkanGraphExecutor.cpp:1823` 光栅化状态：`depthClampEnable = rasterizationStates.enableDepthClamp ? VK_TRUE : VK_FALSE`（替代当前默认 VK_FALSE）。

## 3. D3D12 后端

- [ ] 3.1 `GPUPipelineInstance.cpp:159` 去除硬编码 `DepthClipEnable = FALSE`，改为 `DepthClipEnable = rasterizerStates.enableDepthClamp ? FALSE : TRUE`（逆映射，与 Vulkan 语义拉齐）。
- [ ] 3.2 `PipelineStatesObject.cpp:31` 采用同一逆映射（`DepthClipEnable = !enableDepthClamp`），两条 D3D12 管线路径结果一致。

## 4. 测试数据对齐

- [ ] 4.1 `Main.cpp` `TestTriangleWithConstantColor` 顶点 z：`-0.25/-0.25/0` → `0.25/0.25/0.5`（对齐合法 clip z，使其在默认 clip 下双端渲染）。
- [ ] 4.2 `Main.cpp` `TestTriangleWithStructuredBufferColor` 顶点 z：`-0.25/-0.25/0` → `0.25/0.25/0.5`。

## 5. 构建 + 回归验证

- [ ] 5.1 `python build.py --config Debug`，确保构建成功；若失败则分析并修复直到通过。
- [ ] 5.2 双后端（vulkan/d3d12）各跑 8 测试，全部 exit 0；`ConstantColor`/`StructuredBufferColor` 双端都渲染出可见三角形且像素一致；其余测试不回归；无新增 VUID（validation 日志干净）。

## 6. 审查闭环（Review & Adversarial Verify）

- [ ] 6.1 开 workflow 对抗验证上述实现的正确性：Vulkan `depthClamp` feature 启用、`depthClampEnable` 映射、D3D12 逆映射去硬编码、双路径一致、测试 z 改动、全 8 测试无回归、无 validation 错误；对照 MCP 核实的 API 语义与文档 URL 真实性。
