## ADDED Requirements

### Requirement: 图像拷贝 aspectMask 必须为单 bit

`VkBufferImageCopy.imageSubresource.aspectMask` SHALL 只包含单个 aspect bit（VUID-VkBufferImageCopy-aspectMask-09103）。depth-stencil 格式（如 `E_D24_UNORM_S8_UINT`）上传时 SHALL 为 depth 与 stencil 分别发起拷贝（各自单 bit），SHALL NOT 一次传入 `DEPTH|STENCIL` 双 bit。

#### Scenario: D24S8 纹理拷贝

- **GIVEN** `VulkanTexture::UploadData` 上传 `E_D24_UNORM_S8_UINT` 格式
- **WHEN** 构建 `VkBufferImageCopy`
- **THEN** aspectMask 为 `VK_IMAGE_ASPECT_DEPTH_BIT`（或 stencil，单 bit）
- **AND** 验证层不报告 VUID-vkCmdCopyBufferToImage-aspectMask-09103

### Requirement: image barrier 的 newLayout 禁止 UNDEFINED

`VkImageMemoryBarrier.newLayout` SHALL NOT 为 `VK_IMAGE_LAYOUT_UNDEFINED`（VUID-VkImageMemoryBarrier-newLayout-01198）。纹理上传等路径在原始布局为 UNDEFINED（新纹理未初始化）时，目标布局 SHALL 为明确的有效布局（如 `SHADER_READ_ONLY_OPTIMAL` / `TRANSFER_DST_OPTIMAL`）。

#### Scenario: 新纹理上传后的布局转换

- **GIVEN** 新创建纹理 `m_CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED`
- **WHEN** 拷贝后执行 `TransitionLayout`
- **THEN** `newLayout` 为明确的有效布局（非 UNDEFINED）
- **AND** 验证层不报告 VUID-VkImageMemoryBarrier-newLayout-01198

### Requirement: CUBE image view 需要 CUBE_COMPATIBLE_BIT

`eCubeMap` 类型的纹理图像创建时 `VkImageCreateInfo.flags` SHALL 包含 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`（VUID-VkImageViewCreateInfo-image-01003）；否则禁止创建 `VK_IMAGE_VIEW_TYPE_CUBE` 视图。

#### Scenario: 创建 CUBE 纹理

- **GIVEN** `textureDesc.textureType == eCubeMap`
- **WHEN** 创建底层 `vk::Image`
- **THEN** flags 含 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`
- **AND** 后续 `VK_IMAGE_VIEW_TYPE_CUBE` 视图创建合法

### Requirement: image view 的 layerCount 与 viewType 匹配

`VkImageViewCreateInfo` 的 `viewType` 与 `subresourceRange.layerCount` SHALL 匹配（VUID-VkImageViewCreateInfo-imageViewType-04973）：`VK_IMAGE_VIEW_TYPE_2D` 时 layerCount 为 1 或 `VK_REMAINING_ARRAY_LAYERS`（且 image 为 2D 布局）；多 layer 图像应使用 `VK_IMAGE_VIEW_TYPE_2D_ARRAY` 或 `VK_REMAINING_ARRAY_LAYERS`。`subresourceRange.aspectMask` 仅含格式实际含有的 aspect（depth-only 格式不得含 stencil）。

#### Scenario: 多 layer 纹理视图

- **GIVEN** 纹理 `arrayLayers > 1`
- **WHEN** 创建 image view
- **THEN** viewType 为 `2D_ARRAY` 或 layerCount 为 `VK_REMAINING_ARRAY_LAYERS`
- **AND** 验证层不报告 VUID-VkImageViewCreateInfo-imageViewType-04973

### Requirement: GPL 管线库创建满足规范 VUID

