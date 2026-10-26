## Context

IMGUI 字体在 **Vulkan** 后端渲染成实心白块（文字不可读），**D3D12** 正常。根因是 Vulkan 后端在创建采样图像视图时丢失接口的 `GPUTextureSwizzle`：

- 字体纹理创建为 `E_R8_UNORM`（`IMGUIContext/IMGUIContext.cpp:491-493`），字形覆盖率在 **R 通道**（`GetTexDataAsAlpha8`，`imgui_draw.cpp:2515` 确认 `bytesPerPixel=1`）。
- 采样视图请求 `GPUTextureSwizzle::SingleChannel(eR)`（`IMGUIContext.cpp:753-754`）——意图把 R 复制到全通道。
- **Vulkan**：`VulkanTexture.cpp:161-177` 建 `vk::ImageViewCreateInfo` 时**从不设 `viewInfo.components`**（identity）；且整个 `VulkanRenderBackendNew/` grep `swizzle|ComponentMapping|SingleChannel|components` **零匹配**，接口 swizzle 从未翻译成 `vk::ComponentMapping`。`VulkanGraphLocalResourceManager::GetTextureView(handle)` 只收 `handle`、丢弃绑定携带的 `GPUTextureView`，始终返回 Init 时的 identity `m_ImageView`（`:948-984`，`:968` 返回 `pTexture->GetImageView()`）。→ `eR8Unorm` identity 采样得 `(R,0,0,1)`，`Imgui.slang:14` `.aaaa` 读 `A=1` → 覆盖率恒 1 → 实心白块。
- **D3D12**：`InterfaceTranslation.h:615-620` 把 swizzle 编进 `Shader4ComponentMapping`（`EColorChannelToInt(eR)=0` → `FROM_MEMORY_COMPONENT_0` = 源 R），`.aaaa` 读到 R=覆盖率。因此 D3D12 **正确**。

像素级验证（PIL/numpy）：`TestIMGUI_d3d12.png` = 细薄可辨识字母（"Options" 字形、细笔画、内部计数器），`TestIMGUI_vulkan.png` = 6px 实心块。**两图同为灰度（R==G==B），像素值仅 `(0,0,0,0)透明/(36,36,36)标题带/(51,51,51)正文/(255,255,255)字形`，且布局、白 bbox `x[14..60] y[6..17]` 均同**——同一帧同一文字，唯一差别是 D3D12 渲染出字形、Vulkan 渲染成实心块。故"D3D12 也白块"系缩略图目测误判。

**TestIMGUI 用模块版** `IMGUIContext/IMGUIContext.cpp`（`Test/GPUBackendTester/CMakeLists.txt:30` `target_link_libraries(IMGUIContext)` + `Main.cpp:36` include）。`Test/VulkanRendererBackendTester/private/IMGUIContext.cpp` 是另一份同款副本，未被 GPUBackendTester 链接。

## Goals / Non-Goals

**Goals:**
- 修复根因：Vulkan 后端在创建采样图像视图时忠实应用接口的 `GPUTextureSwizzle`（翻译成 `vk::ComponentMapping`）。
- 恢复双后端一致性：同一 swizzle 值，Vulkan 与 D3D12 产生相同通道重排结果。
- TestIMGUI 在 Vulkan 上渲染出可读字形（非实心白块），与 D3D12 一致。

**Non-Goals:**
- 不修 D3D12（已正确）。
- 不重写 IMGUI 整体渲染管线（root signature / font atlas 布局）。仅改字体上传数据格式（可选项 D2）。
- 不新增 swizzle 语义到接口层（`GPUTextureSwizzle` 结构已存在，仅需后端应用它）。
- 不做 IMGUI 多 viewport / docking 的其它已知独立问题。

## Decisions

### D1（主）：Vulkan 在图像视图创建时应用接口 swizzle → `vk::ComponentMapping`
把 `GPUTextureSwizzle`（4×`EColorChannel`）翻译成 `vk::ComponentMapping`（`vk::ComponentSwizzle` 四元组），写入 `vk::ImageViewCreateInfo::components`。绑定采样图像时，用"请求 view 的 swizzle"派生/复用对应的 swizzle-aware 图像视图，而非一律返回 Init 时的 identity `m_ImageView`。

- **为何选它（vs 其它）**：这是真正的根因；`GPUTextureSwizzle` 本就是接口用于表达通道重排的字段，D3D12 已应用、Vulkan 未应用是**双后端 API 一致性 bug**（与本项目刚归档的 `unify-depth-clamp-switch` 哲思一致：两端读同一上层字段、行为一致）。它修复字体（external-texture 路径，确证 `GetTextureView` 走 `pTexture->GetImageView()`）并恢复双后端 API 一致性。注：graph-local 图像视图（`VulkanGraphLocalResourceManager.cpp:615/:745`）自建 identity `components`，不在本 change 的 VulkanTexture-centric D1 范围（见 Open Question #2）。是根治而非绕过。
- **替代方案**：
  - *③ shader 改 `.aaaa`→`.rrrr`*：只绕当前字体通道，不修后端 swizzle 缺失，且依赖"覆盖率恰在 R"的隐含约定，治标。
  - *只做 D2*：把字体的单通道依赖洗掉，但**留下后端 swizzle 缺失隐患**（未来任何单通道纹理仍白块），且 `GPUTextureSwizzle` 依然双端不一致。

