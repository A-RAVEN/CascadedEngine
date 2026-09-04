## Why

GPUBackendTester 的视觉断言（`--capture` / `TestReadback`）都依赖 `Readback` 把 GPU 图读回 CPU 像素。当前这条跨后端链路有**三类会静默给错数据 / 越界写 / 截断**的缺陷，且没有任何一道防线——一旦读回某种纹理，坏数据会直接进 PNG、进字节对比断言，且无报错：

- **bpp 双实现分叉**：后端私有的 `GetReadbackBpp` / `D3D12ReadbackBpp` 各自复制了一份 `GetFormatBlockSize`，但**都缺 `E_D32_SFLOAT_S8_UINT`**（后端回 `4`，主函数回 `8`），且三处 `default` 一律静默回 `4`。读回深度-模板纹理即静默错位。
- **载体 subresource 硬编码**：`Readback(image)` 只拷 `mip0/layer0`（Vulkan `RenderBackend_Vulkan.cpp:1235-1237`、D3D12 `RenderBackend_D3D12.cpp:705`），但入口不校验 `mipLevels/layers/samples/type`，多 mip / 多 layer / 非 2D 纹理被**静默截断**到第一层；`samples > 1` 的 MSAA 也不 resolve。
- **dst 生命周期仅靠 prose**：`IReadbackToken` 持有调用者的裸 `std::span`（Vulkan `RenderBackend_Vulkan.cpp:1115`、D3D12 `RenderBackend_D3D12.cpp:602`），异步 `Wait()` 里才 `memcpy(m_Dst.data(), ...)`；调用方若在 `Readback()` 与 `Wait()` 之间回收/重分配 `dst` 底层内存 → UAF，契约只写在接口注释 `CRenderBackend.h:44`，无约束也无防御。**且公开面让调用者自行按 `W*H*bpp` 算载体大小**——这正是 `docs/TODO.md` 压缩条目的「原则 3」点名要避免的（公开面应为单一字节数查询，不暴露 per-pixel bpp）。

主 spec `openspec/specs/resource-readback/spec.md` 已承诺「异步声明 + 显式同步」与「一次 memcpy 不追求零拷贝」，但**公开的载体字节数来源**与**载体受支持的范围**（哪些格式 / 哪些 subresource / MSAA）都未界定，也没界定**调用者义务**——所以这些是 spec 层面的空洞，不是实现小 bug。

## What Changes

- **读回载体契约硬化（新增 Requirement，全部落到 `resource-readback` 主 spec）**：
  - **载体字节数单一事实来源（公开面）**：新增公共查询 `GetImageByteSize(width, height, format)`，是「读回该资源需多少字节」的**唯一公开来源**；两端 `Readback` 弃用私有 `GetReadbackBpp` / `D3D12ReadbackBpp`，改用 `GetImageByteSize`（非压缩单平面 = `width*height*GetFormatBlockSize`；压缩为记录目标 = 块线性 + D3D12 256 对齐；仅无紧凑布局的未知格式返回 0）。`GetFormatBlockSize` 退为**内部承载中间量**（仅供 `width*bpp` 紧凑行距等内部计算），不再作为调用者的公开字节数基准。消除 `E_D32_SFLOAT_S8_UINT`（8 vs 4）分叉。**字节数 ≠ 可读性**：`GetImageByteSize` 非零只是尺寸可算，不代表 `Readback` 接受。
  - **未支持格式显式拒绝**：读回支持范围收窄为「**单平面（非 multi-planar）、非压缩、可紧凑复制、非深度-模板 multi-aspect** 的格式」；单深度（`E_D16_UNORM`/`E_D32_SFLOAT`，单 aspect）**接受**；对压缩（块线性载体）/ 深度+模板 multi-aspect（`E_D24_UNORM_S8_UINT`/`E_D32_SFLOAT_S8_UINT`）/ 无紧凑布局格式**报错并返回 null token**（替代静默回一个错 bpp）。**BREAKING 语义**：原先 `default` 会静默继续，现在未列入支持集的格式会显式失败。
  - **subresource 收窄**：`Readback(image)` 支持范围 = **单 mip、单 layer、非 MSAA、`ETextureType::e2D` 的图像**；对 `mipLevels>1`/`layers>1`/`samples != e1`/非 2D 的纹理**报错并返回 null token**（替代静默只拷 `mip0/layer0`）。原行为会把这类资源静默截断——**BREAKING 语义**：现已显式失败。
  - **dst 生命周期契约强化**：`IReadbackToken` / `Readback` 契约明确「调用者必须保持 `dst` 底层内存存活直至 `Wait()`（含 `Wait` 前不 resize/释放底层容器）」；后端在**把数据写入 `dst` 前**（Vulkan 异步=`Wait()` 内 `memcpy` 前；D3D12 同步=`Readback()` 内）**防御性校验 `dst.size()`**（不足则报错而非越界写）。

