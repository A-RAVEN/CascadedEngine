## Why

Vulkan 后端 `TestSimpleTriangle` 白屏。经诊断确认三个独立问题：

1. **RenderPassCacheKey POD 字段未初始化**（全部 6 个 VUID 的根因）：`hasDepth` 和 `depthFormat` 在栈上声明时未初始化。当 render pass 没有 depth attachment 时，这两个字段保留栈垃圾值——`hasDepth` 可能为 true，`depthFormat` 为随机值。`GetOrCreateRenderPass` 据此创建 phantom depth attachment（color format + depth layout），触发全部 VUID 违规。

2. **PipelineDescData 合并缺失**（pipeline=0 的根因）：测试调用 `RenderPass::SetShaderInfo()` 存入 `renderPass.m_PipelineStates`，但 `BuildPipelineStates` 和 `CollectShaderBindings` 读的是 `batch.pipelineStateDesc`——batch 从未被设置 shader，`isValid()` 返回 false，pipeline 创建被跳过。D3D12 通过 `PipelineDescData::CombindDescData(parent, child)` 合并两层配置，Vulkan 缺失此调用。

3. **Headless 模式缺少 GPU 同步**：N 帧后直接 `exit`，不等待 GPU 完成。validation layer 在进程退出时被暴力卸载导致崩溃。`CRenderBackend` 接口缺少 `WaitIdle()` 方法。

## What Changes

1. **初始化 RenderPassCacheKey**：添加成员默认初始值 `bool hasDepth = false;`，hash 计算加 `if (key.hasDepth)` 守卫。**这一行修复全部 6 个 VUID。**

2. **添加 PipelineDescData 合并**：在 `BuildPipelineStates` 和 `CollectShaderBindings` 中调用 `PipelineDescData::CombindDescData(renderPass.GetPipelineStates(), batch.pipelineStateDesc)`。

3. **添加 `CRenderBackend::WaitIdle()` 接口**：抽象接口加纯虚方法，`RenderBackend_Vulkan` 实现。headless 循环结束后调用。

4. **移除 headless watchDog**：删除 `headlessTimedOut` 线程和 `headlessTimeout`，外部用 `timeout` 命令管理超时。

## Capabilities

### Modified Capabilities
- _（纯 bug 修复，不引入新 capability，不修改已有 spec 契约）_

## Impact

- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.h`：`RenderPassCacheKey` 加成员默认初始值
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`：`GetOrCreateRenderPass` 的 hash 加 depth guard；实现 `WaitIdle()`
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`：加 `CombindDescData` 合并 + 移除两处 `RenderPassCacheKey rpKey;` 的未初始化声明
- `Interface/RenderInterface/header/CRenderBackend.h`：加 `virtual void WaitIdle() = 0;`
- `Test/GPUBackendTester/Main.cpp`：headless 循环后加 `WaitIdle()`；移除 watchDog
