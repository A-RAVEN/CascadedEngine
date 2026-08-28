## 1. 接口层（通用 Readback，Buffer + Texture）

- [ ] 1.1 新增跨后端读回接口：`IReadbackToken`（`Wait()`/`IsReady()`/`GetByteSize()`/`Reset()`）+ 后端入口 `Readback(ImageHandle, span<uint8_t> dst)` 与 `Readback(BufferHandle, span<uint8_t> dst)` 两个纯虚声明，置于 `RenderBackend`/`CRenderBackend` 基类；接口层 `#include <memory>`（`unique_ptr`）并引入/选定 `span` 类型（[AUDIT-R1-11]：确认 `span` 可用性——std::span 或 castl 自定义）
- [ ] 1.2 载体契约 + 越界校验：`dst` 调用者拥有、读回期间存活、大小≥所需字节；**接口层用 `dst.size()` 校验，越界即报错拒绝读回**；`Wait()` 前不保证有效，`Wait()` 后可读（[AUDIT-R1-3/载体]）
- [ ] 1.3 载体形态定死（[AUDIT-R1-f]）：Image → 紧凑 RGBA（`w*h*4`，后端解码去 pitch/swizzle）；Buffer → 原始线性字节（size=字节数）；`GetByteSize()` 定义为"实际写入字节数"；`IReadbackToken` 暴露 `{width,height,rowPitch}` 供解释 dst（消 GetByteSize 歧义）
- [ ] 1.4 `IReadbackToken::Reset()` 与 `unique_ptr` RAII 所有权定死（[AUDIT-R1-3]）：token 析构**不**释放仍飞行的 staging；`Reset()` 显式归还；定义 Reset-after-Wait / 未 Wait 即毁 的语义
- [ ] 1.5 **读回贴图创建能力（portability 前置）**：`ETextureAccessType` 增 `eTransferSrc`（/`eReadback`），`VulkanTexture::Init` 据此给 `imageInfo.usage` 加 `eTransferSrc`；使测试能创建 `eTransferSrc|eTransferDst|eColorAttachment` 的离屏读回 RT（当前 `VulkanTexture::Init(:120)` 仅 `eTransferDst|eSampled`+按 accessType，**无 `eTransferSrc`**）——source=离屏 RT 的前提

## 2. Graph-internal 资源解析（前置决策 + 实现）

- [ ] 2.1 定稿 D5 方案（[AUDIT-R1-4/5]）：A（`ExecuteGraph` 结束把 `ResourceHandleKey→{后端资源,layout,format,extent,size}` 导出生存活表到 `RenderBackend`）或 B（延长 graph local resource manager 生命周期）。**拍板后填写**；默认 A
- [ ] 2.2 据 D5 实现 Internal `ImageHandle`/`BufferHandle` → 后端资源解析（Vulkan/D3D12），使 `Readback` 可在 `ExecuteGraph` 后取到 graph-internal 资源（含 compute 输出 buffer）

## 3. Vulkan 后端实现（buffer + image）

- [ ] 3.1 buffer（host-visible）：`GetBufferPtr<VulkanBuffer>()` → CPU 可访问则直 `map`→`memcpy`→`unmap`（无 GPU copy、无 fence）
- [ ] 3.2 buffer（device-local）：分配 host staging（VMA `HOST_VISIBLE|HOST_ACCESS_RANDOM`,`eTransferDst`）→ `vkCmdCopyBuffer(src,staging,size)` → submit+fence → map→memcpy→unmap
- [ ] 3.3 image：布局 barrier（源 scope=`COLOR_ATTACHMENT_OUTPUT`+`COLOR_ATTACHMENT_WRITE`，目标=`eTransfer`+`eTransferRead`；oldLayout→`eTransferSrcOptimal`，[AUDIT-R1-8]）+ `vkCmdCopyImageToBuffer`（bufferRowLength=0 紧凑、bufferOffset 4 对齐）→ host staging → submit+fence → map→memcpy→unmap
- [ ] 3.4 **布局注册表回写**（[AUDIT-R1-2]）：读回 barrier 改动的 image 布局写回 window/graph 布局状态，防下帧 oldLayout 失配（VUID-01197/-01189）
- [ ] 3.5 实现 `Readback(ImageHandle/BufferHandle)` 入口与 `IReadbackToken`（Vulkan 侧），接入 `RenderBackend_Vulkan`；经 D5 解析 Internal 资源

## 4. D3D12 后端实现（buffer + image）

- [ ] 4.1 buffer（CPU 可达 heap 直 map；否则 `D3D12_HEAP_TYPE_READBACK` + `CopyBufferRegion` + 资源 barrier（src `COPY_SOURCE`）+ fence）
- [ ] 4.2 image：`D3D12_HEAP_TYPE_READBACK` + 资源 barrier（src `COPY_SOURCE`）→ `CopyTextureRegion`（dst rowPitch=`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256`、PLACEMENT=`512` 对齐）→ fence → map→memcpy→unmap（[AUDIT-R1-9-对齐]）
- [ ] 4.3 实现 `Readback(ImageHandle/BufferHandle)` 入口与 `IReadbackToken`（D3D12 侧）；经 D5 解析 Internal 资源

