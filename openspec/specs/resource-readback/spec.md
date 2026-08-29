## Purpose

GPU→host 资源回读：调用者提供目标 CPU 内存（载体），GPU 系统把该资源的字节写入其中。跨后端（Vulkan / D3D12）语义一致。

## Requirements

### Requirement: 调用者提供载体发起读回（Buffer / Texture）
系统 SHALL 提供跨后端读回入口 `Readback(ImageHandle, span<uint8_t> dst)` 与 `Readback(BufferHandle, span<uint8_t> dst)`，调用者提供目标 CPU 内存（载体），GPU 系统负责把该资源（图像像素或 buffer 字节）写入 `dst`。载体生命周期与内容有效性由调用者保证。

#### Scenario: 声明一次图像读回
- **WHEN** 调用者以一张已渲染的 `ImageHandle`（离屏 RT / 外部纹理，**非已呈现 backbuffer**）和一块足够大的 `span<uint8_t> dst` 调用 `Readback`
- **THEN** 系统返回一个读回 token，且 `dst` 尚未保证有效（在同步前不可读）

#### Scenario: 声明一次 buffer 读回
- **WHEN** 调用者以一个计算输出 `BufferHandle` 和一块足够大的 `span<uint8_t> dst` 调用 `Readback`
- **THEN** 系统返回一个读回 token，`Wait()` 后 `dst` 含该 buffer 的原始字节

#### Scenario: 载体过小
- **WHEN** 调用者提供的 `dst` 大小小于该资源读回所需字节
- **THEN** 系统报错并拒绝读回（接口/后端校验写入越界）

### Requirement: 异步声明 + 显式同步
读回 SHALL 为"异步声明 + 显式等待"语义：`token.Wait()` 是唯一同步点，`Wait()` 之前 `dst` 内容不保证有效，`Wait()` 返回后 `dst` 已填好（图像像素 / buffer 字节）。该契约 SHALL 独立于引擎底层同步实现（不依赖"每帧 waitIdle"这一实现巧合）。

#### Scenario: 等待后读取
- **WHEN** 调用者在 `Readback` 后调用 `token.Wait()`
- **THEN** 系统阻塞至 GPU 系统完成把该资源写入 `dst`，之后 `dst` 可安全读取

#### Scenario: 未等待即读
- **WHEN** 调用者在 `Wait()` 之前就读取 `dst`
- **THEN** `dst` 内容未定义（接口不承诺，调用者违约）

### Requirement: 不追求零拷贝
GPU 系统 SHALL 经 host-visible staging 中转 + 一次 memcpy 将数据写入调用者 `dst`，不为"零拷贝"向调用者暴露 ReadbackBuffer/map 抽象。（host-visible buffer 可直 map 免拷贝属实现优化，不引入新抽象。）

#### Scenario: 标准回读路径
- **WHEN** 调用者走默认 `Readback(resource, dst)` 路径
- **THEN** GPU 系统内部完成主机可见中转与拷贝，`dst` 收到结果，调用者无需接触 staging/map 细节

### Requirement: 后端各自实现搬运与同步
Vulkan 与 D3D12 后端 SHALL 各自实现"图像/buffer → 载体"的搬运与同步，接口层语义一致。

#### Scenario: Vulkan 实现
- **WHEN** 使用 Vulkan 后端回读图像
- **THEN** 系统经 `vkCmdCopyImageToBuffer` 搬运，前置布局 barrier 到 `TRANSFER_SRC_OPTIMAL`（源 scope = COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE），并以 fence 同步

#### Scenario: Vulkan 实现（buffer）
- **WHEN** 使用 Vulkan 后端回读 device-local buffer
- **THEN** 系统经 `vkCmdCopyBuffer` 到 host staging 并 fence 同步；host-visible buffer 直 map 写 `dst`

#### Scenario: D3D12 实现
- **WHEN** 使用 D3D12 后端
- **THEN** 系统经 `CopyTextureRegion`/`CopyBufferRegion` 写入 READBACK heap（dst rowPitch 256 / placement 512 对齐），并以 fence 同步

### Requirement: 回读源 = 引擎自建/可控制的图像（非 swapchain backbuffer）
系统 SHALL 不为视觉断言回读已呈现的 swapchain backbuffer（post-present 不可读/帧漂移；且 swapchain image 的 `eTransferSrc` usage 受 surface `supportedUsageFlags` 限制、**非平台保证**）。视觉断言以**引擎自建、带 `eTransferSrc` usage 的离屏/内部 RT 或外部纹理**为源——其 usage 由引擎完全控制，`eTransferSrc` 一定可加，跨平台可保证。

#### Scenario: 视觉断言走带 eTransferSrc 的离屏 RT
- **WHEN** 测试渲染到自建离屏 RT（创建时含 `eTransferSrc|eTransferDst|eColorAttachment`，非 backbuffer）后 `Readback` 该 RT 并 `Wait()`
- **THEN** 读到该 RT 的像素，可导出 PNG 供视觉断言（规避 post-present；usage 可控可保证）

### Requirement: 可导出为 PNG 供视觉断言
测试流程 SHALL 能将读回结果经 `stbi_write_png` 导出为 `test_output/*.png`，供 CPU 视觉读取断言。

#### Scenario: 抓帧导出
- **WHEN** 某测试帧经 `Readback` 读回并 `Wait()` 完成
- **THEN** 系统可调用 `stbi_write_png` 将 `dst`（紧凑 RGBA）写成 PNG，且可通过外部图像读取验证画面内容（三角形/颜色/非空帧）
