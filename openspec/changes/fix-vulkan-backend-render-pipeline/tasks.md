# Tasks: 修复 Vulkan 后端渲染管线关键缺口

**Change ID**: fix-vulkan-backend-render-pipeline
**Updated**: 2026-06-28（四轮审查后：GPL+VK_NULL_HANDLE 经 spec 验证正确，`else` 分支修复为唯一 pipeline 创建项）

---

## 1. Buffer/Texture UploadData Staging 路径 [US1]

- [x] 1.1 [US1] 实现 `VulkanBuffer::UploadData` device-local staging 上传路径：创建 HOST_VISIBLE staging buffer（VK_BUFFER_USAGE_TRANSFER_SRC）→ memcpy → vkCmdCopyBuffer（含正确的 srcOffset=0, dstOffset=offset）→ pipeline barrier（TRANSFER_WRITE → 目标 usage：VERTEX|INDEX|UNIFORM|STORAGE|INDIRECT）→ Submit+WaitIdle → 销毁 staging buffer
- [x] 1.2 [US1] 实现 `VulkanTexture::UploadData` staging 上传路径：计算全 mip/layer 紧密打包字节数 → 创建 staging buffer → memcpy → TransitionLayout(TRANSFER_DST_OPTIMAL) → 构建 `castl::vector<VkBufferImageCopy>`（每 mip+layer 一个 region）→ vkCmdCopyBufferToImage → TransitionLayout(原始布局) → Submit+WaitIdle → 销毁 staging buffer

## 2. Pipeline States 结构体完整化 [US2]

- [x] 2.1 [US2] [P] 填充 `FragmentOutputStateCache` 结构体字段：`colorFormats`（`castl::vector<vk::Format>`）、`depthStencilFormat`（`vk::Format`）、`sampleCount`（`vk::SampleCountFlagBits`）
- [x] 2.2 [US2] [P] 将 `FragmentShaderStates.h` 从空命名空间改写为 `FragmentShaderStateCache` 结构体，包含 `fragmentShaderCache`（`TypedVKHashVal<ShaderModuleCache>`），并验证无外部引用依赖空命名空间
- [x] 2.3 [US2] [P] 补齐 `GeometryShaderStates` 结构体字段：添加 `tessControlShaderCache`、`tessEvalShaderCache`、`geometryShaderCache`
- [x] 2.4 [US2] [P] 修正 `FragmentOutputStateManager.h`：外部类名 `VertexInputStateManager` → `FragmentOutputStateManager`，方法名 `EnsureFragmentOutputState` / `GetFragmentOutputState`，类型引用使用 `FragmentOutputStateCache`
- [x] 2.5 [US2] [P] 修正 `GeometryShaderStateManager.h` 全部 8 处复制粘贴污染：外部类名 `VertexInputStateManager` → `GeometryShaderStateManager`；内部类名 `FragmentOutputState` → `GeometryShaderState`；内部类成员类型 `FragmentOutputStateCache` → `GeometryShaderStates`；内部类的 `Init(FragmentOutputStateCache const&)` → `Init(GeometryShaderStates const&)`；内部类的 `GetCache()` 返回类型 `FragmentOutputStateCache const&` → `GeometryShaderStates const&`；`shared_dic` 的 value 类型 `FragmentOutputState` → `GeometryShaderState`；方法名 `EnsureFragmentOutputState` → `EnsureGeometryShaderState`；方法名 `GetVertexInputState` → `GetGeometryShaderState`

## 3. CreateMonolithicPipeline 硬编码值动态化 [US3]

- [x] 3.1 [US3] 将 `inputAssemblyState.topology` 从硬编码 `eTriangleList` 改为从 `batch.pipelineStateDesc.m_InputAssemblyStates.Get().topology` 转换
- [x] 3.2 [US3] [P] 将 `rasterizationState.polygonMode` / `cullMode` / `frontFace` 从硬编码改为从 `batch.pipelineStateDesc.m_PipelineStates.Get().rasterizationStates` 读取
- [x] 3.3 [US3] [P] 将 `multisampleState.rasterizationSamples` 从硬编码 `e1` 改为从 `batch.pipelineStateDesc.m_PipelineStates.Get().msCount` 转换
- [x] 3.4 [US3] [P] 将 `colorBlendAttachment.colorWriteMask` 从硬编码 `RGBA 全通道` 改为从 `.colorAttachments.attachmentBlendStates[0].channelMask` 读取
- [x] 3.5 [US3] [P] 将 `colorBlendState.attachmentCount` 从硬编码 `1` 改为从 `rasterPass.GetColorAttachmentCount()` 读取（注意：`attachmentBlendStates.size()` 是定长数组永远返回 8，不可用）
- [x] 3.6 [US3] 确认 `viewportCount=1` / `scissorCount=1` 保持不动——这是 dynamic viewport 标准 Vulkan 用法，无需修改
- [x] 3.7 [US3] 实现 enum 转换函数（需从旧 Vulkan 后端 `InterfaceTranslator.h` 迁移或重新实现）：`ETopology → VkPrimitiveTopology`、`EPolygonMode → VkPolygonMode`、`ECullMode → VkCullModeFlags`、`EFrontFace → VkFrontFace`、`EColorChannelMask → VkColorComponentFlags`、`EMultiSampleCount → VkSampleCountFlagBits`（后者 `VulkanTexture::ConvertSampleCount` 已有可复用）

## 4. Pipeline 创建 Dynamic Rendering 补齐 [US4]

- [x] 4.1 [US4] 确认 GPL 路径 `VK_NULL_HANDLE` 无需修改——四个 GPL library part 均 render-pass-independent 时链接产生兼容任意 render pass 的 pipeline（VUID-VkGraphicsPipelineCreateInfo-flags-06579）
- [x] 4.2 [US4] 将 7 个 state struct 声明从 `if` 块内移至 `if/else` 之前使其在 else 分支可访问；在 else 分支组装完整的 `VkGraphicsPipelineCreateInfo`：shader stages（vertexShaderStage + fragmentShaderStage）、renderPass（`GetOrCreateRenderPass`）、vertexInputState、inputAssemblyState、viewportState、rasterizationState、multisampleState、colorBlendState

## 5. GPUGraph.h 死代码清理

- [x] 5.1 从 `GPUGraph.h` 移除 `EGraphStageType::eSubGraph` 枚举值
- [x] 5.2 移除 `GPUGraph::SubGraph()` 方法
- [x] 5.3 移除 `m_SubGraphs` 成员变量

## 6. Spec 文档更新

- [x] 6.1 更新 `specs/vulkan-backend-alignment/spec.md` 的 Phase 1 差距表，标记本次变更修复的条目，移除 Indirect/SubGraph 相关条目
- [x] 6.2 删除 `specs/vulkan-indirect-rendering/` 整个目录（API 字段不存在，独立变更处理）
- [x] 6.3 删除 `specs/vulkan-subgraph-execution/` 整个目录（独立变更处理）

## 7. 编译验证与修复

- [x] 7.1 运行 `build.py`，若编译失败则分析并修复直到 BUILD SUCCESSFUL
