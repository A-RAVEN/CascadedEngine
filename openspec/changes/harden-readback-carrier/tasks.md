## 1. 载体字节数单一事实来源（公开 `GetImageByteSize`）

- [ ] 1.1 `Interface/RenderInterface/header/Common.h`：新增公共 `GetImageByteSize(width, height, format)`——返回该图紧凑数据的字节数，是唯一公开字节数基准；**先判白名单**（`GetFormatBlockSize` 有显式 case 且单平面/非压缩/可紧凑复制），命中才走公式——非压缩单平面 = `width*height*GetFormatBlockSize(format)`；压缩分支为记录目标（块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐，未入枚举故当前返回 0）；**仅无紧凑布局的未知格式返回 0**（否则会因 `GetFormatBlockSize default=4` 把未知格式算成 `W*H*4`）。**字节数 ≠ 可读性**（见 2.x）。与 `GetFormatBlockSize` 相邻放置。
- [P] 1.2 `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`：删除私有 `GetReadbackBpp`（`:1009-1030`）；`Readback(image)` 的 `byteSize` 与紧凑 `rowPitch` 改用 `GetImageByteSize` / 内部 `GetFormatBlockSize`。
- [P] 1.3 `D3D12RenderBackend/private/RenderBackend_D3D12.cpp`：删除私有 `D3D12ReadbackBpp`（`:480-500`）；`byteSize`、紧凑 `dstStride` 改用 `GetImageByteSize` / 内部 `GetFormatBlockSize`。
- [ ] 1.4 单深度（单 aspect，`E_D16_UNORM`/`E_D32_SFLOAT`）**归入可读支持集**；深度+模板 multi-aspect（`E_D24_UNORM_S8_UINT`/`E_D32_SFLOAT_S8_UINT`）**降为「读回拒绝」**（见 2.x）：`GetImageByteSize` 对 multi-aspect 返回 packed 字节数（`E_D32_SFLOAT_S8_UINT` = `W*H*8`），但 `Readback` 对 multi-aspect 返回 null（读数需 aspect/plane 选择 + D3D12 256 行对齐，深度值解释属格式语义非本 change 目标）。bpp 值分叉经删私有表自动消除（`GetFormatBlockSize=8` 唯一来源）。

## 2. 未支持格式显式拒绝（`GetImageByteSize` 分支 + 白名单）

- [P] 2.1 `RenderBackend_Vulkan.cpp` `Readback(image)`：取 descriptor 后查**支持集**——不在支持集（压缩块线性 / multi-aspect 深度-模板 / 无紧凑布局未知 / `GetImageByteSize` 返回 0）→ `CA_LOG_ERR`（含格式名 + 原因）+ `return nullptr`（替代静默 `default=4`）。**注意**：multi-aspect 深度-模板 `GetImageByteSize` 返回**非零**（`W*H*8`）可算尺寸，但**仍被拒读**——判定走支持集，勿以 `GetImageByteSize==0` 为准。
- [P] 2.2 `RenderBackend_D3D12.cpp` `Readback(image)`：同上，按支持集判定；不在支持集 → 报错 + `return nullptr`。
- [ ] 2.3 确认 `E_D32_SFLOAT_S8_UINT` 走 `GetFormatBlockSize=8`（无分叉），且 multi-aspect 深度-模板读回被拒（不声称支持）；单深度 `E_D16_UNORM`/`E_D32_SFLOAT` 被接受。**诚实注**：`IsCompressedFormat`（Common.h）恒 `false`，本条不得把它当可靠前提；以 `GetImageByteSize` 分支/白名单为准。

## 3. subresource 收窄（单 mip / 单 layer / 非 MSAA / 2D）

