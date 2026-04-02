## Context

### 当前状态

VulkanRenderBackendNew的Shader系统存在以下架构差距：

| 模块 | D3D12 | Vulkan (当前) |
|------|-------|---------------|
| ShaderLibrary | 完整数据结构 (m_ShaderFiles, m_ShaderPrograms, m_ShaderStructs, m_ShaderRootStructs) | 仅缓存ShaderModule |
| ShaderImporter | ResourceImporterFree模式，目录扫描导入 | 仅编译功能 |
| ShaderResourceBindingInfo | 完整的绑定信息结构 + Root Signature序列化 | 不存在 |
| ShaderStruct | 完整实现，版本控制，staging buffer | 待实现的PendingUpdate |

### 约束

1. 不修改D3D12RenderBackend代码
2. 不改变RenderInterface抽象接口
3. 必须与D3D12后端保持接口一致性
4. 使用ShaderCompilerSlang::IShaderCompilerManager进行shader编译

### 相关方

- 渲染工程师：使用ShaderStruct API设置shader参数
- GPUGraph系统：通过VulkanResourceBindingInstance绑定资源
- PipelineLibrary：需要ShaderModule和descriptor set布局

## Goals / Non-Goals

**Goals:**
1. 扩展Vulkan ShaderLibrary，支持shader文件/程序/结构体的完整管理
2. 实现VulkanShaderResourceBindingInfo，从反射数据构建descriptor set布局信息
3. 完善VulkanShaderStruct，实现版本控制和uniform buffer staging
4. 确保与D3D12后端API一致性

**Non-Goals:**
1. 不实现shader热重载
2. 不实现目录扫描导入（作为独立后续任务）
3. 不修改D3D12后端
4. 不添加compute shader以外的shader类型支持

## Decisions

### Decision 1: VulkanShaderResourceBindingInfo结构设计

**选择**: 创建独立的VulkanShaderResourceBindingInfo结构，不直接复用D3D12的ShaderResourceBindingInfo

**理由**:
- D3D12使用Root Signature概念，Vulkan使用Pipeline Layout
- D3D12使用Descriptor Heap，Vulkan使用Descriptor Set
- 两者的descriptor类型映射不同
- 分离结构避免D3D12依赖污染Vulkan后端

**替代方案**:
- 方案A: 模板化共享结构 → 拒绝：增加复杂度，收益有限
- 方案B: 继承基类 → 拒绝：Vulkan和D3D12绑定模型差异太大

**VulkanShaderResourceBindingInfo结构**:
```cpp
struct VulkanShaderResourceBindingInfo {
    // Descriptor counts
    uint32_t totalDescriptorCount;
    uint32_t samplerDescriptorCount;

    // Per-set layout info
    castl::vector<VulkanDescriptorSetLayoutInfo> setLayoutInfos;

    // Binding info by type (mirrors D3D12 structure)
    castl::vector<VulkanCBufferBindingInfo> cbufferInfos;
    castl::vector<VulkanImageBindingInfo> imageInfos;
    castl::vector<VulkanBufferBindingInfo> bufferInfos;
    castl::vector<VulkanSamplerBindingInfo> samplerInfos;

    // Struct hierarchy info
    castl::vector<VulkanStructBindingInfo> structBindingInfos;
};

struct VulkanDescriptorSetLayoutInfo {
    uint32_t setIndex;
    castl::vector<vk::DescriptorSetLayoutBinding> bindings;
    vk::DescriptorSetLayoutCreateInfo createInfo;
};
```

### Decision 2: ShaderLibrary数据结构扩展

**选择**: 在Vulkan ShaderLibrary中添加与D3D12相同的数据成员

**理由**:
- 保持两个后端数据结构一致
- 便于跨后端工具和调试
- 已有的ShaderModule缓存逻辑不受影响

**数据成员**:
```cpp
class ShaderLibrary : public VulkanSubobjectBase {
public:
    // 新增成员（与D3D12对齐）
    castl::unordered_map<cacore::PathHash, VulkanShaderFileInfo> m_ShaderFiles;
    castl::unordered_map<cahash::sha256_hash::result_type, VulkanShaderCode> m_ShaderPrograms;
    castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
    castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;

    // 现有缓存逻辑
    bool TryAquireShaderModule(...);
    // ...
};
```

