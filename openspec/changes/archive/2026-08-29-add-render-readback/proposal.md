## Why

后端目前只有 host→GPU 上行（`ScheduleData`→`copyBufferToImage`、`VulkanBuffer::UploadData`），**无 GPU→host 下行**。渲染/计算结果**只能看数字**（退出码 + validation log），无法直接验证"内容对不对"——画面（视觉断言）与计算输出（数值断言）都验证不了。

本 change 给后端添加**跨后端 GPU 资源回读（readback）**能力：把 GPU 侧**任意资源（Buffer 或 Texture）**回读成调用者提供的 CPU span，支撑：
- **视觉断言**：抓一帧渲染结果 → PNG → CPU 读图断言（三角形/颜色/非空帧）；
- **数值断言**：读回 compute 输出 buffer（structured buffer / SSBO）→ CPU 断言数值；
- 抓帧、分析、临时导出等任意"把 GPU 结果拿回 CPU"的场景。

**为什么现在**：当前代码库无任何下行 readback（grep `Readback/CaptureFrame/Screenshot/ReadTexture/DownloadImage` 0 命中）；测试只能靠 `exit 0 + validation log` 间接判断。近期已确认 CPU 侧具备图像读取能力（可读 PNG），打通"渲染→读回→验证"闭环的前提就是这条读回管线。

## What Changes

- **接口层新增通用 `Readback`（跨后端抽象，Buffer + Texture 都读）**：`backend->Readback(ImageHandle, span<uint8_t> dst)` 与 `backend->Readback(BufferHandle, span<uint8_t> dst)` 两个入口，共享 `IReadbackToken` 契约（`Wait()` 是唯一同步点）。调用者提供目标 CPU span（载体），GPU 系统只负责把资源字节写入它。
- **异步显式同步**："异步声明 + 显式 `Wait()` 确定可读"，不依赖引擎是否每帧 `waitIdle`。
- **不追求零拷贝**：GPU 系统内部统一经 host-visible staging 中转 + 一次 memcpy 写入调用者 span。
- **回调源 = 离屏/内部 RT 或外部资源（放弃直接读回 swapchain backbuffer）**：**Round-1 对抗验证 + 用户 portability 拍板确定**——① `--capture` 若在 `ExecuteGraph`（内已 Present）后读 backbuffer，读到的是已交给 presentation engine 的帧（invalid/UAF/帧漂移）；② **`eTransferSrc` 非平台保证**：swapchain `imageUsage = desiredUsage & supportedUsageFlags`，`vkCmdCopyImageToBuffer` 读它要求 `eTransferSrc`，而 `supportedUsageFlags` 在某些 surface 不含 → 该平台 backbuffer 读不了，**跨平台不可保证**。故视觉断言走**测试自建离屏 RT / 用户外部纹理**（self-created image usage 完全可控，`eTransferSrc` 一定能加 → 可保证），不从 presented backbuffer 读。
- **补"读回贴图创建能力"（新硬前置）**：`ETextureAccessType::eTransferSrc` **枚举已存在**（`Common.h:296`）；但外部 `VulkanTexture::Init(:120)` 路径**未**给 `imageInfo.usage` 加 `eTransferSrc`（graph-local 路径已有）。故当前经 `CreateGPUTexture` 的离屏 RT/用户 texture 读不了 —— 补 `VulkanTexture::Init` 的 `eTransferSrc` 分支即可。
- **Vulkan 后端**：buffer（host-visible 直 map；device-local 用 `vkCmdCopyBuffer` 到 host staging）+ image（布局 barrier + `vkCmdCopyImageToBuffer`）+ `waitForFences`。
- **D3D12 后端**：`D3D12_HEAP_TYPE_READBACK` + `CopyBufferRegion`/`CopyTextureRegion` + 资源 barrier + fence。
- **读回只支持 External（调用者持有）资源；graph-internal 读回另立 change（定稿）**：两个后端都在 `ExecuteGraph` 返回前清空 graph-local 资源表，`Readback` 解析 Internal 涉及"延长生命周期 / 导出资源表"且 D3D12 有真实 UAF 风险。经用户 2026-08-28 定向（读回=纯拷贝、不背资源生命周期）：读回源 = **调用者自建/持有的 External 资源**（`CreateGPUTexture`/`CreateGPUBuffer`，`shared_ptr` 终身有效，`GetTexturePtr`/`GetBufferPtr` 直解，零生命周期负担）；`Readback(Internal/Backbuffer)` 报错拒绝。graph-internal（含 compute 输出 buffer）读回属独立生命周期问题，另立 change。
- **测试接入**：GPUBackendTester 增加 `--capture <N>`（抓第 N 帧的离屏 RT），经 `stbi_write_png`（`STB_Impl.cpp` 已链接）导出 `test_output/*.png`；并提供 compute buffer 数值断言入口。

## Capabilities

### New Capabilities
- `resource-readback`: GPU 资源回读接口——调用者提供目标 CPU span，异步声明 + 显式 `Wait()` 同步，后端（Vulkan/D3D12）各自实现搬运与同步；支持 Buffer 与 Texture 两种资源；不追求零拷贝。

### Modified Capabilities
无（本次为纯接口新增，不改变既有渲染行为契约）。

## Impact

- `Interface/RenderInterface/` — 新增 `Readback(ImageHandle, span)` / `Readback(BufferHandle, span)` / `IReadbackToken` 声明 + `span` 载体契约；**`ETextureAccessType` 增 `eTransferSrc`/`eReadback`**（读回贴图 usage 前置）。
- `VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp` — `Init` 按 accessType 增 `eTransferSrc` usage（读回 RT 前置）。
- `VulkanRenderBackendNew/private/` — buffer/image 下行 helper（`vkCmdCopyBuffer`/`vkCmdCopyImageToBuffer` + 布局 barrier + `waitForFences`）；Graph-internal 资源导出表/生命周期。
- `D3D12RenderBackend/private/` — `D3D12_HEAP_TYPE_READBACK` + `CopyBufferRegion`/`CopyTextureRegion` + fence；Graph-internal 资源解析。
- `Test/GPUBackendTester/` — 回读 + `stbi_write_png` 导出（visual）+ compute buffer 数值断言（numeric）+ `--capture`。
- **API 外部正确性**：`vkCmdCopyImageToBuffer`（VUID-00186/01998）、`vkCmdCopyBuffer`、`CopyTextureRegion`/`CopyBufferRegion`（READBACK 行 pitch 256/512 对齐），按 CLAUDE.md 用 MCP 工具引用官方文档验证。

## Non-goals

- 不追求零拷贝 / 不引入"调用者常驻 ReadbackBuffer"抽象（既定决策，低频抓帧一次 memcpy 可接受）。
- 不改动现有渲染管线同步模型。
- 不实现 shader-driven / render-pass 内嵌读回。
- **不读已呈现的 swapchain backbuffer**（源头规避 post-present，改走离屏/内部 RT）。
- 不处理 validation layer 其它告警 / 既有崩溃修复（另案）。
