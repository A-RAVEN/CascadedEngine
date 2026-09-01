## 1. 根因确认与 API 语义核实

- [ ] 1.1 确认 `m_ShaderResourceInstances` 迭代/清空位置（`VulkanGraphExecutor.cpp:482/575/593/623/662/831` 迭代、`2972-2985` Reset clear），确定改键类型影响面。
- [ ] 1.2 核实 `cacore::hash_combine`（`CACore/header/Hasher.h:10`）为纯函数（返回组合后 seed），确认"折入指针"的正确写法。
- [ ] 1.3 MCP 核实外部 API 前提：`VkPipeline`（含 GPL）缓存键不隐含 rasterization 一致性、`vk::PipelineCache` 语义；并复核 D3D12 参照（`GPUResourceBindingInstance.h:27-33` 绑定实例内容键；`GPUPipelineInstance.h:68-104` 输入覆盖完整 `CPipelineStateObject` 但等值为 hash-only，如实描述）——引用真实文档 URL。

## 2. VulkanShaderResourceSet 去重身份纳入 per-pass struct 指针

- [ ] 2.1 `VulkanShaderResourceSet`（`VulkanGraphExecutor.h`）：`operator==` 改为内容式（比较 `info.path` + `resourceDic` 的 name→structPtr 映射）；`hash` 计算基于完整身份（path + 每个 `[name, structPtr]`）。
- [ ] 2.2 `VulkanShaderResourceSet::Init`（`VulkanGraphExecutor.cpp:205-211`）：hash 循环内同时 `hash_combine` 每个 `structPtr`（与 `name.GetHash()` 一起），不再只 hash name。
- [ ] 2.3 `m_ShaderResourceInstances` 键（`VulkanGraphExecutor.h:321`）：按设计决策（选项 A）将键改为完整 `VulkanShaderResourceSet` 内容键（含每 `[name, structPtr]`）；提供 `cacore::hashable<VulkanShaderResourceSet>` / `cacore::equal_test<VulkanShaderResourceSet>` 特化（参照 `GPUResourceBindingInstance.h:34-35` 的 `static_assert`）；同步更新 `.cpp:482/575/593/623/662/831` 六处迭代解构。**不接受**保留 `size_t` 键的选项 B（见 design Open Questions）。
- [ ] 2.4 `createOrGetBindingInstance`（`VulkanGraphExecutor.cpp:1085-1096`）：基于新键 find/insert，确认 raster（`1119-1121`）与 compute（`1144-1146`）两个触发面都被正确拆分。

## 3. PipelineLibraryCache 纳入 rasterization

- [ ] 3.1 `RenderStateCombination`（`PipelineLibraryCache.h:10-39`）：增加 rasterization 字段（polygonMode/cullMode/frontFace/depthClampEnable；**不含 lineWidth**，后端硬编码 `1.0f` 且从不读 per-pass 值）。
- [ ] 3.2 `GenerateHashKey`（`PipelineLibraryCache.cpp:19-96`）：哈希上述 4 个 rasterization 字段。
- [ ] 3.3 `VulkanGraphExecutor.cpp:1899-1917`（`stateCombo` 拷贝）：从**已转换的** `vk::PipelineRasterizationStateCreateInfo`（`rasterizationState`）拷贝 polygonMode/cullMode/frontFace/depthClampEnable 到 `RenderStateCombination`（与既有约定一致：topology/sampleCount/depthCompareOp/stencil 均取自已转换 vk 结构）。

## 4. 测试用例

- [ ] 4.1 IMGUI 多窗口回归：用已落地的逐窗口回读（`TestIMGUI --capture`）生成 `test_output/TestIMGUI_viewport<N>_<backend>.png`（N=0 主窗；实际命名 `TestIMGUI_viewport<N>_<backend>.png`），用**外部 PIL 量化**验证副窗非背景 bbox 回到 `(1,1,...)`（偏移 `(副窗pos − 主窗pos)` 归零），与 d3d12 一致。
- [ ] 4.2 compute 双 dispatch 同 shader 不同 struct：加用例确保两个 dispatch 各自绑定各自的 cbuffer/UAV（首个 dispatch 常量不泄漏）。
- [ ] 4.3 同 shader 不同 cullMode/frontFace：加用例确保两个 pass 得到各自 `VkPipeline`（正确剔除/前向面），而非共享首个管线。

## 5. 编译验证

- [ ] 5.1 `python build.py --config Debug` 直到 BUILD SUCCESSFUL，无新增编译错误（不手动敲 ninja/cmake）。

## 6. 运行时回归验证

- [ ] 6.1 vulkan + d3d12 全部 8 个具名 `--test` 条目（4.2/4.3 并入既有 `TestComputeBuffer`/`TestDoublePass` 体，不新增具名条目）headless exit 0，validation log 无新增 VUID/object-tracking（D3D12 参照不得回归）。
- [ ] 6.2 IMGUI 副窗回读 PNG 偏移归零：PIL 量化非背景 bbox 回到 `(1,1,...)`（与 d3d12 一致），记录前后 bbox 对比（路径 `test_output/TestIMGUI_viewport<N>_<backend>.png`）。

## 7. 审查闭环（Review & Adversarial Verify）

- [ ] 7.1 开 workflow 对抗验证：多独立审查者逐 claim 核对——struct 指针折入 hash/内容等值的正确性、`operator==` 与 hash 一致性、六处迭代点更新、compute 触发面是否被同一修复覆盖且无回归、PipelineLibraryCache rasterization 字段与 `VkPipeline` 架构一致性、D3D12 参照无回归、测试证据（PNG 偏移归零 / exit 0 / 无新增 VUID）真实；引用的 API 文档 URL 真实性由对抗者独立复查。
- [ ] 7.2 回归检查与去重：核对本 change 是否引入新问题；验证发现的新问题以 `[AUDIT-]` 前缀追加为新的 task 并追加新的审查任务；结果写入 tasks.md 末尾 `## Review Log`。循环直到无新问题或达 3 轮；**停下时交付 Review Log**（而非实现进度汇报）。
