# Vulkan后端 D3D12 对齐路线图 — Delta

参考 spec: `openspec/specs/vulkan-backend-alignment/spec.md`

---

## MODIFIED Requirements

### Requirement: Phase 3 架构完善 — SamplerManager 状态更新

Phase 3 表中 SamplerManager 项目状态从 ❌ 未开始 更新为 ✅ 已完成，ApplyExternalResourceStates 从 ❌ 未开始 更新为 ✅ 已完成。

| # | 模块 | 差距 | 优先级 | 状态 |
|---|------|------|--------|------|
| 1 | SamplerManager | 无独立 sampler 管理器，每帧重建 sampler | P2 | ✅ 已完成 |
| 2 | ApplyExternalResourceStates | 空函数，不更新外部资源状态 | P2 | ✅ 已完成 |
| 3 | RunTestCode | 测试入口未实现 | P3 | ❌ 未开始 |