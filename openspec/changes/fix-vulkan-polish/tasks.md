## 1. Descriptor Pool 生命周期约束记录

- [ ] 1.1 在 `VulkanFrameContext::Aquire()` 中 `vkResetDescriptorPool` 调用处添加注释，说明该操作隐式释放池内所有 descriptor set，新分配的 set 内容为空，每帧必须全量重写——这是 descriptor 写入缓存不可行的根本原因
- [ ] 1.2 在 `VulkanResourceBindingInstance::BuildDescriptors()` 头部添加注释，说明当前每帧全量重建所有 binding 的 `vkWriteDescriptorSet` 是因为 FrameContext 每帧 reset pool 导致 descriptor set 内容为空白（空 set 不含任何有效描述），并标注 TODO: future work — 若引入跨帧 descriptor set 复用（VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT + vkFreeDescriptorSets），可在此处实现写入缓存
- [ ] 1.3 在 `VulkanResourceBindingInstance.h` 类定义处添加文档注释，说明 descriptor 写入缓存的设计约束与 future work 方向（三种可选方案：FREE_DESCRIPTOR_SET_BIT、跨帧复永、多 pool 轮转）

## 2. Debug 命名

- [ ] 2.1 检查 `VulkanApp` 初始化流程中 `VK_EXT_debug_utils` 扩展的启用状态，添加 `m_DebugUtilsEnabled` 标志
- [ ] 2.2 在 `VulkanMemoryManager::CreateBuffer()` / `CreateImage()` 中调用 `SetVKObjectDebugName`（若扩展启用）
- [ ] 2.3 在 `VulkanGraphLocalResourceManager` 的 buffer/image 创建点调用 `SetVKObjectDebugName`（若扩展启用）
- [ ] 2.4 在 `VulkanPipelineLibrary` 各 `Create*Pipeline()` / `LinkPipeline()` 方法中调用 `SetVKObjectDebugName`，传入 shader 名称
- [ ] 2.5 在 `VulkanCommandListManager` 的 `GraphicsCommand()` / `ComputeCommand()` / `TransferCommand()` 中，分配 command buffer 后调用 `SetVKObjectDebugName`
- [ ] 2.6 在 `VulkanResourceBindingInstance::AllocateDescriptorSets()` 中，为每个分配的 descriptor set 调用 `SetVKObjectDebugName`
- [ ] 2.7 在 `VulkanPipelineLibrary` 各 `CreateShaderModule()` 或 shader 编译路径中，为 `vk::ShaderModule` 调用 `SetVKObjectDebugName`，名称格式 `"ShaderMod:<shader_name>:<stage>"`
- [ ] 2.8 在 `VulkanGraphLocalResourceManager` 的 framebuffer 创建点，为 `vk::Framebuffer` 调用 `SetVKObjectDebugName`，名称格式 `"Framebuf:<render_pass_name>"`
- [ ] 2.9 在 `VulkanGraphLocalResourceManager` 的 render pass 创建点，为 `vk::RenderPass` 调用 `SetVKObjectDebugName`，名称格式 `"RenderPass:<name>"`

## 3. PipelineLibrary Hash 查找

- [ ] 3.1 在 `VulkanPipelineLibrary.h` 中定义 `PipelineLibraryKeyHash` functor，从三个 `VKHashVal`（各 32-byte SHA-256）各取首 8-byte XOR 得 `size_t`；同时定义 `PipelineLibraryKeyEqual` functor（调用 `PipelineLibraryKey::operator==` 做完整三字段 compare），作为 `unordered_map` 的 `key_eq` 回退
- [ ] 3.2 将 `m_LibraryCache` 类型从 `castl::vector<castl::pair<...>>` 改为 `castl::unordered_map<PipelineLibraryKey, PipelineLibraryParts, PipelineLibraryKeyHash, PipelineLibraryKeyEqual>`
- [ ] 3.3 更新 `GetCachedLibrary()` 使用 `unordered_map::find()` 替代线性搜索
- [ ] 3.4 更新 `CacheLibrary()` 使用 `unordered_map::insert()` 或 `operator[]` 替代 `push_back()`
- [ ] 3.5 在 `PipelineLibraryKeyHash` 实现处添加注释，说明 64-bit hash 的碰撞风险（birthday bound ~2^32）及 `PipelineLibraryKeyEqual` 完整 key compare 回退机制的安全网作用

## 4. CommandList Handle 泄漏修复

- [ ] 4.1 审查 `VulkanCommandListManager::Reset()` 当前实现：确认 `m_GraphicsCommand = nullptr` / `m_ComputeCommand = nullptr` / `m_TransferCommand = nullptr` 置零后，下次 `Command()` 调用 `allocateCommandBuffers` 重新分配新 handle 的泄漏路径
- [ ] 4.2 修改 `Reset()`：移除对 `m_GraphicsCommand`、`m_ComputeCommand`、`m_TransferCommand` 的置零操作。`vkResetCommandPool(pool, 0)` 已将池内所有 command buffer 重置到 initial 状态，句柄仍然有效。保留 reset 后的句柄以便复用
- [ ] 4.3 修改 `GraphicsCommand()`、`ComputeCommand()`、`TransferCommand()` 的懒加载逻辑：原 `if (!m_GraphicsCommand)` 判空改为状态标志机——添加 `m_GraphicsCommandAllocated` 等 bool 成员，Init() 时设为 false，首次 allocate 后设为 true，Reset() 后保持 true 但表示已 reset 状态需重新 begin
- [ ] 4.4 在 `Init()` 中初始化新增的状态标志（`m_GraphicsCommandAllocated` 等），在 `Destroy()` 中调用 `vkFreeCommandBuffers` 释放所有已分配的 command buffer handle（而非依赖 pool 销毁隐式释放）
- [ ] 4.5 在 `Reset()` 方法头部添加注释和 `CA_ASSERT`（若 `CA_ASSERT_BREAK` 可用），说明调用方必须在 GPU fence 等待完成后才能调用，否则 `vkResetCommandPool` 在 GPU 执行中 reset 是未定义行为
- [ ] 4.6 确认 `VulkanGraphExecutor` 帧循环中 fence 等待在 `Reset()` 之前完成，若不满足则修复调用顺序

## 5. 编译验证

- [ ] 5.1 运行 `build.bat`，验证 BUILD SUCCESSFUL
- [ ] 5.2 若有编译错误或警告，分析并修复后重新验证
