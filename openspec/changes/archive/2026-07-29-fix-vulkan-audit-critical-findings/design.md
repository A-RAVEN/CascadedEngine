## Context

Vulkan 后端并行审查发现 46 个已验证缺陷。经对抗验证审查，确认 18 critical+high 缺陷 + 审查新发现的 1 个遗漏项（pipelineLayout null check）为本 change 范围。修复按 6 个组组织：资源注册、空句柄防御、资源泄漏、同步修复、描述符管线、LinearMemoryManager 加固。

`CollectResources` 是资源注册的核心入口。~~当前仅调用 `RegisterTemporary*`，从未调用 `Register*Handle`~~ → **已修复**（commit 9f3fe542，tasks 1.1-1.5 完成）。剩余工作集中在空句柄防御、资源泄漏、同步、描述符安全和内存加固。

**2026-07-29 基线审查**（26 agent 对抗验证，0 REFUTED）：行号全部对齐，发现 4.1 设计与现有架构冲突（已修订），4.2 已由局部 bool 实现，4.3 调用点从 6 减至 4（frame-3 修复删除了 Aquire 的 2 处），新增 3 个风险（physical device 空检查、WaitIdle 首帧 hang、Route A bind* 未包裹）。

## Goals / Non-Goals

**Goals:**
- 修复资源注册缺口：每个 RegisterTemporary* 调用点配对 Register*Handle
- Image barrier 路径获得与 buffer barrier 同构的 null guard
- pipelineLayout/ShaderModule/RenderPass/pipeline/staging buffer 全部 null 检查
- 错误路径上正确清理部分分配的资源（按 Route A/B 分支）
- Fence 同步：双 bool 跟踪、仅 wait 已提交的 fence、6 处等待返回值检查
- 描述符写入 reserve() + sampler maxLod/borderColor + null pipeline guard（raster+compute）

**Non-Goals:**
- 不修复性能问题（D-32 eAllCommands 屏障合并、D-46 计算过度屏障、D-53 冗余 release+acquire）
- 不修复 medium/low 级别的状态语义问题（D-37 效率指标、D-54 InitializedState 语义）
- 不添加 validation layer NDEBUG 条件编译（当前开发阶段需要）
- 不扩展 TextureSamplerDescriptor 添加 anisotropy/compare 等字段（需独立 change）

## Decisions

### D1: CollectResources 中逐点配对注册（非 foreach 统一注册）

- **选择**：在每个 `RegisterTemporary*` 调用点后紧跟 `Register*Handle(handle, returnedResourceId)`，利用已存在的 handleKey 参数
- **备选**：foreach lambda 内统一注册（原方案 task 1.1/1.2）
- **理由**：foreach 遍历所有 buffer/image 但会创建空 usage flags 的新 ResourceID，与已有逐点注册产生双重 ResourceID。逐点配对不会重复，且与现有代码结构一致

### D2: Image barrier null guard 与 buffer 同构

- **选择**：在 `PrepareBatchResourceBarriers` 的 4 处 `GetTexture(image)` 后添加 `if (!img) continue;`
- **理由**：与 buffer 路径完全一致的防御模式（fix-null-buffer-barrier 中已实现）

### D3: 描述符写入：显式 reserve() 防止重分配

- **选择**：`m_BufferInfos.reserve(CBufferBindings.size() + BufferBindings.size())`；`m_ImageInfos.reserve(ImageBindings.size() + SamplerBindings.size())`
- **理由**：vector push_back 重分配会使已存储的 `pBufferInfo`/`pImageInfo` 指针失效，这是最危险的 correctness bug

### D4: Fence 同步简化（修订，原方案：双 bool 拆分）

