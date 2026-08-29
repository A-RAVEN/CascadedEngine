## Context

后端目前只有 host→GPU 上行（`ScheduleData`→`copyBufferToImage`、`VulkanBuffer::UploadData`），**无 GPU→host 下行**。经前期代码勘察（workflow `wc4nbadm0`）+ **Round-1 对抗验证（workflow `wf_9ee548b1-d93`，29 agents）** 确认：

- **无任何现成 readback**：grep `Readback/CaptureFrame/Screenshot/ReadTexture/DownloadImage` 全为 0 命中。
- **接口层无统一资源 handle**：`ImageHandle`（Internal/External/Backbuffer，ShaderResourceHandle.h:40-117）与 `BufferHandle`（Internal/External/:119-176）是两个独立类型。
- **stbi_write_png 已链接未用**：`Test/GPUBackendTester/STB_Impl.cpp:1-4`（`STB_IMAGE_IMPLEMENTATION`+`STB_IMAGE_WRITE_IMPLEMENTATION`）。
- **Vulkan 下行零件已在**：`VulkanMemoryManager::MapMemory(:109)`、staging `eTransferSrc` 惯例（VulkanTexture.cpp:424）、`waitForFences`（SubmitBatches）。
- **D3D12 无回读**：只有 host→GPU staging 上行（GPUGraphExecutor.cpp:648-652），无 READBACK heap、无 `CopyBufferRegion`/`CopyTextureRegion`。

**Round-1 定根因（关键，决定了本设计的方向）**：
- **不能读已呈现的 swapchain backbuffer**。`--capture` 若在 `ExecuteGraph`（内已 `Present`：Vulkan :601/:2683，D3D12 `PresentWindows` :2245）之后 `Readback` backbuffer，读到的是已交给 presentation engine 的帧——Vulkan `vkQueuePresentKHR` 后必须重 `vkAcquireNextImageKHR` 才能再用；D3D12 flip-model 已 advance `m_BackBufferIndex` 并置 `D3D12_RESOURCE_STATE_PRESENT`。→ **回调源必须走离屏/内部 RT 或外部资源，而非 presented backbuffer。**
- **Graph-internal 资源无法在 `Readback` 时解析**：Internal 型 ImageHandle/BufferHandle 的资源活在 graph executor 的 local resource manager（stack-local，`ExecuteGraph` 内即析构），`RenderBackend_D3D12` 只持有 m_MemoryManager/m_GPUFrameManager/window 上下文，无对 graph local resource 的引用（RenderBackend_D3D12.cpp:323）。Vulkan 同理（graph local 在 `VulkanGraphExecutor`）。→ **任何"读回 graph-internal 资源"（含 compute buffer）都需先解决该资源的生命周期/导出。**
- 其余修正：`vkCmdCopyImageToBuffer` 的 VUID 为 `-00186`（format feature `-01998`），设计原引 `-07777` 为幻觉；Vulkan barrier 源 scope 应为 `COLOR_ATTACHMENT_OUTPUT`（非 `eTransfer`）；读回 barrier 需回写 window layout 注册表；D3D12 READBACK 行 pitch 对齐（`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256` / `PLACEMENT_ALIGNMENT=512`）。

**调用场景**：测试抓帧（`--capture` → 离屏 RT → `stbi_write_png` → 视觉断言）、compute buffer 数值校验（读回 SSBO/structured buffer → CPU 断言）、稀疏帧分析、临时导出 GPU 中间结果。

## Goals / Non-Goals

**Goals:**
- 提供跨后端通用 `Readback`：**Buffer 与 Texture 都可回读**，调用者给 CPU 目标 span，GPU 系统写入。
- 异步显式同步：`token.Wait()` 后 `dst` 可读；契约独立于引擎底层同步实现。
- **回调源 = 离屏/内部 RT 或外部资源**（不读 presented swapchain backbuffer）。
- 解决 **Graph-internal 资源解析**（读回 compute buffer/内部 RT 的前提）。
- Vulkan / D3D12 各自实现搬运 + 同步。
- 测试接入：抓帧 → PNG → 视觉断言；compute buffer → 数值断言。

