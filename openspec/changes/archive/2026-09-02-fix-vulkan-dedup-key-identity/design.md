## Context

IMGUI 多窗口副窗偏移的根因（实测 + 源级核实）是 Vulkan 后端 shader 绑定实例去重键不足：`VulkanShaderResourceSet::Init` 的 hash 循环（`VulkanGraphExecutor.cpp:211-219`）的 `hash` 只由 `info.path.GetHash()` + 各绑定名 `name.GetHash()` 组成，**忽略了 `resourceDic` 的 `[name, structPtr]` 对里的 per-pass `VulkanShaderStruct*`**。`VulkanShaderResourceSet::operator==`（`.h:221-233`）仅 `hash==hash`，`m_ShaderResourceInstances` 是 `unordered_map<size_t, ...>`（`.h:370`）以裸 `size_t` 为键——因此所有 ImGui viewport pass（同 path "Shaders/Imgui" + 同绑定名 imguiCommon/imguiTextureBinding）哈希全等，`createOrGetBindingInstance`（`.cpp:1096-1107`）按 hash 命中**首个（主窗）**绑定实例，副窗因此绑定主窗的 `imguiCommon` 常量 → shader 用主窗pos 减副窗顶点 → 内容偏移 `(副窗pos − 主窗pos)`。

同一根因的第二个触发面在 compute 路径（`.cpp:1158`）：compute dispatch 各自持有独立 `ShaderStructDic`（`GPUGraph.h:374-381`），复用同一 `Init` + `createOrGetBindingInstance` → 同 path+绑定名、不同 per-dispatch struct 指针时同样被合并、绑首个 dispatch 的 cbuffer/UAV。

另有独立同类缺陷：`PipelineLibraryCache`（`PipelineLibraryCache.h:10-43`、`GenerateHashKey` `.cpp:19-104`）的缓存键**不含光栅化状态**（polygonMode/cullMode/frontFace/depthClampEnable），但 `VkPipeline` 却由这些字段构建（`VulkanGraphExecutor.cpp:1835-1844` 载入 rasterizationState，再进 GPL pre-raster `:1952`/monolithic `:1990`）→ 同 shader 不同光栅化状态的 pass 碰撞缓存键，绑首个建立的管线（错误剔除/前向面/深度钳制）。

D3D12 的 shader 绑定实例（`ShaderResourceSet`，`GPUResourceBindingInstance.h:32` `operator<=> = default` 含 per-viewport struct 指针）为内容键——这是本 change 对齐的参照；D3D12 的管线状态键（`GPUPipelineStateKey`，`GPUPipelineInstance.h:68-104`）输入覆盖完整 `CPipelineStateObject`（含 rasterizationStates）从而能区分光栅化，但其等值为 **hybrid**（非全键 hash-only）：`GPUPipelineStateKey::operator==` 对 `m_ShaderInfo`/`m_AttachmentFormats`/`m_DepthFormat`/`m_VertexInputBindingData`（h:70/73/74/75）做内容比较，仅 `m_PipelineStates`/`m_InputAssemblyStates`（h:71/72）委托 `HashObj::operator==`（`Hasher.h:258-261`，m_HashValue==m_HashValue）为 hash-only；相较 `ShaderResourceSet` 的彻底内容键，其光栅化区分靠在 2^-64 HashObj 碰撞概率之上（同 `CPipelineStateObject` 反射含 rasterizationStates 于 `CPipelineStateObject.h:133`）。

`m_ShaderResourceInstances` 在 `VulkanGraphExecutor::Reset()`（`.cpp:3005`）中被 `clear()`，即**每次 execute/每帧重建**，因此把 struct 指针纳入键不会跨帧累积增长。

## Goals / Non-Goals