## 5. 测试接入（--capture → 离屏 RT → PNG；compute buffer → 数值断言）

- [ ] 5.1 GPUBackendTester 增加 `--capture <N>`（抓第 N 帧）或等价触达
- [ ] 5.2 视觉断言走**自建离屏 RT**：测试渲染到离屏 RT（非 backbuffer），`std::vector<uint8_t> px(rtW*rtH*4)`，`Readback(rtImage,px)`→`Wait()`→`stbi_write_png`（`STB_Impl.cpp` 已链接）导出 `test_output/<test>.png`（[AUDIT-R1-1]：规避 post-present）
- [ ] 5.3 compute 数值断言：执行含 compute pass 的图到 structured buffer/SSBO（Internal BufferHandle）→ `Readback(outBuffer,data)`→`Wait()`→CPU 断言数值（[AUDIT-R1-4]）
- [ ] 5.4 用内置读图能力（visual）/ 数值比较（compute）验证读回结果，确认"渲染/计算→读回→断言"闭环成立

## 6. 编译验证

- [ ] 6.1 `python build.py --config Debug` 全量构建成功（构建只经项目脚本）

## 7. Review & Adversarial Verify（审查闭环——最后一个任务）

- [ ] 7.1 对全部修改做对抗验证审查：接口契约（跨端一致、载体越界拦截、Wait 契约不依赖实现巧合）、Vulkan/D3D12 搬运+同步正确性、Graph-internal 解析
- [ ] 7.2 **API 外部正确性**：按 CLAUDE.md 用 MCP 工具（禁用 WebSearch/WebFetch）核对 `vkCmdCopyImageToBuffer`（VUID-00186/01998）、`vkCmdCopyBuffer`、`CopyTextureRegion`/`CopyBufferRegion`（READBACK 行 pitch 256/512）、`D3D12_RESOURCE_STATE` 转换——确认参数/枚举/返回值/前提，**引用真实 URL**
- [ ] 7.3 对抗者独立复查审查者引用的每个文档 URL 真实性（存在性 + API 是否在该页 + 语义一致）
- [ ] 7.4 每轮审查先做回归检查（跨端字节对称、载体越界拦截、布局注册表一致、Internal 解析正确），记录 Review Log
- [ ] 7.5 新增 [AUDIT] task 前与 Review Log 去重；发现新问题作为 [AUDIT] task 追加，循环直到无新问题或 3 轮；**停下的那一刻交付 Review Log 报告**

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| 1 | 2026-08-26 | workflow 29 agents / 1.49M tokens（wf_9ee548b1-d93） | **12 confirmed（其中 7 HIGH）**：设计**整体不 apply-ready** | 见下：[AUDIT-R1-*]；用户**已拍板**（源=离屏/内部 RT + 扩展 Buffer/Texture）→ 工件已改版 | D5（Graph-internal 解析 A/B）待定稿 |

**Round 1 — 实施前 design 对抗验证（29 agents / 1.49M tokens）——外部 API 经 MCP 核验（禁用 WebSearch/WebFetch）。**

### 核心结论：设计**整体不 apply-ready**，根因单一

所有 HIGH 级发现都指向**同一个根因——回调目标选错了**：`--capture` 在 `ExecuteGraph`（内部已同步 Present）之后读回一个**已呈现的 swapchain backbuffer**。两个后端都在 `ExecuteGraph` 内部完成 acquire→render→present，读到的是已交给 presentation engine 的图像（Vulkan `vkQueuePresentKHR` 后须重新 acquire 才能再用；D3D12 flip-model 已 advance `m_BackBufferIndex` 并置 `D3D12_RESOURCE_STATE_PRESENT`）→ **invalid/UAF/帧漂移**。**这不是实现细节，是把"源"选错了**——读回应该以**离屏 RT/内部 RT** 为源（渲染到 RT，读回 RT），而非已呈现的 backbuffer。离屏 RT 方案在 design D2/D4 只是被提到（"也可走离屏 RT"），却**无任何 task 实现它**，且 D3 标题/内容被**错当**"决定 swapchain vs 离屏 RT"（实际 D3 决定的是 A/B 载体形态）。

**用户决策（Round-1 后）**：① Readback **扩展为 Buffer + Texture 通用**（任意传入资源都可读回）；② **回调源 = 离屏/内部 RT 或外部资源**（不读 presented backbuffer）；③ 顺带强化 Round-1 暴露的**Graph-internal 资源解析**（[AUDIT-R1-4]，现对 buffer/内部 RT 都成立）。**工件已据此改版**（proposal/design 泛化 + D2 源决策 + D5 内部解析；tasks 已重排为 1.x-7.x）。下列 Round-1 findings 均已在改版后的 design/tasks 落位，对应关系见每条的 [AUDIT-R1-*] 标记。

