## 1. D1 根因修复：Vulkan 图像视图忠实应用接口 swizzle

- [x] 1.1 实现 `EColorChannel` → `vk::ComponentSwizzle` 翻译，以及 `GPUTextureSwizzle` → `vk::ComponentMapping` 的 helper（`VulkanRenderBackendNew` 内新增工具函数；注意 `EColorChannel` 可能的"未指定"值兜底，identity → 默认映射）。
- [x] 1.2 `VulkanTexture`：采样图像视图改为"按请求 swizzle 派生"——identity swizzle 复用现有 `m_ImageView`；非 identity 用 `viewInfo.components = swizzleToComponents(swizzle)` 新建视图并按 swizzle 缓存，随纹理生命周期（view→image 顺序）销毁。
- [x] 1.3 `VulkanGraphLocalResourceManager`：`GetTextureView` 增加接受 `GPUTextureView`（或 swizzle）的重载，把请求 swizzle 传给 `VulkanTexture` 派生；保留仅 `handle` 的旧版本（identity 语义）避免连锁改动。
- [x] 1.4 `VulkanResourceBindingInstance`：采样图像绑定时，把携带的 `GPUTextureView` 的 swizzle 传入视图解析（`GetTextureView(handle, view)`），不再丢弃 swizzle。
- [x] 1.5 MCP 核实 API 语义：`vk::ImageViewCreateInfo::components`、`vk::ComponentMapping` / `vk::ComponentSwizzle`、单通道 `eR8Unorm` 采样 + 非默认 mapping 的结果，以及 `VUID-VkImageViewCreateInfo-components-` 相关前提。引用真实文档 URL。

## 2. D1 运行时验证（swizzle 路径被 IMGUI 字体真实触达）

- [x] 2.1 执行 `python build.py --config Debug`，直到 BUILD SUCCESSFUL。
- [x] 2.2 `TestIMGUI` headless vulkan 运行 → 读 `test_output/TestIMGUI_vulkan.png`（放大字形区），确认渲染出可辨字形（字数/条纹，非 6px 实心块）。
- [x] 2.3 双端对比：vulkan 与 d3d12 的 TestIMGUI 都非实心白块；记录白像素/字形差异收敛（之前 vulkan 233 块 vs d3d12 97 字形）。
- [x] 2.4 无新增 VUID、无回归：全部测试 exit 0，validation log 无新错误（IMAGE 字体图既有 benign 除外）。

## 3. D2 可选加固：IMGUIContext 规范字体上传（非阻塞）

- [ ] 3.1 `IMGUIContext/IMGUIContext.cpp:489` `GetTexDataAsAlpha8` → `GetTexDataAsRGBA32`；`:491-493` 纹理 `E_R8_UNORM` → `E_R8G8B8A8_UNORM`；`:496` 上传大小 `texW*texH*4`；`:754` 采样 view swizzle `SingleChannel(eR)` → **不再保留 `SingleChannel(eR)`**（规范做法改 identity；否则 RGBA 读 R=255 仍白）。仅改模块版（TestIMGUI 链接的是它）。｜**本轮不做**——见 ## Review Log（D2 deferred）：D1 已完整修复白块（white 97=字形，与 d3d12 一致），D2 为可选加固（规范 RGBA+AA 灰阶+去 swizzle 依赖），作为后续独立 change 跟进。
- [ ] 3.2 若做：`python build.py --config Debug` + 双端 headless 验证仍渲染且出现正常 AA 灰阶。｜（同 D2 deferred）
- [x] 3.3 若不做：在 tasks 末尾注明 D2 作为 follow-up（`docs/TODO.md` 记录 AA 观察 + swizzle 一致性）。

## 4. 审查闭环（Review & Adversarial Verify）

- [x] 4.1 开 workflow 做对抗验证：多独立审查者逐 claim 核对——swizzle→`vk::ComponentMapping` 翻译正确、view 生命周期/缓存销毁、identity 复用无回归、`GetTextureView` 重载调用面无连锁、双后端行为一致、验证证据（png/白像素/exit 0）真实、Vulkan API 语义 + MCP 文档 URL 真实性。
- [x] 4.2 回归检查与去重：核对本 change 是否引入新问题；回归 D3D12 参照（原本正确，不得回归）。验证发现的新问题以 `[AUDIT-]` 前缀追加为新 task，并追加新的审查任务；结果写入 tasks.md 末尾 `## Review Log`。

## Review Log

### 对抗验证（4 独立审查者，逐 I1-I8，≥2/4 判 issue）—— 全票通过，mustFix 空
- **I1-I8 全部 `pass`（0 issue）**，bottomLine「CORRECT and COMPLETE for the target; no revision required」。
- **I6 实证成立**：`TestIMGUI_vulkan.png` white_px=97（=d3d12，非原 233 实心块）、4 色 palette `{0,36,51,255}`、渲染 `Options` 字形、`TestIMGUI_validation.log` 0 字节。
- **I4 存储描述符确实安全**：`isUAV` → 单参 `GetTextureView(handle)` → identity `m_ImageView`，永不调 `GetOrCreateSampledImageView`，非 identity 视图不可能进 storage descriptor；经 MCP 核实 identity 仅 YCbCr/input-attachment/framebuffer/storage 必需，普通采样可非 identity。
- **I7 无回归**：vulkan+d3d12 全 8 测试 exit 0；仅既有的正交 `VUID-VkShaderModuleCreateInfo-pCode-08740`（DrawParameters）遗留于 ConstantColor，与 image-view swizzle 无关。
- **I8 API 语义** MCP 用真实 URL 核实（docs.vulkan.org VkImageViewCreateInfo）：普通 sampled descriptor 非 identity 合法，font `{R,R,R,R}` 采样有效。

### 非阻塞 latent 改进项（non-regression，列作记录，均未触达、可后续单独改）
1. `GetTextureView(handle, view)` 仅 External 纹理 honor swizzle；graph-local/Internal 注册 handle 仍走 identity `resource->imageView`（丢弃 swizzle）。字体是 External 无碍；未来 Internal 单通道采样将不 swizzle，考虑通用化。
2. `VulkanTexture.cpp:240,247` 的 `baseMip`/`baseLayer` 未 clamp（仅 `mipCount`/`layerCount` 做 0/~0u clamp）→ 潜伏 `VUID-01718/06725`。字体路径 baseMip=0/baseLayer=0 安全；建议防御性 clamp。
3. identity 短路返回全范围 `m_ImageView`，忽略携带的 subresource range；与既有行为一致，未触发。
4. 审查侧引用差异（VUID 01762 vs 12397，format-must-match-image）：非代码缺陷，需求已满足（format=image format、无 MUTABLE_FORMAT_BIT）。

### D2（可选加固）deferred（见 3.x）
D1 已完整修复白块（white 97=字形、与 d3d12 一致、无 VUID、无回归）。D2（规范 RGBA32+AA+去 swizzle 依赖）记为后续独立 change。**注**：done 后主 spec 已验证字形非白块；若后续落 D2，需保证 swizzle 改 identity（否则 RGBA 读 R=255 仍白）。
