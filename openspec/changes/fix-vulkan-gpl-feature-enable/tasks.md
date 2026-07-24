## 1. 启用 GPL + dynamicRendering Feature

- [ ] 1.1 在 `RenderBackend_Vulkan::Init()` 中创建 `vk::PhysicalDeviceVulkan13Features`（`.dynamicRendering = VK_TRUE`）和 `vk::PhysicalDeviceGraphicsPipelineLibraryFeaturesEXT`（`.graphicsPipelineLibrary = VK_TRUE`），挂入 DeviceCreateInfo pNext 链（`RenderBackend_Vulkan.cpp` line 227）

## 2. 修复 CreateFragmentLibrary 的 VUID-09035

- [ ] 2.1 `VulkanPipelineLibrary.h`：`CreateFragmentLibrary` 声明添加 `vk::PipelineDepthStencilStateCreateInfo const* pDepthStencilState` 参数
- [ ] 2.2 `VulkanPipelineLibrary.cpp`：函数实现中设置 `createInfo.pDepthStencilState = pDepthStencilState;`
- [ ] 2.3 `VulkanGraphExecutor.cpp`：GPL 路径调用处传入 `&depthStencilState`

## 3. 验证

- [ ] 3.1 `python build.py` 编译通过
- [ ] 3.2 普通模式运行 `TestSimpleTriangle`，4 个 GPL 相关 VUID（06606/06642/06576/09035）全部消失
- [ ] 3.3 headless 模式（`--headless 3`）validation layer 无 GPL 相关 VUID 报错
- [ ] 3.4 **MANUAL**：窗口显示三角形（用户目视确认）