**Non-Goals:**
- 不追求零拷贝、不引入"调用者常驻 ReadbackBuffer"（既定决策）。
- 不改现有渲染同步模型。
- 不做 render-pass 内嵌 / shader-driven 读回。
- **不读已呈现的 swapchain backbuffer**。

## Decisions

### D1: 通用读回接口（Buffer + Texture）

接口层 `Interface/RenderInterface` 提供两个入口，共享同一 `IReadbackToken` 契约：

```cpp
class IReadbackToken {
public:
    virtual void Wait() = 0;          // 阻塞直到 GPU 系统将该读回写入 dst 完成
    virtual bool IsReady() const = 0; // 无阻塞查询
    virtual void Reset() = 0;         // 归还内部 staging/sync（幂等）
};

virtual std::unique_ptr<IReadbackToken> Readback(ImageHandle const&,  span<uint8_t> dst) = 0;
virtual std::unique_ptr<IReadbackToken> Readback(BufferHandle const&, span<uint8_t> dst) = 0;
```

- **不**引入统一 resource handle（沿用现有 `ImageHandle`/`BufferHandle`，最小改动、复用具体类型语义）。
- `dst` 所有权在调用者；大小须 ≥ 读回所需字节。**所需字节由资源 descriptor 推导**：Image = `width*height*GetFormatBlockSize(format)`（紧凑）；Buffer = `descriptor.SizeInByte()`。**接口层须校验 `dst.size()` ≥ 所需字节，越界即报错拒绝**（载入 spec 的"载体过小"场景）。**token 不携带资源元数据**——width/height/format 由 `GPUTexture::GetDescriptor()`（/`GPUBuffer::GetDescriptor()`）随时可查，`GetFormatBlockSize(format)` 随时算 bpp；这些是贴图/资源自身属性，不跟随读回 handle。
- `Wait()` 是唯一同步点；`Wait()` 后 `dst` 填好。
- `Reset()` 与 `unique_ptr` RAII 的所有权**定死**（见 D5）：token 的析构**不**释放还在飞的 staging；`Reset()` 显式归还。二者不一致时以 D5 为准。

### D2: 回调源选择（**离屏/内部 RT**；放弃直接读回 swapchain backbuffer）

**读回源 = 离屏/内部 RT 或外部资源；绝不读 presented swapchain backbuffer。** 逐类：

| 源类型 | 目的 | 回读可行？ | 可保证？ | 说明 |
|--------|------|-----------|----------|------|
| **Internal（离屏 RT / graph local 资源）** | 渲染/计算输出 | ✅（需 D5 解析） | ✅ **可控** | **本 change 主路径**：测试渲染到自建离屏 RT、或读回 compute 输出 buffer |
| **External（用户 `CreateGPUTexture`/`CreateGPUBuffer`）** | 用户资源 | ✅（可 downcast） | ✅ 可控 | 持 `shared_ptr`，直接可得后端资源 |
| **Backbuffer（swapchain）** | 窗口呈现 | ❌ | ❌ **不可保证** | **放弃**（见下 portability） |

**为何放弃 backbuffer（决定性）**：
1. **post-present 不可读**：present 同步嵌在 `ExecuteGraph` 内，backbuffer 已交回 presentation engine（Vulkan 须重 acquire；D3D12 置 PRESENT 态）。
2. **`eTransferSrc` 非平台保证**：swapchain `imageUsage = desiredUsage & capabilities.supportedUsageFlags`——`vkCmdCopyImageToBuffer` 读它要求 `eTransferSrc`，而 `supportedUsageFlags` 在某些 surface 上**不含** `eTransferSrc` → 该平台 backbuffer 读不了，**跨平台不可保证**。

**反之，离屏 RT 的 usage 由应用完全控制**（自建 VkImage，无 surface 裁剪），`eTransferSrc` 一定能加 → **回读可保证**。这就是"读回与呈现/swapchain 解耦"的根本原因。

