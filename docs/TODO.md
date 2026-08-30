# TODO

> 单一待办清单。以后有**未完成 / 需后续处理**的事项都记在这里，按「`## [日期] 标题（所属 change/模块）`」追加，标注：状态、问题、为何、改法、验收口径、相关代码。完成即删除该条（或标记已解决）。

---

## [2026-08-30] IMGUI 字体渲染成白块（文字不可读）——Vulkan+D3D12 双端复现（IMGUIContext / 后端视图）

**状态：未修（双后端均复现）**

**问题**：TestIMGUI 的文字不显示，渲染成白色实心方块（菜单栏/左侧 tab 文字都变白块）。Vulkan / D3D12 截图一致（2026-08-30 重读确认两侧均白块）。双后端 validation 均干净（0 VUID）——是**静默渲染错误**，非 API 误用。

**根因（通道错配：字形在 R、shader 读 A）**：
- 字形用 `GetTexDataAsAlpha8` → 单通道 alpha8 存进 **R8_UNORM** 纹理（IMGUIContext.cpp:489-493），字形覆盖率在 **R** 通道。
- ImGui shader 采样 `.aaaa`——读 **alpha** 通道（Imgui.slang:14）。
- 桥接靠 `CreateDefaultForSampling(R8G8B8A8, SingleChannel(R))`（IMGUIContext.cpp:754）把 R 复制进 A。
- **Vulkan 裂点（已钉死）**：`VulkanTexture.cpp:161-177` 创建 VkImageView 用 `viewInfo.format=图像格式(R8)` 且**不设 `viewInfo.components`**（无 VkComponentMapping）→ swizzle 被忽略 → 采样 R8 得 `(r,0,0,1)`，A=1 → `.aaaa`=1 → 恒白。
- **D3D12 待细化**：`InterfaceTranslation.h:615-620` **有**把 swizzle 编码进 `Shader4ComponentMapping`（A→R），`Sanitize` 也不动 swizzle → 理论应正常；但截图仍白块。故 D3D12 机制存疑：可能同症状但另一路径/第二个原因，须确认。

**修法候选**：
- ① 后端尊重 swizzle：Vulkan 设 `viewInfo.components`（SingleChannel(R)→{R,R,R,R}）。D3D12 已做。
- ② IMGUIContext 改 `GetTexDataAsRGBA32` + R8G8B8A8 纹理（标准 ImGui 做法，一处修双端，洗掉 swizzle 依赖）——**最省、最稳**。
- ③ `Imgui.slang:14` `.aaaa`→`.rrrr`（字形在 R）。

**注**：与「Vulkan 三角形缺失（clipspace z）」是两个独立根因（一个通道错配、一个 clip volume），均被上轮"只看非黑、3 对字节一致"的验收口径漏掉。

**相关代码**：`IMGUIContext/IMGUIContext.cpp:456,489-497,750-754`、`CAResources/Shaders/Imgui.slang:11-14,52`、`Interface/RenderInterface/header/GPUTexture.h:64,124-145`、`VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp:161-177`、`D3D12RenderBackend/private/Utils/InterfaceTranslation.h:607-621`。

---

## [2026-08-30] 暴露 enableDepthClamp（z-clip/z-clamp 开关）为 GPUGraph 级开关，统一双后端光栅化（vulkan 三角形缺失根因）

**状态：已解决（2026-08-30，`unify-depth-clamp-switch` 落地并归档 → `openspec/changes/archive/2026-08-30-unify-depth-clamp-switch/`）**

**结果**：统一 `RasterizerStates::enableDepthClamp`（默认 false = 标准近平面裁剪），双后端一致实现——Vulkan 映射 `depthClampEnable` + 设备启用 `depthClamp` feature；D3D12 去硬编码、`DepthClipEnable = !enableDepthClamp`（双路径一致）；ConstantColor/StructuredBufferColor 顶点 z 改 ≥0。`python build.py --config Debug` 通过，双后端 8 测试 exit 0，ConstantColor/StructuredBufferColor 双端渲染出三角形且字节一致，无新增 VUID。对抗验证 11/11 全票（mustFix 空）。主 spec `openspec/specs/pipeline-depth-clamp/spec.md` 已同步（valid）。**遗留**：[AUDIT-1] 覆盖缺口——clamp 正向路径（enableDepthClamp=true）未被任何测试运行时触达，仅代码审+MCP 语义核实，可补一条真 clamp 渲染测试（非必须）。

