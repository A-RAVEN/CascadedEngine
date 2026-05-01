## 1. 前置修正 — CBuffer使用状态注册

- [x] 1.1 在`CollectShaderBindings`完成后、`BuildDependencyFreeBatches`之前，遍历`m_ShaderResourceInstances`，对每个`VulkanResourceBindingInstance`的`m_CBufferBindings`，调用`VulkanExecutorRWState::SetCBufferUsageState(pCBufferStruct, stageFlags, queueType)`，使`m_CBufferLifetimes`被正确填充
- [x] 1.2 确保compute pass的CBuffer binding同样被注册到`m_CBufferLifetimes`

## 2. CBufferBindingElement 扩展与 GetCBufferSize 修正

- [x] 2.1 在CBufferBindingElement结构体中添加`uint64_t gpuBufferResourceId = 0`字段（LocalResourceManager 临时资源标识），初始值为0（无效）
- [x] 2.2 修改VulkanShaderStruct::GetCBufferSize()，将`p_StructData->m_StructUniforms.m_MemorySize`对齐到256字节后返回（用于GPU buffer分配和DescriptorBufferInfo的range）

## 3. BuildResources 实现

- [x] 3.1 修改BuildResources签名为`void BuildResources(VulkanGraphLocalResourceManager& resourceManager, GPUGraph const& graph)`
- [x] 3.2 实现CBuffer GPU buffer分配：遍历m_CBufferBindings，对每个有pCBufferStruct的元素，调用`VulkanGraphLocalResourceManager::AddBuffer(desc, usage, batchIndex)`分配GPU buffer（desc.size使用GetCBufferSize()对齐后的大小，usage为`eUniformBuffer`），将返回的`uint64_t resourceId`存入`CBufferBindingElement::gpuBufferResourceId`。同时将该pair插入`VulkanGraphExecutor::m_CBufferResourceIdMap`。注意：`AddBuffer`是本次新增方法，内部调用`RegisterTemporaryBuffer`并返回resourceId
- [x] 3.3 在BuildResources中遍历m_ImageBindings，对每个有有效ImageHandle的元素，若`LocalResourceManager.GetTexture(imageHandle)`返回空（未注册），则通过`GetDescriptor(graph, imageHandle)`获取descriptor并调用`RegisterTemporaryTexture`注册。External/Backbuffer资源通常已可直接解析，internal资源已在CollectResources中注册，此步骤作为兜底
- [x] 3.4 在BuildResources中遍历m_BufferBindings，对每个有有效BufferHandle的元素，若`LocalResourceManager.GetBuffer(bufferHandle)`返回空，则通过`GetDescriptor(graph, bufferHandle)`获取descriptor并调用`RegisterTemporaryBuffer`+`RegisterBufferHandle`注册
- [x] 3.5 在BuildResources中为每个binding element设置usingStages，调用pShaderFileInfo->GetShaderStageUsage(bindingInfo.usageMask)

## 4. DescriptorPool 创建

- [x] 4.1 在VulkanGraphExecutor::CompileAndExecute中（Prepare阶段之后、BuildDescriptors之前），若`m_DescriptorPool`非空则先`destroy`旧pool
- [x] 4.2 遍历所有m_ShaderResourceInstances统计各类descriptor（uniform buffer、sampled image、storage buffer、storage image、sampler）的总数量
- [x] 4.3 使用统计结果创建vk::DescriptorPool，包含VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT，maxSets为BindingInstance数量乘以每实例最大set数，存入m_DescriptorPool
- [x] 4.4 在`Reset()`中确保`m_DescriptorPool`被销毁（避免异常路径泄漏）

## 5. BuildDescriptors 实现

- [x] 5.1 修改BuildDescriptors签名为`void BuildDescriptors(VulkanGraphExecutor& executor, vk::DescriptorPool pool)`
- [x] 5.2 实现DescriptorSet分配：遍历pShaderFileInfo->shaderBindingInfo.setLayoutInfos，按setIndex排序，对每个set通过`executor.GetDescriptorSetLayoutCache()`查找已缓存的vk::DescriptorSetLayout，构建`(setIndex, layout)` pair数组。调用AllocateDescriptorSets(m_DescriptorPool, pairs)，内部按`setIndex`作为key存入m_DescriptorSets（而非数组索引）
- [x] 5.3 实现CBuffer描述符写入：遍历m_CBufferBindings，对每个有gpuBufferResourceId的元素，通过`executor.GetLocalResourceManager().GetBuffer(resourceId)`获取vk::Buffer，创建vk::DescriptorBufferInfo（offset=0, range=GetCBufferSize()），调用SetUniformBuffer写入DescriptorSet
- [x] 5.4 实现Image描述符写入：遍历m_ImageBindings，对每个有有效binding的元素，通过`executor.GetLocalResourceManager().GetTextureView(imageHandle)`获取vk::ImageView，调用SetSampledImage或SetStorageImage写入DescriptorSet
- [x] 5.5 实现Buffer描述符写入：遍历m_BufferBindings，对每个有有效binding的元素，通过`executor.GetLocalResourceManager().GetBuffer(bufferHandle)`获取vk::Buffer，调用SetStorageBuffer写入DescriptorSet
- [x] 5.6 实现Sampler描述符写入：遍历m_SamplerBindings，对每个有samplerDescriptor的元素，从TextureSamplerDescriptor创建vk::Sampler，调用SetSampler写入DescriptorSet
- [x] 5.7 所有binding处理完毕后，调用UpdateDescriptorSets提交m_PendingWrites

