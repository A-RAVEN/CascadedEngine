## Context

跨后端读回链路（`Readback` @ `Interface/RenderInterface/header/CRenderBackend.h:78-79`）承载着测试端视觉断言（`--capture` / `TestReadback`）。现状四处会静默出错：

1. **载体字节数来源反模式**：接口注记（`CRenderBackend.h:38-44`）让调用者按 `GetFormatBlockSize` 推 `W*H*bpp` —— 这正是 `docs/TODO.md` 压缩条目「原则 3」点名要避免的（公开面应为单一字节数查询，不暴露 per-pixel bpp）。
2. **bpp 三处实现分叉**：后端私有 `GetReadbackBpp`（`VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp:1009`）与 `D3D12ReadbackBpp`（`D3D12RenderBackend/private/RenderBackend_D3D12.cpp:480`）都各自复制了一份 `Common.h:237 GetFormatBlockSize`，且都缺 `E_D32_SFLOAT_S8_UINT`（后端 `default=4`，主函数 `=8`）。
3. **subresource 硬编码**：`Readback(image)` 拷贝区硬编码 `mip0/layer0`（Vulkan `RenderBackend_Vulkan.cpp:1235-1237`、D3D12 `RenderBackend_D3D12.cpp:705`），但入口不校验 `desc.mipLevels/layers/samples/textureType`——多 mip/多 layer/非 2D 被静默截断，MSAA 不 resolve。
4. **dst 生命周期靠 prose**：token 持调用者裸 `std::span`（Vulkan `:1115`、D3D12 `:602`），异步 `Wait()` 才 `memcpy(m_Dst.data(), ...)`（Vulkan `:1056`）；契约只在接口注释 `CRenderBackend.h:44`。

约束：主 spec `openspec/specs/resource-readback/spec.md` 已承诺「异步声明 + 显式同步」「一次 memcpy 不追求零拷贝」「仅引擎自建带 `eTransferSrc` 的离屏 RT」——本 design 不改变这些语义，只补「载体契约」。

## Goals / Non-Goals

**Goals:**
- 载体字节数**唯一公开来源 = `GetImageByteSize(width, height, format)`**（内部按压缩/非压缩分支）；`GetFormatBlockSize` 退为内部中间量；两端弃用私有 bpp 表。
- 未支持格式显式拒绝（替代静默 `default=4`）。
- subresource 收窄（仅单 mip / 单 layer / 非 MSAA / `eTextType::e2D`），越界显式拒绝（替代静默截断）。
- dst 生命周期契约显式化 + 后端在**写入 `dst` 前**防御性 `dst.size()` 校验（防越界写）。

