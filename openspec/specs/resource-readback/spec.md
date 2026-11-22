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

### Requirement: 回读源默认引擎自建图像；窗口 backbuffer 仅限呈现前受控读回
系统 SHALL 默认以**引擎自建、带 `eTransferSrc` usage 的离屏/内部 RT 或外部纹理**为视觉断言回读源——其 usage 由引擎完全控制，`eTransferSrc` 一定可加，跨平台可保证。

**受控例外（2026-11-22 新增，editor-window-panel 展示证明需求）**：为证明"渲染后端输出已呈现进窗口交换链表面"（GPU 级展示能力，环境无关——虚拟显示/远程会话下屏幕捕获抓不到硬件 flip 内容），系统 SHALL 支持读回**窗口已渲染、尚未呈现的 swapchain backbuffer**，**复用现有 `Readback(ImageHandle)` 入口、不新增接口方法**（CRenderBackend 纯虚集合不变，绑定/审计/基线零影响），且 MUST 同时满足：
- **登记式呈现前捕获**：`Readback(Backbuffer)` 登记"该窗口下一帧 Present 前捕获"并返回 token；真正拷贝发生在该窗口 Present 之前（本帧已渲染、尚未 present 的交换链 image）。调用方顺序：`Readback → 跑一帧（ExecuteGraph 内含 Present）→ token.Wait()`。MUST NOT 读 post-present 回收的旧帧（规避帧漂移/不可读）。交换链重建（resize）时 MUST 清空 pending 捕获请求（旧 image 索引不悬空）。
- **surface 能力检查**：Vulkan MUST 先查 surface `supportedUsageFlags` 含 `TRANSFER_SRC`，不含则**明确拒绝并返回 null token**（usage 非平台保证，不得静默假设）；D3D12 swapchain buffer 拷贝前 MUST 做 COPY_SOURCE 状态转换、读后转回 PRESENT。
- **色序按格式**：dst 为格式原生字节序（Vulkan 常 BGRA、D3D12 常 RGBA），读取方按 `GetBackbufferDescriptor().format` 判色序断言。
- 载体字节数仍按 `GetImageByteSize` 单一事实来源；subresource 收窄（单 mip/单层/非 MSAA/2D）与格式拒绝集**原样保留**。

原"不为视觉断言回读已呈现 backbuffer"的禁令针对 **post-present 读**仍然有效；本例外是**呈现前受控读**，两者不冲突。一般视觉断言仍优先离屏 RT。

#### Scenario: 视觉断言走带 eTransferSrc 的离屏 RT
- **WHEN** 测试渲染到自建离屏 RT（创建时含 `eTransferSrc|eTransferDst|eColorAttachment`，非 backbuffer）后 `Readback` 该 RT 并 `Wait()`
- **THEN** 读到该 RT 的像素，可导出 PNG 供视觉断言（usage 可控可保证）

#### Scenario: 呈现前读窗口 backbuffer 证明展示
- **WHEN** 以窗口 `ImageHandle`（Backbuffer 类型）调 `Readback` 登记捕获（surface 支持 TRANSFER_SRC），再跑一帧渲染三角形到该窗口（ExecuteGraph 内含 Present，拷贝发生在 Present 前），然后 `token.Wait()`
- **THEN** 返回有效 token，`Wait()` 后 dst 含该窗口已渲染像素（按格式色序中心为三角形红）——证明 Present 把后端输出渲染进了窗口交换链表面

#### Scenario: surface 不支持 TRANSFER_SRC 明确拒绝
- **WHEN** 窗口 surface 的 supportedUsageFlags 不含 TRANSFER_SRC 时读回 backbuffer
- **THEN** MUST 明确报错并返回 null token，MUST NOT 静默假设 usage 或越界拷贝

#### Scenario: post-present 读仍禁止
- **WHEN** 试图读已呈现回收的旧 backbuffer 帧
- **THEN** MUST 拒绝（帧漂移/不可读），本例外不覆盖 post-present 读

### Requirement: 可导出为 PNG 供视觉断言
测试流程 SHALL 能将读回结果经 `stbi_write_png` 导出为 `test_output/*.png`，供 CPU 视觉读取断言。

#### Scenario: 抓帧导出
- **WHEN** 某测试帧经 `Readback` 读回并 `Wait()` 完成
- **THEN** 系统可调用 `stbi_write_png` 将 `dst`（紧凑 RGBA）写成 PNG，且可通过外部图像读取验证画面内容（三角形/颜色/非空帧）

