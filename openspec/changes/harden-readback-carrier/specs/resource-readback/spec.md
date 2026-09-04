## ADDED Requirements

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
