# TODO

> 单一待办清单。以后有**未完成 / 需后续处理**的事项都记在这里，按「`## [日期] 标题（所属 change/模块）`」追加，标注：状态、问题、为何、改法、验收口径、相关代码。完成即删除该条（或标记已解决）。
>
> 本文件已按「待办 / 已解决记录」两段重排（2026-09-02），**待办**在上便于一眼看清还剩什么；已解决条目保留供追溯，不再占待办视野。

---

# 待办（未解决 / 需后续处理）

## [2026-09-02] IMGUI 字体 D2 加固：规范字体上传（AA 灰阶 + 去 swizzle 依赖）｜`fix-imgui-font-whitebox` deferred follow-up

**类型：变更内 deferred 跟进**（`fix-imgui-font-whitebox` 任务 3.1 / 3.2，明确"本轮不做"）

**问题**：D1（Vulkan 图像视图 honor swizzle，commit `034dcb4`）已让**白块**消失（white=97=字形，与 d3d12 一致），但链路仍是 "`GetTexDataAsAlpha8`(R 单通道) → `E_R8_UNORM` 纹理 → `SingleChannel(R)` swizzle" 的脆弱路径，且**无正常 AA 灰阶**。

**改法**（规范 ImGui 做法）：
- `IMGUIContext/IMGUIContext.cpp:489` `GetTexDataAsAlpha8` → `GetTexDataAsRGBA32`
- `:491-493` 纹理 `E_R8_UNORM` → `E_R8G8B8A8_UNORM`
- `:496` 上传大小 `texW*texH*4`
- `:754` 采样 view swizzle `SingleChannel(eR)` → **不再保留**（identity 语义；否则 RGBA 读 R=255 仍是白块）
- 仅改模块版（TestIMGUI 链接的是它）

**验收**：双端 headless 渲染出正常字形 + 出现正常 AA 灰阶；vulkan/d3d12 字节收敛；无新增 VUID。
**注**：落地时务必把 swizzle 改 identity（否则 RGBA 读 R=255 仍白）。done 后记录 AA 观察 + swizzle 一致性（`docs/TODO.md` 该 entry 移除）。

---

## [2026-07-12] vulkan-gpu-queries：Vulkan GPU 查询体系（OpenSpec change，0/13，未开始）

**状态：未开始（0/13；2026-07-12 建立后未推进，疑似搁置/废弃，待用户拍板）**

**内容**：Vulkan 完全缺 GPU 查询体系。新增 `vk::QueryPool` + `vkCmdWriteTimestamp`/`vkCmdBeginQuery`/`vkCmdEndQuery`/`vkCmdCopyQueryPoolResults`，盖 Timestamp（profiling）、Occlusion、Pipeline Statistics；暴露给引擎 profiling 层。对齐 D3D12 的 `D3D12_QUERY_HEAP`。
**决策点**：① 照做 → 从 `/opsx:apply vulkan-gpu-queries` 真开干；② 废弃 → 归档 change；③ 暂缓 → 保留现状但明确"非近期优先级"。
**相关**：`openspec/changes/vulkan-gpu-queries/`（proposal/design/tasks 齐，0 实现）。

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

> 背景：对本读回实现跑了对抗式 review workflow（27 agents），确认 4 条 MED。其中「bpp 单一事实来源」已在上方压缩条目、`D3D12 异步`已在上面第一条；本条目补剩余 2 条。测试过的 RGBA8 视读回路径本身可靠，这些是**边界/契约**缺陷。

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

## [2026-08-30] capture 的 2 条 LOW（GPUBackendTester 截图扩展收尾）

**状态：未修（LOW，从已解决的 capture 4 问题条目中摘出的剩余项）**

- **LOW-1**：`--capture` 不带 `--headless` 时**静默 noop**（无任何提示）——capture 只对 headless 生效，非 headless 下用户可能以为截了图。
- **LOW-2**：capture 帧号**越界无提示**——请求的 capture 帧号超出运行帧数时静默不写。

**改法**：① 非 headless + 带 capture 时打 stderr 提示"capture 仅 headless 生效"；② capture 帧越界时 stderr 警告/拒绝。**验收**：跑测试无意外截图、越界有告警。
**相关**：`Test/GPUBackendTester/Main.cpp`（`--capture`/`--headless` 解析、`WriteReadbackPNG`）。

---

