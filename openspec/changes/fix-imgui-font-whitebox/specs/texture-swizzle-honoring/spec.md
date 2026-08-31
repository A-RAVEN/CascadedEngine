## ADDED Requirements

### Requirement: 双后端忠实应用采样 swizzle
Vulkan 与 D3D12 后端对绑定到采样管线的图像，SHALL 忠实应用接口请求的 `GPUTextureSwizzle`：Vulkan 把它翻译成 `vk::ComponentMapping` 并写入 `vk::ImageViewCreateInfo::components`；D3D12 把它编码进 `Shader4ComponentMapping`。同一 swizzle 值在两个后端 SHALL 产生相同的通道重排/读取结果。

#### Scenario: Vulkan 绑定 SingleChannel(R) 的 R8 纹理
- **WHEN** 一张 `E_R8_UNORM` 图像按 `GPUTextureSwizzle::SingleChannel(EColorChannel::eR)` 绑定到采样管线
- **THEN** Vulkan 获取的图像视图 `components.{r,g,b,a}` 都映射到源 R，使得 shader 采样 `.aaaa` 读到 R 通道（字形覆盖率），而非常量 1.0

#### Scenario: Vulkan identity swizzle 行为不变
- **WHEN** 一张图像按 identity swizzle（默认）绑定
- **THEN** Vulkan 生成/复用的视图与现有 identity 视图等价，无需新增视图，无行为回归

#### Scenario: 双后端对同一 swizzle 结果一致
- **WHEN** 两个后端被设置相同的 `GPUTextureSwizzle`（如 `SingleChannel(eR)`）
- **THEN** 两者对同一份纹理的采样通道结果一致（Vulkan 走 `vk::ComponentMapping`，D3D12 走 `Shader4ComponentMapping`）

### Requirement: IMGUI 字体非白块（直因收敛）
同一份 IMGUI 字体纹理经 swizzle 处理后，SHALL 在两个后端都渲染出可读字形而非实心白块。Vulkan 侧经 `SingleChannel(R)` 修正后，`Imgui.slang` 的 `.aaaa` SHALL 读回字形覆盖率。

#### Scenario: Vulkan IMGUI 渲染字形
- **WHEN** TestIMGUI 在 Vulkan 后端运行，字体纹理为 `E_R8_UNORM` 且采样 view 为 `SingleChannel(eR)`
- **THEN** 渲染结果中标题/菜单文字呈可辨字形（非实心一块），与 D3D12 参考一致

#### Scenario: D3D12 参照不受影响
- **WHEN** 同一 change 后 D3D12 后端运行 TestIMGUI
- **THEN** D3D12 原本正确的字形行为保持不变（无回归）