**⚠️ 前置——读回 RT 必须带 `eTransferSrc`（引擎当前缺）**：`VulkanTexture::Init(:120)` 只设 `eTransferDst|eSampled` + 按 accessType，**无 `eTransferSrc`**；`ETextureAccessType` 枚举也无此 flag。因此**现在任何离屏 RT / 用户 texture 都读不了**。→ 须给引擎加"创建带 `eTransferSrc` 的贴图"能力（`ETextureAccessType` 增 `eTransferSrc`/`eReadback`，或读回贴图专用创建路径）。**这是本 change 的新硬前置。**

**视觉断言如何拿画面**：测试用 `--capture <N>` 时，往**自建离屏 RT**（创建时即含 `eTransferSrc|eTransferDst|eColorAttachment`）渲染，再 `Readback(该离屏RT, px)`。读的永远是"刚渲染完、未呈现"的图，规避 post-present，且 usage 可控可保证。

### D4（载入原 D2）：通用 dispatch 矩阵（按资源 kind × 可访问性）

```
Buffer, host-visible / HOST_ACCESS_RANDOM
   → 直接 map（vmaMapMemory / ID3D12Resource::Map）→ memcpy 到 dst → unmap
   → 无需 GPU copy、无需 fence（同步）        ← 最简单
Buffer, device-local
   → 分配 host staging → vkCmdCopyBuffer / CopyBufferRegion → map → memcpy → fence
Image（External / Internal RT）
   → 布局 barrier 到 eTransferSrcOptimal / COPY_SOURCE → vkCmdCopyImageToBuffer / CopyTextureRegion
     → host staging → map → memcpy → fence
```

**Image vs Buffer 差异**：Image 需 layout 转换 + pitch/格式处理；Buffer 是线性字节（无 pitch，size=字节数），host-visible 还能零拷贝直 map。

### D5: Graph-internal 资源解析（实施时定稿 —— **读回只支持 External（调用者持有）资源；graph-internal 读回另立 change**）

Internal 型 `ImageHandle`/`BufferHandle` 的资源由 graph executor 的 **local resource manager** 持有，栈上、`ExecuteGraph` 返回即析构；两个后端都在 `ExecuteGraph` 返回**之前**清空 graph-local 资源映射表（Vulkan `ReleaseAllResources`、D3D12 `Reset`）。

**实施时确认（2026-08-28）**：读回是"把调用者给的资源拷贝到 CPU span"的**纯拷贝**，**不背资源生命周期**。调用者（tester）自己持有读回目标（External：`CreateGPUTexture`/`CreateGPUBuffer`），`Readback` 经 `GetTexturePtr<VulkanTexture>()`/`GetBufferPtr<D3DImageObject>()` 直解，资源 `shared_ptr` 终身有效 → **不需要 D5-A / D5-B**。D5-A（导出资源表）需在 `Release`/`Reset` 前抓表 + 所有权转移、D3D12 还要保 aliased frame-context 存活（真实 UAF 风险），其前提与代码不符；**graph-internal 读回（Internal handle）是一个独立的资源生命周期问题，不在本 change 范围**，从 `Readback` 的职责里剔除，另立 change。

> 影响：`Readback(Internal Handle)` 报错拒绝（"只支持 External"）；读回源 = 调用者自建 External 离屏 RT / 外部 resources。graph-internal（含 compute 输出 buffer）读回**暂缓**。

### D3（载入原 D3）：载体内容形态（定死）

- **Image → 紧凑字节**：后端把图像按 `width*height*GetFormatBlockSize(format)` **紧凑**拷贝到 `dst`（去 pitch padding；`E_R8G8B8A8_UNORM` 即紧凑 RGBA8，`stbi_write_png` 一步到位）。原始带 pitch 拷贝（选项 A）本 change 不提供（如需另案）。
- **Buffer → 原始线性字节**：`dst` 收 buffer 字节（size = `descriptor.SizeInByte()`），无 A/B 之分。
- **载体元数据不在 token 上**：调用者解释 dst 所需的信息（width/height/format/字节数）**随时**由资源自身的 `GPUTexture::GetDescriptor()`（/`GPUBuffer::GetDescriptor()`）提供，bpp 经 `GetFormatBlockSize(format)` 计算——**不是读回 handle 的专属**。读回载体"紧凑字节"契约在此定死（消除"extent*rowPitch 或紧凑像素"歧义），无需 token 暴露 `{width,height,rowPitch,byteSize}`。