## 6. CBuffer初始化上传

- [x] 6.1 在PrepareBatchResourceBarriers的CBuffer循环中，通过`m_CBufferResourceIdMap[pStruct]`查找resourceId，将`pair<uint64_t, VulkanShaderStruct const*>`添加到对应batch的cbufferBarriers（或computeCBufferBarriers）
- [x] 6.2 在RecordBatchCommands中，处理batch的cbufferBarriers：对每个`(uint64_t resourceId, VulkanShaderStruct* pStruct)`对，创建staging buffer（VMA分配，HOST_VISIBLE | HOST_COHERENT），Map后调用pStruct->ComputeMaxChildrenVersion()和UpdateUniformBuffer(0, mappedPtr, bufferSize, 0)写入数据，Unmap
- [x] 6.3 在RecordBatchCommands中，执行vkCmdCopyBuffer从staging拷贝到目标GPU buffer（目标buffer通过`m_LocalResourceManager.GetBuffer(resourceId)`获取）
- [x] 6.4 在vkCmdCopyBuffer之后，插入vk::BufferMemoryBarrier（srcStage=transfer, srcAccess=transferWrite, dstStage=vertexShader|fragmentShader|computeShader, dstAccess=uniformRead），确保上传数据对shader可见
- [x] 6.5 将staging buffer添加到m_PendingStagingBuffers追踪，帧结束后由CleanupStagingBuffers回收

## 7. 调用链串联

- [x] 7.1 在VulkanGraphExecutor::CompileAndExecute中，AllocateAliasedResources之后、PrepareBatchResourceBarriers**之前**添加BuildResources调用：遍历m_ShaderResourceInstances，对每个调用`BuildResources(m_LocalResourceManager, graph)`
- [x] 7.2 在BuildPipelineStates之后添加BuildDescriptors调用：遍历m_ShaderResourceInstances，对每个调用`BuildDescriptors(*this, m_DescriptorPool)`（传入executor自身）
- [x] 7.3 修改RecordRenderPass中的bindDescriptorSets调用：从仅绑定set 0改为按连续set index区间分组绑定所有已分配的DescriptorSet。遍历m_DescriptorSets，将连续的set聚合成批次，对每个批次调用`cmdBuf.bindDescriptorSets(pipelineBindPoint, pipelineLayout, firstSet, sets, ...)`
- [x] 7.4 修改RecordComputePass中的bindDescriptorSets调用：同上，按连续区间分组绑定所有DescriptorSet

## 8. 审查发现的遗留问题 (2026-04-30)

- [x] 8.1 **Image fallback 注册缺少 RegisterTextureHandle** — [VulkanResourceBindingInstance.cpp:267](VulkanResourceBindingInstance.cpp#L267)。BuildResources 的 image fallback 路径只调用了 `RegisterTemporaryTexture` 但丢弃了返回值，缺少对应的 `RegisterTextureHandle` 调用。对比 Buffer fallback（line 283-284）正确地调用了 `RegisterBufferHandle`。这导致 fallback 注册的 image 的 `ImageHandle → resourceId` 映射永远不会建立，后续 `GetTextureView(ImageHandle)` 查询将返回 null
- [x] 8.2 **CombinedImageSampler 使用零初始化默认 Sampler** — [VulkanResourceBindingInstance.cpp:379-382](VulkanResourceBindingInstance.cpp#L379-L382)。vk::SamplerCreateInfo 全字段都是零值（magFilter/minFilter/addressMode 等），可能触发 Validation Layer 报错
- [x] 8.3 **TextureSamplerDescriptor 未映射到 vk::SamplerCreateInfo** — [VulkanResourceBindingInstance.cpp:407-409](VulkanResourceBindingInstance.cpp#L407-L409)。sampler.samplerDescriptors[0] 被读取但未将其 filter/anisotropy/addressMode 等参数映射到 vk::SamplerCreateInfo
- [x] 8.4 **SetUniformBuffer 在 key 不存在时 operator[] 静默插入 null DescriptorSet** — [VulkanResourceBindingInstance.cpp:423](VulkanResourceBindingInstance.cpp#L423)。若某个 binding 的 `spaceID` 在 AllocateDescriptorSets 中未被分配，`m_DescriptorSets[set]` 会插入默认值 null，传给 vkUpdateDescriptorSets 会导致未定义行为
- [x] 8.5 **RegisterCBufferUsageStates 中的死代码** — [VulkanGraphExecutor.cpp:573-574](VulkanGraphExecutor.cpp#L573-L574)。`pShaderFileInfo` 变量被赋值但从未被使用，循环体中只用到了 `cbuffer.pCBufferStruct`
