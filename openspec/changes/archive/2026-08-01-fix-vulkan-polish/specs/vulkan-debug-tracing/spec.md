## ADDED Requirements

### Requirement: 所有 Vulkan 资源对象可设置 Debug 名称

系统 SHALL 在 Vulkan Buffer、Image、Pipeline、CommandBuffer、DescriptorSet、ShaderModule、Framebuffer、RenderPass 创建时为其设置人类可读的 debug 名称，使得 RenderDoc、NSight Graphics 等调试工具中可显示有意义的资源标识，而非裸 Vulkan 句柄。

命名通过 `VK_EXT_debug_utils` 扩展的 `vkSetDebugUtilsObjectNameEXT` 实现。

#### Scenario: Buffer 创建时设置 debug 名称
- **WHEN** 系统通过 VulkanMemoryManager 或 VulkanLinearMemoryManager 创建 `vk::Buffer`
- **THEN** 在 buffer 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"Buffer:<用途描述>"`
- **AND** 该名称可在 RenderDoc 的 Resource Inspector 中查看

#### Scenario: Image 创建时设置 debug 名称
- **WHEN** 系统通过 VulkanMemoryManager 或 VulkanGraphLocalResourceManager 创建 `vk::Image`
- **THEN** 在 image 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"Image:<用途描述>"`
- **AND** 该名称可在 RenderDoc 的 Texture Viewer 中查看

#### Scenario: Pipeline 创建时设置 debug 名称
- **WHEN** VulkanPipelineLibrary 创建或链接任意 `vk::Pipeline`（vertex input / pre-rasterization / fragment / fragment output / linked / monolithic）
- **THEN** 在 pipeline 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"Pipeline:<shader名称>"`
- **AND** 该名称可在 RenderDoc 的 Pipeline State 视图中查看

#### Scenario: CommandBuffer 分配时设置 debug 名称
- **WHEN** VulkanCommandListManager 懒加载分配 Graphics、Compute 或 Transfer command buffer
- **THEN** 在 command buffer 分配成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"CmdBuf:<Graphics/Compute/Transfer>"`
- **AND** 该名称可在 NSight 的 Command List 视图中查看

#### Scenario: DescriptorSet 分配时设置 debug 名称
- **WHEN** VulkanResourceBindingInstance 为 shader 分配 descriptor set
- **THEN** 在 descriptor set 分配成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"DescSet:<shader名称>:set<N>"`
- **AND** 该名称可在 RenderDoc 的 Descriptor Set 视图中查看

#### Scenario: ShaderModule 创建时设置 debug 名称
- **WHEN** VulkanPipelineLibrary 编译或加载 shader 并创建 `vk::ShaderModule`
- **THEN** 在 ShaderModule 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"ShaderMod:<shader名称>:<stage>"`
- **AND** 该名称可在 RenderDoc 的 Shader 视图中查看

#### Scenario: Framebuffer 创建时设置 debug 名称
- **WHEN** VulkanGraphLocalResourceManager 创建 `vk::Framebuffer`
- **THEN** 在 Framebuffer 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"Framebuf:<render_pass名称>"`
- **AND** 该名称可在 RenderDoc 的 Framebuffer 视图中查看

#### Scenario: RenderPass 创建时设置 debug 名称
- **WHEN** VulkanGraphLocalResourceManager 创建 `vk::RenderPass`
- **THEN** 在 RenderPass 创建成功后立即调用 `SetVKObjectDebugName`，名称格式为 `"RenderPass:<名称>"`
- **AND** 该名称可在 RenderDoc 的 Render Pass 视图中查看

### Requirement: Debug Utils 扩展可用性检查

系统 SHALL 在 Vulkan 设备初始化时检测 `VK_EXT_debug_utils` 扩展是否可用，并仅在扩展可用时调用 debug 命名 API。若扩展不可用，系统 SHALL 静默跳过所有 debug 命名调用（不产生错误日志）。

#### Scenario: 设备支持 debug utils 扩展
- **WHEN** Vulkan 物理设备支持 `VK_EXT_debug_utils` 扩展
- **THEN** 系统启用 debug 命名功能，所有 `SetVKObjectDebugName` 调用正常执行

#### Scenario: 设备不支持 debug utils 扩展
- **WHEN** Vulkan 物理设备不支持 `VK_EXT_debug_utils` 扩展
- **THEN** 系统禁用 debug 命名功能，`SetVKObjectDebugName` 调用静默跳过
- **AND** 不产生任何 WARNING 或 ERROR 级别日志

### Requirement: SetVKObjectDebugName 辅助函数

系统 SHALL 提供 `SetVKObjectDebugName<T>` 模板函数，接受 `vk::Device`、Vulkan 对象句柄（模板参数 `T`）和名称字符串，自动推导 `T::objectType` 并构造 `vk::DebugUtilsObjectNameInfoEXT` 调用 `vkSetDebugUtilsObjectNameEXT`。

函数 MUST 在 device 或对象无效时安全返回（不崩溃）。

#### Scenario: 为有效对象设置名称
- **WHEN** 调用 `SetVKObjectDebugName(device, buffer, "MyBuffer")`
- **THEN** 函数构造正确的 `vk::DebugUtilsObjectNameInfoEXT`（objectType = T::objectType, objectHandle = buffer 的 native handle, pObjectName = "MyBuffer"）
- **AND** 调用 `device.setDebugUtilsObjectNameEXT(nameInfo)` 且无异常

#### Scenario: 为无效对象设置名称
- **WHEN** 调用 `SetVKObjectDebugName(device, nullptrBuffer, "MyBuffer")` 其中 buffer 为空
- **THEN** 函数检测到空对象并安全返回，不调用任何 Vulkan API
- **AND** 不产生崩溃或异常
