# TODO

> 单一待办清单。以后有**未完成 / 需后续处理**的事项都记在这里，按「`## [日期] 标题（所属 change/模块）`」追加，标注：状态、问题、为何、改法、验收口径、相关代码。完成即删除该条（或标记已解决）。

---

## [2026-08-28] D3D12 回读改为异步，与 Vulkan 对齐（add-render-readback）

**状态：未修（技术债）**

**问题**：`add-render-readback` 的回读接口是跨后端通用的（`Readback` + `IReadbackToken`），但当前两后端实现不对称：
- **Vulkan**：异步 token —— `Readback()` 提交后返回 token，token 自持 staging+fence；`Wait()` 阻塞、`Reset()` 归还、`IsReady()` 有实义。
- **D3D12**：同步 token —— 整个 `record→execute→signal→wait→copy→释放 COM` 在 `Readback()` 内完成，token 不持 COM，`Wait()/IsReady()/Reset()` 全是 no-op。

后果：`IReadbackToken` 成了最小公分母（D3D12 上半个函数空转），两后端内部实现裂开。**目标：两后端都是真异步、对称。**

**为何当初 D3D12 做成同步**：初版 D3D12 是异步的，token 自管每次新建的 `CreateCommandAllocator/CommandList/Fence`，析构 `Release()` 时 COM 悬垂崩溃（dump：`ACCESS_VIOLATION`、`Rip=0x0` = null vtable 调用 / double-release；debug layer 另抓到 `0x87D`：分配器早于命令列表被释放）。**这是工程权衡，不是 D3D12 基础限制**——当时没根因清楚那个悬垂，又考虑低频读回同步够用，就用同步兜底了。

**正确修法（优先 A）**：
- A（推荐）：复用引擎已有 `CommandListManager`（`DirectCommand`/`ComputeCommand`）、`PooledCommandAllocator`、`GPUFrameManager`（`FrameContext::GPUWaitIdle`/`SetEventOnCompletion`），用帧上下文的命令列表/分配器做 copy+fence，与图执行共用一套，消灭"per-call 新建/裸释放"。
- B：修 per-call COM 生命周期——命令列表先于分配器释放 + 命令列表 `Reset` + 无 double-addref/release + fence 等完才释 READBACK allocation。

**验收口径**：两后端真异步对称、`Wait()` 真阻塞、无 no-op 空转；`python build.py --config Debug` 通过；vulkan+d3d12 各 8 测试 `exit 0` 全 `pass`；无 debug-layer 生命周期类报错、无 D3D12MA leaked-allocation 断言、无 crash dump、无 VUID 错误；两后端 `test_output/readback_*.png` 字节一致、`nonEmpty=true`。

**相关代码**：接口 `Interface/RenderInterface/header/CRenderBackend.h`；Vulkan（参照，异步）`VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`；D3D12（待改，同步）`D3D12RenderBackend/private/RenderBackend_D3D12.cpp`；可复用设施 `D3D12RenderBackend/private/ResourceManagment/CommandListManager.h`、`FrameBoundResourceManager.h`。

**收尾**：修好→删本条、更新 `openspec/changes/add-render-readback/design.md`（D7）与 `tasks.md`（4.3 的 [TODO] 标注）。

---

## [2026-08-28] 压缩/复杂格式读回载体 & bpp 设计原则（add-render-readback 扩展边界）

**类型：设计边界 / 原则**（当前 `ETextureFormat` 无压缩格式，README 路径不触发；但因 `GetReadbackBpp` fallback=4，将来加格式会**静默读错** → 部分属待修）

**背景**：当前读回把载体锁死成「单平面、非压缩、像素线性、base mip/layer」。对压缩/多平面格式，这套是**错误概念**——不是换个 bpp 的事，是载体结构不同。

