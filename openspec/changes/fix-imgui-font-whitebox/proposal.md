## Why

IMGUI 字体在 **Vulkan** 后端渲染成实心白块（文字不可读），**D3D12** 正常。根因是 Vulkan 后端在创建采样图像视图时**丢失了接口请求的 `GPUTextureSwizzle`**：`GPUTextureSwizzle::SingleChannel(eR)` 从未被翻译成 `vk::ComponentMapping`，`eR8Unorm` 视图按 identity 采样得 `(R,0,0,1)`，`CAResources/Shaders/Imgui.slang:14` 的 `.aaaa` 读到 `A=1` → 字形覆盖率恒为 1.0 → 实心白块。这是一个**双后端不一致的静默渲染错误**：D3D12 把 swizzle 正确编进 SRV `Shader4ComponentMapping`，Vulkan 全程没用。

经像素级验证（PIL/numpy）：`test_output/TestIMGUI_d3d12.png` 渲染出细薄可辨识的字母字形（"Options"），`TestIMGUI_vulkan.png` 渲染成 6px 实心块——**D3D12 无 bug，白块是 Vulkan 专属**。此前"双端均复现"的判断源于缩略图目测，误把 D3D12 的细字形当成白块。

## What Changes

- **Vulkan 后端在创建采样图像视图时应用接口的 `GPUTextureSwizzle`**：把 swizzle 翻译成 `vk::ComponentMapping`，写入 `vk::ImageViewCreateInfo::components`，使单通道纹理 `SingleChannel(R)` 在 `.aaaa` 读到 R 覆盖率——**修复根因，恢复双后端 swizzle 一致性**（D3D12 已正确，无需动）。
- **绑定时把 view 的 swizzle 传给视图解析**：`VulkanGraphLocalResourceManager::GetTextureView` 现在只收 `handle`、丢弃绑定携带的 `GPUTextureView`；需让绑定处把 swizzle 传入，生成 swizzle-aware 的采样视图。
- **IMGUIContext 改用规范的 ImGui 字体上传（可选加固）**：`GetTexDataAsRGBA32` + `E_R8G8B8A8_UNORM` 纹理 + identity swizzle（覆盖率落在 A 通道，`.aaaa` 读 A=覆盖率），不再依赖"单通道+swizzle"脆弱路径，并得到正常 AA 覆盖率。

无破坏性变更；最终两个后端对 swizzled 纹理行为一致。

## Capabilities

### New Capabilities
- `texture-swizzle-honoring`: 采样图像视图在双后端都忠实应用接口的 `GPUTextureSwizzle`——Vulkan 翻译成 `vk::ComponentMapping`（写入 `vk::ImageViewCreateInfo::components`），D3D12 已是 `Shader4ComponentMapping`；同一 swizzle 值在双后端产生一致的通道重排/读取结果，单通道纹理经 `SingleChannel(R)` 后 `.aaaa` 双端都读到 R 覆盖率。

### Modified Capabilities
- （无）——现有 spec 不涉及采样 swizzle 行为。

## Impact

- `VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp`（图像视图创建 honors swizzle；swizzle→`vk::ComponentMapping` 翻译）
- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp`（`GetTextureView` 接收并应用 view swizzle）
- `VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.cpp`（绑定处把 `GPUTextureView` swizzle 传给视图解析）
- `IMGUIContext/IMGUIContext.cpp`（可选：默认字体上传改 `GetTexDataAsRGBA32` + `E_R8G8B8A8_UNORM` + identity swizzle）
- 无外部依赖变更；`D3D12RenderBackend` 不变（已正确）。
