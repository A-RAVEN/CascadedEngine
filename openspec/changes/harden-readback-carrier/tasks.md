## 1. 载体字节数单一事实来源（公开 `GetImageByteSize`）

- [x] 1.1 `Interface/RenderInterface/header/Common.h`：新增公共 `GetImageByteSize(width, height, format)`——返回该图紧凑数据的字节数，是唯一公开字节数基准；**先判白名单**（`GetFormatBlockSize` 有显式 case 且单平面/非压缩/可紧凑复制），命中才走公式——非压缩单平面 = `width*height*GetFormatBlockSize(format)`；压缩分支为记录目标（块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐，未入枚举故当前返回 0）；**仅无紧凑布局的未知格式返回 0**（否则会因 `GetFormatBlockSize default=4` 把未知格式算成 `W*H*4`）。**字节数 ≠ 可读性**（见 2.x）。与 `GetFormatBlockSize` 相邻放置。
- [x] 1.2 `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp`：删除私有 `GetReadbackBpp`（`:1009-1030`）；`Readback(image)` 的 `byteSize` 与紧凑 `rowPitch` 改用 `GetImageByteSize` / 内部 `GetFormatBlockSize`。
- [x] 1.3 `D3D12RenderBackend/private/RenderBackend_D3D12.cpp`：删除私有 `D3D12ReadbackBpp`（`:480-500`）；`byteSize`、紧凑 `dstStride` 改用 `GetImageByteSize` / 内部 `GetFormatBlockSize`。
- [x] 1.4 单深度（单 aspect，`E_D16_UNORM`/`E_D32_SFLOAT`）**归入可读支持集**；深度+模板 multi-aspect（`E_D24_UNORM_S8_UINT`/`E_D32_SFLOAT_S8_UINT`）**降为「读回拒绝」**（见 2.x）：`GetImageByteSize` 对 multi-aspect 返回 packed 字节数（`E_D32_SFLOAT_S8_UINT` = `W*H*8`），但 `Readback` 对 multi-aspect 返回 null（读数需 aspect/plane 选择 + D3D12 256 行对齐，深度值解释属格式语义非本 change 目标）。bpp 值分叉经删私有表自动消除（`GetFormatBlockSize=8` 唯一来源）。

## 2. 未支持格式显式拒绝（`GetImageByteSize` 分支 + 白名单）

- [x] 2.1 `RenderBackend_Vulkan.cpp` `Readback(image)`：取 descriptor 后查**支持集**——不在支持集（压缩块线性 / multi-aspect 深度-模板 / 无紧凑布局未知 / `GetImageByteSize` 返回 0）→ `CA_LOG_ERR`（含格式名 + 原因）+ `return nullptr`（替代静默 `default=4`）。**注意**：multi-aspect 深度-模板 `GetImageByteSize` 返回**非零**（`W*H*8`）可算尺寸，但**仍被拒读**——判定走支持集，勿以 `GetImageByteSize==0` 为准。
- [x] 2.2 `RenderBackend_D3D12.cpp` `Readback(image)`：同上，按支持集判定；不在支持集 → 报错 + `return nullptr`。
- [x] 2.3 确认 `E_D32_SFLOAT_S8_UINT` 走 `GetFormatBlockSize=8`（无分叉），且 multi-aspect 深度-模板读回被拒（不声称支持）；单深度 `E_D16_UNORM`/`E_D32_SFLOAT` 被接受。**诚实注**：`IsCompressedFormat`（Common.h）恒 `false`，本条不得把它当可靠前提；以 `GetImageByteSize` 分支/白名单为准。

## 3. subresource 收窄（单 mip / 单 layer / 非 MSAA / 2D）

- [x] 3.1 `RenderBackend_Vulkan.cpp` `Readback(image)`：校验 `desc.mipLevels==1 && desc.layers==1 && desc.samples==EMultiSampleCount::e1 && desc.textureType==ETextureType::e2D`，任一越界 → `CA_LOG_ERR` + `return nullptr`（替代静默只拷 `mip0/layer0`）；收窄后 `region.imageSubresource` 固定 `{mip0, layer0, 1}`。
- [x] 3.2 `RenderBackend_D3D12.cpp` `Readback(image)`：同上校验 `desc` 的 mip/layer/`samples==EMultiSampleCount::e1`/`textureType==ETextureType::e2D`；越界 → 报错 + `return nullptr`（替代静默 `SubresourceIndex=0`）。
- [x] 3.3 **注意** `desc.samples` 是 `EMultiSampleCount`（enum class，`Common.h:397-406`，`e1=0`）——不要写 `==1`/`>1`（不可编译/语义反：`==1`=e2x MSAA、`>1` 漏 e2）。