**Goals:**
- 让 `VulkanShaderResourceSet` 的去重身份**纳入 per-pass 的 shader-struct 指针**（对齐 D3D12 `ShaderResourceSet` 内容键语义），使"同 shader path + 同绑定名、但不同 per-pass 常量"的 pass 各自得到独立 `VulkanResourceBindingInstance` 及其常量绑定。同时修复 raster（`1133`）与 compute（`1158`）两个触发面。
- 让 `PipelineLibraryCache` 的缓存键纳入真正驱动 `VkPipeline` 的光栅化状态（polygonMode/cullMode/frontFace/depthClampEnable），使"同 shader 不同光栅化"的 pass 得到独立 `VkPipeline`。
- 回归验证：IMGUI 多窗口副窗回读 PNG 偏移消失；新增 compute 双 dispatch、双光栅化用例；双端全测试 exit 0、无新增 VUID。

**Non-Goals:**
- 不修 `RenderPassCacheKey` sampleCount/loadOp 未纳入键（已证伪为"错误合并"——render pass 的 attachment samples 与 loadOp 为硬编码常量（`RenderBackend_Vulkan.cpp:895/910`）；msCount 是 per-pipeline 变量（`CPipelineStateObject.h:135` 默认 e1）由 rasterizationSamples 消费 `.cpp:1847`，非合并问题；且非 1x msCount 会触 renderpass-samples(e1) vs pipeline-samples 不一致 VUID——独立 gap，另记）→ 记为独立 follow-up。
- 不修 D3D12 `EnsureResourceBindingInstance` 空返回（死代码）、D3D12 `HashObj` 纯 hash 等值（仅理论 2^-64 碰撞，本 change 不改为内容键，仅如实描述）。
- 不改 D3D12 任何行为。

## Decisions

### Decision 1：`VulkanShaderResourceSet` 以完整内容为去重身份（选项 A，唯一方案）

让 `VulkanShaderResourceSet` 可作 `unordered_map` 键：`operator==` 改为比较 `resourceDic`（name→structPtr，含指针）与 `info.path`；提供 hash 函数把 path + 每个 `[name, structPtr]` 对折入（order-stable，见风险）；`m_ShaderResourceInstances` 键类型由 `size_t` 改为 `VulkanShaderResourceSet`。
- 理由：与 D3D12 `ShaderResourceSet` 内容键语义完全对齐；消除"hash-only operator== 对不同 struct 返回相等"的潜在错误合并；`Reset()` 每帧 clear 使数量上界 = 每帧 pass 数。
- 代价：`.cpp:490/583/601/631/670/839` 六处 `for (auto& [hash, instance] : m_ShaderResourceInstances)` 的解构需改为 `[resourceSet, instance]`；需为 `VulkanShaderResourceSet` 提供内容 hash + 内容 `operator==`。
- 备选 B（保留 `size_t` 键 + 仅折叠指针，哈希仍 hash-only 等值）**已否决**：无法满足 spec 的"等值基于完整身份"，且保留 2^-64 碰撞脆弱性——本 change 不接受。

### Decision 2：`PipelineLibraryCache` 键纳入光栅化状态

`RenderStateCombination`（`PipelineLibraryCache.h:10-43`）增加 polygonMode/cullMode/frontFace/depthClampEnable（**不含 lineWidth**——后端 `VulkanGraphExecutor.cpp:1839` 硬编码 `lineWidth = 1.0f` 且从不读 `rasterizationStates.lineWidth`，纳入会形成"键随不变量变化"的空维度）；`GenerateHashKey`（`.cpp:19-104`）哈希它们；`VulkanGraphExecutor.cpp:1931-1934` 的 `stateCombo` 从**已转换的** `vk::PipelineRasterizationStateCreateInfo`（`rasterizationState`）拷贝这 4 个字段（与既有约定一致：topology 从 inputAssemblyState、sampleCount 从 multisampleState、depthCompareOp/stencil 从 vk depthStencilState）。
- 理由：键与 `VkPipeline` 实际构建输入对齐；字段集对"当前 rasterization→VkPipeline 映射"是完整的。缓存**仍以哈希为键**（与 D3D12 一致），因 `lineWidth`/`discardRasterization`/`depth-bias` 不随 per-pass 变化或未映射，字段集收敛，2^-64 哈希碰撞为理论纯概率（同 D3D12），列为 Non-Goal 记录而非本 change 处理。
- 完整性注：`RasterizerStates.discardRasterization`/depth-bias（`CPipelineStateObject.h:16-48`）当前未映射到 Vulkan（`rasterizerDiscardEnable`/`depthBias*` 留默认，`.cpp:1835-1844`）；若将来接入，键必须扩展。
- 备选（被拒）：把整个缓存键改为"完整管线状态对象"内容键（`unordered_map<RenderStateCombination,...>`）——更彻底但改动面大，超出本 change 范围，且本 change 的字段收敛已消除确定性的字段排除错误。

