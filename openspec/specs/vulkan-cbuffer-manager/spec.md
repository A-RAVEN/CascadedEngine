## Requirements

### Requirement: VulkanConstantBufferManager SHALL provide unique ShaderStruct-to-resourceId mapping

系统SHALL提供`VulkanConstantBufferManager`类，管理`VulkanShaderStruct const* → uint64_t resourceId`的唯一映射，确保同一个ShaderStruct对象在多处引用时只分配一个GPU buffer。

#### Scenario: 首次获取创建新映射
- **WHEN** 调用`GetOrCreateResourceId(pShaderStruct, resourceManager, desc)`传入一个新的ShaderStruct指针
- **THEN** 系统调用`RegisterTemporaryBuffer`注册buffer元数据到`VulkanGraphLocalResourceManager`
- **AND** 返回新分配的`uint64_t resourceId`
- **AND** 内部`shared_dic`记录该映射

#### Scenario: 重复获取返回已有映射
- **WHEN** 不同`VulkanResourceBindingInstance`对同一`ShaderStruct*`调用`GetOrCreateResourceId`
- **THEN** 返回与首次调用相同的`resourceId`
- **AND** 不重复调用`RegisterTemporaryBuffer`
- **AND** 不创建新的GPU buffer

#### Scenario: 多实例共享同一CBuffer
- **WHEN** Pass A使用shader "Mesh.hlsl"绑定TransformCB
- **AND** Pass B使用shader "Particle.hlsl"绑定同一个TransformCB对象
- **THEN** 两个Pass的DescriptorSet指向同一个`vk::Buffer`
- **AND** CBuffer上传只发生一次

---

### Requirement: VulkanConstantBufferManager SHALL be per-frame

系统SHALL在每帧结束时清空CBuffer映射，下一帧重新建立。

#### Scenario: 帧结束时清空
- **WHEN** 调用`VulkanConstantBufferManager::Clear()`
- **THEN** 所有`ShaderStruct* → resourceId`映射被清除
- **AND** 下一帧对同一ShaderStruct调用GetOrCreateResourceId会重新注册

---

### Requirement: CBuffer SHALL participate in aliasing memory allocation

系统SHALL在`AllocateAliasedResources`之前通过`RegisterTemporaryBuffer`注册CBuffer资源元数据，使CBuffer的GPU buffer通过aliasing机制分配。

#### Scenario: CBuffer在aliasing前注册
- **WHEN** `RegisterCBufferForAliasing`在`AllocateAliasedResources`之前被调用
- **THEN** 所有CBuffer的buffer描述符（大小、usage、batchIndex）被注册到`VulkanGraphLocalResourceManager`
- **AND** `AllocateAliasedResources`遍历`m_LocalResources`时包含CBuffer条目

#### Scenario: 生命周期不重叠的CBuffer共享内存
- **WHEN** CBufferA仅在batch 0使用
- **AND** CBufferB仅在batch 5使用
- **THEN** aliasing分析判定两者的生命周期不重叠
- **AND** 两者可能被分配到同一段aliased memory的不同时间窗口

---

### Requirement: BuildResources SHALL stop allocating CBuffer GPU buffers

`VulkanResourceBindingInstance::BuildResources`SHALL不再直接分配CBuffer的GPU buffer，改为从`VulkanConstantBufferManager`获取已注册的resourceId。

#### Scenario: BuildResources获取已有resourceId
- **WHEN** 调用`BuildResources(cbufferManager, ...)`
- **THEN** 对每个`CBufferBindingElement`调用`cbufferManager.GetOrCreateResourceId`获取resourceId
- **AND** resourceId存入`gpuBufferResourceId`
- **AND** 不调用`VulkanGraphLocalResourceManager::AddBuffer`

#### Scenario: BuildResources仍处理Image/Buffer fallback
- **WHEN** 调用`BuildResources(...)`
- **THEN** Image bindings的fallback注册逻辑保持不变
- **AND** Buffer bindings的fallback注册逻辑保持不变
- **AND** usingStages设置逻辑保持不变

---

### Requirement: CBuffer resource lookup SHALL use VulkanConstantBufferManager

系统SHALL删除`VulkanGraphExecutor::m_CBufferResourceIdMap`，所有CBuffer resourceId查询统一通过`VulkanConstantBufferManager`进行。

#### Scenario: PrepareBatchResourceBarriers使用Manager查询
- **WHEN** `PrepareBatchResourceBarriers`需要查找CBuffer的resourceId
- **THEN** 调用`m_ConstantBufferManager.GetResourceId(pStruct)`替代原有的`m_CBufferResourceIdMap.find(pStruct)`

#### Scenario: Reset时清理Manager
- **WHEN** 调用`VulkanGraphExecutor::Reset()`
- **THEN** `m_ConstantBufferManager.Clear()`被调用
- **AND** `m_CBufferResourceIdMap`不再存在

---

### Requirement: RegisterCBufferForAliasing SHALL execute after BuildResourceUsageRanges

系统SHALL确保`RegisterCBufferForAliasing`在`BuildResourceUsageRanges`之后、`AllocateAliasedResources`之前执行，使CBuffer生命周期数据可用时正确传播到AliasingManager。

#### Scenario: RegisterCBufferForAliasing读取已填充的lifetime
- **WHEN** `RegisterCBufferForAliasing`遍历CBuffer并查找`m_CBufferLifetimes`
- **THEN** `m_CBufferLifetimes`已被`BuildResourceUsageRanges`填充
- **AND** `m_CBufferLifetimes.find(pStruct)`返回有效迭代器
- **AND** `MarkResourceUse(resourceId, firstBatch)`被调用（firstBatch = lifeTime.begin()）
- **AND** `MarkResourceUse(resourceId, lastBatch)`被调用（lastBatch = lifeTime.rbegin()）

#### Scenario: CBuffer生命周期正确传播到aliasing
- **WHEN** CBufferA仅在batch 0-2使用，CBufferB仅在batch 3-5使用
- **THEN** aliasing分析判定两者生命周期不重叠
- **AND** 可能被分配到同一aliasing slot的不同时间窗口

---

### Requirement: BuildResources SHALL only lookup CBuffer resourceIds

`BuildResources`中CBuffer处理SHALL仅调用`GetResourceId`（lookup-only），不再调用`GetOrCreateResourceId`，因为所有CBuffer已在`RegisterCBufferForAliasing`中预注册。

#### Scenario: BuildResources仅查找已有resourceId
- **WHEN** 调用`BuildResources(cbufferManager, ...)`
- **THEN** 对每个`CBufferBindingElement`调用`cbufferManager.GetResourceId`获取resourceId
- **AND** 不调用`GetOrCreateResourceId`
- **AND** 不创建`GPUBufferDescriptor`

---

### Requirement: VulkanConstantBufferManager SHALL NOT expose IterateResources

`VulkanConstantBufferManager`SHALL不包含`IterateResources`方法，因当前无调用需求且生命周期信息已在`m_CBufferLifetimes`中维护。

#### Scenario: IterateResources已移除
- **WHEN** 查看`VulkanConstantBufferManager`的公开接口
- **THEN** `IterateResources`不在方法列表中
- **AND** 无对应的实现代码
