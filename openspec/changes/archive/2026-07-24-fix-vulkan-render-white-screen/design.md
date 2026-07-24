## Context

对抗验证确认：原设计对 VUID 的根因分析有误。6 个 VUID 由单一根因触发——`RenderPassCacheKey` POD 字段未初始化。`CombindDescData` 合并的设计确认正确。headless 修复缺少 `CRenderBackend::WaitIdle()` 接口。

## Goals / Non-Goals

**Goals:**
- 修复 `RenderPassCacheKey` 未初始化（全部 VUID 的根因）
- 添加 `CombindDescData` 合并（pipeline=0 的根因）
- 添加 `CRenderBackend::WaitIdle()` 接口并在 headless 循环后调用（headless 崩溃的根因）
- 移除 watchDog

**Non-Goals:**
- 不修改 `ConvertFormat`（验证确认从不返回 VK_FORMAT_UNDEFINED）
- 不修改 `PrepareBatchResourceBarriers` 的 layout 逻辑（验证确认正确）
- 不重构 render pass 管理架构

## Decisions

### Decision 1: 初始化 RenderPassCacheKey（原 Decision 3 重写）

**根因**：`RenderPassCacheKey`（`RenderBackend_Vulkan.h:85-90`）的两个 POD 字段未初始化：

```cpp
struct RenderPassCacheKey {
    castl::vector<vk::Format> colorFormats;
    vk::Format depthFormat;  // ← 未初始化
    bool hasDepth;           // ← 未初始化
};
```

两处使用时栈声明为 `RenderPassCacheKey rpKey;`（无 `{}` 值初始化）。当 `GetDepthAttachmentIndex()` 返回 -1（无 depth），loop 不设置 `hasDepth`/`depthFormat`，保留栈垃圾。

**修复**：
```cpp
struct RenderPassCacheKey {
    castl::vector<vk::Format> colorFormats;
    vk::Format depthFormat = vk::Format::eUndefined;
    bool hasDepth = false;
};
```

同时 `GetOrCreateRenderPass` 中 hash 计算加守卫：
```cpp
// Before:
cacore::hash_combine(hash, static_cast<uint32_t>(key.depthFormat));
cacore::hash_combine(hash, key.hasDepth);
// After:
if (key.hasDepth)
    cacore::hash_combine(hash, static_cast<uint32_t>(key.depthFormat));
cacore::hash_combine(hash, key.hasDepth);
```

这确保 `hasDepth=false` 时 hash 与 `depthFormat` 值无关。

### Decision 2: CombindDescData 合并（确认正确，不变）

`PipelineDescData::CombindDescData` 位于 `Interface/RenderInterface/header/GPUGraph.h:56`，合并逻辑：child 有效 → child；否则 → parent。

修改点：
- `CollectShaderBindings`（line 798）：`CombindDescData(renderPass.GetPipelineStates(), batch.pipelineStateDesc).m_ShaderInfo`
- `BuildPipelineStates`（lines 1347, 1391-1392）：合并后使用 `pipelineData.m_ShaderInfo`、`m_InputAssemblyStates`、`m_PipelineStates`

Vulkan 使用 dynamic state 处理 viewport/scissor，合并后的 `m_Viewport`/`m_Scissor` 不会被使用（无害死数据）。

额外防护：合并后若 `!hasVertex || !hasFragment`，log warning 并 `continue`——防止 nullptr shader module 进入 `vk::GraphicsPipelineCreateInfo`。

### Decision 3: CRenderBackend::WaitIdle() 接口 + headless 修复

`CRenderBackend` 抽象接口当前没有 `WaitIdle()` 方法。headless 修复需要在循环后同步 GPU。

1. `Interface/RenderInterface/header/CRenderBackend.h`：加 `virtual void WaitIdle() = 0;`
2. `RenderBackend_Vulkan` 实现：`m_Device.waitIdle()`（或在 `GPUFrameManager` 中实现更精确的 fence 等待）
3. `Main.cpp` headless 循环后调用 `ctx.pGPUBackend->WaitIdle();`
4. 移除 `headlessTimedOut` watchDog 线程和相关代码

### Decision 4: 移除误诊的修复目标

对抗验证确认以下原 tasks 目标错误，予以删除：
- ~~Task 3.2~~（ConvertFormat 从不返回 VK_FORMAT_UNDEFINED）
- ~~Task 3.3~~（barrier layout 逻辑正确）
- ~~Task 3.4~~（clearValue/attachment count 不匹配是 C1 的症状）
- ~~Task 3.5~~（同上，症状而非根因）

## Risks / Trade-offs

- **[风险]** `CRenderBackend::WaitIdle()` 作为纯虚方法需要 D3D12 后端也实现 → **缓解**: D3D12 后端实现 `m_Device->WaitForGpu()` 或留空实现（D3D12 无 headless 需求）
- **[风险]** validation layer 仍可能在 headless 快速退出时不稳定 → **缓解**: `WaitIdle()` 确保 GPU 完成，validation layer 可正常清理