## Risks / Trade-offs

- **[绑定实例去重按指针拆分会增加实例/描述符集数量]** → 每帧上界 = 该帧 pass 数，`Reset()` 每帧清空（`.cpp:3005`）使总量不回放；仅真正"不同 struct 指针"的 pass 才拆分（正确行为），共享同 struct 的 pass 仍去重。代价可忽略。
- **[struct 指针逐帧不同导致同逻辑 pass 跨帧分裂]** → 实例集每帧重建（`Reset()` clear），无跨帧持久 identity 漂移；同一帧内 struct 指针稳定（`PrepareDrawData` 每 viewport 创建一次，`IMGUIContext.cpp:813`）。
- **[hash 顺序依赖]** → `Init` 的 hash 循环遍历 `resourceDic`（unordered_map），当前因插入路径固定（`rootHierarchy.m_SubBindingHierarchies` `.cpp:197-203` 固定 per-shader 顺序）而 order-stable；为使"等值集 ⇒ 同 hash"由构造保证而不依赖反射顺序，实现时对 `[name, structPtr]` 按 NameHash 排序（或 XOR/有序组合）后再哈希——**落地采用 XOR/有序组合**（`.cpp:213-219`），不依赖 map 迭代顺序。
- **[PipelineLibraryCache 仍为 hash-only]** → 存储以哈希键检索（与 D3D12 一致）。因 `lineWidth`/`discardRasterization`/`depth-bias` 不随 per-pass 变化或未映射，字段集收敛，2^-64 哈希碰撞为纯理论概率（同 D3D12 `HashObj`）；列为 Non-Goal 记录（本 change 修的是确定性的字段排除错误，非纯撞概率）。
- **[改 `operator==` 影响其它用 `VulkanShaderResourceSet` 比较处]** → 实现时 grep 全部使用点确认只有 `createOrGetBindingInstance`/`find` 与迭代处；若有其它依赖 hash-only 的调用需一并核对。

## Migration Plan

无运行时/数据迁移。代码改动属本地后端修复；实施后 `python build.py --config Debug` 到 BUILD SUCCESSFUL → vulkan/d3d12 全 8 测试 headless exit 0 → IMGUI 多窗口副窗回读 PNG 偏移消失（`(副窗pos − 主窗pos)` 归零、与 d3d12 一致）→ 新增 compute/光栅化用例通过。回滚：`git revert` 本 change 对应 commit（改动局部、无外部契约）。

## Open Questions

- `castl::unordered_map` 以 `VulkanShaderResourceSet` 为键接入——需提供 `cacore::hashable<VulkanShaderResourceSet>` / `cacore::equal_test<VulkanShaderResourceSet>`（参照 `GPUResourceBindingInstance.h:34-35` 的 `static_assert`），task 2.3 已含。**已确认唯一可行方案**：内容键（选项 A）才能消除 `size_t` 键在 2^-64 碰撞时仍合并的错误；退回到 `size_t` 键不能消除该碰撞（`operator==` 对 size_t-keyed map 无效），故不采纳。
