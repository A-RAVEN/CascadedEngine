# Vulkan 后端 D3D12 对齐路线图 — Delta

**Change**: fix-vulkan-backend-render-pipeline
**Updated**: 2026-06-28

---

## ADDED Requirements

### Requirement: Phase 1 新增差距项

Phase 1 差距表 SHALL 新增以下条目，追踪本次变更补全的核心功能缺口。

#### Scenario: 表格新增条目

- **WHEN** 查看 Phase 1 差距表
- **THEN** 包含以下新行：
  - `| 8 | **Buffer UploadData** | GPU-only buffer 的 staging 上传未实现 | P0 | fix-vulkan-backend-render-pipeline | ✅ 已完成 |`
  - `| 9 | **Texture UploadData** | 纹理 staging 上传完全空实现 | P0 | fix-vulkan-backend-render-pipeline | ✅ 已完成 |`
  - `| 10 | **Pipeline States 完整化** | FragmentOutputStates/FragmentShaderStates 空结构体，StateManager 复制粘贴占位 | P2 | fix-vulkan-backend-render-pipeline | ✅ 已完成 |`
  - `| 11 | **CreateMonolithicPipeline 硬编码** | topology/cullMode/frontFace/polygonMode/sampleCount/blend 等 7 个值硬编码 | P2 | fix-vulkan-backend-render-pipeline | ✅ 已完成 |`

## MODIFIED Requirements

### Requirement: Phase 1 目标达成

Phase 1 目标 "Vulkan 后端能完整执行 GPUGraph 的所有 Pass 类型" SHALL 在本次变更后达成，所有 Pass 类型（RenderPass、ComputePass、TransferPass）均可被识别和提交。

#### Scenario: 所有 Pass 类型可执行

- **WHEN** GPUGraph 包含任意 EGraphStageType 组合（eRenderPass、eComputePass、eTransferPass）
- **THEN** VulkanGraphExecutor 正确识别每种类型，无静默丢弃

#### Scenario: Buffer/Texture 数据上传可用

- **WHEN** Buffer 分配在 device-local 内存或 Texture 需要 CPU→GPU 数据上传
- **THEN** `UploadData` 正确通过 staging buffer 完成数据上传，数据在 GPU 端可见