**原则 1：压缩格式读回 = "块线性"（block-linear）载体，不是像素线性**
- 压缩格式**没有 per-pixel 布局**：像素编码在块内，不能按 `W*H*bpp` 寻址。
- 真正大小 = `ceil(W/blockW)*ceil(H/blockH)*blockBytes`（再叠 D3D12 256 对齐）；buffer 里"一行"是**一行块**。
- ASTC 块尺寸可变(4~12)且存在数据/footer，**不能由 W/H 推导**；PVRTC 是 superblock、非简单栅格。
- 解释需要 **format + 块尺寸 + 图像尺寸**，用**块解码器**（bcdec 等）还原；`stbi_write_png` 写不了（视觉断言对压缩不适用）。

**原则 2：block 参数是独立于贴图分辨率的正交轴**
- 分辨类（width/height/mips/layers）→ **贴图 descriptor**；block 类（blockW/blockH/blockBytes/是否块压缩）→ **格式属性**（format→查表）。两者**不可互推**。
- 但会在**计算 footprint 时结合**：总字节/行距 = f(分辨率, blockInfo)。独立 ≠ 永不结合。

**原则 3：公开面 = 单一尺寸查询 `GetImageByteSize(width,height,format)`，不暴露 `GetBytesPerPixel`**
- 调用者需要的是「读回该贴图要多少字节」（分配 dst），**不是**「每像素几字节」标量。
- bpp 只是非压缩 `W*H*bpp` 的**内部中间量**；压缩格式 bpp 只是压缩比率统计、无布局意义 → **不暴露公开 `GetBytesPerPixel`**。
- format（来自 `GetDescriptor()`）作为「怎么解释这坨字节」的键（像素线性 vs 块线性、用哪个解码器）。

**待修（防将来静默读错，review workflow 确认 MED）**：
- `GetReadbackBpp`（Vulkan）/`D3D12ReadbackBpp`（D3D12）是私有、与 `GetFormatBlockSize` 重复且**已分叉**——`E_D32_SFLOAT_S8_UINT` 现为 `GetFormatBlockSize=8` vs 后端 fallback=4。
- 应：① 让 `GetImageByteSize` 成为**唯一事实来源**（内部分支压缩/非压缩）；② 未支持格式**显式报错/断言**（非静默 fallback=4）；③ 补 `E_D32_SFLOAT_S8_UINT` case。

**相关**：`Interface/RenderInterface/header/Common.h`（`GetFormatBlockSize`）、`VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`（`GetReadbackBpp`）、`D3D12RenderBackend/private/RenderBackend_D3D12.cpp`（`D3D12ReadbackBpp`）。

---

## [2026-08-28] review workflow 确认的另两条读回缺陷（4 条 MED 中本文件已含 2 条）

> 背景：对本读回实现跑了对抗式 review workflow（27 agents），确认 4 条 MED。其中「bpp 单一事实来源」已在上面压缩条目、`D3D12 异步`已在第一条；本条目补剩余 2 条。测试过的 RGBA8 视读回路径本身可靠，这些是**边界/契约**缺陷。

**缺陷 A：载体硬编码 base mip / base layer，无 subresource 选择器；multisample 未 resolve**（MED）
- `Readback(ImageHandle)` 只拷 `mip0/layer0`：Vulkan 硬编码 `imageSubresource.mipLevel=0, baseArrayLayer=0, layerCount=1`；D3D12 `SubresourceIndex=0`。
- `samples>1` 的 MSAA 未 `vkCmdResolveImage` / D3D12 resolve 就直接拷原始 pattern → 读到的是 MSAA 样本而非解析后图像。
- 后果：多 mip / 多 layer / MSAA 贴图**静默截断/读错**，且调用者无从得知（无参数、无 token 元数据）。
- **修法**：① 把支持范围**收窄**为「单层、单 mip、非 MSAA 的 2D」并对其余**显式拒绝**（同 Internal handle 的报错），或 ② 给 `Readback` 加 subresource 选择器（mip/layer/aspect）+ 让调用者能得知拷的是哪个 subresource；MSAA 用 `vkCmdResolveImage`/D3D12 resolve 或拒绝。