### Requirement: 载体字节数单一事实来源（公开面 = GetImageByteSize）+ 未支持格式显式拒绝

系统 SHALL 提供公共字节数查询 `GetImageByteSize(width, height, format)`，返回读回该图像资源所需的**字节数**（供调用者以此分配 `dst`），调用者 SHALL NOT 自行按每像素字节（`width*height*GetFormatBlockSize`）推算载体大小——`GetFormatBlockSize` 是**内部承载中间量**（仅用于紧凑行距/内部换算），不是公开字节数基准。后端 SHALL 用 `GetImageByteSize`（而非私有每像素 bpp 表）计算载体字节数。

系统 SHALL 仅接受 **单平面（非 multi-planar）、非压缩、可紧凑复制、非深度-模板 multi-aspect** 的格式读回；对压缩（块线性载体）/ 深度-模板 multi-aspect（`E_D24_UNORM_S8_UINT` / `E_D32_SFLOAT_S8_UINT`）/ 无紧凑布局的格式，`Readback` SHALL **报错并返回 null token**，不得静默回退到某一默认 bpp。

**载体字节数与可读性是两个正交信号**：`GetImageByteSize` 返回该图紧凑数据的字节数（对任意有确定紧凑布局的格式——含单深度 `E_D16_UNORM`/`E_D32_SFLOAT` 与深度-模板组合——均可算出；非压缩 = `width*height*GetFormatBlockSize`，压缩（未来）= 块线性 `ceil(W/blockW)*ceil(H/blockH)*blockBytes`；仅对无紧凑布局的未知格式返回 0）。它**不代表** `Readback` 会接受；可接受性由本要求的支持集单独界定，**不依赖** `GetImageByteSize` 的返回值。**单深度**格式（`E_D16_UNORM`、`E_D32_SFLOAT`，单 aspect）属可读支持集；仅**深度+模板组合 multi-aspect**（`E_D24_UNORM_S8_UINT`、`E_D32_SFLOAT_S8_UINT`）被拒。

> **诚实注**：`IsCompressedFormat`（`Common.h`）当前恒返回 `false`（压缩格式尚未入 `ETextureFormat` 枚举），故「非压缩」这一前置条件**不可依赖** `IsCompressedFormat`；实际的不支持判定以 `GetImageByteSize` 内部分支/显式白名单为准。规范不得把 `IsCompressedFormat == false` 写成可靠保证（否则未来压缩格式入枚举后会静默漏档）。

#### Scenario: 读回受支持格式以 GetImageByteSize 计算字节数
- **WHEN** 调用者对一张 `E_R8G8B8A8_UNORM` 的离屏 RT 调用 `Readback(image, dst)`，`dst.size() == GetImageByteSize(width, height, E_R8G8B8A8_UNORM)`（= width*height*4），且该图 `mip=1/layer=1/sample=e1/e2D`
- **THEN** 系统按 `GetImageByteSize` 返回值校验载体大小，返回有效 token，`Wait()` 后 `dst` 含像素

#### Scenario: 读回 `E_D32_SFLOAT_S8_UINT` 不再有 bpp 分叉
- **WHEN** 调用者查询 `GetImageByteSize(w, h, E_D32_SFLOAT_S8_UINT)`（value = `GetFormatBlockSize=8`）
- **THEN** 系统返回按 `8`（非任何私有表 `4`）换算的字节数，不再静默错位

#### Scenario: 读回深度-模板 multi-aspect 格式被拒绝
- **WHEN** 调用者对一张 `E_D32_SFLOAT_S8_UINT` 深度-模板纹理调用 `Readback(image, dst)`（`GetImageByteSize(w, h, E_D32_SFLOAT_S8_UINT)` 可算出 = `w*h*8`）
- **THEN** 系统打印「深度-模板 multi-aspect 读回未支持（需 aspect/plane 选择与行对齐处理，深度值解释属格式语义，本 change 非目标）」并返回 null token——字节数虽可算，但该格式不在读回支持集内

