# Tasks: Unify Depth-Clamp Pipeline Switch

## 1. 接口打通（上层字段暴露）

- [x] 1.1 确认 `RasterizerStates::enableDepthClamp`（`CPipelineStateObject.h:19`）已存在且语义为「true=近平面沿 z clamp 不裁，false=标准近平面裁剪」；打通从 GPUGraph pipeline state 到两个后端 rasterization-state 构建点的传递，确保字段每 draw 可设、非硬编码。
- [x] 1.2 核对两后端组装 rasterization state 时读取 `enableDepthClamp` 的入口（Vulkan `VulkanGraphExecutor` / D3D12 `GPUPipelineInstance` + `PipelineStatesObject`），确认字段无中途丢失。

## 2. Vulkan 后端

- [x] 2.1 设备创建时启用 `VkPhysicalDeviceFeatures::depthClamp`（`RenderBackend_Vulkan.cpp`），满足 `depthClampEnable=TRUE` 的 feature 前提（否则 `VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782`）。
- [x] 2.2 `VulkanGraphExecutor.cpp:1823` 光栅化状态：`depthClampEnable = rasterizationStates.enableDepthClamp ? VK_TRUE : VK_FALSE`（替代当前默认 VK_FALSE）。

## 3. D3D12 后端

- [x] 3.1 `GPUPipelineInstance.cpp:159` 去除硬编码 `DepthClipEnable = FALSE`，改为 `DepthClipEnable = rasterizerStates.enableDepthClamp ? FALSE : TRUE`（逆映射，与 Vulkan 语义拉齐）。
- [x] 3.2 `PipelineStatesObject.cpp:31` 采用同一逆映射（`DepthClipEnable = !enableDepthClamp`），两条 D3D12 管线路径结果一致。

## 4. 测试数据对齐

- [x] 4.1 `Main.cpp` `TestTriangleWithConstantColor` 顶点 z：`-0.25/-0.25/0` → `0.25/0.25/0.5`（对齐合法 clip z，使其在默认 clip 下双端渲染）。
- [x] 4.2 `Main.cpp` `TestTriangleWithStructuredBufferColor` 顶点 z：`-0.25/-0.25/0` → `0.25/0.25/0.5`。

## 5. 构建 + 回归验证

- [x] 5.1 `python build.py --config Debug`，确保构建成功；若失败则分析并修复直到通过。
- [x] 5.2 双后端（vulkan/d3d12）各跑 8 测试，全部 exit 0；`ConstantColor`/`StructuredBufferColor` 双端都渲染出可见三角形且像素一致；其余测试不回归；无新增 VUID（validation 日志干净）。

## 6. 审查闭环（Review & Adversarial Verify）

- [x] 6.1 开 workflow 对抗验证上述实现的正确性：Vulkan `depthClamp` feature 启用、`depthClampEnable` 映射、D3D12 逆映射去硬编码、双路径一致、测试 z 改动、全 8 测试无回归、无 validation 错误；对照 MCP 核实的 API 语义与文档 URL 真实性。

## Review Log

### Round 1（实现对抗验证）— 收敛，无缺陷

4 个独立 reviewer、11 claims、逐 claim 多数票（mustFix = issue≥2）。**结果：11/11 全票通过（0 issue / 0 uncertain），mustFix 空，bottomLine「CORRECT AND COMPLETE — no revision required」。**

- **根因/映射**：I1 Vulkan feature（deviceFeatures.depthClamp 复制设备支持、pEnabledFeatures 设置、pNext 无 VkPhysicalDeviceFeatures2 → VUID 合法）、I2 Vulkan depthClampEnable=字段（默认 false=裁）、I3 D3D12 GPUPipelineInstance.cpp:162 逆映射、I4 PipelineStatesObject.cpp:33 逆映射（双路径一致）、I5 测试 z {0.25,0.25,0.5}（与 SimpleTriangle 一致）——全部 4/4 通过。
- **经验**：I6（双后端 8 测试 exit 0；ConstantColor md5 952d268c 一致、StructuredBufferColor md5 7cff0e45 一致，vulkan==d3d12）、I7（SimpleTriangle/ImageBuffer/DoublePass/readback md5 一致无回归；ComputeBuffer 因 baseline 同款 sin(time) 时间动画 DIFFER、compute 代码未触碰）、I8（validation 日志全 0 字节，仅 ConstantColor 含 benign 08740；无新增 depthClampEnable-00782）——全部 4/4 通过。
- **API/一致性**：I9（MCP 核实 depthClampEnable TRUE=clamp z<0、FALSE=clip；depthClamp feature 必需 00782；D3D DepthClipEnable FALSE=不裁 → 逆映射）、I10（design D1/D2/D3 + spec Req 1/3 与实现一致）、I11（tester 实跑 8 测试，CLAUDE.md 的"7"已过时；TestIMGUI 白块另案）——全部 4/4 通过。

**非缺陷注意点（无需改码，记录在案）**：
1. **覆盖缺口**：8 个测试都未设 `enableDepthClamp=true`，故 clamp 路径（true → depthClampEnable=true → 渲染 z<0）仅靠代码审 + MCP 语义核实，无实际 clamp render。属可选补强（见下方 AUDIT 待办）。
2. design.md 引用的行号已偏移（GPUPipelineInstance.cpp:159→现在 162；PipelineStatesObject.cpp:31→33）——cosmetic，可更新。
3. CLAUDE.md"7 个测试"过时，tester 实跑 8。
4. ConstantColor 08740 为良性（baseline 同款），非本次引入。
5. TestComputeBuffer 顶点仍 z=-0.25（本 diff 未触碰），被预期的 D3D12 不裁→裁 BREAKING 吸收，记录为预期、非回归。

**去重/回归检查**：对照触发根因（z-clip 开关双后端错配）——本 change 已根治（Vulkan 映射+feature+双路径一致+测试 z 修正），无与既有问题重叠；无回归。

**诚实声明**：clamp 正向路径（enableDepthClamp=true）未被运行时触达，仅代码 + 官方文档语义核实。若需更强保证，可补一条显式设 `enableDepthClamp=true` 且 z<0 的测试做真 clamp render（非本 change 必须，见下方待办）。

### [AUDIT-1]（待定）补 clamp 路径运行时验证

- **背景**：Round 1 审查标记覆盖缺口——无任何测试设 `enableDepthClamp=true`，故 clamp（true→渲染 z<0）路径未运行时验证。
- **建议**：加一个显式设 `enableDepthClamp=true` 且顶点 z<0 的渲染测试，双后端跑确认 z<0 被渲染（clamp 而非裁）。若采纳，追加为 [AUDIT-1] task + 再跑一轮对抗验证。
- **状态**：待用户拍板是否补（非缺陷，可留作后续）。
