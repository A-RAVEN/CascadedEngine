## 1. 格式转换替换

- [x] 1.1 将 depth attachment 的 `rpKey.depthFormat = vk::Format::eD32Sfloat` 替换为 `rpKey.depthFormat = VulkanTexture::ConvertFormat(desc.format)`，删除 `// TODO: Proper format conversion` 注释
- [x] 1.2 将 color attachment 的 `rpKey.colorFormats.push_back(vk::Format::eR8G8B8A8Unorm)` 替换为 `rpKey.colorFormats.push_back(VulkanTexture::ConvertFormat(desc.format))`，删除 `// TODO: Proper format conversion` 注释

## 2. 对齐文档更新

- [x] 2.1 更新 `openspec/specs/vulkan-backend-alignment/spec.md`：将 Phase 1 项目 7 状态改为 ✅ 已完成，更新变更历史
- [x] 2.2 更新 `Documents/Vulkan后端与D3D12后端对齐分析.md`：从剩余 TODO 清单移除"RenderPass 格式硬编码"条目