#### Scenario: 读回压缩/块线性格式被拒绝
- **WHEN** 调用者对一张压缩纹理（块线性载体）调用 `Readback(image, dst)`
- **THEN** 系统打印「压缩格式读回为块线性载体（`ceil(W/blockW)*ceil(H/blockH)*blockBytes`，非 `W*H*bpp`；需块解码器，`stbi_write_png` 不可导出）」并返回 null token，绝不用 `W*H*bpp` 套压缩

#### Scenario: 读回无紧凑布局的未知格式被拒绝
- **WHEN** 调用者对一张 `GetImageByteSize` 返回 0（无紧凑布局的未知格式）的图像调用 `Readback(image, dst)`
- **THEN** 系统打印错误并返回 null token（不静默按某默认 `bpp` 继续）

#### Scenario: 读回单深度格式（单 aspect）被接受
- **WHEN** 调用者对一张单深度、单 aspect 的 `E_D16_UNORM` 或 `E_D32_SFLOAT`（单 mip/单层/非 MSAA/2D）纹理调用 `Readback(image, dst)`
- **THEN** 系统归入可读支持集（非 multi-aspect），按 `GetImageByteSize`（= `w*h*GetFormatBlockSize`）校验载体并返回有效 token

### Requirement: 载体 subresource 收窄——仅单 mip / 单 layer / 非 MSAA 的 2D

图像读回 SHALL 仅支持 **单 mip、单 layer、非 MSAA、`ETextureType::e2D`** 的纹理。系统 SHALL 在 `desc.mipLevels > 1` 或 `desc.layers > 1` 或 `desc.samples != EMultiSampleCount::e1` 或 `desc.textureType != ETextureType::e2D` 时**报错并拒绝**（返回 null token），不得静默只拷贝 `mip0/layer0` 导致截断。

#### Scenario: 单 mip 单层非 MSAA 2D 读回成功
- **WHEN** 调用者对一张 `mipLevels==1 && layers==1 && samples==EMultiSampleCount::e1 && textureType==ETextureType::e2D` 的纹理调用 `Readback(image, dst)`
- **THEN** 系统返回有效 token，`Wait()` 后 `dst` 含该层像素

#### Scenario: 多 mip 纹理读回被拒绝
- **WHEN** 调用者对一张 `mipLevels > 1` 的纹理调用 `Readback(image, dst)`
- **THEN** 系统打印「仅支持单 mip」类错误并返回 null token（不静默只拷 mip0）

#### Scenario: 多 layer 纹理读回被拒绝
- **WHEN** 调用者对一张 `layers > 1` 的纹理调用 `Readback(image, dst)`
- **THEN** 系统打印「仅支持单 layer」类错误并返回 null token（不静默只拷 layer0）

#### Scenario: MSAA 纹理读回被拒绝
- **WHEN** 调用者对一张 `samples != EMultiSampleCount::e1` 的 MSAA 纹理调用 `Readback(image, dst)`
- **THEN** 系统打印「MSAA 读回未支持/需 resolve」类错误并返回 null token

#### Scenario: 非 2D 纹理读回被拒绝
- **WHEN** 调用者对一张 `textureType != ETextureType::e2D`（如 3D / cube / array）的纹理调用 `Readback(image, dst)`
- **THEN** 系统打印「仅支持 2D」类错误并返回 null token

### Requirement: 调用者保持 dst 存活直至 Wait + 后端在写入 dst 前校验

调用者 SHALL 保持 `dst` 指代的底层内存存活、且其大小不缩水，直到 `token.Wait()` 返回（含 `Wait` 前不得 resize/释放承载 `dst` 的容器）。后端 SHALL 在**把数据写入 `dst` 前**（Vulkan 异步路径= `Wait()` 内 `memcpy` 前；D3D12 同步路径= `Readback()` 内）记录读回所需字节数并校验 `dst.size()` 不小于该值，不足则**报错而非越界写**。

#### Scenario: 调用者遵守约定读取成功
- **WHEN** 调用者在一张离屏 RT `Readback(image, dst)` 返回 token 后、`Wait()` 返回前保持 `dst` 底层内存存活
- **THEN** `Wait()` 把像素填入 `dst`，无越界写

#### Scenario: 后端在写入点防御性校验 dst 大小
- **WHEN** 后端在写入 `dst` 前（Vulkan=`Wait()` 内 memcpy 前；D3D12=`Readback()` 内）发现 `dst.size()` 小于读回所需字节数
- **THEN** 系统打印错误并跳过写入（不越界写 `dst`）

