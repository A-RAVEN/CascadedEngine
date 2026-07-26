# Tasks: 修复 null buffer barrier

**Change ID**: fix-null-buffer-barrier

---

## 1. QFOT 路径 null guard（3 处）

- [x] 1.1 Line **1206**：跨帧 QFOT acquire（`if (qfotNeeded && isFirstState && buffer.GetType() == External)` 块内）。已有 External 类型守卫，null guard 属于额外防御。
- [x] 1.2 Line **1230**：同帧 QFOT release（`if (qfotNeeded && !isFirstState)` 块内）。
- [x] 1.3 Line **1248**：同帧 QFOT acquire（与 1.2 在同一 if 块内配对）。

## 2. 常规 barrier 路径 null guard（1 处）

- [x] 2.1 Line **1269**：在 `PrepareBatchResourceBarriers` 的常规 barrier 创建处（qfot skip + 状态变化检查之后）。

## 3. 编译验证与测试

- [x] 3.1 运行 `build.py`，确认 BUILD SUCCESSFUL。
- [ ] 3.2 运行 `GPUBackendTester --backend vulkan --test TestSimpleTriangle --headless 5 --headless-timeout 30`，确认不崩溃且 log 中无 `VUID-VkBufferMemoryBarrier-buffer-parameter` 错误。
