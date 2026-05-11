## 1. VulkanSamplerManager 类创建

- [x] 1.1 [P] 创建 `VulkanRenderBackendNew/private/GPUGraph/VulkanSamplerManager.h`，定义类接口（继承 `VulkanSubobjectBase`）：`GetOrCreateSampler(TextureSamplerDescriptor const&) → vk::Sampler`、`Release()`；内部使用 `castl::shared_dic<TextureSamplerDescriptor, vk::Sampler>` 存储缓存
- [x] 1.2 [P] 创建 `VulkanRenderBackendNew/private/GPUGraph/VulkanSamplerManager.cpp`，实现 `GetOrCreateSampler`（内部调用 `m_SamplerCache.get_or_create(desc, [&] { return GetDevice().createSampler(MakeSamplerCreateInfo(desc)); })`）、`Release`（遍历销毁所有 sampler 并清空缓存）
- [x] 1.3 将 `MakeSamplerCreateInfo` 从 `VulkanResourceBindingInstance.cpp` 移至 `VulkanSamplerManager.cpp`（或保持 static 直接内联），确保 `GetOrCreateSampler` 可调用

## 2. RenderBackend_Vulkan 集成

- [x] 2.1 在 `RenderBackend_Vulkan.h` 中添加 `VulkanSamplerManager m_SamplerManager` 成员和 `GetSamplerManager()` 访问器
- [x] 2.2 在 `RenderBackend_Vulkan::Init()` 中初始化 `m_SamplerManager`（调用 `InitSubObj` 设置 app 指针）
- [x] 2.3 在 `RenderBackend_Vulkan::Release()` 中调用 `m_SamplerManager.Release()`

## 3. BuildDescriptors 重构

- [x] 3.1 修改 `VulkanResourceBindingInstance::BuildDescriptors()` 中 sampler 处理逻辑：将 `device.createSampler(samplerInfo)` 替换为 `GetApp()->GetSamplerManager().GetOrCreateSampler(samplerDesc)`，移除 `m_CreatedSamplers.push_back(vkSampler)`
- [x] 3.2 从 `VulkanResourceBindingInstance` 中移除 `m_CreatedSamplers` 成员声明、`BuildDescriptors()` 开头的旧 sampler 销毁循环（含 `auto device = GetDevice()` 声明）、`Release()` 中的 sampler 销毁循环

## 4. 对齐文档更新

- [x] 4.1 更新 `Documents/Vulkan后端与D3D12后端对齐分析.md`：将 1.4 节 SamplerManager 状态从 ❌ 改为 ✅；更新 Part 5 Phase 3；从剩余 TODO 清单移除 SamplerManager 条目
- [x] 4.2 更新 `openspec/specs/vulkan-backend-alignment/spec.md`：将 Phase 3 SamplerManager 状态从 ❌ 改为 ✅

## 5. 编译验证

- [x] 5.1 运行 `build.py` 编译项目，若失败则分析并修复直到 BUILD SUCCESSFUL