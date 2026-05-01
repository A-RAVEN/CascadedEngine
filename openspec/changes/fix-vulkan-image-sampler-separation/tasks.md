## 1. VulkanResourceBindingInstance修正

- [x] 1.1 修改`SetSampledImage`签名（`VulkanResourceBindingInstance.h:73`）：移除`vk::Sampler sampler`参数
- [x] 1.2 修改`SetSampledImage`实现（`VulkanResourceBindingInstance.cpp:481-496`）：`descriptorType`从`eCombinedImageSampler`改为`eSampledImage`，`vk::DescriptorImageInfo`不再填充sampler字段（改为`{ {}, imageView, layout }`）
- [x] 1.3 修改`BuildDescriptors`中ImageBinding路径（`VulkanResourceBindingInstance.cpp:389-414`）：移除`device.createSampler()`临时创建默认sampler的代码，移除对`m_CreatedSamplers`的push，调用`SetSampledImage`时不再传sampler参数
- [x] 1.4 在`BuildDescriptors`的Image路径（非UAV分支）添加assertion：检查`shaderBindingInfo.samplerInfos`非空，若无sampler binding则CA_LOG_WARN警告开发者配置sampler

## 2. VulkanGraphExecutor修正

- [x] 2.1 修改DescriptorPool创建（`VulkanGraphExecutor.cpp:493`）：`poolSizes`中`vk::DescriptorType::eCombinedImageSampler`改为`vk::DescriptorType::eSampledImage`。说明：变量名`sampledImageCount`（行448）语义正确无需更改
