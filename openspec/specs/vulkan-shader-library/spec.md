## Requirements

### Requirement: ShaderLibrary SHALL manage shader file information

系统SHALL维护shader文件到ShaderFileInfo的映射，存储每个shader文件的反射数据和入口点信息。

#### Scenario: 查询已导入的shader文件
- **WHEN** 调用GetShaderFileInfo(pathHash)查询已导入的shader
- **THEN** 系统返回对应的VulkanShaderFileInfo指针
- **AND** 包含reflectionData、shaderBindingInfo、entryPointToShaderProgram信息

#### Scenario: 查询不存在的shader文件
- **WHEN** 调用GetShaderFileInfo(pathHash)查询未导入的shader
- **THEN** 系统返回nullptr

---

### Requirement: ShaderLibrary SHALL manage compiled shader programs

系统SHALL维护编译后shader程序的哈希到ShaderCode的映射，支持shader代码的去重和复用。

#### Scenario: 获取已编译的shader代码
- **WHEN** 调用GetShaderCode(shaHash)查询已编译的shader
- **THEN** 系统返回对应的VulkanShaderCode指针
- **AND** 包含vk::ShaderModule、stage、spirvCode

#### Scenario: 相同shader代码去重
- **WHEN** 两个不同shader文件编译出相同的SPIR-V代码
- **THEN** 系统只存储一份ShaderCode
- **AND** 两个ShaderFileInfo引用同一份ShaderCode

---

### Requirement: ShaderLibrary SHALL manage shader struct definitions

系统SHALL维护shader结构体类型名到ShaderStructData的映射，支持跨shader的结构体复用。

#### Scenario: 获取已注册的shader结构体
- **WHEN** 调用GetShaderStruct(nameHash)查询已注册的结构体
- **THEN** 系统返回对应的ShaderStructData指针
- **AND** 包含结构体成员布局信息

#### Scenario: 获取root结构体
- **WHEN** 调用GetShaderRootStruct(pathHash)查询特定shader的root结构体
- **THEN** 系统返回对应的ShaderStructData指针
- **AND** 该结构体代表该shader的根绑定结构

---

### Requirement: VulkanShaderFileInfo SHALL contain complete binding information

VulkanShaderFileInfo SHALL包含完整的shader绑定信息，与D3D12的ShaderFileInfo功能对等。

#### Scenario: ShaderFileInfo包含binding info
- **WHEN** shader成功编译并导入到ShaderLibrary
- **THEN** VulkanShaderFileInfo包含VulkanShaderResourceBindingInfo
- **AND** 包含descriptor set布局信息
- **AND** 包含cbuffer/image/buffer/sampler绑定信息
