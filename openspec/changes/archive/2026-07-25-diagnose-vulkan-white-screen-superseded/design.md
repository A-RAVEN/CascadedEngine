## Context

`TestSimpleTriangle` 测试经过 shader 路径修复后，shader 导入成功（`Import complete. Files=15, Programs=28`），但窗口仍显示白屏。问题可能出现在从初始化到每帧渲染的 17 个环节中的任何一个，需要逐个验证。

## Goals / Non-Goals

**Goals:**
- 对 17 个可疑点逐一添加 `[DIAG-N]` 标记的诊断 fprintf
- 每个诊断输出关键状态值（布尔、指针非 null、hash 值等）
- 通过一次 `--headless 5` 运行收集所有诊断数据
- 根据诊断结果定位故障根因

**Non-Goals:**
- 不修复任何功能 bug（诊断阶段只加日志）
- 不改动任何逻辑行为
- 不修改 shader 编译流程

## Decisions

### Decision 1: 分级诊断标记 `[DIAG-N]`

所有诊断行使用 `[DIAG-N]` 格式（N=0..17），`fprintf(stderr, ...)` + `fflush(stderr)`。运行后 `grep '\[DIAG-'` 提取全部诊断输出。

### Decision 2: 诊断点列表

**初始化阶段：**

| ID | 位置 | 诊断内容 |
|----|------|---------|
| DIAG-0 | Main.cpp | DeriveProjectRoot 结果（rootPath、found flag） |
| DIAG-1 | RenderBackend_Vulkan::Init | SetSourceDirectory 是否成功（m_SourceDirectory 路径） |
| DIAG-2 | Main.cpp | assetPath 值（SetResourceRootPath 参数） |
| DIAG-3 | ShaderImporter_Vulkan::ImportResource | effectiveSourcePath + PathHash 样本（首个 shader 的 pathHash 值） |
| DIAG-4 | ShaderImporter_Vulkan::ImportResource | GetOrNewResource 前后的 ShaderLibrary 状态 |
| DIAG-5 | ResourceImportingSystem | SerializeAll 调用前 ShaderLibrary 数据量 |
| DIAG-6 | VulkanWindowHandle::Init | swapchain 参数（format, extent, imageCount, presentMode） |
| DIAG-7 | VulkanGraphLocalResourceManager | Backbuffer GetTextureView 返回的 imageView 是否有效 |

**每帧执行阶段：**

| ID | 位置 | 诊断内容 |
|----|------|---------|
| DIAG-8 | CollectShaderBindings | GetShaderFileInfo 返回值（null？pathHash 是多少？） |
| DIAG-9 | BuildPipelineStates | ShaderInfo.isValid、pFileInfo、hasVert/hasFrag、pipeline 创建结果 |
| DIAG-10 | BuildPipelineStates | RenderPassCacheKey format、renderPass/framebuffer 非 null |
| DIAG-11 | BuildPipelineStates | VertexInput 匹配（shader attributes count vs batch descriptors count） |
| DIAG-12 | RecordRenderPass | beginRenderPass 后的 batch pipeline/layout 状态 |
| DIAG-13 | RecordRenderPass | Draw call 参数（vertexCount, instanceCount） |
| DIAG-14 | SubmitBatches | 提交的 batch 数量、command buffer 状态 |
| DIAG-15 | PresentWindows | vkQueuePresentKHR 结果（success / suboptimal / outOfDate） |
| DIAG-16 | RenderBackend_Vulkan::GetShaderFileInfo | ShaderLibrary 中 m_ShaderFiles.size()、查找的 pathHash 值 |

### Decision 3: 诊断后清理

所有 `[DIAG-N]` 在定位问题后统一移除。诊断代码通过明确的 `[DIAG-N]` 标记与正常代码区分，便于后续 sed 批量删除。

## Risks / Trade-offs

- **[风险]** 诊断输出量大 → **缓解**: 用 `grep '\[DIAG-'` 过滤，只看诊断行
- **[风险]** 诊断代码可能影响时序 → **缓解**: fprintf+fflush 开销极小；headless 模式下无视觉影响