`VK_EXT_graphics_pipeline_library` 相关管线创建 SHALL 满足：
- library 管线（pNext 含 `VkGraphicsPipelineLibraryCreateInfoEXT`）的 `VkGraphicsPipelineCreateInfo.flags` SHALL 包含 `VK_PIPELINE_CREATE_LIBRARY_BIT_EXT`（VUID-VkGraphicsPipelineCreateInfo-graphicsPipelineLibrary-06606）
- `VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT` 库 SHALL 提供 `pMultisampleState`（VUID-pRasterizationState-09039）
- shader stage 含 tessellation 时 SHALL 提供 `pTessellationState`（VUID-pStages-09022）
- `VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT` 库在 subpass 使用 depth/stencil attachment 时 SHALL 提供 `pDepthStencilState`（VUID-renderPass-09028）
- `LinkPipeline` SHALL 校验调用方传入 layout/renderPass/subpass 与库的一致性（VUID-flags-06612 等）

#### Scenario: vertex input library 创建

- **GIVEN** `VertexInputStateManager` 经 GPL 分支创建 `VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT` 库
- **WHEN** 组装 `VkGraphicsPipelineCreateInfo`
- **THEN** `flags` 包含 `VK_PIPELINE_CREATE_LIBRARY_BIT_EXT`
- **AND** 验证层不报告 VUID-VkGraphicsPipelineCreateInfo-graphicsPipelineLibrary-06606

#### Scenario: pre-rasterization 库包含 tessellation 阶段

- **GIVEN** `CreatePreRasterizationLibrary` 收到 tessControl/tessEval shader stage
- **WHEN** 组装管线创建参数
- **THEN** 提供 `pTessellationState`
- **AND** 提供 `pMultisampleState`
- **AND** 验证层不报告对应 VUID

### Requirement: pipeline 句柄单一所有权

任意 `vk::Pipeline` 句柄 SHALL 至多被一个组件持有销毁责任（SHALL NOT 双销毁）。`PipelineLibraryCache` 缓存 SHALL 仅引用（不销毁）`VulkanPipelineLibrary::m_CreatedPipelines` 负责销毁的管线；`LinkPipeline` 失败路径 SHALL NOT 在 `Release` 之外再次 `destroyPipeline`。

#### Scenario: teardown 时管线仅销毁一次

- **GIVEN** `CachePipeline` 缓存了由 `VulkanPipelineLibrary` 创建的管线
- **WHEN** 后端 `Release`
- **THEN** 每条管线句柄恰好被 `destroyPipeline` 一次
- **AND** 无 second-handle 错误或 double-free

### Requirement: SPIR-V 字节拷贝长度安全

Shader 字节码从编译产物到 `vk::ShaderModuleCreateInfo.code` 的拷贝 SHALL 使用正确的字节长度（`codeSize`），SHALL NOT 以 `(dataSize/4)` 元素数截断导致 `dataSize` 非 4 的倍数时目标缓冲区超写或数据截断。

#### Scenario: 编译产物字节数非 4 倍数

- **GIVEN** Slang 编译产物 `dataSize` 非 4 的倍数
- **WHEN** 拷贝到 `shaderCode.spirvCode`
- **THEN** 拷贝长度为 `dataSize` 字节（或按 4 字节对齐且缓冲区尺寸匹配）
- **AND** 无堆溢出

### Requirement: 顶点缓冲绑定索引与管线 binding 一致

draw 时 `vkCmdBindVertexBuffers` 的 binding 索引顺序 SHALL 与管线创建时（`BuildPipelineStates`）为各顶点流分配的 binding 索引一致。两侧 SHALL 使用同一确定顺序（如按属性位置排序），SHALL NOT 一侧按反射扫描顺序、另一侧按哈希表迭代顺序。

#### Scenario: 多顶点流 draw

- **GIVEN** draw call 绑定 2+ 个顶点流（哈希表存储）
- **WHEN** 执行 `vkCmdBindVertexBuffers` 与管线创建
- **THEN** 两侧的 binding 索引顺序一致
- **AND** 顶点数据与 shader 输入正确对应