### D6: Vulkan 实现路径

- **buffer**：`GetBufferPtr<VulkanBuffer>()` → 若 CPU 可访问（m_MappedPtr 有效）直 map；否则分配 host staging（VMA `HOST_VISIBLE|HOST_ACCESS_RANDOM`, `eTransferDst`）→ `vkCmdCopyBuffer(src, staging, size)` → submit+fence → map → memcpy → unmap。
- **image**：`GetTexturePtr<VulkanTexture>()` / 离屏 RT → 布局 barrier：源 scope=`COLOR_ATTACHMENT_OUTPUT`+`COLOR_ATTACHMENT_WRITE`（生产者），目标=`eTransfer`+`eTransferRead`（copy 读），oldLayout→`eTransferSrcOptimal` → `vkCmdCopyImageToBuffer`（VUID-00186/01998；bufferRowLength=0 紧凑；bufferOffset 4 对齐）→ host staging → submit+fence → map → memcpy → unmap。
- **布局注册表回写**：读回 barrier 改动的 image 布局，须**写回** `VulkanWindowHandle::m_BackBufferResourceStates`/graph 的 layout 状态，防下帧 oldLayout 失配（VUID-01197/-01189）。**对离屏 RT：由离屏 RT 自管布局（RT 读回后保持 `eTransferSrcOptimal` 或转回 `eShaderReadOnly`），下次渲染 barrier 以其为准。**
- **`IReadbackToken`（Vulkan）**：持该次读回的 staging 所有权。

### D7: D3D12 实现路径

- **buffer**：`GetBufferPtr<D3D12BufferObject>()`（或经 graph 导出表）→ CPU 可达 heap 直 map；否则 `D3D12_HEAP_TYPE_READBACK` buffer + `CopyBufferRegion` + 资源 barrier（src `COPY_SOURCE`）+ fence。
- **image**：`D3D12_HEAP_TYPE_READBACK`（`D3D12_HEAP_FLAG_NONE`）→ resource barrier（src `COPY_SOURCE`）→ `CopyTextureRegion`（dst rowPitch/slicePitch 对齐：`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT=256`、`PLACEMENT_ALIGNMENT=512`）→ fence → map → memcpy → unmap。
- **Graph-internal 资源**：经 D5 资源表解析。
- **⚠️ 待修：D3D12 回读当前为同步兜底，需改为异步与 Vulkan 对齐**——实施时因异步 token 自管 per-call COM 生命周期悬垂（`ACCESS_VIOLATION`/`Rip=0x0`，见 Review Log Round 2 #3），暂以"读回函数内同步做完、token 不持 COM"规避；`Wait/IsReady/Reset` 在 D3D12 上为 no-op，造成跨端不对称。**改回异步的路线与验收口径见 `docs/TODO.md`（2026-08-28「D3D12 回读改为异步」条）**（优先复用引擎 `CommandListManager`/`FrameContext`，或正确修复 per-call COM 生命周期）。

### D8: 数据流（测试接入）

**视觉断言（--capture N）**：
```
GPUBackendTester
  ├─ 渲染到自建离屏 RT（而非 backbuffer）
  ├─ std::vector<uint8_t> px(rtW*rtH*4)
  ├─ auto token = backend->Readback(rtImage, px)
  ├─ token->Wait()
  ├─ stbi_write_png("test_output/<test>.png", rtW, rtH, 4, px.data(), rtW*4)   // option B 紧凑 RGBA
  └─ CPU 读图断言（三角形/颜色/非空帧）
```

