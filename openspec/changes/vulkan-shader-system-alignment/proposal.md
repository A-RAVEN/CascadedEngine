## Why

VulkanRenderBackendNew的Shader系统与D3D12RenderBackend架构不一致，导致：
1. ShaderLibrary功能缺失（仅有ShaderModule缓存，缺少文件/程序/结构体管理）
2. 缺少ShaderResourceBindingInfo结构和构建机制
3. VulkanShaderStruct实现不完整，缺少版本控制和uniform buffer staging
4. 没有统一的shader资源导入流程

这使得Vulkan后端无法像D3D12那样完整地管理shader资源和binding。现在需要将Vulkan后端与D3D12架构对齐，确保两个后端具有相同的功能和接口一致性。

## What Changes

- **扩展ShaderLibrary**: 添加m_ShaderFiles、m_ShaderPrograms、m_ShaderStructs、m_ShaderRootStructs数据结构
- **添加VulkanShaderResourceBindingInfo**: 对应D3D12的ShaderResourceBindingInfo，存储descriptor set布局信息
- **完善VulkanShaderStruct**: 实现完整的版本控制、uniform buffer staging、子结构体管理
- **添加ConstructShaderDescriptorInfo**: 从ShaderReflectionData构建VulkanShaderResourceBindingInfo
- **扩展ShaderImporter_Vulkan**: 添加目录扫描和资源导入功能（可选，作为后续任务）

## Capabilities

### New Capabilities

- `vulkan-shader-library`: 完整的Vulkan shader资源库管理，包括shader文件、编译程序、结构体定义
- `vulkan-shader-binding`: Vulkan shader资源绑定信息构建，从反射数据生成descriptor set布局

### Modified Capabilities

- `vulkan-shader-struct`: 扩展VulkanShaderStruct实现，添加版本控制、uniform buffer staging、完整的SetValueInternal/SetImageInternal等方法

## Impact

### 直接影响的模块
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h/cpp`
- `VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h/cpp`
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.h/cpp`

### 间接影响的模块
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` - 使用ShaderStruct
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.h/cpp` - descriptor set绑定
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` - CreateShaderStruct实现

### 参考实现
- `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` - 数据结构参考
- `D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.cpp` - ConstructShaderDescriptorInfo参考
- `D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.h/cpp` - ShaderStruct实现参考

## Non-goals

- 不修改D3D12RenderBackend的任何代码
- 不改变RenderInterface的抽象接口
- 不实现热重载（hot-reload）功能
- 不实现ShaderImporter的目录扫描导入功能（作为独立后续任务）
