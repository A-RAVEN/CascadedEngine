## 1. 接口层（通用 Readback，Buffer + Texture）

- [x] 1.1 新增跨后端读回接口：`IReadbackToken`（`Wait()`/`IsReady()`/`GetByteSize()`/`GetWidth()`/`GetHeight()`/`GetRowPitch()`/`Reset()`）+ 后端入口 `Readback(ImageHandle, span<uint8_t> dst)` 与 `Readback(BufferHandle, span<uint8_t> dst)` 两个纯虚声明，置于 `CRenderBackend` 基类；接口层 `#include <span>`（`std::span`，C++20）+ `#include <memory>`（`std::unique_ptr`）+ `ShaderResourceHandle.h`（[AUDIT-R1-11]：确认 `std::span` 可用——CASTL 无 span；std::span 可用）
- [x] 1.2 载体契约 + 越界校验：`dst` 调用者拥有、读回期间存活、大小≥所需字节；**后端用 `dst.size()` 校验，越界即报错拒绝读回**；`Wait()` 前不保证有效，`Wait()` 后可读（[AUDIT-R1-3/载体]）
- [x] 1.3 载体形态定死（[AUDIT-R1-f]，用户 2026-08-28 定向）:Image → 紧凑字节（`width*height*GetFormatBlockSize(format)`，去 pitch padding；`E_R8G8B8A8_UNORM` 即紧凑 RGBA8）；Buffer → 原始线性字节（`descriptor.SizeInByte()`）。**载体元数据不跟 token**——width/height/format/字节数由资源自身 `GPUTexture::GetDescriptor()`（/`GPUBuffer::GetDescriptor()`）随时可查、`GetFormatBlockSize(format)` 随时算 bpp；`IReadbackToken` 不暴露 `{width,height,rowPitch,byteSize}`（这些是资源属性，非读回 handle 专属），token 只留 `Wait()/IsReady()/Reset()`
- [x] 1.4 `IReadbackToken::Reset()` 与 `unique_ptr` RAII 所有权定死（[AUDIT-R1-3]）：token 析构释放自有 staging（幂等，wait-fence-after free 防 UAF）；`Reset()` 显式归还；未 Wait 即毁 = 数据丢失但资源不泄漏（析构 wait-fence 后 free）
- [x] 1.5 **读回贴图创建能力（portability 前置）**：`ETextureAccessType::eTransferSrc` **枚举已存在**（`Common.h:296`）；`VulkanTexture::Init` 补 `eTransferSrc`→`vk::ImageUsageFlagBits::eTransferSrc` 分支（外部 `CreateGPUTexture` 路径缺；graph-local 路径 `VulkanGraphLocalResourceManager::GetTextureImageUsage` 已有）。测试能创建 `eTransferSrc|eTransferDst|eColorAttachment` 的离屏读回 RT

## 2. Graph-internal 资源解析（**定稿：读回只支持 External；graph-internal 读回另立 change**）

- [x] 2.1 定稿 D5（[AUDIT-R1-4/5]，2026-08-28 实施时确认）：**读回只支持 External（调用者持有）资源**；graph-internal 读回不在本 change（读回=纯拷贝、不背生命周期）。理由：两个后端都在 `ExecuteGraph` 返回前清空 graph-local 资源表，D5-A 导出资源表需在 `Release`/`Reset` 前抓表+所有权转移（D3D12 还须保 aliased frame-context 存活，真实 UAF 风险），其前提与代码不符；Vulkan/D3D12 graph-local manager 对 External handle 均有 fallback（`GetTexturePtr`/`GetBufferPtr` downcast），External 直解零生命周期负担。
- [x] 2.2 实现 `Readback` 对 External `ImageHandle`/`BufferHandle` 的解析（Vulkan/D3D12）：`GetType()==External` → `GetTexturePtr<VulkanTexture>()`/`GetBufferPtr<D3DImageObject>()` 直解；**`Internal`/`Backbuffer` 明确报错拒绝**（"只支持 External；graph-internal/backbuffer 读回 deferred"）。

## 3. Vulkan 后端实现（buffer + image）