**数值断言（compute buffer）**：
```
GPUBackendTester
  ├─ 执行含 compute pass 的图（输出到 structured buffer / SSBO = Internal BufferHandle）
  ├─ std::vector<uint8_t> data(bufBytes)
  ├─ auto token = backend->Readback(outBuffer, data)   // D5 经导出表解析 Internal buffer
  ├─ token->Wait()
  └─ CPU 断言数值
```
挂接点：现有测试循环 `ExecuteGraph` 之后（Main.cpp:110-129/192-194 等），源必须是未呈现的离屏 RT / 内部资源（D2）。

## Risks / Trade-offs

- **Graph-internal 资源解析（D5）是最大不确定**：A（导出表）更解耦但需确认 `ExecuteGraph` 结束点与 graph local 资源表可导出性；B 改动小但拉长局部对象生命周期。**apply 前需定稿，否则 Internal 读回不可行。**
- **swapchain usage 无需改动（放弃 backbuffer）**：因不回读 swapchain backbuffer，无需给其加 `eTransferSrc`。但**离屏 RT 必须带 `eTransferSrc`**——引擎当前 `VulkanTexture::Init(:120)` 不加、`ETextureAccessType` 无此 flag，**不能假设离屏 RT"天然含 eTransferSrc"**；故本 change 需新增"创建带 `eTransferSrc` 的读回 RT"前置（D2）。
- **跨后端字节对称**：image 统一解码到紧凑 RGBA（Vulkan/D3D12 各自 swizzle 到同色序），PNG/数值断言跨端一致；buffer 原始字节天然一致。
- **一次 memcpy**：低频抓帧可接受；不做零拷贝。
- **离屏 RT 新增成本**：测试需自建+渲染到离屏 RT（`CreateGPUTexture` + renderpass 到该 RT），比"直接回读 backbuffer"多一步，但规避 post-present 且 D3D12 可解析（外部纹理）。
- **API 外部正确性**：`vkCmdCopyImageToBuffer`（VUID-00186/01998）、`vkCmdCopyBuffer`、`CopyTextureRegion`/`CopyBufferRegion`（READBACK 行 pitch 256/512）须按 CLAUDE.md 用 MCP 工具联网核对并引用真实 URL。

## Migration Plan
1. 定稿 D5（Graph-internal 资源解析 A/B）——**前置决策**。
2. 接口层 `Readback(ImageHandle/BufferHandle, span)` + `IReadbackToken`。
3. Vulkan 下行（buffer+image）+ 布局注册表回写。
4. D3D12 下行（buffer+image）+ READBACK 对齐。
5. 测试接入（--capture → 离屏 RT → PNG；compute buffer → 数值断言）。
6. `python build.py --config Debug` → 回归。
回滚：各点 `git revert`。

## Open Questions
- ~~**D5 A vs B**~~ —— **已定：读回只支持 External（调用者持有）资源；graph-internal 读回不在本 change**（用户 2026-08-28 定向：读回=纯拷贝、不背资源生命周期）。实施时确认两个后端都在 `ExecuteGraph` 返回前清空 graph-local 表、D5-A 需在 `Release`/`Reset` 前抓表+所有权转移+D3D12 保 frame-context（真实 UAF 风险），故不做 D5；`Readback(Internal)` 报错拒绝，graph-internal 读回另立 change。
- **离屏 RT 由谁管**：**已定 tester 自建（External）**——`TestReadback` 用 `CreateGPUTexture(desc, eTransferSrc|eTransferDst|eRT)` 自建离屏 RT，渲染后 `Readback` 读回。已落地。
- **`--capture` 的离屏 RT 来源**：**已定——capture 帧渲染到自建离屏 RT**（带 `eTransferSrc|eTransferDst|eColorAttachment`），与 backbuffer 完全解耦。现有 7 测试**主 pass 仍渲染到 backbuffer 并正常 present**（headless 下窗口可见/正常关闭），仅 `--capture` 的指定帧额外走离屏 RT 路径（该帧渲到 RT 读回），不改变其它帧/测试的 present 行为。已在 D2/D8 明确。
