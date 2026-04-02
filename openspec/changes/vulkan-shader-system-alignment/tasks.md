## 1. Data Structure Definitions

- [x] 1.1 [P] Define VulkanDescriptorSetLayoutInfo in ShaderLibrary.h
- [x] 1.2 [P] Define VulkanCBufferBindingInfo, VulkanImageBindingInfo, VulkanBufferBindingInfo, VulkanSamplerBindingInfo
- [x] 1.3 [P] Define VulkanStructBindingInfo with sub-struct references
- [x] 1.4 Define VulkanShaderResourceBindingInfo aggregating all binding info types
- [x] 1.5 [P] Define VulkanShaderFileInfo with reflectionData, bindingInfo, entryPoints
- [x] 1.6 [P] Define VulkanShaderCode with vk::ShaderModule, stage, spirvCode

## 2. ShaderLibrary Extension

- [ ] 2.1 Add m_ShaderFiles (PathHash → VulkanShaderFileInfo) to ShaderLibrary
- [ ] 2.2 Add m_ShaderPrograms (SHA256Hash → VulkanShaderCode) to ShaderLibrary
- [ ] 2.3 Add m_ShaderStructs (NameHash → ShaderStructData) to ShaderLibrary
- [ ] 2.4 Add m_ShaderRootStructs (PathHash → ShaderStructData) to ShaderLibrary
- [ ] 2.5 Implement GetShaderFileInfo(pathHash) const method
- [ ] 2.6 Implement GetShaderCode(shaHash) const method
- [ ] 2.7 Implement GetShaderStruct(nameHash) const method
- [ ] 2.8 Implement GetShaderRootStruct(pathHash) const method

## 3. VulkanShaderResourceBindingInfo Construction

- [ ] 3.1 [P] Implement IterateHierarchyElements helper (reference D3D12)
- [ ] 3.2 Implement ConstructShaderDescriptorInfo() main function
- [ ] 3.3 Implement cbuffer binding extraction from BindingHierarchy
- [ ] 3.4 Implement image binding extraction from BindingHierarchy
- [ ] 3.5 Implement buffer binding extraction from BindingHierarchy
- [ ] 3.6 Implement sampler binding extraction from BindingHierarchy
- [ ] 3.7 Build DescriptorSetLayoutBinding arrays per set index
- [ ] 3.8 Add logging for binding info construction

## 4. ShaderImporter_Vulkan Extension

- [ ] 4.1 Modify CompileFromSource to populate VulkanShaderFileInfo
- [ ] 4.2 Call ConstructShaderDescriptorInfo after compilation
- [ ] 4.3 Store shader programs to m_ShaderPrograms
- [ ] 4.4 Register shader structs to m_ShaderStructs
- [ ] 4.5 Register root struct to m_ShaderRootStructs
- [ ] 4.6 Handle multiple entry points from single compilation

## 5. VulkanShaderStruct Version Control

- [ ] 5.1 Add m_Version member variable
- [ ] 5.2 Add m_MaxChildrenVersion mutable member
- [ ] 5.3 Implement UpdateVersion() private method
- [ ] 5.4 Implement ComputeMaxChildrenVersion() const method

## 6. VulkanShaderStruct Uniform Buffer Staging

- [ ] 6.1 Add m_StructLocalUniformStagingBuffer member
- [ ] 6.2 Add m_NameToUniformElementMetaID mapping
- [ ] 6.3 Add m_UniformElementOffsetInStagingBuffer vector
- [ ] 6.4 Initialize staging buffer in Init() based on ShaderStructData
- [ ] 6.5 Implement UpdateUniformBuffer() method for GPU upload

## 7. VulkanShaderStruct Set*Internal Methods

- [ ] 7.1 Implement SetValueInternal() with staging buffer write
- [ ] 7.2 Implement SetImageInternal() with handle storage
- [ ] 7.3 Implement SetBufferInternal() with handle storage
- [ ] 7.4 Implement SetSamplerInternal() with descriptor storage
- [ ] 7.5 Implement SetStructInternal() with sub-struct storage
- [ ] 7.6 Add m_NameToImageHandles map
- [ ] 7.7 Add m_NameToBufferHandles map
- [ ] 7.8 Add m_NameToSamplerDescriptors map
- [ ] 7.9 Add m_NameToSubStructs map

## 8. VulkanShaderStruct Accessors

- [ ] 8.1 Implement GetImageHandles() const
- [ ] 8.2 Implement GetBufferHandles() const
- [ ] 8.3 Implement GetSamplerDescriptors() const
- [ ] 8.4 Implement GetSubStructs() const
- [ ] 8.5 Implement GetStructData() const
- [ ] 8.6 Implement GetCBufferSize() const
- [ ] 8.7 Implement GetTextureAccessType() const
- [ ] 8.8 Implement GetBufferRWType() const

## 9. Integration

- [ ] 9.1 Update VulkanGraphExecutor to use new binding info
- [ ] 9.2 Update VulkanResourceBindingInstance to use VulkanShaderStruct accessors
- [ ] 9.3 Update RenderBackend_Vulkan::CreateShaderStruct to use completed implementation
- [ ] 9.4 Verify descriptor set layout creation from binding info
- [ ] 9.5 Add CA_REFLECTION for new ShaderLibrary members

## 10. Testing & Validation

- [ ] 10.1 Test ConstructShaderDescriptorInfo with simple shader
- [ ] 10.2 Test VulkanShaderStruct SetValue/GetImage/GetBuffer flow
- [ ] 10.3 Test nested struct version propagation
- [ ] 10.4 Verify rendering pipeline works with new system

---

## Progress Summary

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Data Structures | ✓ 6/6 |
| 2 | ShaderLibrary Extension | ⏳ 0/8 |
| 3 | Binding Info Construction | ⏳ 0/8 |
| 4 | ShaderImporter Extension | ⏳ 0/6 |
| 5 | Version Control | ⏳ 0/4 |
| 6 | Uniform Buffer Staging | ⏳ 0/5 |
| 7 | Set*Internal Methods | ⏳ 0/9 |
| 8 | Accessors | ⏳ 0/8 |
| 9 | Integration | ⏳ 0/5 |
| 10 | Testing | ⏳ 0/4 |

**Total**: 6/63 Complete (10%)
