## 1. 启用 VK_EXT_vertex_attribute_robustness（VUID-07904）

- [ ] 1.1 `RenderBackend_Vulkan.cpp` `GetDeviceExtensionNames()`（line 59-67）：添加 `VK_EXT_VERTEX_ATTRIBUTE_ROBUSTNESS_EXTENSION_NAME`
- [ ] 1.2 同上文件 line ~233：在 `gplFeatures` 之前创建 `vk::PhysicalDeviceVertexAttributeRobustnessFeaturesEXT vertexAttrRobustFeatures{}; vertexAttrRobustFeatures.vertexAttributeRobustness = VK_TRUE;`，调整 pNext 链为 `vertexAttrRobustFeatures.pNext = &gplFeatures; deviceCreateInfo.pNext = &vertexAttrRobustFeatures;`

## 2. Fragment output library 添加 VkPipelineRenderingCreateInfo（VUID-06055）

- [ ] 2.1 `VulkanPipelineLibrary.cpp` `CreateFragmentOutputLibrary`（line 177-185）：在 `libraryInfo` 声明前构建：
  - `castl::array<vk::Format, 1> colorFormats = { vk::Format::eUndefined };`
  - `vk::PipelineRenderingCreateInfo renderingInfo{};`
  - `renderingInfo.colorAttachmentCount = colorBlendState.attachmentCount;`
  - `renderingInfo.pColorAttachmentFormats = colorFormats.data();`
  - pNext 链：`renderingInfo.pNext = nullptr; libraryInfo.pNext = &renderingInfo; createInfo.pNext = &libraryInfo;`

## 3. Swapchain layout barrier（VUID-09600 + VUID-02684 + layout error）

- [ ] 3.1 修完 1+2 后重新运行，检查 layout error 是否仍存在
- [ ] 3.2 如仍存在：将 `ExecuteBarriers` 的 `srcStageMask` 从 `eAllCommands` 改为 `eTopOfPipe`（仅针对 UNDEFINED 旧 layout 的 barrier）`VulkanGraphExecutor.cpp` line 289
- [ ] 3.3 如仍存在：检查 barrier 的 `subresourceRange.aspectMask` 是否正确覆盖 COLOR aspect

## 4. 验证

- [ ] 4.1 `python build.py` 编译通过
- [ ] 4.2 headless 模式（`--headless 3`）+ 窗口模式（无参数）运行 `TestSimpleTriangle`，grep VUID 无报错
- [ ] 4.3 运行 `TestTriangleWithConstantColor` / `TestDoublePass` / `TestTriangleWithImageBuffer` headless 各 3 帧，无新增 VUID（回归测试）
- [ ] 4.4 **MANUAL**：窗口显示三角形
