# Vulkan后端 D3D12 对齐路线图 (Delta)

**Version**: 1.1
**Parent Spec**: `openspec/specs/vulkan-backend-alignment/spec.md`

---

## MODIFIED Requirements

### Requirement: Phase 3 架构完善差距追踪

本规范追踪 VulkanRenderBackendNew 在接口功能上向 D3D12RenderBackend（参考实现）对齐的阶段计划。Phase 3 新增差距项 #4。

#### Scenario: Phase 3 差距表包含别名资源绑定

- **WHEN** 查阅 Phase 3 差距表
- **THEN** 表中包含以下行：

| # | 模块 | 差距 | 优先级 | Change | 状态 |
|---|------|------|--------|--------|------|
| 1 | SamplerManager | 无独立 sampler 管理器，每帧重建 sampler | P2 | - | ✅ 已完成 |
| 2 | ApplyExternalResourceStates | 空函数，不更新外部资源状态 | P2 | - | ✅ 已完成 |
| 3 | RunTestCode | 测试入口未实现 | P3 | - | ❌ 未开始 |
| 4 | Resource Aliasing | 别名资源未绑定到 aliased pool 的计算偏移，`AllocateAliasedResources()` 对每个资源独立分配显存，内存复用无效 | P1 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
| 5 | ImageView AspectMask | ImageView `aspectMask` 硬编码为 `eColor`，深度/模板格式触发 VUID 验证错误 | P1 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
| 6 | Image Usage Flags | Image `usage` 硬编码为 `eSampled\|eColorAttachment`，深度缓冲/transfer-only 纹理缺少正确 flags | P2 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
| 7 | Buffer Usage Flags | Buffer `usage` 硬编码为 `eTransferDst\|eVertexBuffer`，未根据 `bufferUsage` 动态推导 | P2 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
| 8 | mappedPtr Propagation | `ManagedGPUResource.mappedPtr` 未从 aliased pool 传播，CBuffer CPU 端更新路径断裂 | P2 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
| 9 | AddBuffer Aliasing | `AddBuffer()` 独立分配路径完全绕过 aliasing 系统，后注册 buffer 无法复用内存 | P2 | `fix-vulkan-aliasing-resource-binding` | ❌ 未开始 |
