## 1. VulkanConstantBufferManager 类创建

- [x] 1.1 创建 `VulkanRenderBackendNew/private/GPUGraph/VulkanConstantBufferManager.h`，定义类接口：`GetOrCreateResourceId(VulkanShaderStruct const*, VulkanGraphLocalResourceManager&, GPUBufferDescriptor const&)`、`GetResourceId(VulkanShaderStruct const*) const`、`Clear()`、`IterateResources(callback)` [^1]
- [x] 1.2 创建 `VulkanRenderBackendNew/private/GPUGraph/VulkanConstantBufferManager.cpp`，实现 `GetOrCreateResourceId` 内部调用 `resourceManager.RegisterTemporaryBuffer`（不调用 `AddBuffer`），通过 `shared_dic::get_or_create` 保证唯一性
- [x] 1.3 在 `VulkanConstantBufferManager` 中使用 `castl::shared_dic<VulkanShaderStruct const*, uint64_t>` 存储映射

## 2. CompileAndExecute 阶段调整

- [x] 2.1 在 `VulkanGraphExecutor` 中添加 `VulkanConstantBufferManager m_ConstantBufferManager` 成员
- [x] 2.2 新增 `VulkanGraphExecutor::RegisterCBufferForAliasing(GPUGraph const&)` 方法：遍历所有 `m_ShaderResourceInstances` 的每个 instance 的 `m_CBufferBindings`，对每个有 `pCBufferStruct` 的元素调用 `m_ConstantBufferManager.GetOrCreateResourceId(pCBufferStruct, m_LocalResourceManager, desc)`
- [x] 2.3 在 `CompileAndExecute` 中，将 `RegisterCBufferForAliasing` 插入到 `RegisterCBufferUsageStates` 之后、`AllocateAliasedResources` 之前
- [x] 2.4 确保 `RegisterCBufferForAliasing` 中传入的 `batchIndex` 覆盖所有使用该 CBuffer 的 pass（使用 `m_CBufferLifetimes` 中的生命周期信息设置 `firstUseBatch`/`lastUseBatch`）

## 3. BuildResources 重构

- [x] 3.1 修改 `VulkanResourceBindingInstance::BuildResources` 签名，新增 `VulkanConstantBufferManager&` 参数
- [x] 3.2 将 CBuffer 处理逻辑从 `AddBuffer` 调用改为 `cbufferManager.GetOrCreateResourceId` 获取 resourceId，存入 `gpuBufferResourceId`
- [x] 3.3 Image/Buffer fallback 注册和 usingStages 设置逻辑保持不变
- [x] 3.4 更新 `CompileAndExecute` 中 `BuildResources` 的调用点，传入 `m_ConstantBufferManager`

## 4. 移除 m_CBufferResourceIdMap

- [x] 4.1 删除 `VulkanGraphExecutor.h` 中的 `m_CBufferResourceIdMap` 成员声明
- [x] 4.2 删除 `CompileAndExecute` 中填充 `m_CBufferResourceIdMap` 的循环代码
- [x] 4.3 修改 `PrepareBatchResourceBarriers` 中 CBuffer 查询：将 `m_CBufferResourceIdMap.find(pStruct)` 替换为 `m_ConstantBufferManager.GetResourceId(pStruct)`
- [x] 4.4 修改 `Reset()` 中的 `m_CBufferResourceIdMap.clear()` 为 `m_ConstantBufferManager.Clear()`

## 5. Release/Reset 集成

- [x] 5.1 在 `VulkanConstantBufferManager` 中添加 `Release()` 方法（当前只需 `Clear()`，无需释放 GPU 资源——GPU 资源由 `LocalResourceManager` 管理）
- [x] 5.2 在 `VulkanGraphExecutor::Release()` 中调用 `m_ConstantBufferManager.Release()`
- [x] 5.3 在 `VulkanGraphExecutor::Reset()` 中确保 `m_ConstantBufferManager.Clear()` 被调用（替换原来的 `m_CBufferResourceIdMap.clear()`）

## 6. 验证与对齐检查

- [x] 6.1 对照 D3D12 `GPUConstantBufferManager`，确认接口功能对齐（`GetOrCreateResourceId` ↔ `GetConstantBufferHandle`，`Clear` ↔ `Clear`，`IterateResources` ↔ `IterateResources`）[^1]
- [x] 6.2 更新 `Documents/Vulkan后端与D3D12后端对齐分析.md` 中 1.2 节 GPUConstantBufferManager 从 "部分对齐" 改为 "已对齐"

## 7. 修复项（审查发现）

- [x] 7.1 **修复 RegisterCBufferForAliasing 排序**：将 `RegisterCBufferForAliasing` 从 `CompileAndExecute` 中 `Prepare` 之后移到 `BuildResourceUsageRanges` 之后、`AllocateAliasedResources` 之前，使 `m_CBufferLifetimes` 在 `MarkResourceUse` 调用时已被填充
- [x] 7.2 **确保 CBuffer lifetime 正确传播到 AliasingManager**：`RegisterCBufferForAliasing` 中从 `m_CBufferLifetimes` 读取 `lifeTime.begin()` 和 `lifeTime.rbegin()`，调用 `m_LocalResourceManager.MarkResourceUse(resourceId, firstBatch)` 和 `MarkResourceUse(resourceId, lastBatch)`，使 aliasing 分析获得正确的使用范围
- [x] 7.3 **移除 IterateResources 死代码**：从 `VulkanConstantBufferManager.h/.cpp` 中删除 `IterateResources` 方法声明和实现（当前无调用点，且 `CBufferLifetimes` 生命周期信息已在 `m_CBufferLifetimes` 中维护，迭代需求不存在）
- [x] 7.4 **消除 CBuffer 描述符创建重复**：将 `GPUBufferDescriptor` 创建逻辑（`GPUBufferDescriptor::Create(1, bufferSize)`）统一到一处——在 `BuildResources` 中移除，仅保留 `RegisterCBufferForAliasing` 中的创建逻辑，`BuildResources` 仅通过 `GetResourceId` 查找已有的 resourceId（不再调用 `GetOrCreateResourceId`，因为所有 CBuffer 已在 `RegisterCBufferForAliasing` 中注册）

[^1]: `IterateResources` 在任务 7.3 中作为死代码被移除——Vulkan 端无相应调用需求，CBuffer 生命周期由 `m_CBufferLifetimes` 管理。