**缺陷 B：token 持裸 `std::span` 非 owning，异步写无 RAII 绑定 → dst UAF 隐患**（MED）
- `std::unique_ptr<IReadbackToken>` 内部存裸 `std::span<uint8_t> m_Dst`；异步 Vulkan 路径在 `Wait()` 里才 `memcpy(m_Dst.data(), ...)`。
- 调用者若在 `Readback()` 与 `Wait()` 之间 resize/free 底层容器（如 growable `std::vector`），`Wait()` 会写进**已释放/已重分配内存**（UAF）。
- 契约只靠一句 prose「调用者保持 dst 存活直至 Wait()」，无 RAII 绑定、无检测。
- **修法**：① 加重契约（在 token 持有期由调用者保证 dst 不动，README/接口注释明确），或 ② token 在 `Readback()` 时把数据**拷到自己持有的一份**（代价：多一次 memcpy），从根上消除异步写悬垂。

**相关代码**：`Interface/RenderInterface/header/CRenderBackend.h`（`IReadbackToken`/`Readback`）、`VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`（`VulkanReadbackToken`/`Readback`）、`D3D12RenderBackend/private/RenderBackend_D3D12.cpp`。

---

## [2026-08-29] 截图功能的 4 个 capture 代码问题（GPUBackendTester 截图扩展）

**状态：已解决（2026-08-29，A 修复落地）**

**结果**：① `WriteReadbackPNG` 在 null/空白时 `return false` 并打 stderr（vulkan ConstantColor 实测输出 `[capture] ... capture is BLANK (nonEmpty=false)`）；② TestReadback 在"请求了 capture 但读到 null/空白"时 `throw`（兑现 assert，读回验证失败即 fail）；③ TestIMGUI 赋值行补 `captureRT.IsValid()`；④ IMGUIContext::DrawSingleView 用 `ImGuiViewportFlags_CanHostOtherWindows` 门控到主 viewport（此 imgui 版本无 IsMainWindow）。重编译通过，双后端 8 测试 exit 0、4 对字节一致不变。剩余："capture 不带 headless 静默 noop" + "capture 帧越界无提示" 两条 LOW 未列入本条，留待后续。

**背景**：给全部 GPUBackendTester 测试加截图（`--capture` 逐测试写 `test_output/<测试名>_<后端>.png`）。经对抗式深审（15 agents）确认 4 个问题，均为**捕获代码自身**的质量/健壮性问题（非后端 bug、非 capture 引起的渲染错误）。

**问题**：
1. **`WriteReadbackPNG` 的 bool 返回在 7 处调用点被丢弃**（只 TestReadback 检查）：null-token 或空白帧时 `--capture` 的失败被静默吞掉，`result.json` 的 status/exit code 不反映。
2. **TestReadback 注释承诺"assert non-empty"，但从不 assert**：`nonEmpty` 只 print 不抛/不返回失败，**空白帧被当成成功**。
3. **TestIMGUI `drawTarget = captureRT` 没加 `captureRT.IsValid()` 守卫**（读回行有、赋值行没有）——不一致，内部 IsValid 兜底故无崩溃。
4. **IMGUI capture 目标（captureRT 按主窗尺寸建）被传给每个 viewport 的 DrawSingleView**：多 viewport 下每个 pass `eClear` 后各自画 → **last-writer-wins**，只留最后一个 viewport，且 clip 不对。单窗口测试 dormant，多 viewport 是隐患。

**修法**：① helper 在 null/空白时 `return false` 并打印原因；② TestReadback 在"请求了 capture 但得到的帧为 null/空白"时 `throw`（兑现 assert 承诺，读回验证测试真正 fail）；③ TestIMGUI 赋值行补 `captureRT.IsValid()`；④ IMGUIContext::DrawSingleView 的 capture pass 用 `viewPort->Flags & ImGuiViewportFlags_IsMainWindow` 门控到主 viewport。

**相关代码**：`Test/GPUBackendTester/Main.cpp`（`WriteReadbackPNG`/`TestReadback`/各测试 capture 调用）、`IMGUIContext/IMGUIContext.cpp`（`Draw`/`DrawSingleView`）。

---

## [2026-08-29] vulkan 后端"写描述符"路径缺陷（08114/00331）——待定位根因（fix-vulkan-render-* 基础）

**状态：未修（根因未钉死）**