**Non-Goals:**
- D3D12 回读改异步（独立 change `fix-readback-async`，见「迁移」）。
- **压缩/复杂格式块线性读回载体**（独立后续）——压缩读回是块线性（`ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐）非像素线性，解释需 format+块尺寸+尺寸+块解码器（bcdec），`stbi_write_png` 写不了，bpp 对压缩无布局意义；本 change 用 `GetImageByteSize` 内部压缩分支 + `Readback` 显式拒绝压缩，绝不用 `W*H*bpp` 套压缩。
- subresource 选择器 / MSAA resolve 扩展（收窄而非扩展）。
- 格式 → canonical-RGBA 归一化。
- swapchain backbuffer 读回；零拷贝抽象（主 spec 已排除）。

## Decisions

### D1: 载体字节数单一事实来源 = 公开 `GetImageByteSize(width, height, format)`
- 新增公共 `GetImageByteSize(width, height, format)`（`Common.h`）：返回该图紧凑数据的字节数，是**唯一公开**字节数基准。非压缩单平面格式 = `width*height*GetFormatBlockSize(format)`；**压缩分支（未来目标，压缩格式未入枚举故当前不可达/返回 0）** = 块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐；**仅无紧凑布局的未知格式返回 0**。
- **字节数 ≠ 可读性（正交信号）**：`GetImageByteSize` 非零只表示尺寸可算，**不代表** `Readback` 会接受；可接受性由 D2 的支持集**单独界定**。
- **gate 先行**：`GetFormatBlockSize` 对未知格式 `default=4`，故必须**先判定**「该格式在 `GetFormatBlockSize` 有显式 case 且非压缩、非 multi-aspect、可紧凑复制」，命中才应用公式，否则直接返回 0（否则会把未知格式按 `default=4` 算成 `W*H*4`，静默回潮）。
- 两端 `Readback` 弃用私有 `GetReadbackBpp` / `D3D12ReadbackBpp`，改调 `GetImageByteSize`。
- **为何依据 TODO 原则 3 而非「沿用 `GetFormatBlockSize` + 调用者自己 `W*H*bpp`」**：TODO 原则 3 明确公开面 = 单一尺寸查询 `GetImageByteSize`、不暴露 per-pixel bpp。调用者需要的是「读回要多少字节」，不是「每像素几字节」。之前把 `GetFormatBlockSize` 作为公开字节数来源是**反模式**。**澄清一个常见混淆**：D3D12 行解压仍需 `width*GetFormatBlockSize` 作为紧凑 `dstStride` —— 这是**内部中间量**，原则 3 明确允许；它**不**意味着可以把它暴露给调用者当字节数基准。二者冲突，选原则 3。

### D2: 读回支持集 = 独立白名单（与 `GetImageByteSize` 返回值解耦）
- **支持集（可读回）** = **单平面（非 multi-planar）、非压缩、可紧凑复制、非深度-模板 multi-aspect、`GetFormatBlockSize` 有显式 case** 的格式。`Readback(image)` 对**不在支持集**的格式报错 + `return nullptr`（压缩 / multi-aspect 深度-模板 / 未知/无紧凑布局）。不再单独一个 `IsReadbackSupportedFormat`（避免分叉；支持判定在 `Readback` 内以显式支持集为准）。**注意**：`GetImageByteSize` 只对「无紧凑布局/尺寸不可算」的格式返回 0；对「可算尺寸但**不在支持集**」（如压缩、multi-aspect 深度-模板）返回**非零**——故**不支持判定必须走支持集，不能只信 `GetImageByteSize==0`**。
- **单深度（单 aspect）**：`E_D16_UNORM`、`E_D32_SFLOAT` → **归入可读支持集**（单平面、非压缩、可紧凑复制、非 multi-aspect）。
- **深度+模板组合（multi-aspect）**：`E_D24_UNORM_S8_UINT`、`E_D32_SFLOAT_S8_UINT` → **读回拒绝**——需按 aspect/plane 选择 + D3D12 深度-模板 256 行对齐，且深度/模板值解释属格式语义（本 change 非目标）。`GetImageByteSize` 仍返回其 packed 字节数（`E_D32_SFLOAT_S8_UINT` = `W*H*8`，供尺寸查询/未来），但 `Readback` 对 multi-aspect 深度-模板返回 null token。
- **保留 bpp 值对齐**：`GetFormatBlockSize=8` 唯一来源（删私有表即消除 8 vs 4 分叉）——TODO 待修③ 只要求"补 bpp 值 case"，本 change 仅对齐值、**不**声称支持深度-模板读回。
- **诚实注（IsCompressedFormat 空转）**：`IsCompressedFormat`（`Common.h:261`）现在恒 `false`（压缩格式未入枚举）。故「非压缩」**不可依赖** `IsCompressedFormat` 表达；`GetImageByteSize` 的压缩分支/支持判定依赖**显式白名单**，规范不得把 `IsCompressedFormat==false` 写成可靠前提。
- **诚实注（IsCompressedFormat 空转）**：`IsCompressedFormat`（`Common.h:261`）现在恒 `false`（压缩格式未入枚举）。故「非压缩」**不可依赖** `IsCompressedFormat` 表达；`GetImageByteSize` 的压缩分支/支持判定依赖**显式白名单**，规范不得把 `IsCompressedFormat==false` 写成可靠前提。

### D3: subresource 收窄 —— 端点显式校验并拒绝
- `Readback(image)` 取 descriptor 后校验：`desc.mipLevels==1 && desc.layers==1 && desc.samples==EMultiSampleCount::e1 && desc.textureType==ETextureType::e2D`，任一越界 → `CA_LOG_ERR` + `return nullptr`。
- **注意 `desc.samples` 是 `EMultiSampleCount`（enum class，`Common.h:397-406`，`e1=0`）**：与 `==1`/`>1` 不可编译/语义反（`==1`=e2x MSAA、`>1` 漏 e2）；正确用 `samples != EMultiSampleCount::e1`。（修正原稿 `samples==1` 缺陷。）
- 收窄后拷贝区固定 `{mip0, layer0, layerCount=1}`（Vulkan `region.imageSubresource={0,0,1}`；D3D12 `GetCopyableFootprints(...,0,1,0,...)` 子资源 0）——因已校验单 mip/单层/2D，这些值即正确。
- 替代方案（选择器 / resolve）留作后续扩展，本 change 不新增接口参数。

### D4: dst 生命周期契约 + 后端在「写入 dst 前」防御校验
- 接口注释（`CRenderBackend.h` `IReadbackToken` / `Readback`）显式写明：**调用者必须保持 `dst` 指代的底层内存存活、且大小不缩水，直到 `Wait()` 返回（含 `Wait` 前不 resize/释放承载 `dst` 的容器）**；载体字节数以 `GetImageByteSize` 为准。
- 后端在**把数据写入 `dst` 前**校验 `dst.size() < 所需字节`：不足 → `CA_LOG_ERR` + 跳过写入（不越界）。写入点随后端同步模型不同：**Vulkan 异步 = `Wait()` 内 `memcpy` 前**（`:1054-1056`）；**D3D12 同步 = `Readback()` 内**（`:635` 已校验，保留并作为兜底）。契约表述**独立于引擎底层同步实现**（主 spec「契约 SHALL 独立于引擎底层同步实现」）。
- **诚实声明**：防御校验只防「size 不足」，**防不了「底层内存已被释放/重分配」**（UAF 需调用者守约，无法以库态检测；与主 spec「由调用者保证」一致，此处仅把契约写显式 + 加一道 size 防线）。

## Risks / Trade-offs

- **[BREAKING] 原先 `default=4` 静默读回的未知格式现在显式失败** → Mitigation：全仓核对读回调用点（`GPUBackendTester` 仅用离屏 `E_R8G8B8A8_UNORM`）；若有依赖需同步。测试证据范围：RGBA8 路径。
- **[BREAKING] 多 mip / 多 layer / MSAA / 3D 读回现被拒绝** → 若未来需要，开"subresource 选择器 / MSAA resolve"变体，本 change 不背。
- **[白名单/分支漂移]** `GetImageByteSize` 压缩/非压缩分支与 `GetFormatBlockSize` 明确 case 需保持一致 → 对抗审查核对；压缩分支**当前为记录目标（块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐），因压缩格式未入枚举故当前返回 0（不可达）**；若 `ETextureFormat` 未来加压缩格式，分支从"返回 0"收敛为上述块线性公式。
- **[IsCompressedFormat 恒 false]** 「非压缩」相关表述不可依赖 `IsCompressedFormat` → 以显式白名单 + `GetImageByteSize` 分支为准；规范已诚实注明。
- **[契约软限制]** 防御校验无法检测「dst 底层内存失效」 → 依赖文档契约 + 调用者守约（诚实声明，非代码可完全消除）。

## Migration Plan

- 无运行时状态迁移。改动是_读回行为收紧_：单 mip/单层/非 MSAA/2D/已知格式照常，未知格式/多-aspect 深度-模板/多层/非2D/MSAA 从"静默坏数据"转为"显式报错返回 null token"。
- 回滚：`git revert`。
- **D3D12 异步化**：独立 change `fix-readback-async`（另案，涉及 D3D12 内部同步模型重构，含当年 COM 悬垂教训）。本 change 只把 D3D12 在**字节数单源 / 格式-层级拒绝 / 契约**维度拉齐，不动其同步/异步实现。

## Open Questions

- 深度-模板读回（multi-aspect）本 change 明确拒绝；若未来要读深度/模板，需另行设计 aspect/plane 选择 + D3D12 256 行对齐（独立 change）。
- 未来加压缩格式时，`GetImageByteSize` 的压缩分支从"返回 0（未入枚举）"收敛为块线性公式（`ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐）——开放，需在新增压缩格式的 change 落地。