- [P] 3.1 `RenderBackend_Vulkan.cpp` `Readback(image)`：校验 `desc.mipLevels==1 && desc.layers==1 && desc.samples==EMultiSampleCount::e1 && desc.textureType==ETextureType::e2D`，任一越界 → `CA_LOG_ERR` + `return nullptr`（替代静默只拷 `mip0/layer0`）；收窄后 `region.imageSubresource` 固定 `{mip0, layer0, 1}`。
- [P] 3.2 `RenderBackend_D3D12.cpp` `Readback(image)`：同上校验 `desc` 的 mip/layer/`samples==EMultiSampleCount::e1`/`textureType==ETextureType::e2D`；越界 → 报错 + `return nullptr`（替代静默 `SubresourceIndex=0`）。
- [ ] 3.3 **注意** `desc.samples` 是 `EMultiSampleCount`（enum class，`Common.h:397-406`，`e1=0`）——不要写 `==1`/`>1`（不可编译/语义反：`==1`=e2x MSAA、`>1` 漏 e2）。

## 4. dst 生命周期契约 + 后端在「写入 dst 前」校验

- [ ] 4.1 `Interface/RenderInterface/header/CRenderBackend.h`：`IReadbackToken` + `Readback` 注释显式写明——调用者必须保持 `dst` 指代底层内存**存活、且大小不缩水，直到 `Wait()` 返回**（含 `Wait` 前不 resize/释放承载 `dst` 的容器）；载体字节数以 `GetImageByteSize` 为准。
- [P] 4.2 `VulkanReadbackToken`：记录 `m_ByteSize`；`Wait()` 在 `memcpy` 前校验 `m_Dst.size() < m_ByteSize` → `CA_LOG_ERR` + 跳过写入（不越界写）。
- [P] 4.3 `D3D12ReadbackToken` / `Readback(image)`：在**写入 `dst` 前**校验 `dst.size()`（同步路径在 `Readback()` 内，已校验 `:635`，作为兜底）；契约表述独立于后端同步实现（Vulkan 异步=Wait 内 memcpy 前，D3D12 同步=Readback 内）。

## 5. 编译验证

- [ ] 5.1 执行 `python build.py --config Debug`，直到 BUILD SUCCESSFUL（含修复触发到的编译问题）。
- [ ] 5.2 双后端 headless 回归：vulkan + d3d12 各 8 测试 `exit 0`，`test_output` 的 RGBA8 字节对比（现有测试路径）不变；无新增 VUID/validation 错误。
- [ ] 5.3 读回"未知/多层/MSAA/3D/深度-模板 格式"返 null token 的用例验证（tester 可改造成手动/小用例；若难造则记录为静态核实）。

## 6. 审查闭环（Review & Adversarial Verify）

- [ ] 6.1 开 workflow 做对抗验证：多独立审查者逐 claim 核对——(a) `GetImageByteSize` 公开单源且压缩/非压缩分支与 `GetFormatBlockSize` 一致；(b) 双后端字节数单源后 `E_D32_SFLOAT_S8_UINT` 不再分叉；(c) subresource 收窄（含 `samples==EMultiSampleCount::e1`、`textureType==e2D`）不破坏现有 RGBA8 单层用例（capture 字节对比仍一致）；(d) 后端"写入 dst 前"`dst.size()` 防御校验正确（不漏越界、不误拒合法大小）；(e) 接口契约表述与主 spec「异步声明」一致（本 change 不改同步语义）；(f) `IsCompressedFormat` 空转/深度-模板降级表述诚实；(g) 证据诚实。
- [ ] 6.2 回归检查与去重：核对本 change 是否引入新问题；确认 D3D12 同步语义未被本 change 意外改动（async 属另案）。验证发现的新问题以 `[AUDIT-]` 前缀追加为新 task，追加新的审查任务；结果写入本文件末尾 `## Review Log`。

## Review Log

### Artifact audit（2026-09-04，开 workflow 对抗审计 draft artifacts 后按结论修正）

审计结论 **DIVERGENT**（1 blocker + 4 mustfix + 3 nit），已全部修正进 proposal/design/specs/tasks：