### Decision 3: VulkanShaderStruct实现策略

**选择**: 参考D3D2ShaderStruct实现，添加版本控制和staging buffer

**理由**:
- D3D2ShaderStruct已经过验证
- 版本控制支持增量更新
- Staging buffer支持uniform buffer批量上传

**关键实现**:
```cpp
class VulkanShaderStruct : public ShaderStruct, public VulkanSubobjectBase {
private:
    // 版本控制
    uint64_t m_Version = 0;
    mutable uint64_t m_MaxChildrenVersion = 0;

    // Uniform buffer staging
    castl::vector<uint8_t> m_StructLocalUniformStagingBuffer;
    castl::unordered_map<cacore::NameHash, uint32_t> m_NameToUniformElementMetaID;
    castl::vector<uint64_t> m_UniformElementOffsetInStagingBuffer;

    // Resource handles
    castl::unordered_map<cacore::NameHash, castl::vector<castl::shared_ptr<VulkanShaderStruct>>> m_NameToSubStructs;
    castl::unordered_map<cacore::NameHash, castl::vector<ImageHandle>> m_NameToImageHandles;
    castl::unordered_map<cacore::NameHash, castl::vector<TextureSamplerDescriptor>> m_NameToSamplerDescriptors;
    castl::unordered_map<cacore::NameHash, castl::vector<BufferHandle>> m_NameToBufferHandles;

    // Struct data reference
    ShaderCompilerSlang::ShaderStructData const* p_StructData;
};
```

### Decision 4: ConstructShaderDescriptorInfo实现

**选择**: 在ShaderImporter_Vulkan中添加ConstructShaderDescriptorInfo函数

**理由**:
- 与D3D12的ConstructShaderDescriptorInfo对应
- 复用现有的ExtractBindingsFromReflection逻辑
- 生成VulkanShaderResourceBindingInfo用于descriptor set创建

**流程**:
1. 从ShaderReflectionData提取BindingInfo
2. 遍历BindingHierarchy，收集cbuffer/image/buffer/sampler信息
3. 构建DescriptorSetLayoutBinding数组
4. 返回VulkanShaderResourceBindingInfo

## Risks / Trade-offs

### Risk 1: 数据结构复制导致维护成本增加
- **风险**: Vulkan和D3D12各自维护类似的数据结构
- **缓解**: 两个后端独立演进，通过RenderInterface保证接口一致性

### Risk 2: VulkanShaderStruct实现复杂度
- **风险**: VulkanShaderStruct实现可能引入bug
- **缓解**: 参考已验证的D3D2ShaderStruct实现，编写单元测试

### Risk 3: Descriptor Set布局兼容性
- **风险**: 不同shader的descriptor set布局可能不兼容
- **缓解**: 使用相同的binding space映射策略，确保布局一致性

### Trade-off: 不实现目录扫描
- **权衡**: 当前仅支持手动编译，不支持自动目录扫描
- **收益**: 减少实现范围，专注于核心功能
- **代价**: 用户需要手动管理shader编译流程

## Migration Plan

### Phase 1: 数据结构定义
1. 定义VulkanShaderResourceBindingInfo及相关结构
2. 扩展ShaderLibrary数据成员

### Phase 2: 构建逻辑实现
1. 实现ConstructShaderDescriptorInfo
2. 修改ShaderImporter_Vulkan调用流程

### Phase 3: VulkanShaderStruct完善
1. 实现版本控制机制
2. 实现uniform buffer staging
3. 实现完整的Set*Internal方法

### Phase 4: 集成测试
1. 更新VulkanGraphExecutor使用新的binding info
2. 更新VulkanResourceBindingInstance
3. 验证渲染管线正确性

## Open Questions

1. **Q**: VulkanShaderResourceBindingInfo是否需要缓存PipelineLayout？
   - **A**: 是，缓存PipelineLayout避免重复创建

2. **Q**: Uniform buffer使用VMA还是自定义分配器？
   - **A**: 使用现有的VulkanMemoryManager (VMA封装)

3. **Q**: 是否需要支持descriptor indexing (VK_EXT_descriptor_indexing)？
   - **A**: 作为后续任务，当前不实现
