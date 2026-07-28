## Why

Vulkan 后端的渲染结果与 D3D12 上下颠倒。根因：D3D12 NDC y 轴向上（-1=底，1=顶），Vulkan NDC y 轴向下（-1=顶，1=底）。引擎没有做任何补偿，导致同一组顶点在两个后端画出镜像图像。Slang 编译器不会自动翻转 Y（已确认）。部分 shader 有手动坐标补偿：`Imgui.slang`/`testFinalBlit.slang` 翻转 position.y（`pos.y = -pos.y`），`FinalBlit.slang` 翻转纹理坐标（`uv.y = 1.0 - uv.y`）——这些是为 D3D12 坐标系写的，负高度 viewport 下语义不变。

RenderDoc 抓帧确认：三角形上下颠倒 + 近裁剪面裁剪（z=-0.25 被 0 ≤ z 裁掉）。

## What Changes

1. **Vulkan viewport 使用负高度**：`vk::Viewport` 的 height 改为负值、y 改为 `height`，利用 Vulkan 1.1 core 的 `VK_KHR_maintenance1` 特性在硬件层面翻转 Y 轴，使 Vulkan 的屏幕空间坐标系与 D3D12 一致
2. **scissor rect 无需修改**：Vulkan scissor 在 framebuffer 坐标系中定义，与 viewport 方向无关（对抗验证确认）
3. **测试顶点 z 值修正**：`TestSimpleTriangle` 的顶点 z=-0.25 被近裁剪面裁掉（Vulkan 默认 clip 0 ≤ z ≤ w，与 D3D12 相同），改为合法值
4. **shader 不需要修改**：负高度 viewport 使 Vulkan 的屏幕空间与 D3D12 一致。`Imgui.slang`/`testFinalBlit.slang` 的 position.y flip 和 `FinalBlit.slang` 的 UV flip 在两个后端下语义相同，无需修改

## Non-Goals

- 不修改 D3D12 后端
- 不修改 Slang 编译器配置
- 不修改投影矩阵（相机代码）
- 不处理 Vulkan 1.0 兼容（引擎已用 Vulkan 1.3）

## Capabilities

### New Capabilities

- _（纯 bug 修复，不引入新 capability）_

### Modified Capabilities

- _（无 spec 变更）_

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：viewport / scissor 设置（默认 + per-drawcall 覆盖）
- `Test/GPUBackendTester/Main.cpp`：测试顶点 z 值