## 4. dst 生命周期契约 + 后端在「写入 dst 前」校验

- [x] 4.1 `Interface/RenderInterface/header/CRenderBackend.h`：`IReadbackToken` + `Readback` 注释显式写明——调用者必须保持 `dst` 指代底层内存**存活、且大小不缩水，直到 `Wait()` 返回**（含 `Wait` 前不 resize/释放承载 `dst` 的容器）；载体字节数以 `GetImageByteSize` 为准。
- [x] 4.2 `VulkanReadbackToken`：记录 `m_ByteSize`；`Wait()` 在 `memcpy` 前校验 `m_Dst.size() < m_ByteSize` → `CA_LOG_ERR` + 跳过写入（不越界写）。
- [x] 4.3 `D3D12ReadbackToken` / `Readback(image)`：在**写入 `dst` 前**校验 `dst.size()`（同步路径在 `Readback()` 内，已校验 `:635`，作为兜底）；契约表述独立于后端同步实现（Vulkan 异步=Wait 内 memcpy 前，D3D12 同步=Readback 内）。

## 5. 编译验证

- [x] 5.1 执行 `python build.py --config Debug`，直到 BUILD SUCCESSFUL（含修复触发到的编译问题）。
- [x] 5.2 双后端 headless 回归：vulkan + d3d12 各 8 测试 `exit 0`，`test_output` 的 RGBA8 字节对比（现有测试路径）不变；无新增 VUID/validation 错误。
- [x] 5.3 读回"未知/多层/MSAA/3D/深度-模板 格式"返 null token 的用例验证（tester 可改造成手动/小用例；若难造则记录为静态核实）。

## 6. 审查闭环（Review & Adversarial Verify）

- [x] 6.1 开 workflow 做对抗验证：多独立审查者逐 claim 核对——(a) `GetImageByteSize` 公开单源且压缩/非压缩分支与 `GetFormatBlockSize` 一致；(b) 双后端字节数单源后 `E_D32_SFLOAT_S8_UINT` 不再分叉；(c) subresource 收窄（含 `samples==EMultiSampleCount::e1`、`textureType==e2D`）不破坏现有 RGBA8 单层用例（capture 字节对比仍一致）；(d) 后端"写入 dst 前"`dst.size()` 防御校验正确（不漏越界、不误拒合法大小）；(e) 接口契约表述与主 spec「异步声明」一致（本 change 不改同步语义）；(f) `IsCompressedFormat` 空转/深度-模板降级表述诚实；(g) 证据诚实。
- [x] 6.2 回归检查与去重：核对本 change 是否引入新问题；确认 D3D12 同步语义未被本 change 意外改动（async 属另案）。验证发现的新问题以 `[AUDIT-]` 前缀追加为新 task，追加新的审查任务；结果写入本文件末尾 `## Review Log`。
- [x] [AUDIT-1] 对抗验证确认的唯一新问题（low/soft）：Main.cpp 多-mip 拒读测试是**软校验**——`if (mmTok) { stderr } else { cout }`，若 subresource 门回归、Readback 错误接受多-mip 图，只打 stderr 不抛异常，`runTestWithDiagnostics` 仍记 pass（该保证不受 harness 约束）。→ 已改为**硬断言**（`mmTok` 分支抛 `std::runtime_error`，镜像同函数顶部 capture 的闭环 throw :289-293），回归时即失败。
- [x] 6.3 Round-2 对抗复验 [AUDIT-1]：开 workflow 确认 (a) 硬断言生效（stderr 无多余、TestReadback 仍 exit 0、reject=null OK、正常 capture 未被破坏）；(b) 无新回归；(c) 证据诚实；结果写入 Review Log。
- [x] [AUDIT-2] 自洽性缺口：接口契约/design 规定"载体字节数唯一公开来源 = `GetImageByteSize`、调用方 SHALL NOT 按 `width*height*bpp` 自算"，但现有调用方（tester `WriteReadbackPNG` 的 `w*h*4`、`TestReadback` 拒读分支的 `64*64*4`）仍硬算（对 RGBA8 数值相等故未出错，但不通用）。→ 已迁移这两个载体大小计算为 `GetImageByteSize(...)`，使契约自洽；重编 + 双后端 8 测试回归通过（行为不变）。

