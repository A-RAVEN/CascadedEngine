## Context

Vulkan 后端并行审查发现 46 个已验证缺陷。经对抗验证审查，确认 18 critical+high 缺陷 + 审查新发现的 1 个遗漏项（pipelineLayout null check）为本 change 范围。修复按 6 个组组织：资源注册、空句柄防御、资源泄漏、同步修复、描述符管线、LinearMemoryManager 加固。

`CollectResources` 是资源注册的核心入口。当前仅调用 `RegisterTemporary*`（建立临时 ResourceID），从未调用 `Register*Handle`（建立 Handle→ResourceID 映射）。这是所有后续 null handle 问题的根因。

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

### D4: Fence 双 bool 跟踪

- **选择**：`m_DirectFenceSubmitted` + `m_ComputeFenceSubmitted` 两个 bool，在每条提交路径设置对应标志；batch sync 仅 wait+reset 已提交的 fence；wait+reset 后清除标志
- **理由**：两个 fence 独立提交，单个 bool 无法区分。跨 queue 路径也需要正确标记

### D5: AllocateAliasedResources 清理按 Route A/B 分支

- **选择**：检查 `managed.allocation` 是否非空。非空 → Route B（VMA 分配）→ `FreeImage/FreeBuffer`；空 → Route A（bindImageMemory）→ `device.destroyImage/destroyBuffer`
- **理由**：两条路径的分配 API 不同，清理 API 也必须不同

### D6: Init 部分失败用步进清理而非 Release()

- **选择**：每个失败点就地销毁已初始化的组件（逆序），而非调用全量 Release()
- **备选**：调用 Release()（原方案 task 3.7）
- **理由**：Release() 假设所有子对象已初始化，在半初始化状态调用会访问未初始化的 handle/指针

### D7: Sampler 修复范围限制

- **选择**：仅修 `maxLod = VK_LOD_CLAMP_NONE` + 映射 `boarderColor`（struct 中实际存在的字段）
- **理由**：anisotropy/compare/minLod 等字段在 `TextureSamplerDescriptor` 中不存在，需先扩展 descriptor struct

## Risks / Trade-offs

- **[逐点注册遗漏风险]**：若未来有人添加新的 RegisterTemporary* 但忘记配对 Register*Handle → 添加编译期断言或 CA_ASSERT 在 GetBuffer/GetTexture 中检测
- **[reserve 估算偏差]**：若 `reserve` 不准确仍可能重分配 → 在 debug 构建中 CA_ASSERT 验证 capacity
- **[Sampler 默认值变更]**：maxLod 0→VK_LOD_CLAMP_NONE 改变所有 sampler 行为 → 这是 bug 修复，非行为变更
- **[Init 步进清理复杂]**：步进清理需要逆序销毁 → 代码行数增加，但正确性优先
- **[LinearMemoryManager Reset 裁剪]**：裁剪策略可能过于激进或保守 → 先用简单策略（保留最近 N 帧的页面），后续可调
