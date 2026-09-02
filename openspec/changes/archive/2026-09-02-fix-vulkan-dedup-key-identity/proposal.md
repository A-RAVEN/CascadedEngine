## Why

IMGUI 多窗口副窗偏移已通过新增的逐窗口回读捕获实测（Vulkan 副窗内容整体平移 `(副窗pos − 主窗pos)`，D3D12 正确、主窗正确），根因定位并源级核实为 **Vulkan 后端 shader 绑定实例去重键不足**：

- `VulkanShaderResourceSet::Init` 的去重 hash 只取 shader path + 各绑定名，**忽略 per-pass 的 `VulkanShaderStruct*` 指针**。所有 ImGui viewport pass 共享同一 path（"Shaders/Imgui"）与绑定名（imguiCommon/imguiTextureBinding），因此 hash 全等 → `createOrGetBindingInstance` 按 hash 命中的是**首个（主窗）**绑定实例 → 每个副窗 pass 绑定的都是**主窗的** `imguiCommon` 常量（其中 `IMGUIScale_Pos.zw` = 主窗pos）→ shader 用主窗pos 减副窗自己的正确顶点 → 内容偏移 `(副窗pos − 主窗pos)`。`operator==` 仅为 `hash==hash`，无法区分 viewport。
- 同一根因另有两个关联面：(a) **compute 路径**（`VulkanGraphExecutor.cpp:1144-1146`）复用同一 `Init` + `createOrGetBindingInstance`，两个 dispatch 若同 shader path+绑定名但 per-dispatch struct 指针不同也会合并、绑首个 dispatch 的常量/UAV；(b) **`PipelineLibraryCache` 漏 rasterization 键**（`GenerateHashKey` 不含 polygonMode/cullMode/frontFace/depthClampEnable，但 `VkPipeline` 却由它们构建）→ 同 shader 不同光栅化的 pass 碰撞缓存键、绑首个建立的管线，导致错误的消除/前向面/深度钳制。
- D3D12 绑定实例为内容键（`ShaderResourceSet operator<=> = default` 含 per-viewport struct 指针），故本缺陷为 **Vulkan 专属**，以 D3D12 绑定实例语义为正确性参照。注：D3D12 管线状态键（`GPUPipelineStateKey`）输入覆盖完整 `CPipelineStateObject`（含 rasterization），但其等值为 **hybrid**——`m_ShaderInfo`/`m_AttachmentFormats`/`m_DepthFormat`/`m_VertexInputBindingData` 内容比较，仅 `m_PipelineStates`/`m_InputAssemblyStates` 委托 `HashObj::operator==`（`Hasher.h:258-261`，m_HashValue）为 hash-only（理论 2^-64 碰撞）。

该缺陷是**后端语义正确性**问题（任何"同 shader path + 同绑定名、但 per-pass struct/状态不同"的 pass 都会被错对），非 IMGUI 专属；且同一去重模式会持续在新增多 viewport / 多实例渲染中复现，故统一作为一次修，而非贴 patch。

## What Changes

