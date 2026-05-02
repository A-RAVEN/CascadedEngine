## Context

`VulkanGraphExecutor::RecordBatchCommands()` 在构建 `RenderPassCacheKey` 时遍历 attachments，需要将 `GPUTextureDescriptor.format`（`ETextureFormat` 枚举）转换为 `vk::Format`。

当前状态：
- `VulkanTexture::ConvertFormat(ETextureFormat)` 已存在且覆盖所有引擎格式
- `GetDescriptor(graph, attachment)` 已调用，`desc.format` 可用
- 但转换这一步跳过了 —— 直接写入硬编码值

## Goals / Non-Goals

**Goals:**
- 用 `VulkanTexture::ConvertFormat(desc.format)` 替换两处硬编码
- 删除两个 `// TODO: Proper format conversion` 注释
- 确保 `GetOrCreateRenderPass` 对不同格式 attachment 的正确缓存分离

**Non-Goals:**
- 不新增转换函数（复用已有的 `VulkanTexture::ConvertFormat`）
- 不修改 GPL linking 流程中的 dummyRenderPass（那是另一个独立问题）
- 不修改 `GetDescriptor` 的返回值结构

## Decisions

**使用 `VulkanTexture::ConvertFormat` 而不新增函数**

已有函数是 `VulkanTexture` 的 `static` 方法，覆盖了所有 `ETextureFormat` 值（包括 `E_D32_SFLOAT`, `E_D16_UNORM`, `E_D24_UNORM_S8_UINT`, `E_R8G8B8A8_UNORM` 等），default 分支回退到 `eR8G8B8A8Unorm`。无需新建或复制。

**不封装新的 helper**

`VulkanTexture::ConvertFormat(desc.format)` 调用足够简洁清晰，不需要在 `VulkanGraphExecutor` 中添加 wrapper。如果未来多处需要，届时再提取。

## Risks / Trade-offs

- [Risk] default 分支返回 `eR8G8B8A8Unorm` 可能掩盖未识别格式 → **Mitigation**: 现有的 `ETextureFormat` 值均已处理，硬编码原本也是这个值，不会引入新问题