- **原方案**：`m_DirectFenceSubmitted` + `m_ComputeFenceSubmitted` 两个持久 bool
- **问题**：SubmitBatches 已用局部 `directSubmitted`/`computeSubmitted`（line 2292-2293）实现了 per-batch sync（4.2 已完成）。持久 `m_FenceSubmitted` 仅被 `GPUFrameManager::WaitIdle` 使用，且 fence 创建时 unsignaled → 首帧 WaitIdle 的 waitForFences 永远等不到（hang）
- **修订选择**：删除 `m_FenceSubmitted` / `MarkFenceSubmitted()` / `IsFenceSubmitted()`。`WaitIdle` 改为 `device.waitIdle()`（与 `RenderBackend_Vulkan::WaitIdle` 一致，简单且正确）
- **理由**：双 bool 拆分解决的是不存在的问题（per-batch sync 已由局部变量覆盖），而 WaitIdle 的 fence-based 实现在首帧有 hang 风险

### D5: AllocateAliasedResources 清理按 Route A/B 分支（scope 扩展）

- **选择**：检查 `managed.allocation` 是否非空。非空 → Route B（VMA 分配）→ `FreeImage/FreeBuffer`；空 → Route A（bindImageMemory）→ `device.destroyImage/destroyBuffer`
- **扩展**：Route A 的 `bindBufferMemory`（line 360）和 `bindImageMemory`（line 401）也需要 try-catch 包裹——审查发现这两个调用同样可抛 vk::SystemError，但原 task 仅覆盖 create* 调用
- **理由**：两条路径的分配 API 不同，清理 API 也必须不同；bind* 是 Route A 独有的失败点

### D6: Init 部分失败用步进清理而非 Release()

- **选择**：每个失败点就地销毁已初始化的组件（逆序），而非调用全量 Release()
- **备选**：调用 Release()（原方案 task 3.7）
- **理由**：Release() 假设所有子对象已初始化，在半初始化状态调用会访问未初始化的 handle/指针

### D7: Sampler 修复范围限制

- **选择**：仅修 `maxLod = VK_LOD_CLAMP_NONE` + 映射 `boarderColor`（struct 中实际存在的字段）
- **理由**：anisotropy/compare/minLod 等字段在 `TextureSamplerDescriptor` 中不存在，需先扩展 descriptor struct

### D8: Init 物理设备空检查（审查新增）

- **选择**：`enumeratePhysicalDevices()` 返回空列表时（无 GPU），不能直接 `.front()`（line 222，UB/crash），需检查并提前返回错误
- **理由**：审查发现 `.front()` 无空检查。虽然实际部署不太可能无 GPU，但这是 UB 级别的缺陷

### D9: 描述符 reserve 成员名对齐（审查修正）

- **选择**：`m_BufferInfos.reserve(m_CBufferBindings.size() + m_BufferBindings.size())`；`m_ImageInfos.reserve(m_ImageBindings.size() + m_SamplerBindings.size())`
- **理由**：原 D3 使用了不带 `m_` 前缀的名称，与实际成员变量不一致

### D10: Sampler try-catch 位置（审查修正）

- **选择**：try-catch 包在 `m_SamplerCache.get_or_create()` **外层**，不在 lambda 内
- **理由**：lambda 内 catch 会导致 CASharedDic 缓存 null sampler（get_or_create 存储 lambda 返回值），后续调用命中缓存得到永久 null

## Risks / Trade-offs

- **[逐点注册遗漏风险]**：若未来有人添加新的 RegisterTemporary* 但忘记配对 Register*Handle → 添加编译期断言或 CA_ASSERT 在 GetBuffer/GetTexture 中检测
- **[reserve 估算偏差]**：若 `reserve` 不准确仍可能重分配 → 在 debug 构建中 CA_ASSERT 验证 capacity
- **[Sampler 默认值变更]**：maxLod 0→VK_LOD_CLAMP_NONE 改变所有 sampler 行为 → 这是 bug 修复，非行为变更
- **[Init 步进清理复杂]**：步进清理需要逆序销毁 → 代码行数增加，但正确性优先
- **[LinearMemoryManager Reset 裁剪]**：裁剪策略可能过于激进或保守 → 先用简单策略（保留最近 N 帧的页面），后续可调
