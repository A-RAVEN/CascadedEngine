## 0. ShaderLibrary Base Class Refactoring (Prerequisite)

- [x] 0.1 [P] 修改ShaderLibrary基类：VulkanSubobjectBase → resource_management::TResource<ShaderLibrary>
- [x] 0.2 [P] 从VulkanShaderCode中移除vk::UniqueShaderModule成员
- [x] 0.3 添加CA_REFLECTION宏支持序列化
- [x] 0.4 移除或重构TryAquireShaderModule/ShaderModuleCacheValid等运行时方法
- [x] 0.5 确保ShaderLibrary可通过GetOrNewResource<ShaderLibrary>()获取

## 1. Data Structure Definitions

- [x] 1.1 [P] Define VulkanDescriptorSetLayoutInfo in ShaderLibrary.h
- [x] 1.2 [P] Define VulkanCBufferBindingInfo, VulkanImageBindingInfo, VulkanBufferBindingInfo, VulkanSamplerBindingInfo
- [x] 1.3 [P] Define VulkanStructBindingInfo with sub-struct references
- [x] 1.4 Define VulkanShaderResourceBindingInfo aggregating all binding info types
- [x] 1.5 [P] Define VulkanShaderFileInfo with reflectionData, bindingInfo, entryPoints
- [x] 1.6 [P] Define VulkanShaderCode with vk::ShaderModule, stage, spirvCode

## 2. ShaderLibrary Extension

- [x] 2.1 Add m_ShaderFiles (PathHash → VulkanShaderFileInfo) to ShaderLibrary
- [x] 2.2 Add m_ShaderPrograms (SHA256Hash → VulkanShaderCode) to ShaderLibrary
- [x] 2.3 Add m_ShaderStructs (NameHash → ShaderStructData) to ShaderLibrary
- [x] 2.4 Add m_ShaderRootStructs (PathHash → ShaderStructData) to ShaderLibrary
- [x] 2.5 Implement GetShaderFileInfo(pathHash) const method
- [x] 2.6 Implement GetShaderCode(shaHash) const method
- [x] 2.7 Implement GetShaderStruct(nameHash) const method
- [x] 2.8 Implement GetShaderRootStruct(pathHash) const method

## 3. VulkanShaderResourceBindingInfo Construction

- [x] 3.1 [P] Implement IterateHierarchyElements helper (reference D3D12)
- [x] 3.2 Implement ConstructShaderDescriptorInfo() main function
- [x] 3.3 Implement cbuffer binding extraction from BindingHierarchy
- [x] 3.4 Implement image binding extraction from BindingHierarchy
- [x] 3.5 Implement buffer binding extraction from BindingHierarchy
- [x] 3.6 Implement sampler binding extraction from BindingHierarchy
- [x] 3.7 Build DescriptorSetLayoutBinding arrays per set index
- [x] 3.8 Add logging for binding info construction

## 4. ShaderImporter_Vulkan重构 (参考D3D12ShaderResourceImporter)

- [x] 4.1 修改基类：VulkanSubobjectBase → ResourceImporterFree
- [x] 4.2 添加GetTags()方法，返回"Vulkan;Slang"
- [x] 4.3 实现ImportResource()方法框架
- [x] 4.4 在ImportResource中实现目录遍历（recursive_directory_iterator）
- [x] 4.5 设置编译目标为eSpirV（而非eDXIL）
- [x] 4.6 获取ShaderLibrary：resourceManager->GetOrNewResource<ShaderLibrary>()
- [x] 4.7 填充m_ShaderFiles[pathHash]（reflectionData, shaderBindingInfo, entryPoints）
- [x] 4.8 填充m_ShaderPrograms[shaHash]（spirvCode, shaderType）
- [x] 4.9 填充m_ShaderStructs和m_ShaderRootStructs
- [x] 4.10 调用ConstructShaderDescriptorInfo生成bindingInfo
- [x] 4.11 删除fantasy接口：CompileFromSource, CompileFromSPIRV等
- [x] 4.12 删除fantasy结构体：VulkanCompiledShaderInfo, VulkanDescriptorBindingInfo等
- [x] 4.13 [P] 修复：清空所有ShaderLibrary集合（m_ShaderFiles, m_ShaderStructs, m_ShaderRootStructs）
- [x] 4.14 [P] 修复：DescriptorSetLayoutCreateInfo的pBindings生命周期问题（在实际使用前重建createInfo或确保生命周期稳定）

