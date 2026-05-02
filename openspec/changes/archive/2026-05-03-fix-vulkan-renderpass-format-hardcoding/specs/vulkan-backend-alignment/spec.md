## MODIFIED Requirements

### Requirement: Phase 1 核心功能补全

目标：Vulkan 后端能完整执行 GPUGraph 的所有 Pass 类型。

| # | 模块 | 差距 | 优先级 | Change | 状态 |
|---|------|------|--------|--------|------|
| 1 | Shader Module | ShaderModule 创建 | P0 | - | ✅ 已完成 |
| 2 | Pipeline Layout | PipelineLayout + DescriptorSetLayout 缓存 | P0 | - | ✅ 已完成 |
| 3 | Pipeline 创建 | Graphics + Compute Pipeline 完整流程 | P0 | - | ✅ 已完成 |
| 4 | Descriptor 流程 | DescriptorPool → Allocate → Write → Bind | P0 | - | ✅ 已完成 |
| 5 | CBuffer 流程 | ShaderStruct → staging upload → barrier | P0 | - | ✅ 已完成 |
| 6 | Compute Pass 资源注册 | CollectResources 中 Compute image/buffer 注册 | P0 | `vulkan-compute-pass-resources` | ✅ 已完成 |
| 7 | **RenderPass 格式转换** | 消除硬编码 `eD32Sfloat` / `eR8G8B8A8Unorm` | P1 | `fix-vulkan-renderpass-format-hardcoding` | 🚧 Proposal |

#### Scenario: Phase 1 所有项目完成
- **WHEN** Phase 1 的 7 个项目全部标记为已完成
- **THEN** Phase 1 关闭，可以开始 Phase 2 开发