- **[blocker→已修] 公开契约违背 TODO 原则 3**：draft 用 `GetFormatBlockSize` + 调用者 `W*H*bpp`。→ 改为公开 `GetImageByteSize(width,height,format)` 单一字节数查询；`GetFormatBlockSize` 降为内部中间量。design.md D1 原"为何不用 GetImageByteSize（D3D12 行解压需 width*bpp）"论证已被推翻——那只是[内部需要，允许]，不构成公开暴露 per-pixel bpp 的理由。
- **[mustfix→已修] 压缩 block-linear 载体未体现/误折进"GetFormatBlockSize 无确定值"**（`GetFormatBlockSize` 恒 `default=4`，无"无确定值"）。→ 明确压缩=块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes`，非像素线性；划入 Non-goal；绝不用 `W*H*bpp` 套压缩。
- **[mustfix→已修] `IsCompressedFormat==false` 空转**（恒 `false`，过滤不了）。→ 规范注明不可依赖，以 `GetImageByteSize` 分支/显式白名单为准。
- **[mustfix→已修] `desc.samples==1`/`>1` 不可编译/语义反**（`EMultiSampleCount` enum class，`e1=0`）。→ 用 `samples==EMultiSampleCount::e1` / `!=e1`。
- **[mustfix→已修] `E_D32_SFLOAT_S8_UINT`"定为支持"自相矛盾**（depth+stencil multi-aspect vs spec"仅单平面"）。→ 深度-模板降为「读回拒绝」（需 aspect/plane 选择 + 256 行对齐，深度值解释属格式语义非目标），仅保留 bpp 值对齐。
- **[nit→已修] 未校验 `textureType==e2D`** → 加入门。
- **[nit→已修] Wait() 锚定措辞** → 改"在把数据写入 dst 前"（Vulkan=Wait 内 memcpy 前；D3D12=Readback() 内）。
- **[nit→已修] 异步 change 名不统一**（`fix-rendrear-back-readback-async` vs `fix-readback-async`）→ 统一 `fix-readback-async`。

### Artifact re-audit（2026-09-04，Round-2，开 workflow 对修订后 artifacts 复查）

结论 **REVISE**（0 blocker + 1 mustfix + 2 shouldfix）。**Round-1 全部 findings 确认真修复**（背向 confirm-vs-fixed）：公开 `GetImageByteSize` 单源、压缩块线性载体、`IsCompressedFormat` 诚实注、`samples==e1`、`D32S8` 去支持化、`textureType==e2D`、Wait 措辞、async 名 —— 均逐条读实际文案确认非仅措辞。残留 3 处已修正：

- **[mustfix→已修] `GetImageByteSize` 对深度-模板 multi-aspect 返回值自相矛盾**：spec 一个场景写 `E_D32_SFLOAT_S8_UINT` 返回非零 `W*H*8`（无分叉），另一个场景写"也已返回 0"；tasks 2.1 也把它归入返回 0 桶。→ 统一为**两正交信号**：`GetImageByteSize` 非零只是**尺寸可算**（`D32S8`=非零），不代表 `Readback` 接受；**可读性由支持集单独界定**（multi-aspect 深度-模板返回非零但被拒读）。删掉"也返回 0"与 tasks 2.1 的"multi-aspect 归 0 桶"。
- **[shouldfix→已修] `E_D16_UNORM` 归属未裁定**：单深度（单 aspect）格式（`E_D16_UNORM`/`E_D32_SFLOAT`）**归入可读支持集**；仅深度+模板 multi-aspect（`D24_S8`/`D32_S8`）被拒——跨 proposal/design/spec/tasks 补一致，并加"单深度被接受"场景。
- **[shouldfix→已修] 压缩分支公式三处不一致**：design D1 写"无对齐块线性公式"，Non-goal/Risks 写"+ 256 对齐"，OpenQuestions 写"当前返回0"。→ 统一为「压缩分支为记录目标（块线性 + 256 对齐），因未入枚举当前返回 0」，删"保留分支语义=现为公式"与"当前返回0"的离群表述。

**遗留诚实注**：`GetImageByteSize` 把"尺寸可算"与"可读"拆开——调用者不能只靠 `GetImageByteSize` 返回值判断能否读回，需结合支持集（此属设计决策，已在 spec Requirement 1 明示）。
