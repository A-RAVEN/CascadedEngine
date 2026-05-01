## Why

VulkanRenderBackendNew的Shader系统与D3D12RenderBackend架构不一致，导致：
1. ShaderLibrary继承结构错误（继承VulkanSubobjectBase而非TResource，无法作为资源管理）
2. VulkanShaderCode包含不可序列化的运行时对象（vk::UniqueShaderModule）
3. ShaderLibrary功能缺失（仅有ShaderModule缓存，缺少文件/程序/结构体管理）
4. 缺少ShaderResourceBindingInfo结构和构建机制
5. VulkanShaderStruct实现不完整，缺少版本控制和uniform buffer staging
6. ShaderImporter_Vulkan架构错误（继承VulkanSubobjectBase而非ResourceImporterFree）
7. ShaderImporter_Vulkan包含AI生成的fantasy接口（CompileFromSource等），不符合设计

这使得Vulkan后端无法像D3D12那样完整地管理shader资源和binding。现在需要将Vulkan后端与D3D12架构对齐，确保两个后端具有相同的功能和接口一致性。

## What Changes

- **重构ShaderLibrary基类**: 从VulkanSubobjectBase改为resource_management::TResource<ShaderLibrary>，使其成为可序列化的资源
- **分离VulkanShaderCode数据**: 移除vk::UniqueShaderModule运行时对象，仅保留可序列化的spirvCode
- **扩展ShaderLibrary**: 添加m_ShaderFiles、m_ShaderPrograms、m_ShaderStructs、m_ShaderRootStructs数据结构
- **添加VulkanShaderResourceBindingInfo**: 对应D3D12的ShaderResourceBindingInfo，存储descriptor set布局信息
- **完善VulkanShaderStruct**: 实现完整的版本控制、uniform buffer staging、子结构体管理
- **添加ConstructShaderDescriptorInfo**: 从ShaderReflectionData构建VulkanShaderResourceBindingInfo（独立函数）
- **重构ShaderImporter_Vulkan**:
  - 修改基类为ResourceImporterFree（与D3D12ShaderResourceImporter一致）
  - 实现ImportResource方法（目录扫描 + 编译 + 存储到ShaderLibrary）
  - 设置编译目标为eSpirV
  - 删除fantasy接口：CompileFromSource、CompileFromSPIRV、GetDescriptorSetLayoutBindings等
  - 删除fantasy结构体：VulkanCompiledShaderInfo、VulkanDescriptorBindingInfo等

## Capabilities

### New Capabilities

- `vulkan-shader-library`: 完整的Vulkan shader资源库管理，包括shader文件、编译程序、结构体定义
- `vulkan-shader-binding`: Vulkan shader资源绑定信息构建，从反射数据生成descriptor set布局
- `vulkan-shader-import`: Vulkan shader资源导入，支持目录扫描和批量编译

### Modified Capabilities

- `vulkan-shader-struct`: 扩展VulkanShaderStruct实现，添加版本控制、uniform buffer staging、完整的SetValueInternal/SetImageInternal等方法

## Impact

### 直接影响的模块
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h/cpp` - **需要重构基类和数据结构**
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h/cpp` - **需要重构**
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.h/cpp`
- `VulkanRenderBackendNew/private/PipelineLibrary/PipelineLibraryCache.h/cpp` - **需要处理ShaderModule创建逻辑迁移**

### 间接影响的模块
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` - 使用ShaderStruct
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/cpp` - descriptor set绑定
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` - CreateShaderStruct实现

### 参考实现
- `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` - 数据结构参考
- `D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.h/cpp` - **ImportResource流程参考**
- `D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.h/cpp` - ShaderStruct实现参考

## Non-goals

- 不修改D3D12RenderBackend的任何代码
- 不改变RenderInterface的抽象接口
- 不实现热重载（hot-reload）功能
- 不添加compute shader以外的shader类型支持