## Review Log

### Caller migration（2026-09-05，[AUDIT-2]，重编 + 双后端回归）

审计采纳用户指出的一致性问题：契约/design 规定 `GetImageByteSize` 是载体字节数唯一公开来源、调用方 SHALL NOT 按 `width*height*bpp` 自算，但现有调用方仍在 DIY（tester `WriteReadbackPNG` 的 `w*h*4`、`TestReadback` 拒读分支 `64*64*4`）。对 RGBA8 二者数值相等（`GetImageByteSize(W,H,RGBA8)==W*H*4`），故功能未出错，但该模式不通用（压缩/未知格式会错），且与本 change 的公开契约相悖。→ 已迁移为 `GetImageByteSize(...)`；重编 BUILD SUCCESSFUL；vulkan + d3d12 各 8 测试 `exit 0`；`readback`/`TestSimpleTriangle`/`TestComputeBuffer`/`TestTriangleWithConstantColor` 跨后端 PNG 字节 cmp **IDENTICAL**；拒读分支仍 `[reject] multi-mip readback -> null token OK`；无新增 VUID。行为不变（RGBA8 字节数不变），故无新增语义风险。

### Implementation re-verify（2026-09-04，Round-2，开 workflow 对抗复验 AUDIT-1，3 agents）

审计结论 **APPROVE**（0 blocker + 0 mustfix + 0 shouldfix，0 findings）。`[AUDIT-1]` 硬断言修复确认正确且证据诚实：`Main.cpp:307-314` 已把软校验 `if(mmTok) stderr` 改为硬 `throw std::runtime_error`（镜像 `:289-293` 闭环 throw）；该 throw 在正确路径上**惰性**——`RenderBackend_Vulkan::Readback(image)` 对 `mipLevels>1` 返回 null token（`:1155-1161`），测试的 `mmRT` 用 `mipCount=2` 创建故 `mmTok` 为 null，仅当 subresource 门回归（错误返回 token）时才触发。无新问题，审查收敛。

**审查闭环结论**：Round-1 APPROVE（1 shouldfix→AUDIT-1 已修），Round-2 APPROVE（0 findings）。核心契约（字节数单源/格式-subresource 拒绝/dst 防御校验）双后端确认正确；D3D12 保持同步为有意（async 属另案）。本 change 无需第三轮。

### Implementation audit（2026-09-04，Round-1，开 workflow 对抗验证实现代码，13 agents）

审计结论 **APPROVE**（0 blocker + 0 mustfix + 1 shouldfix）。逐 claim 独立核对 + 对抗反驳后，6 claims 中的核心逻辑均**确认正确**：Vulkan `:1155-1156` 与 D3D12 `:635-636` 均用精确门 `desc.mipLevels>1 || desc.layers>1 || desc.samples != EMultiSampleCount::e1 || desc.textureType != ETextureType::e2D`（`e1=0`，与 spec:39 一致）；全源码无 `samples==1`/`>1`（grep 仅命中 doc/分析脚本对旧 bug 的引用）；拷贝区固定 `mip0/layer0/layerCount=1`（Vulkan `:1245-1247`、D3D12 `:708 SubresourceIndex=0`）；正常 capture 路径为单 mip/单层/e2D/e1（`CreateCaptureRT Main.cpp:102-104`、TestReadback 离屏 RT `:243-245` 用全默认 → 通过门）；D3D12 保持同步为**有意**（async 属另案）。唯一确认的 finding：

- **[shouldfix→已修 AUDIT-1]** 多-mip 拒读测试是软校验（`if(mmTok)` 只打 stderr，回归时仍记 pass），无法约束「拒读」这是 head 行为保证的实现。→ 已改硬断言（`mmTok` 分支抛 `std::runtime_error`，镜像 `:289-293` 闭环 throw）。

**诚实注（baseline VUID）**：`TestTriangleWithConstantColor_validation.log` 含 `VUID-VkShaderModuleCreateInfo-pCode-08740`（SPIR-V capability 提示，来自该测试的 `TestStaticMeshShader`）。本 change 只动读回载体（字节数/拷贝），**无法影响** shader module 创建路径，且 08740 属既有 known-issue 族（见记忆 vulkan-display-anomaly）；判定为 baseline，非本 change 引入。若需硬证实可 stash+重编对比。

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
