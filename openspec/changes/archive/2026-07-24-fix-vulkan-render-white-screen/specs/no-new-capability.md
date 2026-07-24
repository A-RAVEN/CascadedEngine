## 说明

此 change 为纯 bug 修复，不引入新 capability，不修改已有 spec 的行为契约。

### 修复的 bug

1. **RenderPassCacheKey POD 字段未初始化**：`hasDepth` 和 `depthFormat` 栈上声明时未初始化，导致 phantom depth attachment 被创建，触发 6 个 VUID
2. **PipelineDescData 合并缺失**：`RenderPass::SetShaderInfo` 存入 renderPass 但 `BuildPipelineStates`/`CollectShaderBindings` 读 batch，导致 shader 不传播，pipeline=0
3. **Headless 模式缺少 GPU 同步**：N 帧后直接 exit，validation layer 被暴力卸载崩溃
4. **CRenderBackend 缺少 WaitIdle() 接口**：headless 修复的前置依赖

### 已有关联 spec（行为不变，仅修复实现）
- `vulkan-shader-library`