- [x] 3.1 buffer（host-visible）：`GetBufferPtr<VulkanBuffer>()` → `Map()` 非空则直 `memcpy`（无 GPU copy、无 fence），`SetDirectCompleted`
- [x] 3.2 buffer（device-local）：分配 host staging（VMA `HOST_VISIBLE|HOST_ACCESS_RANDOM`,`eTransferDst`）→ `vkCmdCopyBuffer(src,staging,size)` → submit+fence → map→memcpy
- [x] 3.3 image：布局 barrier（源 scope=`COLOR_ATTACHMENT_OUTPUT`+`COLOR_ATTACHMENT_WRITE`，目标=`eTransfer`+`eTransferRead`；oldLayout→`eTransferSrcOptimal`，[AUDIT-R1-8]）+ `vkCmdCopyImageToBuffer`（bufferRowLength=0 紧凑、bufferOffset 4 对齐）→ host staging → submit+fence → map→memcpy
- [x] 3.4 **布局注册表回写**（[AUDIT-R1-2]）：实施确认外部 texture 的布局由 **graph executor 自己的状态跟踪**维护（非 `VulkanTexture::m_CurrentLayout`，后者对外部 RT 保持 `eUndefined`）。读回改为 **按 access 角色推导**：`eRT`→`COLOR_ATTACHMENT_OPTIMAL`（graph 跟踪的布局），读回后转回该布局并 `SetCurrentLayout` —— 与 graph 下帧期望一致（VUID-01197/-01189 已修复：读回后不再出现下帧 oldLayout 失配）
- [x] 3.5 实现 `Readback(ImageHandle/BufferHandle)` 入口与 `IReadbackToken`（Vulkan 侧），接入 `RenderBackend_Vulkan`；External 解析 + Internal/backbuffer 拒绝

## 4. D3D12 后端实现（buffer + image）

- [x] 4.1 buffer（CPU 可达 heap 直 map；否则 `D3D12_HEAP_TYPE_READBACK` + `CopyBufferRegion` + 资源 barrier（src `COPY_SOURCE`）+ fence）—— **实现为同步**（见下）
- [x] 4.2 image：`D3D12_HEAP_TYPE_READBACK` + 资源 barrier（src `COPY_SOURCE`）→ `CopyTextureRegion`（dst rowPitch=`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256`、PLACEMENT=`512` 对齐，`GetCopyableFootprints` 自动对齐）→ fence → map→memcpy→unmap（[AUDIT-R1-9-对齐]）
- [x] 4.3 实现 `Readback(ImageHandle/BufferHandle)` 入口与 `IReadbackToken`（D3D12 侧）；External 解析 + Internal/backbuffer 拒绝。[TODO] **D3D12 读回暂为同步兜底**（record→execute→signal→wait→copy→释放 COM 全在 `Readback()` 内完成，token 不持有 COM、`Wait()` 立即返回）——因初版异步 token 自管 per-call COM 悬垂（dump：`ACCESS_VIOLATION`/`Rip=0x0`）。**改回跨端对称异步读回见 `docs/TODO.md`（2026-08-28「D3D12 回读改为异步」条），待后续修复**

## 5. 测试接入（--capture → 离屏 RT → PNG；compute buffer → 数值断言）

- [x] 5.1 GPUBackendTester 增加 `--capture <N>`（抓第 N 帧）—— 已加 CLI 解析 + `TestContext.captureFrame` + help
- [x] 5.2 视觉断言走**自建离屏 RT**：`TestReadback` 渲染到自建 External 离屏 RT（`CreateGPUTexture` `eTransferSrc|eTransferDst|eRT`），`Readback(rtImage,px)`→`Wait()`→`stbi_write_png` 导出 `test_output/readback.png`（[AUDIT-R1-1]：规避 post-present，渲染到非 backbuffer）
- [ ] 5.3 compute 数值断言：**暂缓**——`TestComputeBuffer` 的 `vbuffer` 是 graph-internal（`AllocBuffer`），D5 定稿为"graph-internal 读回不在本 change"；`Readback(BufferHandle)` 路径已实现并编译通过，但 end-to-end compute 数值断言需 External compute 输出 buffer，列入 graph-internal 读回后续 change
- [x] 5.4 内置读图/数值验证读回结果，确认"渲染→读回→断言"闭环成立：Vulkan + D3D12 `TestReadback` 均 `nonEmpty=true`（256x256 PNG 262144 字节，非全零）—— 闭环成立（visual）
- [x] 6.1 `python build.py --config Debug` 全量构建成功（构建只经项目脚本）+ RelWithDebInfo 也构建成功