- **`VulkanShaderResourceSet` 去重键纳入 per-pass struct 指针**：在 `resourceDic` 循环把每个 `structPtr` 折入 hash；`operator==` 改为比较完整 `resourceDic`（name→structPtr）；`m_ShaderResourceInstances`（`size_t` 键）改为以 struct 指针集 / 完整 `resourceDic` 为键。一次改动同时修复 **raster（`1133`）与 compute（`1158`）** 两个触发面，对齐 D3D12 全键语义。
- **`PipelineLibraryCache` 键纳入 rasterization**：`RenderStateCombination` 增加 rasterization 成员（polygonMode/cullMode/frontFace/depthClampEnable；**不含 lineWidth**，后端 `VulkanGraphExecutor.cpp:1827` 硬编码 `lineWidth = 1.0f` 且从不读 per-pass 值），`GenerateHashKey` 哈希它们，`VulkanGraphExecutor.cpp:1899-1917` 的 `stateCombo` 从**已转换的** `vk::PipelineRasterizationStateCreateInfo`（`rasterizationState`）拷贝这 4 个字段。同 shader 不同光栅化的 pass 得到不同缓存键。
- **测试**：`Test/GPUBackendTester` 增两用例，均**扩展既有测试体**（compute 双 dispatch 并入 `TestComputeBuffer`、双光栅化并入 `TestDoublePass`），保持 8 个具名 `--test` 条目不变——(1) 两个 compute dispatch 同 shader path+绑定名但不同 per-dispatch struct 指针 → 各自绑 UBO/UAV 正确；先以 IMGUI 多窗口复现为本 bug 回归项（副窗回读 PNG 偏移消失），(2) 同 shader 不同 cullMode/frontFace 的两次绘制 → 各自正确光线（当前无运行时触发，属可证但未被 fuzz 的加强）。
- **非目标（记录为独立 follow-up，不入本 change）**：`RenderPassCacheKey` sampleCount/loadOp 未纳入键（非失效——render pass 的 attachment samples 与 loadOp 为硬编码常量（`RenderBackend_Vulkan.cpp:895/910`）、msCount 是 per-pipeline 变量（`CPipelineStateObject.h:135` 默认 e1）由 rasterizationSamples 消费 `.cpp:1834`，属"忽略 per-pass AttachmentConfig"的 config-ignoring 缺陷，无错误实体合并；非 1x msCount 会触 renderpass-vs-pipeline samples VUID——独立 gap，另记）；D3D12 `EnsureResourceBindingInstance` 空返回（死代码）；D3D12 `HashObj` 纯 hash 等值（仅理论 2^-64 碰撞）；`PipelineLibraryCache` 仍以哈希为存储键（与 D3D12 一致）；因字段集收敛，2^-64 哈希碰撞为纯理论概率（同 D3D12），列为 Non-Goal 记录，本 change 修的是确定性的字段排除错误。

## Capabilities

### New Capabilities
- `vulkan-pipeline-cache`: Vulkan 后端的 VkPipeline 缓存键 SHALL 包含真正驱动管线光栅化行为的状态（rasterization 的 polygonMode/cullMode/frontFace/depthClampEnable），同一 shader 不同光栅化状态不得共享同一缓存键（字段排除错误；hash-only 的 2^-64 纯概率碰撞列为非目标）。

### Modified Capabilities
- `vulkan-shader-binding`: shader 绑定实例去重键 SHALL 包含 per-pass 的 shader-struct 身份（而非仅 shader path + 绑定名），使不同 per-pass 常量（struct）的 pass 各自得到独立绑定实例及其常量绑定。D3D12 绑定实例内容键语义为参照。

## Impact

- 代码：`VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp`（`VulkanShaderResourceSet::Init` 的 hash 循环 `205-211`、`createOrGetBindingInstance` `1085-1096`、raster `1119-1121`、compute `1144-1146`、`stateCombo` `1899-1917`）、`VulkanGraphExecutor.h`（`operator==` `214-216`、`m_ShaderResourceInstances` 键 `321`）；`VulkanRenderBackendNew/private/PipelineLibrary/PipelineLibraryCache.h/.cpp`（`RenderStateCombination`、`GenerateHashKey`、`TryGetCachedPipeline`）。
- 参照：`D3D12RenderBackend/private/GPUGraph/GPUResourceBindingInstance.h:27-33`（绑定实例内容键语义参照；`D3D12` 管线状态键 `GPUPipelineInstance.h:68-104` 输入覆盖完整 `CPipelineStateObject` 但等值为 hybrid——4 字段内容比较 + 2 字段 HashObj 纯 hash，如实描述非全内容键）。`Design` 注释：D3D12 光栅化区分其实际落在 2^-64 HashObj 碰撞概率之上。
- 测试：`Test/GPUBackendTester`（IMGUI 多窗口回归 + compute 双 dispatch + 双光栅化用例，均扩展既有测试体保持 8 个具名 `--test`）；新捕获工具 `test_output/TestIMGUI_viewport<N>_<backend>.png` 已在（`0c0a6e5`）。
- 无外部 API 变更；验证 implicates `python build.py --config Debug` + vulkan/d3d12 headless 全 8 测试 exit 0 + 无新增 VUID。