### D2（可选加固，非阻塞）：IMGUIContext 改用规范 ImGui 字体上传
`GetTexDataAsAlpha8` + `E_R8_UNORM` → `GetTexDataAsRGBA32` + `E_R8G8B8A8_UNORM`，采样视图 swizzle 从 `SingleChannel(eR)` 改 **identity**。覆盖率落在 **A 通道**（`imgui_draw.cpp:2532` `IM_COL32(255,255,255,coverage)`，RGB 恒 255），`.aaaa` 读 A=覆盖率，双端直接正确，不再依赖 swizzle。

- **为何作为可选**：D1 已能把字体渲染正确；D2 的价值是 (a) 得到正常 AA 覆盖率（当前 D3D12 字被量化成 `{0,255}` 无灰阶）、(b) 去掉"单通道+swizzle"脆弱依赖。风险低但触数据管线，故作为非阻塞加固。
- **关键坑**：若保留 `SingleChannel(eR)` + RGBA32 数据，`.aaaa` 读 R=255 → **仍恒白**。故 D2 必须**不再保留** `SingleChannel(eR)`（规范做法改为 identity；因覆盖率在 A，`SingleChannel(eA)` 亦读 A，精确表述是"必须不再 SingleChannel(eR)"）。
- **验证顺序**：先完成 D1 并验证（字体保持 R8+`SingleChannel(R)`，此时 swizzle 路径被真实触达，直接证明 D1 生效），**之后**再做 D2；这样 D1 的运行时行为有实测覆盖，不被 D2 的 identity swizzle 掩盖。

### D3：`EColorChannel` → `vk::ComponentSwizzle` 翻译
`eR→eR, eG→eG, eB→eB, eA→eA`；`GPUTextureSwizzle`（继承 `EColorChannel`）四个分量各翻译一次，组装成 `vk::ComponentMapping{}`。identity 时与当前 `m_ImageView` 等价（可复用）。需支持 `EColorChannel` 可能的默认/零值语义（`Common.h:141-147`），若存在"未指定"枚举需映射到 `eR` 或 identity 兜底。

### D4：swizzle 视图的派生与缓存
采样图像视图改成"按请求 swizzle 派生"：
- `VulkanTexture` 增加 swizzle-aware 视图获取：identity → 复用 `m_ImageView`；非 identity → 用 `viewInfo.components = swizzleToComponents(swizzle)` 新建并**缓存**（以完整 `GPUTextureView`——含 swizzle、baseMip、layerCount——为键，避免多 mip 碰撞），随纹理一起销毁。
- `VulkanGraphLocalResourceManager::GetTextureView` 增加接收 `GPUTextureView`（或 swizzle）的重载，把请求 swizzle 传给纹理派生；`VulkanResourceBindingInstance` 绑定时把携带的 `GPUTextureView` 传入。
- 保持与 `VulkanTexture` 现有视图生命周期（`view→image` 顺序销毁，`VulkanTexture.cpp:161-191`）一致。

## Risks / Trade-offs

- **[多个图像视图/纹理，内存与销毁]** → 按 swizzle 缓存于纹理，随 `m_ImageView` 同生命周期销毁；缓存键 = `GPUTextureView`（含 swizzle），可控数量级（实际仅字体等少数非 identity）。
- **[`GetTextureView` 签名改动影响调用面]** → 加"接受 view 的重载"，旧的仅 handle 版本保留（identity 语义），调用方按需迁移；避免大范围连锁改动。
- **[API 一致性风险：identity 时必须与现状等价]** → identity swizzle 复用 `m_ImageView`，不新增视图，无行为回归。
- **[D2 若保留 `SingleChannel(eR)` 会恒白]** → D2 明确要求**不再保留 `SingleChannel(eR)`**（改 identity 或 `SingleChannel(eA)`），并有 build+headless 验证兜底（若漏改，测试会看到仍白块而失败）。
- **[覆盖范围]** → 主 spec 的运行时验证靠"D1 阶段字体保持 R8+SingleChannel(R)"来触达 swizzle 路径；D2 后字体改为 identity，swizzle 路径转为仅代码保障（后续有单通道纹理用例再实测）。

## Migration Plan
无部署迁移（开发期后端修复）。改动用 `python build.py --config Debug` 验证；回滚 = revert 相关 commit。

## Open Questions
- 是否在本次 change 同时落地 D2（规范 RGBA32 + AA），还是作为后续独立 change？——倾向：D1 必须本次落地；D2 视评审优先级，可本次或后续。
- 是否有其它 `GPUTextureSwizzle` 非 identity 的既有消费方需要一并实测？（字体是唯一确定消费方；`StaticMeshResource.cpp:102` 的 R8 用法疑似，需确认其 view swizzle。）