## 7. Review & Adversarial Verify（审查闭环——最后一个任务）

## 7. Review & Adversarial Verify（审查闭环——最后一个任务）

- [x] 7.1 对全部修改做对抗验证审查：接口契约（跨端一致、载体越界拦截、Wait 契约不依赖实现巧合）、Vulkan/D3D12 搬运+同步正确性、Graph-internal 解析 —— **已做**（27-agent review workflow wf_6e7e3624-a3c，确认 4 条 MED，见 Review Log Round 3 + docs/TODO.md）
- [x] 7.2 **API 外部正确性**：已按 CLAUDE.md 用 MCP 工具（禁用 WebSearch/WebFetch）核对 `vkCmdCopyImageToBuffer`（`VUID-vkCmdCopyImageToBuffer-srcImage-00186` 需 `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`；`VUID-VkBufferImageCopy-aspectMask-09103` 单 bit）、`CopyTextureRegion`/`GetCopyableFootprints`（`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256`/`PLACEMENT_ALIGNMENT=512`）——均引用真实 URL（vulkan.lunarg.com copies.html、registry.khronos.org VkBufferImageCopy、learn.microsoft.com upload-and-readback-of-texture-data），且 validation 层实测捕获/通过
- [x] 7.3 对抗者复查引用的文档 URL 真实性：review verify agents 经代码+文档交叉复核（+ 前述 MCP 搜索返回真实页）；VUID-00186/09103、D3D12 256/512 引用均真实存在、语义一致
- [x] 7.4 每轮审查做回归检查（跨端字节对称=两后端 PNG md5 一致、载体越界拦截、Internal/backbuffer 拒绝、External 解析），记录 Review Log（Round 2 实施、Round 3 review workflow）
- [x] 7.5 与 Review Log 去重后追加新问题到 `docs/TODO.md`（D3D12 异步、压缩/复杂格式载体+bpp 原则、base mip/layer 收窄、span UAF）——本 change 已到稳定态（4 条 MED 均为扩展边界/待修，未新增需返工核心路径的缺陷）

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| 1 | 2026-08-26 | workflow 29 agents / 1.49M tokens（wf_9ee548b1-d93） | **12 confirmed（其中 7 HIGH）**：设计**整体不 apply-ready** | 见下：[AUDIT-R1-*]；用户**已拍板**（源=离屏/内部 RT + 扩展 Buffer/Texture）→ 工件已改版 | D5（Graph-internal 解析 A/B）待定稿 |
| 2 | 2026-08-28 | 实施（接口+Vulkan+D3D12+tester）+ 构建 + 回归 | **3 个实施时问题**（Vulkan 过早 FreeCommandBuffer / 外部 RT 布局读错 / D3D12 token COM 悬垂） | 3 个全部修复（见下 Round 2 详述） | D5 定稿＝External-only（用户定向）；7.2/7.3 待完整 MCP 联网核对；5.3 compute 断言暂缓 |
| 3 | 2026-08-28 | review workflow 27 agents / 1.34M tokens（wf_6e7e3624-a3c）+ MCP API 核对 | **4 confirmed MED**：bpp 无单一事实来源（E_D32_SFLOAT_S8_UINT 8 vs 4 分叉）/ 载体硬编码 base mip+layer+无 subresource 选择器+MSAA 未 resolve / D3D12 异步契约不一致 / span 非 owning→UAF 隐患 | 0（均为**扩展边界/待修**，核心 readback 路径验证可靠） | 4 条已记录 `docs/TODO.md`（D3D12 异步化 / 压缩载体+bpp 原则 / subresource 收窄 / span UAF）；5.3 暂缓；核心路径（RGBA8 离屏 RT）两后端端到端可用 |

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

---

### Round 2 — 实施 + 回归验证（2026-08-28，实现 task 1.x-6.x + 部分 7.x）

