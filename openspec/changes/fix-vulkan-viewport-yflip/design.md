## Context

Vulkan 后端渲染结果与 D3D12 上下颠倒。两个 API 的 NDC 坐标系差异：

```
D3D12 NDC           Vulkan NDC（默认）        Vulkan NDC（负高度 viewport）
  y=1  top            y=-1  top                y=1  top  ← 与 D3D12 一致
  y=-1 bottom         y=1   bottom             y=-1 bottom
  z∈[0,1]             z∈[0,1]（viewport 后）   z∈[0,1]（viewport 后）
```

引擎使用 Vulkan 1.3，`VK_KHR_maintenance1`（Vulkan 1.1 core）的负高度 viewport 保证可用。

Slang 编译器不会自动翻转 Y（已确认）。shader 里的坐标补偿是为 D3D12 坐标系写的：`Imgui.slang`/`testFinalBlit.slang` 翻转 position.y，`FinalBlit.slang` 翻转 UV.y。负高度 viewport 使 Vulkan 屏幕空间与 D3D12 一致后，这些补偿在两个后端下语义相同，**无需修改**（对抗验证确认）。

## Goals / Non-Goals

**Goals:**
- Vulkan 后端的渲染方向与 D3D12 一致（不再上下颠倒）
- 不修改任何 shader 代码
- 不修改 D3D12 后端

**Non-Goals:**
- 不处理 Vulkan 1.0 兼容
- 不修改投影矩阵 / 相机代码
- 不修改 Slang 编译器配置

## Decisions

### Decision 1: 负高度 viewport（而非 shader Y-flip）

**方案对比**：

| 方案 | 改动面 | 对已有 shader 的影响 | 移动端兼容 |
|---|---|---|---|
| A: 负高度 viewport | VulkanGraphExecutor.cpp 1 处 | 无（shader 不动） | Vulkan 1.1 core ✅ |
| B: shader `#ifdef VULKAN` | 所有 vertex shader + 编译器传 macro | Imgui.slang 等已有 flip 变 double-flip，要删 | 不依赖版本 ✅ |
| C: 投影矩阵 bake Y-flip | 相机代码 | 对无矩阵的 shader（TestSimpleTriangle）无效 | ✅ |

选 A：改动最小、最通用、不碰 shader。

**实现**：

默认 viewport（`RecordRenderPass` line 1849）：
```cpp
// 当前
vk::Viewport viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };

// 改为
vk::Viewport viewport{ 0.0f, (float)height, (float)width, -(float)height, 0.0f, 1.0f };
```

per-drawcall 覆盖（line 1983）同理。

scissor 不需要改：Vulkan spec 规定 scissor 的 `offset.y` 和 `extent.height` 定义的是 framebuffer 空间中的矩形，与 viewport 方向无关。`{0, 0, width, height}` 始终覆盖全屏。

### Decision 2: 测试顶点 z 值修正

`TestSimpleTriangle` 的顶点 z=-0.25（w=1）被近裁剪面裁掉。Vulkan 和 D3D12 默认 clip 范围相同（0 ≤ z ≤ w），但 D3D12 的 `DepthClipEnable=false` 跳过了 z 裁剪，Vulkan 的 `depthClampEnable` 默认也是 false 但行为是裁剪而非跳过。

改法：z=-0.25/-0.25/0.0 → z=0.25/0.25/0.5。z=0.0 虽然在 0 ≤ z 边界上合法，但浮点边界行为 implementation-sensitive，改为 0.5 更安全。

## Risks / Trade-offs

- **[风险]** 负高度 viewport 翻转了 winding order → 原本的 CW 变 CCW → **缓解**: `cullMode` 默认为 `eNone`（不剔除），winding order 不影响渲染。如果未来启用 back-face culling，需要同时调整 `frontFace` 设置
- **[风险]** 某些第三方库（ImGui 后端）可能假设 viewport y 向下 → **缓解**: ImGui 的 `Imgui.slang` 已有手动 Y-flip，负高度 viewport 下语义正确（与 D3D12 一致）
- **[风险]** `FinalBlit.slang` 的 UV flip（`uv.y = 1.0 - uv.y`）在负高度 viewport 下可能需要调整 → **缓解**: UV flip 是纹理坐标变换，与 viewport 方向无关（对抗验证确认）；task 3.5 验证 TestDoublePass
- **[已知限制]** Vulkan 后端缺少 batch/pass 级 viewport 继承（D3D12 有 pass→batch→drawcall 三级）。当前无调用方使用 `DrawCallBatch::SetViewPort/SetScissor`（ImGui 只用 drawcall 级 `.Scissor()`），不影响此 change。pre-existing 差异，未来如有调用方需补齐