**问题**：TestTriangleWithConstantColor / TestTriangleWithStructuredBufferColor 在 **Vulkan** 上三角形消失（顶点 z=-0.25/0 被裁），**D3D12** 正常。根因是**光栅化"近平面 z-clip/Z-clamp"开关在双后端不一致**：
- **Vulkan**：`VulkanGraphExecutor.cpp:1823-1831` 建 `VkPipelineRasterizationStateCreateInfo` 时**忽略接口 `enableDepthClamp`**，`depthClampEnable` 字段从未设（默认 `VK_FALSE`）→ **裁 z 面**（z<0 被裁掉）。且设备**未启用 `depthClamp` device feature**（启用后才可设 depthClampEnable=TRUE）。
- **D3D12**：`GPUPipelineInstance.cpp:159` **硬编码 `DepthClipEnable = FALSE`**（不裁，z<0 也渲染）；`PipelineStatesObject.cpp:18-33` 用 `enableDepthClamp`→`DepthClipEnable`（**语义反相**：Vulkan 的 depthClamp 是 depth-clip 的反面，MCP 已核实）。
- 接口字段 `RasterizerStates::enableDepthClamp`（`CPipelineStateObject.h:19`，默认 false）**本已存在**，但 Vulkan 忽略、D3D12 语义反相 + 硬编码→**实际未真正暴露、也未对齐**。

**设计目标（统一上层接口，而非分叉取向）**：把"近平面 z-clip/z-clamp"定义为**一个上层 GPUGraph / PipelineState 字段**（沿用已有 `RasterizerStates::enableDepthClamp`，确认/补全到暴露面，避免只在内部结构里），**两个后端都忠实读取同一值**——值由上层一次性决定，两端照做即一致：

- **Vulkan**：映射 `enableDepthClamp` → `depthClampEnable`（true=沿 z clamp 替代 clip），并**启用 `depthClamp` device feature**（否则 VUID）。当前是"忽略该字段、默认 clip"→ 需补映射 + feature。
- **D3D12**：去掉 `GPUPipelineInstance.cpp:159` 硬编码 `DepthClipEnable=FALSE`，改由字段驱动，且**语义与 Vulkan 拉齐**（注意 inverse：Vulkan `depthClampEnable=true`(clamp) ≈ D3D12 `DepthClipEnable=false`(不 clip)，映射需反转）。
- **关键**：一旦两端都读同一字段，就不存在"哪个后端对/选①还是②"——分叉根因正是"缺少统一上层字段、两端各自局部硬编码"。字段的**默认值**是另一个独立小事（单点决定，两端同值），不是二选一的两条路。

**验收**：设置该上层字段 → 双后端对 z<0 几何行为完全一致；ConstantColor/StructuredBufferColor 双后端同步渲染或同步裁剪；无 VUID；`python build.py --config Debug` 通过。

**相关代码**：`Interface/RenderInterface/header/CPipelineStateObject.h:16-19`、`D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp:18-33`、`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp:155-159`、`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp:1823-1831`、`Test/GPUBackendTester/Main.cpp:309-313,422-426`、根因对比 `Test/GPUBackendTester` compute(RWVertexBuffer z=0.5)。

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

## [2026-08-30] vulkan 08114/00331 + D32 depth usage —— 已由 fix-vulkan-descriptor-registration 落地解决

**状态：已解决（2026-08-30，`fix-vulkan-descriptor-registration` 落地并归档）。**

- **08114/00331**：D-A（RAST shader 绑定→RWState）+ D-B（MarkResourceUse 生命周期）+ D-C（持久 offset 绑所有 + per-pool 峰值）+ D1补全（顶层 Foreach insert-only 防 orphan）+ D2（BuildResources 兜底 inert）+ D3（usage 位）落地后，StructuredBufferColor/DoublePass/ComputeBuffer **08114/00331 = 0**，截图真实非黑；vulkan/d3d12 三对捕获字节一致；IMGUI 08114=0 属独立（外部字体图）。
- **D32 depth 02251 / 01209 / 01758 / 02633 / 08931**：D-C bind-all 暴露后，已修 `CollectResources` depth attachment 用 `eDepthStencil`（非 `eRT`），ConstantColor 现仅剩 2× shader-code 08740（另案）。
- **附带 barrier 缺陷（raster 不登记采样读 → 跨 pass 写→读未串行化）**：D-A 已让 raster shader 采样进 RWState → 写→读入生命周期/屏障链，已被覆盖。

**审查闭环**：3 轮对抗验证（4 审查者 / 轮，逐 claim 投票）——第二轮修正 C10（跨别名内存依赖改诚实 submission-local 记录 + 移除不成立 compute 分支）、C11（IMGUI 归因更正）；第三轮收敛（mustFix 清空，bottomLine "TRUSTWORTHY and behaviorally COMPLETE"）。**诚实声明**：C10 跨别名内存依赖为 submission-local best-effort——真正跨批次内存依赖需提交模型改动（单 submit 全批次 / 批间 semaphore），超范围未 overclaim；跨批次正确性依赖既有 waitForFences 串行化 + 设备一致性。

**归档**：`openspec/changes/archive/2026-08-30-fix-vulkan-descriptor-registration/`；delta spec 已同步主 spec。