**现象**：一批测试绘图命中 `VUID-vkCmdDraw{Indexed}/-Dispatch-None-08114`（"bound descriptor set 从未经 vkUpdateDescriptorSets 更新"）与 `VUID-VkWriteDescriptorSet-descriptorType-00331`（descriptorType 写错）：
- StructuredBufferColor（buffer 绑定）→ 画面全黑
- DoublePass（`textureData.testTexture`=pass0RT 内部图像当纹理采样）→ 画面全黑（08114）
- IMGUI（draw-bind 04007/07312 + 08114）→ 暗场+噪点
- ComputeBuffer（dispatch 08114 + 00331）→ **画面反而字节一致**（说明 VUID 与"画面坏"非 1:1 因果）

**对照（关键反例）**：ImageBuffer（上传 texture+sampler）**完全干净、字节一致**——后端能写对"上传纹理"的 descriptor。坏的集中在 **buffer 绑定 + 内部图形当纹理采样**。

**根因尚未钉死**：需读 vulkan 后端 `ShaderStruct→vkUpdateDescriptorSets` 写出、descriptor pool 的 type/计数、内部(AllocImage)图像的 sampler 描述符是否只在当 color attachment 时写而没写 sampled 分支、以及 layout/set 反射 set·binding 是否对。（ComputeBuffer 有空 VUID 却渲染对，说明还要分辨"写了但 validation 报 type 错"与"真没写"。）

**附带一个独立的 executor barrier 缺陷（深审确认，与 08114 不同）**：`VulkanGraphExecutor` 的 `CollectResources` 只登记 raster pass 的 **color/depth attachment + vertex/index buffer**，**不登记 drawcall 采样图像读**（`RegisterComputeResources` 有 `GetImageBindings()`，raster 无对应）。→ 同一内部图像**跨 pass 写→读未串行化**（`Depends()` 无边、无 write→read barrier）。DoublePass 的 pass0RT 写→blit 读即此。观测到的 DoublePass `08114` 是描述符 bug；此 barrier 是 **latent**（凡"先渲染到内部图再采样"的图都会踩）。

**相关代码**：`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`（`CollectResources` 740-794 缺 raster 采样分支、`Depends` 215-227、`RegisterComputeResources` 939-949 有采样登记）、ShaderStruct→descriptor 写出路径（待定位）、`VulkanGraphLocalResourceManager.cpp:30-31`。

---

## [2026-08-29] vulkan D32_SFLOAT depth 图 usage 创建 bug（VUID-02251）——根因已钉死

**状态：未修（根因已确认）**

**现象**：TestTriangleWithConstantColor vulkan **只剩清屏色、无三角**（d3d12 蓝底+黑色三角，正常）。validation：`VUID-VkImageCreateInfo-imageCreateMaxMipLevels-02251`（D32_SFLOAT 不支持/usage 不对）+ `VUID-VkShaderModuleCreateInfo-pCode-08740`（DrawParameters 能力）。

**根因**：D32_SFLOAT depth image 在 `CollectResources`（VulkanGraphExecutor.cpp:743-744）被当作 `ETextureAccessType::eRT` 注册 → 只映射成 `COLOR_ATTACHMENT` usage（VulkanGraphLocalResourceManager.cpp:30-31），**从不给 DEPTH_STENCIL_ATTACHMENT** → `vkGetPhysicalDeviceImageFormatProperties2` 报 `VK_ERROR_FORMAT_NOT_SUPPORTED` → `vkCreateImage` 失败 → depth ImageView 为 null → `RecordRenderPass` 整段跳过（"Skipping render pass due to null attachment ImageView"）→ 无三角。**与 descriptor 无关，是图像 usage/创建 bug**。

**修法**：depth attachment 注册时给 `DEPTH_STENCIL_ATTACHMENT` usage（`VulkanGraphLocalResourceManager.cpp` 的 eRT→usage 映射需区分"是否为深度格式"，或 depth 走独立 accessType）。

**相关代码**：`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`（`CollectResources` 743-744）、`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp`（30-31 eRT→COLOR_ATTACHMENT 映射）。
