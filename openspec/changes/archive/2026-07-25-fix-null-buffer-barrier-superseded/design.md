## Context

`PrepareBatchResourceBarriers` 为跨 batch 的资源状态转换创建 Vulkan pipeline barrier。对 buffer 资源，它通过 `m_LocalResourceManager.GetBuffer(BufferHandle const&)` 解析 VkBuffer，但这个函数在 buffer 未被注册时返回 `nullptr`。当前代码没有防御这个情况，直接创建含 null buffer 的 barrier。

## Goals / Non-Goals

**Goals:**
- barrier 系统对未注册 buffer 具备防御性，不产生无效 Vulkan API 调用
- `beginRenderPass` 崩溃消失

**Non-Goals:**
- 不在此 change 中修复 buffer 注册缺失的根因（`CollectResources` 中未调用 `RegisterBufferHandle`）。该根因需独立 change `fix-buffer-handle-registration` 处理
- 不影响 Image barrier 路径。Image 路径存在相同结构漏洞（`GetTexture(image)` 在 lines 1060, 1089, 1112, 1140 同样无 null guard），但因当前崩溃场景的实际触发路径是 buffer barrier，Image 侧需独立 change `fix-null-image-barrier` 处理，避免本 change 范围膨胀

## Decisions

### D1: 在 barrier 创建点加 null guard，而非在 GetBuffer 内部中断

- **选择**：在 `PrepareBatchResourceBarriers` 中每次 `GetBuffer(buffer)` 调用后检查返回值，若为 null 则 `continue`
- **备选**：在 `GetBuffer(BufferHandle)` 中 `CA_ASSERT_BREAK` 强制中断
- **理由**：中断会造成更严重的运行时崩溃；跳过未注册的 buffer barrier 是安全的降级策略（该 buffer 的状态转换在错误一侧会 lossy，但不会 corrupt 命令缓冲区）
- **细化**：null 返回有两条路径——Internal buffer 未注册和 External buffer 指针异常。当前 `continue` + skip 对两条路径统一处理。对于 Internal 未注册场景，skip 语义正确（buffer 没有 VkBuffer 对象，无需同步）。对于 External buffer 指针 null 场景，skip 会掩盖 resource lifetime bug；如果后续发现此类问题，可在 GetBuffer 中对 External null 路径单独加 `CA_LOG_WARN` 区分

### D2: 保持现有 CA_LOG_WARN

- 现有的 "Internal buffer not registered" 警告保留不变，帮助后续排查注册缺失的根因

### D3: 4 处 GetBuffer 的覆盖

- Line 1206（跨帧 QFOT acquire）已有 `buffer.GetType() == External` 类型守卫，该路径对 Internal buffer 不可达。但 null guard 仍是合理的防御性代码，覆盖 External buffer 指针异常的边界情况
- Lines 1230（QFOT release）、1248（QFOT acquire）、1269（常规 barrier）对 Internal buffer 可达，为主要修复目标
