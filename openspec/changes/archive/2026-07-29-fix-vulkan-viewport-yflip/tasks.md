## 1. Vulkan viewport 负高度

- [x] 1.1 `VulkanGraphExecutor.cpp` `RecordRenderPass`（line ~1849）：默认 viewport 改为 `{ 0, (float)height, (float)width, -(float)height, 0, 1 }`
- [x] 1.2 `VulkanGraphExecutor.cpp` per-drawcall viewport 覆盖（line ~1983）：同理改为负高度 `{ (float)vp->x, (float)(vp->y + vp->height), (float)vp->width, -(float)vp->height, 0, 1 }`

## 2. 测试顶点 z 值修正

- [x] 2.1 `Test/GPUBackendTester/Main.cpp` `TestSimpleTriangle`：三个顶点 z 从 -0.25/-0.25/0.0 改为 0.25/0.25/0.5（全部在 [0,1] 范围内）

## 3. 验证

- [x] 3.1 `python build.py` 编译通过
- [x] 3.2 Vulkan 后端 `TestSimpleTriangle --headless 6`：validation log 为空，无 crash
- [x] 3.3 **MANUAL**：Vulkan 后端窗口显示三角形，方向与 D3D12 一致（不再上下颠倒）
- [x] 3.4 Vulkan 后端 `TestTriangleWithConstantColor --headless 6`：回归测试
- [x] 3.5 Vulkan 后端 `TestDoublePass --headless 6`：回归测试（含 final blit UV flip）