# 已解决记录（保留供追溯；对应条目上移至待办后此区为空可清理）

## [2026-09-01] 观测 Vulkan IMGUI 多窗口副窗偏移——逐窗口回读 RT（新增捕获能力）｜✅已解决

**状态：已解决（2026-09-02，`fix-vulkan-dedup-key-identity` 修复并归档 → `openspec/changes/archive/2026-09-02-fix-vulkan-dedup-key-identity/`，commit `c3cbc49`）**

> 本条记录"观测能力 + 根因定位"全过程（含多轮证伪）。根因已被上述 change 以内容键修复，下列细节保留供追溯，非新待办。

**结果**：
- **观测能力**：逐窗口回读已落地（每窗同尺寸 readable RT 镜像回读 + `--capture`，输出 `test_output/TestIMGUI_<backend>_viewport<N>.png`），副窗偏移首次被 PIL 量化（`(61,61)`/`(349,341)` = 副窗pos−主窗pos）。headless 不再是盲区。
- **根因（Vulkan 后端）**：`VulkanShaderResourceSet::Init` 去重 hash 忽略 per-viewport `VulkanShaderStruct*`；所有 ImGui viewport 共享 shader path（"Shaders/Imgui"）+ 绑定名（imguiCommon/imguiTextureBinding）→ hash 合并到 **main** 实例 → 绑定 main 的 `IMGUIScale_Pos` → 副窗内容偏移 `(副窗pos−主窗pos)`。
- **修法**：改为**内容键**（`operator==` 比较 path + resourceDic 的 [name→structPtr]；hash 加入 structPtr；`m_ShaderResourceInstances` 以完整 `VulkanShaderResourceSet` 为 key）。修复后副窗 v1/v2 vulkan≡d3d12 字节一致（meanDiff 0.000），偏移消除、测试回归通过。
- **遗留**：交互式关窗时 VUID-09600/01282（本 change 触碰面外，疑似交互式布局/teardown），未在本 change 触达，另案（见「vulkan 显示异常」历史）——如需定论做 baseline 验证。
- 多轮证伪细节（Round-2 等）见归档 change 的 design/tasks.md。

---

## [2026-08-31] Vulkan IMGUI 多窗口副窗偏移（GUI 未与窗口对齐，D3D12 正常）｜✅已解决

**状态：已解决（2026-09-02，与上方 [2026-09-01] 同根同源——均被 `fix-vulkan-dedup-key-identity` 修复**）

> 此前"真因未定位（静态分析穷尽）"，被 [2026-09-01] 的运行插桩推翻——真因 = Vulkan 后端常量绑定去重错误（见上条）。修复后副窗内容与 d3d12 字节一致，偏移消除。
> 已证伪项（视图 Pos 过度偏移/负高 viewport/共享 framebuffer/字体修复相关/逻辑-物理尺寸分裂 等）见归档 change 的 design.md；为避免 TODO 冗余，本条收敛到"已解决"。

---

## [2026-08-30] IMGUI 字体渲染成白块（文字不可读）｜✅已解决（D1）

**状态：已解决（D1，`fix-imgui-font-whitebox`，未归档；D2 见待办 [2026-09-02]）**

**结果**：根因是 Vulkan 采样图像视图丢失接口 `GPUTextureSwizzle::SingleChannel(eR)`（未翻译成 `vk::ComponentMapping`）→ R8 视图按 identity 采样得 `(R,0,0,1)`，`Imgui.slang:14` `.aaaa` 读到 A=1 → 覆盖率恒 1 → 白块。**修正**：此前"双端均复现"是缩略图目测误判——D3D12 的细字形被当成白块，D3D12 实为正常（`Shader4ComponentMapping` 已正确）。
- **D1（已修，commit `034dcb4`）**：Vulkan 创建采样视图应用 swizzle（`viewInfo.components`），`SingleChannel` 在 `.aaaa` 读到 R 覆盖率；`VulkanGraphLocalResourceManager::GetTextureView` 增重载接收 view swizzle；绑定处传入。`TestIMGUI_vulkan.png` white=97（=d3d12，非原 233 实心块），渲染出 "Options" 字形，无新增 VUID。
- **D2（未做 → 待办）**：规范字体上传 `GetTexDataAsRGBA32` + `E_R8G8B8A8_UNORM` + identity swizzle（得到 AA 灰阶），见待办 [2026-09-02]。

