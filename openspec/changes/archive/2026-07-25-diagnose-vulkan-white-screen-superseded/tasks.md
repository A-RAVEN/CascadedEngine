## 1. 初始化阶段诊断

- [ ] 1.1 [DIAG-0] Main.cpp: DeriveProjectRoot 后输出 rootPath 和 found flag
- [ ] 1.2 [DIAG-1] RenderBackend_Vulkan::Init: SetSourceDirectory 后输出 m_SourceDirectory 路径
- [ ] 1.3 [DIAG-2] Main.cpp: SetResourceRootPath 后输出 assetPath
- [ ] 1.4 [DIAG-3] ShaderImporter_Vulkan::ImportResource: 首个 .slang 文件输出 effectiveSourcePath + 相对路径 + PathHash 值
- [ ] 1.5 [DIAG-4] ShaderImporter_Vulkan::ImportResource: GetOrNewResource 前后输出 ShaderLibrary m_ShaderFiles.size()
- [ ] 1.6 [DIAG-5] ResourceImportingSystem: SerializeAll 前输出 ShaderLibrary 数据量（files/programs/structs 数量）
- [ ] 1.7 [DIAG-6] VulkanWindowHandle::Init: swapchain 创建后输出 format、extent、imageCount、presentMode
- [ ] 1.8 [DIAG-7] VulkanGraphLocalResourceManager::GetTextureView: Backbuffer 类型返回时输出 imageView 是否为 VK_NULL_HANDLE

## 2. 每帧执行阶段诊断

- [ ] 2.1 [DIAG-8] CollectShaderBindings: GetShaderFileInfo 返回后输出是否为 null + pathHash 值
- [ ] 2.2 [DIAG-9] BuildPipelineStates: 每个 batch 输出 shaderInfo.isValid、pFileInfo!=null、hasVert/hasFrag、pipeline!=null
- [ ] 2.3 [DIAG-10] BuildPipelineStates: renderPass colorFormats、renderPass!=null、framebuffer!=null
- [ ] 2.4 [DIAG-11] BuildPipelineStates: vertex reflection attributes count vs batch input descriptors count
- [ ] 2.5 [DIAG-12] RecordRenderPass: beginRenderPass 后每个 batch 输出 pipeline + layout 状态
- [ ] 2.6 [DIAG-13] RecordRenderPass: draw call 输出 vertexCount + instanceCount
- [ ] 2.7 [DIAG-14] SubmitBatches: 输出 batch 总数 + 每个 batch 的 command buffer 状态
- [ ] 2.8 [DIAG-15] PresentWindows: vkQueuePresentKHR 结果（success / suboptimal / outOfDate）
- [ ] 2.9 [DIAG-16] RenderBackend_Vulkan::GetShaderFileInfo: 输出 m_ShaderFiles.size() + 查询的 pathHash + 是否找到

## 3. 编译 + 收集诊断

- [ ] 3.1 运行 `python build.py` 确认编译通过
- [ ] 3.2 运行 `--backend vulkan --test TestSimpleTriangle --headless 5` 并 `grep '\[DIAG-'` 提取所有诊断输出
- [ ] 3.3 分析诊断结果，定位根因

## 4. 清理诊断代码

- [ ] 4.1 `sed -i '/\[DIAG-[0-9]\]/d'` 移除所有诊断行
- [ ] 4.2 编译验证 + 提交
