## ADDED Requirements

### Requirement: 渲染管线的逐点诊断框架

Vulkan 后端 SHALL 在 TestSimpleTriangle 的完整调用链上支持逐点诊断，通过带编号的 `[DIAG-N]` 标记 (N=0..16) 输出每个关键环节的状态，以便在无声故障（白屏、无 validation error）时快速定位问题。

#### Scenario: 单次 headless 运行收集所有诊断
- **WHEN** 测试以 `--backend vulkan --test TestSimpleTriangle --headless 5` 运行
- **THEN** stderr 输出包含 17 个 `[DIAG-N]` 标记的诊断行
- **AND** 通过 `grep '\[DIAG-'` 可提取所有诊断数据

#### Scenario: 诊断标记不改变渲染行为
- **WHEN** 所有 `[DIAG-N]` 诊断代码被添加
- **THEN** 渲染结果与添加前完全相同（fprintf/fflush 不改变任何 GPU 状态或逻辑分支）

---

### Requirement: 诊断覆盖初始化阶段

初始化阶段的诊断 SHALL 覆盖：

#### Scenario: 项目根推导验证
- **WHEN** `DeriveProjectRoot()` 执行完成
- **THEN** `[DIAG-0]` 输出 rootPath、found flag

#### Scenario: 源目录回退验证
- **WHEN** `RenderBackend_Vulkan::Init()` 中 `SetSourceDirectory` 调用完成
- **THEN** `[DIAG-1]` 输出 m_SourceDirectory 路径或 "not set" 状态

#### Scenario: 产出路径验证
- **WHEN** `SetResourceRootPath(ctx.assetPath)` 调用完成
- **THEN** `[DIAG-2]` 输出 assetPath 值

#### Scenario: PathHash 一致性验证
- **WHEN** `ImportResource` 处理首个 .slang 文件
- **THEN** `[DIAG-3]` 输出 effectiveSourcePath 和首个 shader 的相对路径及 PathHash 值

#### Scenario: ShaderLibrary 缓存状态验证
- **WHEN** `ImportResource` 调用 `GetOrNewResource` 前后
- **THEN** `[DIAG-4]` 输出 ShaderLibrary 的 m_ShaderFiles.size()

#### Scenario: Swapchain 创建验证
- **WHEN** `VulkanWindowHandle::Init` 完成 swapchain 创建
- **THEN** `[DIAG-6]` 输出 format、extent、imageCount、presentMode

#### Scenario: Backbuffer ImageView 验证
- **WHEN** `GetTextureView` 为 Backbuffer 类型返回 imageView
- **THEN** `[DIAG-7]` 输出 imageView 是否为 VK_NULL_HANDLE

---

### Requirement: 诊断覆盖每帧执行阶段

每帧执行阶段的诊断 SHALL 覆盖：

#### Scenario: GetShaderFileInfo 验证
- **WHEN** `CollectShaderBindings` 或 `BuildPipelineStates` 中调用 `GetShaderFileInfo`
- **THEN** `[DIAG-8]` 输出返回值是否为 null、ShaderLibrary 中 m_ShaderFiles 总大小、查询的 pathHash
- **THEN** `[DIAG-16]` 在 `RenderBackend_Vulkan::GetShaderFileInfo` 中输出同样信息

#### Scenario: Pipeline 创建验证
- **WHEN** `BuildPipelineStates` 完成每个 pass/batch 的 pipeline 创建
- **THEN** `[DIAG-9]` 输出 shaderInfo.isValid、pFileInfo!=null、hasVert/hasFrag、pipeline!=null

#### Scenario: RenderPass 兼容性验证
- **WHEN** `BuildPipelineStates` 中构建 renderPass cache key
- **THEN** `[DIAG-10]` 输出 renderPass colorFormats、depthFormat、创建的 renderPass/framebuffer 是否为 VK_NULL_HANDLE

#### Scenario: Vertex Input 匹配验证
- **WHEN** `BuildPipelineStates` 构建 vertex input state
- **THEN** `[DIAG-11]` 输出 shader reflection attributes count、batch vertex input descriptors count

#### Scenario: Draw call 验证
- **WHEN** `RecordRenderPass` 在 beginRenderPass 后遍历 draw call batches
- **THEN** `[DIAG-12]` 输出每个 batch 的 pipeline 是否为 VK_NULL_HANDLE、pipelineLayout 是否为 VK_NULL_HANDLE
- **THEN** `[DIAG-13]` 输出 draw(vertexCount, instanceCount) 参数

#### Scenario: Submit 验证
- **WHEN** `SubmitBatches` 提交所有 batch
- **THEN** `[DIAG-14]` 输出 batch 总数、每个 batch 的 command buffer 状态

#### Scenario: Present 验证
- **WHEN** `PresentWindows` 调用 vkQueuePresentKHR
- **THEN** `[DIAG-15]` 输出 present 结果（success / suboptimal / outOfDate / error）