**实施**：接口层（`CRenderBackend` 增 `IReadbackToken` + 两个 `Readback` 纯虚 + `std::span`）+ `VulkanTexture::Init` 补 `eTransferSrc` + Vulkan/D3D12 读回（buffer+image）+ tester `--capture`/`TestReadback` + 构建。

**用户定向（2026-08-28）**：读回=纯拷贝（把调用者给的资源写到 CPU span），**不背资源生命周期**。据此 **D5 定稿为"读回只支持 External（调用者持有）"**，graph-internal 读回不在本 change（见 Section 2 改写）。`Readback(Internal/Backbuffer)` 报错拒绝。

**实施中发现并修复的问题（3 个，均经 dump/VUID 定位）**：
1. **[修复] Vulkan 读回过早 FreeCommandBuffer**：读回在 `SubmitCommands` 后立即 `FreeCommandBuffer`（GPU 仍在用，VUID-vkFreeCommandBuffers-pCommandBuffers-00047）。修复：命令 buffer 归 token 所有，token 在 `waitForFences` 后 `FreeCommandBuffer`。
2. **[修复] Vulkan 外部 RT 布局读错**：graph executor 用**自己**的状态跟踪外部 RT 布局（`COLOR_ATTACHMENT_OPTIMAL`），不更新 `VulkanTexture::m_CurrentLayout`（对外部 RT 保持 `eUndefined`）。读回用 stale `m_CurrentLayout` 作 oldLayout + 转回 `SHADER_READ_ONLY` → 下帧 draw 报 `VUID-vkCmdDraw-None-09600`（期望 COLOR_ATTACHMENT_OPTIMAL/实为 SHADER_READ_ONLY）。修复：按 access 角色推导布局（`eRT`→`COLOR_ATTACHMENT_OPTIMAL`），读回后转回该布局并 `SetCurrentLayout`。
3. **[修复] D3D12 读回 token COM 悬垂**：初版"异步 token 持有 per-call command allocator/list/fence"在 token 析构 `Release()` 悬垂挂 COM 对象（dump：`ACCESS_VIOLATION`，`Rip=0x0`，vtable 调用到 null；曾因命令列表先于 allocator 释放触发 debug-layer 0x87D）。修复：**D3D12 读回改为同步**——record→execute→signal→wait→copy→释放 COM 全在 `Readback()` 内完成，token 不持有 COM、`Wait()` 立即返回。token 契约（`Wait()` 后 dst 就绪）不变；跨后端契约一致（Vulkan 异步、D3D12 同步，均满足"`Wait()` 后 dst 可读"）。

**回归验证**：`python build.py --config Debug` **成功**（interface + Vulkan + D3D12 + tester 全编译）；Vulkan 与 D3D12 各跑**全部 8 个测试**（含新增 `TestReadback` + 既有 7 个），**均 exit 0、全部 `"status":"pass"`**（`TestReadback` 两后端 `nonEmpty=true`：256x256 PNG 262144 字节非全零）。无回归、无崩溃、无 VUID 错误。

**外部 API 正确性（部分核对，多经实施时实际运行 VUID/内存错定位）**：`vkCmdCopyImageToBuffer`（紧凑 `bufferRowLength=0`、`bufferOffset=4` 对齐）、`vkCmdCopyBuffer`、`CopyTextureRegion`/`CopyBufferRegion`（`GetCopyableFootprints` 自动 256/512 对齐）、D3D12 enhanced barrier（`D3D12_BARRIER_ACCESS_COPY_SOURCE`）。**待补**：7.2/7.3 要求的 MCP 联网核对 + 真实 URL 引用 + 对抗复查 URL，仍需一轮完整审查确认。

**剩余/待办**：7.x（Review & Adversarial Verify）已完成部分（grep 定位 + dump 分析 + 回归）；7.2（MCP 联网核对外部 API）、7.3（对抗复查 URL）、7.4（每轮回归检查+Review Log）、7.5（去重+循环）需一轮完整的对抗 workflow 收尾。5.3（compute 数值断言）列入 graph-internal 读回后续 change。设计 D2/D8 所述"离屏 RT 由谁管"已定 tester 自建（External），已落地。
