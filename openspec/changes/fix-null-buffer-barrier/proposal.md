## Why

`PrepareBatchResourceBarriers` 中调用 `m_LocalResourceManager.GetBuffer(BufferHandle)` 获取 VkBuffer 屏障资源时，未检查返回值是否为 `VK_NULL_HANDLE`。当 buffer 未被注册到 `m_BufferHandleToResource` 时，`GetBuffer` 返回 `nullptr`，但该值直接赋给 `vk::BufferMemoryBarrier::buffer`，导致后续 `vkCmdPipelineBarrier` 传入无效 buffer handle。Validation layer 报 `VUID-VkBufferMemoryBarrier-buffer-parameter` 错误，且驱动内部状态被污染后，后续 `vkCmdBeginRenderPass` 崩溃。

## What Changes

- 在 `PrepareBatchResourceBarriers` 中所有 4 处调用 `GetBuffer(buffer)` 的位置（lines 1206, 1230, 1248, 1269），添加 null 检查：若返回 null，跳过该 buffer 的 barrier 创建

## Capabilities

### New Capabilities
- `buffer-barrier-null-safety`: 资源屏障系统在 buffer 未注册时不产生无效 barrier，不污染命令缓冲区状态

## Impact

- `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp` — `PrepareBatchResourceBarriers` 函数中 4 处 `GetBuffer(buffer)` 调用后添加空值跳过逻辑

## Known Limitations

此 change 是 **防御性修复**，只解决屏障系统的 null buffer handle 传入 `vkCmdPipelineBarrier` 导致的直接崩溃。不修复以下根因（需独立 change）：

1. **BufferHandle 注册缺失**：`CollectResources`（lines 694-710）中 vertex/index buffer 调用了 `RegisterTemporaryBuffer()` 创建 GPU 资源，但从未调用 `RegisterBufferHandle()` 建立 `BufferHandle → resourceId` 映射。这导致 Internal 类型的 vertex/index buffer 在 `GetBuffer(BufferHandle)` 时永远返回 null。

2. **ImageHandle 同样存在注册缺失**：attachment images (line 664)、transfer images (line 731)、finalize pass images (line 745) 同样只调 `RegisterTemporaryTexture()` 不调 `RegisterTextureHandle()`。

3. **Image barrier 路径的同构漏洞**：`PrepareBatchResourceBarriers` 中 4 处 `GetTexture(image)` 调用（lines 1060, 1089, 1112, 1140）存在相同的 null 漏洞，本 change 不覆盖（见 Non-Goals）。

4. **Draw time 静默失败**：`RecordRenderPass` 中 vertex/index buffer 绑定处（lines 1953, 1983）已有 null guard，但会静默跳过绑定——draw call 在无顶点数据的情况下执行，导致渲染错误。

## Next Steps

- `fix-buffer-handle-registration`: 在 `CollectResources` 中为所有 buffer/image 类型添加 `RegisterBufferHandle`/`RegisterTextureHandle` 调用
- `fix-null-image-barrier`: Image barrier 路径的 null guard（与本次修复同构）
