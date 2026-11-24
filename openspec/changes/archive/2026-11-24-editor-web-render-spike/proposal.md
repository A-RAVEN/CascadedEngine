# Proposal: editor-web-render-spike

## Why

自由 dock 编辑器方向已定（`TODO/008-free-dock-editor.md`、ROADMAP 阶段 6）：网页全窗 + 网页侧 dock 框架 + 原生渲染合成进网页。网页侧 dock 框架是成熟问题，**最大不确定项是「原生渲染进网页」**——共享纹理能否被 WebView2 里的 WebGPU canvas 导入显示。本批是**最小 spike**：先把这条渲染进网页的路跑通（或拿出可核的不可行证据），为后续完整 dock 框架 change 去风险。**D3D12 与 Vulkan 双后端各验一遍**（CPU 读回路径后端无关；零拷贝互操作两后端各研究留结论）。**不做 dock 框架、不做多窗、不迁移现有编辑器**。

## What Changes

- **单全窗 WebView2 原型窗**：复用 EditorService/EditorWindow 的 WebView2 宿主设施，以独立进程 + 原型门控（默认关）起一个原生窗 + 一个覆盖全客户区的 WebView2 控制器（区别于现有三分区），加载原型网页（scene 面板含 `<canvas>`）；控制器关闭先于 hostfxr_close，独立数据目录/端口。
- **D3D12 原生离屏渲染到共享纹理（非绑定面）**：经独立非绑定原生助手（`D3D12_HEAP_FLAG_SHARED` + `CreateSharedHandle`，不改 GPUTexture 绑定面）渲染固定尺寸特征图案（背景绿+中心红三角+四角+帧序号），复用 RenderBackend_D3D12。
- **Vulkan 原生离屏渲染 + CPU 读回（非绑定面）**：经独立非绑定 Vulkan 助手（`VK_KHR_external_memory` Win32 外部内存句柄建共享图像，不改 GPUTexture 绑定面/公共分配路径）渲染同一特征图案，`vkCmdCopyImageToBuffer` 读回像素，复用 RenderBackend_Vulkan。CPU 读回路径与 D3D12 共用网页/通道/判据。
- **共享纹理进网页 canvas（CPU 读回实现，零拷贝只研究）**：C++ 读回共享纹理→C++→C#→网页字节通道→canvas.putImageData 显示（本批实现正控，path=cpu-readback）。**零拷贝互操作只研究留结论、实现是后续**——标准 WebGPU 无直接导入 DXGI 共享句柄入口（importExternalTexture 收 HTMLVideoElement/VideoFrame），核实 WebView2 可行路径留可核结论。
- **正控判据（同源可证伪）**：原生读回与网页 canvas 同帧对账（帧序号+整图哈希+背景/中心/四角多点），禁止网页本地画红；getImageData 精确断言且 CapturePreviewAsync 抓窗 read_image 核对真实显示（不前置，AGENTS §2）；断链超时+失败码，红态自检开关可复跑。
- **可行性结论留档**：零拷贝可行与否判定 MUST 与实际 path 一致；不可行给具体阻塞证据（API 缺失/版本/文档，引 GPUWeb #5167）+ 退路（B 透明叠层 / CPU-GPU 桥）；回写 TODO/008（**D3D12 与 Vulkan 各自结论**）。

## Capabilities

### New Capabilities

- `editor-web-render-spike`: 原生渲染进网页的最小验证——单全窗 WebView2 原型窗 + D3D12/Vulkan 双后端共享纹理离屏渲染 + 网页 canvas 显示原生像素的正控判据 + 互操作可行性结论。

### Modified Capabilities

- 无（spike 不改现有 editor-window-panel / editor-asset-browser 规范；原型窗是独立实验入口，不取代现有编辑器）。

## Impact

- **代码**：托管原型门控 + 全窗控制器（复用 EditorWindow 设施，独立进程）；非绑定面 D3D12 与 Vulkan 共享纹理助手；原型网页 spike.html/spike.ts（canvas + CPU 读回胶水 + 同源回传）；C++ 用例 TestWebRenderSpike（双后端）+ 读回正控；vite/build.py 加 spike.html。
- **不动**：现有 editor-window-panel / editor-asset-browser 实现与规范；Vulkan/D3D12 后端公共代码与 GPUTexture 绑定面（助手走非绑定面）；生成器/绑定产物（逐字节不变）；dock 框架。
- **依赖**：复用 D3D12/Vulkan 后端与现有像素读回惯例；WebGPU/DXGI/Vulkan 外部内存互操作能力须 spike 内核实（可能受 WebView2 版本限制）。

## Non-goals

- **dock 框架 / 拖拽 / 页签 / 浮动 / 多窗 / 布局持久化**：后续 `editor-free-dock` change。
- **Vulkan/D3D12 零拷贝实现**：本 spike 双后端各研究零拷贝可行性留结论，实现以 CPU 读回兜底正控为准；零拷贝实现是后续。
- **迁移现有编辑器到全窗 WebView2**：spike 是独立原型，不动现有三分区编辑器。
- **零拷贝实现**：本 spike 只研究零拷贝可行性留结论，实现以 CPU 读回兜底正控为准；零拷贝实现是后续。
- **资产/对象面板**：spike 只 scene 渲染进网页，不搬资产浏览器/检查器。