## Capabilities

### New Capabilities

- （无）——本 change 不引入新的能力规格；所有改动是对既有 `resource-readback` 能力的需求修正。（新增的 `GetImageByteSize` 是支撑该能力的实现 helper，不单独立能力。）

### Modified Capabilities

- `resource-readback`: 新增「载体契约」类需求——(1) 载体字节数单一事实来源 = 公开 `GetImageByteSize(width,height,format)`（内部压缩/非压缩分支）；(2) 仅接受单平面/非压缩/可紧凑复制格式，其余显式拒绝；(3) 仅接受单 mip/单 layer/非 MSAA 的 2D，其余显式拒绝；(4) 调用者须保持 `dst` 存活至 `Wait()`，后端在写入 `dst` 前校验 `dst.size()`。

## Impact

- `Interface/RenderInterface/header/CRenderBackend.h` — `IReadbackToken` / `Readback` 契约注释强化（dst 生命周期义务；载体字节数以 `GetImageByteSize` 为准）。
- `Interface/RenderInterface/header/Common.h` — 沿用 `GetFormatBlockSize`（内部）；**新增公共 `GetImageByteSize(width,height,format)`**（非压缩 = `width*height*GetFormatBlockSize`；压缩为记录目标 = 块线性 + 256 对齐；仅无紧凑布局的未知格式返回 0；需先判白名单防 `default=4` 静默），作为读回字节数唯一来源。
- `VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp` — 删 `GetReadbackBpp`，改用 `GetImageByteSize`；`Readback(image)` 增格式/多-mip/多-layer/非2D/MSAA 校验并显式拒绝；`VulkanReadbackToken::Wait()` 在 `memcpy` 前增 `dst.size()` 防御校验。
- `D3D12RenderBackend/private/RenderBackend_D3D12.cpp` — 删 `D3D12ReadbackBpp`，改用 `GetImageByteSize`；`Readback(image)` 增校验并显式拒绝；`Readback()` 写入 `dst` 前增 `dst.size()` 防御校验。（D3D12 仍为同步，异步化不属本 change。）
- `Test/GPUBackendTester/Main.cpp` — 视需要验证：读回不支持格式/多-mip/多层/MSAA/非2D 资源时收到 null token 的用例，及 `E_D32_SFLOAT_S8_UINT` 不再静默分叉。
- 无外部依赖变更。

## Non-goals

- **D3D12 回读改异步**：独立 change（`fix-readback-async`，另案，涉及 D3D12 内部 + 同步模型重构）。本条只把 D3D12 在「载体字节数单源 + 格式/层级拒绝 + 契约」维度拉齐，不动其同步/异步实现。
- **压缩/复杂格式块线性读回载体**：独立后续——压缩读回是**块线性**（`ceil(W/blockW)*ceil(H/blockH)*blockBytes` + D3D12 256 对齐），**非像素线性**，不能按 `W*H*bpp` 寻址；解释需 format+块尺寸+尺寸+块解码器（bcdec），`stbi_write_png` 写不了（视觉断言对压缩不适用）；bpp 对压缩无布局意义。本 change 用 `GetImageByteSize` 内部对压缩分支 + `Readback` 显式拒绝压缩，绝不用 `W*H*bpp` 套压缩。
- **subresource 选择器 / MSAA resolve 扩展**：本 change 选择「收窄 + 显式拒绝」，不新增 `Readback` 的 mip/layer/aspect 选择参数，也不实现 MSAA resolve（真用到多 mip/MSAA 时再扩展）。
- **格式 → canonical-RGBA 转换**：读回保留"原格式紧凑字节"，不做 RGB(A) 归一化（既有约定，非目标）。
- **swapchain backbuffer 读回**：已由主 spec 排除（post-present 不可读、usage 非平台保证）。
- **零拷贝抽象 / ReadbackBuffer / map 暴露**：主 spec 已明确不追求。
