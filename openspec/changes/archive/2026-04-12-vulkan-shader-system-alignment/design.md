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
4. 重构ShaderImporter_Vulkan，继承ResourceImporterFree，实现ImportResource目录扫描
5. 确保与D3D12后端API一致性

**Non-Goals:**
1. 不实现shader热重载
2. 不修改D3D12后端
3. 不添加compute shader以外的shader类型支持

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

// ⚠️ 生命周期注意: createInfo.pBindings 指向 bindings.data()
// 在使用 createInfo 创建 DescriptorSetLayout 前，必须确保 bindings 未被移动/重分配
// 建议在实际创建时重建 createInfo，或保证 setLayoutInfo 生命周期稳定
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

**选择**: 实现为独立函数，不属于任何类

**理由**:
- 与D3D12的ConstructShaderDescriptorInfo对应
- 生成VulkanShaderResourceBindingInfo用于descriptor set创建
- 在ImportResource中调用，而非暴露为public API

**流程**:
1. 从ShaderReflectionData提取BindingInfo
2. 遍历BindingHierarchy，收集cbuffer/image/buffer/sampler信息
3. 构建DescriptorSetLayoutBinding数组
4. 返回VulkanShaderResourceBindingInfo

### Decision 5: ShaderLibrary继承结构重构

**选择**: 将ShaderLibrary基类从VulkanSubobjectBase改为resource_management::TResource<ShaderLibrary>

**理由**:
- D3D12和旧版VulkanRenderBackend的ShaderLibrary都继承TResource
- ShaderLibrary需要作为资源被ResourceManagingSystem管理
- GetOrNewResource<ShaderLibrary>()需要TResource基类才能工作
- ShaderLibrary应该是可序列化的资源，而非Vulkan运行时子对象

**当前问题**:
- VulkanRenderBackendNew的ShaderLibrary错误地继承VulkanSubobjectBase
- 无法通过resourceManager->GetOrNewResource<ShaderLibrary>()获取
- 与D3D12和旧版Vulkan架构不一致

**正确设计**:
```cpp
// VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h
#include <CAResource/IResource.h>

class ShaderLibrary : public resource_management::TResource<ShaderLibrary>
{
public:
    // 数据成员与D3D12对齐
    castl::unordered_map<cacore::PathHash, VulkanShaderFileInfo> m_ShaderFiles;
    castl::unordered_map<cahash::sha256_hash::result_type, VulkanShaderCode> m_ShaderPrograms;
    castl::unordered_map<cacore::NameHash, ShaderCompilerSlang::ShaderStructData> m_ShaderStructs;
    castl::unordered_map<cacore::PathHash, ShaderCompilerSlang::ShaderStructData> m_ShaderRootStructs;

    // 访问器方法
    VulkanShaderFileInfo const* GetShaderFileInfo(cacore::PathHash const& pathHash) const;
    VulkanShaderCode const* GetShaderCode(cahash::sha256_hash::result_type const& shaHash) const;
    // ...
};

CA_REFLECTION(graphics_backend::ShaderLibrary
    , m_ShaderFiles
    , m_ShaderPrograms
    , m_ShaderStructs
    , m_ShaderRootStructs);
```

### Decision 6: VulkanShaderCode数据分离

**选择**: 从VulkanShaderCode中移除vk::UniqueShaderModule，仅保留可序列化的SPIR-V字节码

**理由**:
- ShaderLibrary作为TResource应该是可序列化/反序列化的
- vk::UniqueShaderModule是运行时Vulkan对象，不可序列化
- vk::ShaderModule应该从spirvCode按需创建，由PipelineLibrary管理
- 与D3D12版本一致：D3D12的ShaderCode仅存储ID3DBlob（字节码）

**当前问题**:
```cpp
struct VulkanShaderCode
{
    ECompileShaderType shaderType;
    vk::UniqueShaderModule shaderModule;    // ❌ 运行时对象，不可序列化
    castl::vector<uint32_t> spirvCode;      // ✓ 可序列化
};
```

**正确设计**:
```cpp
// 可序列化的ShaderCode（存储在ShaderLibrary中）
struct VulkanShaderCode
{
    ECompileShaderType shaderType;
    castl::vector<uint32_t> spirvCode;      // 仅保留SPIR-V字节码
};

// vk::ShaderModule的创建移至PipelineLibrary或其他运行时组件
// 按需从spirvCode创建：vk::Device::createShaderModule()
```

