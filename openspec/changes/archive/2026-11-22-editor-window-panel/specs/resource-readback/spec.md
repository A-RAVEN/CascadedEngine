# resource-readback 增量

## MODIFIED Requirements

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