---

## [2026-08-30] 暴露 enableDepthClamp（z-clip/z-clamp 开关）为 GPUGraph 级开关，统一双后端光栅化

**状态：已解决（2026-08-30，`unify-depth-clamp-switch` 落地并归档 → `openspec/changes/archive/2026-08-30-unify-depth-clamp-switch/`）**

**结果**：统一 `RasterizerStates::enableDepthClamp`（默认 false = 标准近平面裁剪），双后端一致实现——Vulkan 映射 `depthClampEnable` + 设备启用 `depthClamp` feature；D3D12 去硬编码、`DepthClipEnable = !enableDepthClamp`（双路径一致）；ConstantColor/StructuredBufferColor 顶点 z 改 ≥0。`python build.py --config Debug` 通过，双后端 8 测试 exit 0，ConstantColor/StructuredBufferColor 双端渲染出三角形且字节一致，无新增 VUID。对抗验证 11/11 全票（mustFix 空）。主 spec `openspec/specs/pipeline-depth-clamp/spec.md` 已同步（valid）。**遗留**：[AUDIT-1] 覆盖缺口——clamp 正向路径（enableDepthClamp=true）未被任何测试运行时触达，仅代码审+MCP 语义核实，可补一条真 clamp 渲染测试（非必须）。

---

## [2026-08-29] 截图功能的 4 个 capture 代码问题（GPUBackendTester 截图扩展）

**状态：已解决（2026-08-29，A 修复落地）**

**结果**：① `WriteReadbackPNG` 在 null/空白时 `return false` 并打 stderr（vulkan ConstantColor 实测输出 `[capture] ... capture is BLANK (nonEmpty=false)`）；② TestReadback 在"请求了 capture 但读到 null/空白"时 `throw`（兑现 assert，读回验证失败即 fail）；③ TestIMGUI 赋值行补 `captureRT.IsValid()`；④ IMGUIContext::DrawSingleView 用 `ImGuiViewportFlags_CanHostOtherWindows` 门控到主 viewport（此 imgui 版本无 IsMainWindow）。重编译通过，双后端 8 测试 exit 0、4 对字节一致不变。**剩余 2 条 LOW 已摘出 → 待办 [2026-08-30] capture 2 条 LOW。**

---

## [2026-08-30] vulkan 08114/00331 + D32 depth usage —— 已由 fix-vulkan-descriptor-registration 落地解决

**状态：已解决（2026-08-30，`fix-vulkan-descriptor-registration` 落地并归档）。**

- **08114/00331**：D-A（RAST shader 绑定→RWState）+ D-B（MarkResourceUse 生命周期）+ D-C（持久 offset 绑所有 + per-pool 峰值）+ D1补全（顶层 Foreach insert-only 防 orphan）+ D2（BuildResources 兜底 inert）+ D3（usage 位）落地后，StructuredBufferColor/DoublePass/ComputeBuffer **08114/00331 = 0**，截图真实非黑；vulkan/d3d12 三对捕获字节一致；IMGUI 08114=0 属独立（外部字体图）。
- **D32 depth 02251 / 01209 / 01758 / 02633 / 08931**：D-C bind-all 暴露后，已修 `CollectResources` depth attachment 用 `eDepthStencil`（非 `eRT`），ConstantColor 现仅剩 2× shader-code 08740（另案）。
- **附带 barrier 缺陷（raster 不登记采样读 → 跨 pass 写→读未串行化）**：D-A 已让 raster shader 采样进 RWState → 写→读入生命周期/屏障链，已被覆盖。

**审查闭环**：3 轮对抗验证（4 审查者 / 轮，逐 claim 投票）——第二轮修正 C10（跨别名内存依赖改诚实 submission-local 记录 + 移除不成立 compute 分支）、C11（IMGUI 归因更正）；第三轮收敛（mustFix 清空，bottomLine "TRUSTWORTHY and behaviorally COMPLETE"）。**诚实声明**：C10 跨别名内存依赖为 submission-local best-effort——真正跨批次内存依赖需提交模型改动（单 submit 全批次 / 批间 semaphore），超范围未 overclaim；跨批次正确性依赖既有 waitForFences 串行化 + 设备一致性。

**归档**：`openspec/changes/archive/2026-08-30-fix-vulkan-descriptor-registration/`；delta spec 已同步主 spec。