## 5. VulkanShaderStruct Version Control

- [x] 5.1 Add m_Version member variable
- [x] 5.2 Add m_MaxChildrenVersion mutable member
- [x] 5.3 Implement UpdateVersion() private method
- [x] 5.4 Implement ComputeMaxChildrenVersion() const method

## 6. VulkanShaderStruct Uniform Buffer Staging

- [x] 6.1 Add m_StructLocalUniformStagingBuffer member
- [x] 6.2 Add m_NameToUniformElementMetaID mapping
- [x] 6.3 Add m_UniformElementOffsetInStagingBuffer vector
- [x] 6.4 Initialize staging buffer in Init() based on ShaderStructData
- [x] 6.5 Implement UpdateUniformBuffer() method for GPU upload

## 7. VulkanShaderStruct Set*Internal Methods

- [x] 7.1 Implement SetValueInternal() with staging buffer write
- [x] 7.2 Implement SetImageInternal() with handle storage
- [x] 7.3 Implement SetBufferInternal() with handle storage
- [x] 7.4 Implement SetSamplerInternal() with descriptor storage
- [x] 7.5 Implement SetStructInternal() with sub-struct storage
- [x] 7.6 Add m_NameToImageHandles map
- [x] 7.7 Add m_NameToBufferHandles map
- [x] 7.8 Add m_NameToSamplerDescriptors map
- [x] 7.9 Add m_NameToSubStructs map

## 8. VulkanShaderStruct Accessors

- [x] 8.1 Implement GetImageHandles() const
- [x] 8.2 Implement GetBufferHandles() const
- [x] 8.3 Implement GetSamplerDescriptors() const
- [x] 8.4 Implement GetSubStructs() const
- [x] 8.5 Implement GetStructData() const
- [x] 8.6 Implement GetCBufferSize() const
- [x] 8.7 Implement GetTextureAccessType() const
- [x] 8.8 Implement GetBufferRWType() const

## 9. Integration

- [x] 9.1 Update VulkanGraphExecutor to use new binding info
- [x] 9.2 Update VulkanResourceBindingInstance to use VulkanShaderStruct accessors
- [x] 9.3 Update RenderBackend_Vulkan::CreateShaderStruct to use completed implementation
- [x] 9.4 Verify descriptor set layout creation from binding info
- [x] 9.5 Add CA_REFLECTION for new ShaderLibrary members

## 10. Compilation Validation

- [x] 10.1 Verify VulkanRenderBackendNew compiles with new data structures
- [x] 10.2 Verify ShaderImporter_Vulkan compiles and links correctly
- [x] 10.3 Verify VulkanShaderStruct compiles with all accessors
- [x] 10.4 Verify VulkanResourceBindingInstance compiles with reflection-driven Init

---

## Progress Summary

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | ShaderLibrary Base Class | ✓ 5/5 |
| 1 | Data Structures | ✓ 6/6 |
| 2 | ShaderLibrary Extension | ✓ 8/8 |
| 3 | Binding Info Construction | ✓ 8/8 |
| 4 | ShaderImporter重构 | ✓ 14/14 |
| 5 | Version Control | ✓ 4/4 |
| 6 | Uniform Buffer Staging | ✓ 5/5 |
| 7 | Set*Internal Methods | ✓ 9/9 |
| 8 | Accessors | ✓ 8/8 |
| 9 | Integration | ✓ 5/5 |
| 10 | Compilation | ⏳ 0/4 |

**Total**: 72/76 Complete (95%)
