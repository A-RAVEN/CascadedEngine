# Proposal: 修复 Vulkan 后端渲染管线关键缺口

**Change ID**: fix-vulkan-backend-render-pipeline
**Status**: Proposed
**Created**: 2026-06-27
**Updated**: 2026-06-28（多轮审查后修订：移除 Indirect、SubGraph 实现；GPL+VK_NULL_HANDLE 经 spec 验证正确；聚焦非 GPL else 分支修复）

---

## Why

对 VulkanRenderBackendNew 的源码级审查发现：虽然 CRenderBackend 接口签名和 ExecuteGraph→VulkanGraphExecutor 连接已正确实现，但数据上传路径（Buffer/Texture UploadData）存在关键缺口——device-local buffer 的 staging 上传静默失败，Texture 上传完全空实现。此外多个 PipelineStates 结构体为空占位符、StateManager 为复制粘贴残留、CreateMonolithicPipeline 中 7 个 pipeline state 值硬编码、GPUGraph.h 中 `eSubGraph` 为从未被实现的死代码。最关键的是，Pipeline 创建路径（`LinkPipeline` / `CreateMonolithicPipeline`）因缺少 `VkPipelineRenderingCreateInfo` 而无法创建可用的 Pipeline——GPL 和非 GPL 路径均受影响。这些缺口导致任何需要 CPU→GPU 数据上传和实际渲染的场景无法工作。

## What Changes

- **实现 Buffer::UploadData staging 路径**：为 device-local buffer 创建 staging buffer + `vkCmdCopyBuffer` + pipeline barrier + 同步等待
- **实现 Texture::UploadData staging 路径**：staging buffer + `vkCmdCopyBufferToImage`（含正确的 `VkBufferImageCopy` 逐 mip/逐 layer 复制）+ 布局转换
- **填充 Pipeline States 结构体**：为 `FragmentOutputStateCache`（空结构体）填入 colorFormats/depthStencilFormat/sampleCount 字段、为 `FragmentShaderStates`（空命名空间）创建 `FragmentShaderStateCache` 结构体、为 `GeometryShaderStates`（仅含 vertex）补齐 tess/geom shader module 字段
- **修复 StateManager 占位符**：`FragmentOutputStateManager.h` 和 `GeometryShaderStateManager.h` 修正外部类名、内部类名、方法名和类型引用（当前完全复制自 VertexInputStateManager，包含内外两层类名错误）
- **修复 CreateMonolithicPipeline 硬编码**：将 `inputAssemblyState.topology`、`rasterizationState`（polygonMode/cullMode/frontFace）、`multisampleState.rasterizationSamples`、`colorBlendState`（attachmentCount/colorWriteMask）等 7 个硬编码值从 Pass/DrawCallBatch 数据动态读取（`viewportCount`/`scissorCount` 保持 1——这是 dynamic viewport 标准 Vulkan 用法，无需修改）
- **清理 GPUGraph.h 死代码**：移除 `EGraphStageType::eSubGraph` 枚举值、`SubGraph()` 方法、`m_SubGraphs` 成员——这些声明从未被任何后端实现，也无任何调用方
- **修复 Pipeline 创建非 GPL else 分支空实现**：GPL 路径 `VK_NULL_HANDLE` 经 spec 验证正确（四个 library part 均 render-pass-independent 时兼容任意 render pass）。非 GPL else 分支仅设 `layout`，需补齐完整 `VkGraphicsPipelineCreateInfo`（shader stages + render pass + 各 state），同时将 state struct 声明移出 `if` 块使其可被 else 访问

## Capabilities

### New Capabilities

- `vulkan-resource-upload`: GPU Buffer 和 Texture 的 CPU→GPU 数据上传路径，支持 device-local 内存通过 staging buffer 中转
- `vulkan-pipeline-states-completion`: FragmentOutputStates、FragmentShaderStates、GeometryShaderStates 结构体完整化、StateManager 修正、CreateMonolithicPipeline 硬编码值动态化

### Modified Capabilities

- `vulkan-backend-alignment`: 更新 Phase 1 差距表，标记数据上传路径、Pipeline States 完成为已修复

## Impact

- **VulkanBuffer.cpp**: 添加 `UploadData` device-local staging 上传路径（临时 staging buffer + `vkCmdCopyBuffer` + barrier + 同步等待）
- **VulkanTexture.cpp**: 实现 `UploadData` staging + `vkCmdCopyBufferToImage`（含逐 mip/layer 的 `VkBufferImageCopy`）+ 布局转换
- **VulkanGraphExecutor.cpp**: viewportCount/scissorCount 外的 7 个硬编码值动态化 + 非 GPL else 分支补齐
- **VulkanGraphExecutor.cpp**: `BuildPipelineStates` 中提前构建 `RenderPassCacheKey` 并获取真实 `VkRenderPass`，替代 `VK_NULL_HANDLE`；非 GPL else 分支补齐
- **FragmentOutputStates.h**: 填入 colorFormats/depthStencilFormat/sampleCount 字段
- **FragmentShaderStates.h**: 从空命名空间创建 `FragmentShaderStateCache` 结构体，包含 fragmentShaderCache 字段
- **GeometryShaderStates.h**: 补齐 tessControlShaderCache/tessEvalShaderCache/geometryShaderCache 字段
- **FragmentOutputStateManager.h**: 修正外部类名 `VertexInputStateManager` → `FragmentOutputStateManager`，修正方法名和类型引用
- **GeometryShaderStateManager.h**: 修正外部类名 `VertexInputStateManager` → `GeometryShaderStateManager`，修正内部类名 `FragmentOutputState` → `GeometryShaderState`，修正方法名和类型引用
- **GPUGraph.h**: 移除 `eSubGraph` 枚举值、`SubGraph()` 方法、`m_SubGraphs` 成员
- **specs/vulkan-backend-alignment/**: 更新差距表

## Non-goals

- 接口签名修复（验证已有，无需修改）
- ExecuteGraph 连接（已有完整实现）
- Indirect Draw/Dispatch 支持（需要先在 `DrawInfo`/`ComputeDispatch` 中添加 `drawIndirect`/`indirectArgsBuffer` 等字段——这些在当前 GPUGraph API 中不存在，属于独立接口层变更）
- SubGraph 执行支持（`eSubGraph` 从未被任何后端实现，无调用方，独立变更处理）
- Ray Tracing 支持（独立大变更）
- Mesh Shader 支持（独立大变更）
- DescriptorSet 写入/绑定路径优化（独立中变更）
- XCB/Wayland Surface 创建（独立平台变更）
- PipelineLibrary 缓存 hash map 优化（性能优化，后续）
- `VulkanLinearMemoryManager` buffer overflow on oversize allocations（独立修复）
- `VulkanBuffer::Release` 中的 `vkDestroyBuffer` 绕过 VMA 导致状态腐败（独立修复）
- Graph executor texture upload 仅写入 mip 0 / layer 0（独立修复）
- `TransitionLayout` 的 default srcAccessMask = `eNone` 对未处理 layout 不正确（边缘情况，独立处理）