### 确认发现（12，按严重度，均在改版中落位）

**[AUDIT-R1-1][HIGH] 回调源=已呈现 swapchain backbuffer**（design.md:98/99, VulkanWindowHandle.cpp:357, GPUGraphExecutor.cpp:2245）：D4 在 `ExecuteGraph` 后 `Readback`，但 `ExecuteGraph` 内已 present（Vulkan present :601/:2683；D3D12 PresentWindows :2245）。读回 `GetCurrentImage()` = `m_SwapchainImages[m_CurrentImageIndex]`，已被呈现/需重 acquire；D3D12 背缓冲已 advance + `PRESENT` 态不可 `COPY_SOURCE`。→ 改版 D2：**源走离屏/内部 RT**，读回 never presented 帧；task 5.2。
**[AUDIT-R1-2][HIGH] 读回 barrier 绕过窗口 layout 注册表**（design.md:74, VUID-01197/-01189）：读回在独立 cmd buffer 改布局却未回写 window/graph 布局状态 → 下帧 oldLayout 失配。→ 改版 D6 + task 3.4：**布局注册表回写**。
**[AUDIT-R1-3][HIGH] IReadbackToken::Reset() 与 `unique_ptr` RAII 冲突**（design.md:46）：所有权歧义（析构 free？Reset free？）→ 泄漏或 UAF。→ 改版 D1 + task 1.4：**定死 token 所有权**（析构不释放飞行 staging；Reset 显式归还）。
**[AUDIT-R1-4][HIGH] Graph-internal 资源无法解析 ID3D12Resource/vk::Image**（RenderBackend_D3D12.cpp:323 等）：Internal 型资源活在 graph executor stack-local，`Readback` 时已析构。→ 改版 D5 + task 2.x：**导出资源表/延长生命周期**（现对 buffer 亦成立）。
**[AUDIT-R1-5][HIGH] swapchain-vs-离屏 RT 决策无 owner + 离屏回退悬空**：D3 被错当"定源"。→ 改版 D2：**源已拍板=离屏/内部 RT**，离屏 path 由 task 5.2 落实（测试自建离屏 RT）。

**[AUDIT-R1-6][MED] 离屏 RT 回退未实现**：→ task 5.2（测试自建离屏 RT，勿依赖 swapchain `eTransferSrc`）。
**[AUDIT-R1-7][MED] async+显式 Wait 契约未践行**（engine 每帧 waitForFences；readback submit 无 wait semaphore）：→ 改版保留"显式 Wait"契约（D1），task 3.x/4.x 的 fence 语义独立；**如实记录引擎当前同步、Wait 为快路径**。
**[AUDIT-R1-8][MED] Vulkan barrier 源 scope 配错（eTransfer/eTransferRead 作源）**：→ 改版 D6 + task 3.3：源 scope 改 `COLOR_ATTACHMENT_OUTPUT+COLOR_ATTACHMENT_WRITE`。
**[AUDIT-R1-9][MED] 跨后端字节/颜色空间对称性无保证**：→ 改版 D3 + task 1.3：image 统一紧凑 RGBA；buffer 原始字节；[AUDIT-R1-f]。
**[AUDIT-R1-10][LOW] VUID 幻觉（-07777）**：→ 已改为 `-00186`/`-01998`，design.md:7 + tasks 7.2 同步；经 [Khronos vkCmdCopyImageToBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImageToBuffer.html) 确证。

### Refute（不采信）
- 裸 `span<uint8_t>` 一定编译崩（real=False）：无法仅凭 grep 定死（std::span/自定 span 经 C++20 可达），[AUDIT-R1-11] 保留为"apply 前确认 `span` 引入"。
- D3-B "compact RGBA 由 D2 原始 copy 不可能"（real=False）：可经 staging 后 CPU 重排。
- "每帧 waitIdle 非目标"与"新 readback fence"矛盾（real=False，以 R1-7"未践行"表述）。

### 回归
- 跨后端一致性 / colorSpace 对称性（R1-9）、D3D12 READBACK 行 pitch 256 对齐（design.md:81，经 MCP 确证 `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256`/`PLACEMENT_ALIGNMENT=512`，[Uploading texture data through buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/upload-and-readback-of-texture-data)）、接口层 `ImageHandle`/`span` 提供（R1-11）。

### 审查结论（Round 1）→ 已改版，后续决策

**Round 1 判定：原有设计不 apply-ready（读回 presented backbuffer + 源决策悬空）。** 用户已拍板：读回源 = 离屏/内部 RT、Readback 泛化到 Buffer+Texture、强化 Graph-internal 解析。**工件已改版**（proposal/design 泛化 + D2 源决策 + D5 内部解析；tasks 重排 1.x-7.x）。Round-1 的 [AUDIT-R1-1..10] 已折叠进新 task 清单中对应项。

**仍未定（需拍板）**：**D5**——Graph-internal 资源解析选 A（导出资源表）还是 B（延长 local manager 生命周期）？此决策影响 task 2.x 的实现路径，apply 前须定稿。
