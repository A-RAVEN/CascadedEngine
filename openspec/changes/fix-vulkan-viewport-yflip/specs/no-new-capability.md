## 说明

纯 bug 修复，不引入新 capability，不修改已有 spec。

### 修复的 bug

1. Vulkan 后端渲染结果上下颠倒（NDC y 轴方向与 D3D12 不一致）
2. 测试顶点 z 值不在标准深度范围 [0,1] 内