**影响**:
- ShaderLibrary::TryAquireShaderModule等方法需要重构或移除
- PipelineLibrary需要从spirvCode创建vk::ShaderModule

### Decision 7: ShaderImporter_Vulkan设计

**选择**: 继承ResourceImporterFree，实现ImportResource方法

**理由**:
- 与D3D12ShaderResourceImporter保持一致的架构模式
- ResourceImporterFree是资源导入的标准基类
- ImportResource由ResourceManagingSystem调用，支持批量目录扫描

**当前问题**:
- 现有ShaderImporter_Vulkan继承VulkanSubobjectBase（错误）
- 包含AI生成的fantasy接口：CompileFromSource、CompileFromSPIRV等
- 缺少ImportResource核心方法

**正确设计**:
```cpp
// ShaderImporter_Vulkan.h
#include <CAResource/ResourceImporter.h>

class ShaderImporter_Vulkan : public ResourceImporterFree
{
public:
    virtual castl::string GetTags() const override { return "Vulkan;Slang"; }
    virtual void ImportResource(
        ResourceManagingSystem* resourceManager,
        cafs::path const& sourcePath,
        cafs::path const& destPath) override;

    void SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler);
    void Test();  // 调试用

private:
    ShaderCompilerSlang::IShaderCompilerManager* m_ShaderCompilerManager = nullptr;
};

// 独立函数（非类方法）
VulkanShaderResourceBindingInfo ConstructShaderDescriptorInfo(
    const char* pathName,
    ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData);

void IterateHierarchyElements(
    ShaderCompilerSlang::ShaderReflectionData const* reflectionData,
    castl::function<void(VulkanHierarchyElement const&)> callback);
```

**ImportResource流程** (参考D3D12):
```
ImportResource(resourceManager, sourcePath, destPath)
│
├─► shaderLibrary = resourceManager->GetOrNewResource<ShaderLibrary>()
│    └─► ⚠️ 清空所有集合（避免重复导入时累积旧数据）:
│         ├─► m_ShaderPrograms.clear()
│         ├─► m_ShaderFiles.clear()
│         ├─► m_ShaderStructs.clear()
│         └─► m_ShaderRootStructs.clear()
│
├─► for each .slang file in recursive_directory_iterator(sourcePath):
│
├─► pCompiler = m_ShaderCompilerManager->AquireShaderCompilerShared()
│    └─► BeginCompileTask()
│    └─► AddSourceFile()
│    └─► SetTarget(eSpirV)  // 关键差异：Vulkan用SPIR-V
│    └─► Compile()
│    └─► GetResults()
│    └─► EndCompileTask()
│
├─► 填充 ShaderLibrary 数据:
│    └─► m_ShaderFiles[pathHash] = { reflectionData, shaderBindingInfo, entryPoints }
│    └─► m_ShaderPrograms[shaHash] = { spirvCode, shaderType }
│    └─► m_ShaderStructs[name] = structData
│    └─► m_ShaderRootStructs[pathHash] = rootStructData
│
└─► shaderBindingInfo = ConstructShaderDescriptorInfo(pathName, reflectionData)
```

**需要删除的Fantasy接口**:
| 接口 | 原因 |
|------|------|
| `CompileFromSource()` | 编译逻辑应在ImportResource内部 |
| `CompileFromSPIRV()` | 不符合资源导入流程 |
| `GetDescriptorSetLayoutBindings()` | 消费者从ShaderFileInfo获取 |
| `GetPushConstantRanges()` | 同上 |
| `CreateShaderModule()` | 应在ShaderLibrary中创建 |
| `VulkanCompiledShaderInfo` | Fantasy结构体，不需要 |
| `VulkanDescriptorBindingInfo` | 与ShaderLibrary中的重复 |

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

### Trade-off: ShaderImporter重构范围
- **选择**: 重构ShaderImporter_Vulkan完全对齐D3D12模式
- **收益**: 架构一致性，支持目录扫描导入
- **代价**: 需要删除现有fantasy接口，重写ImportResource

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
