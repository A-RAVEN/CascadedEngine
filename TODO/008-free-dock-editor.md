# 008 · 自由 dock 编辑器（原生网页方案）

- **状态**：探索中（`editor-web-render-spike` 已双后端给出结论：D3D12 与 Vulkan 的共享纹理/图像都**可建可共享**，但**零拷贝进网页两后端都不可行**、实现改走 CPU 读回；下一步按路线 B / CPU-GPU 桥取舍后再转 proposal）
- **日期**：2026-11-24（spike 双后端结论回写：2026-11-24）
- **关联**：[005 网页界面与 C# 编辑器服务](005-web-editor.md)、`openspec/specs/editor-window-panel/spec.md`、`openspec/specs/editor-asset-browser/spec.md`、归档 `2026-11-24-editor-asset-browser`、已归档 `openspec/changes/archive/2026-11-24-editor-web-render-spike/`（本文件的结论来源）
- **结论来源**：`openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/4.2-可行性结论.md`、`5.5-Vulkan可行性结论.md`、`1.1-互操作研究.md`、`5.1-Vulkan互操作研究.md`、`3.1-同源正控与红态.txt`、`5.3-Vulkan绿跑测试日志.txt`

## 背景与目标

`editor-asset-browser`（2026-11-24 归档）落地了「原生壳嵌网页子窗」的三分区编辑器：单原生 GLFW 窗 + 两个 WebView2 子 HWND（对象检查器右上、资产浏览器底部通栏）+ 原生渲染占左上视口。用户问扩展性，结论：**这套适合固定分区，不适合自由 dock**（布局是写死的分区数学、每面板是独立子 HWND + 独立控制器，跨窗搬运/页签合并很难）。

目标：做一个**自由 dock 多窗编辑器**——面板可拖拽重排、合并页签、撕出浮动窗、停靠任意边、多个独立编辑器窗口，布局可持久化。方向定为**原生网页方案**（网页全窗 + 网页侧 dock 框架 + 原生渲染合成进网页）。

## 现状核实（当前方案为什么不够自由）

- 建窗：`EditorWindow.Create`（[EditorWindow.cs:105](../Scripting/Managed/Editor/EditorWindow.cs#L105)）`IWindowSystem.NewWindow` 建唯一原生窗 → `GetNativeWindowHandle` 取 HWND（解引用一次，:153）。
- 嵌网页：`CreateCoreWebView2ControllerAsync(Hwnd)`（[EditorWindow.cs:232](../Scripting/Managed/Editor/EditorWindow.cs#L232)）把 WebView2 控制器建成**原生窗的子窗口**，`Controller.Bounds` 摆子区域（检查器 :259；dock 在 PumpDock）。两个控制器共享一个 `CoreWebView2Environment`。
- 布局：`ApplyLayout`（[EditorWindow.cs:240](../Scripting/Managed/Editor/EditorWindow.cs#L240)）**写死三分区数学**（视口宽=客户区宽−面板宽、高=客户区高−dock高；检查器右上、dock 底部通栏）。原生渲染经 SetViewPort/SetScissor 占视口。
- 规范钉死单壳窗口（`editor-window-panel` 壳要求：GetWindowCount 增量 1，MUST NOT 第二壳窗口）。

不够自由的根因：① 布局硬编码不是 dock 树；② 每面板独立子 HWND + 独立 WebView2 上下文，跨窗重父化/页签合并难；③ 单壳窗口约束。

## 关键决策轴与结论

### 决策 1：布局交给网页（目标架构三层）

1. **宿主层**：每个顶层窗 = 1 原生窗 + **1 个全窗 WebView2**（不再写死分区、不再多子 HWND）。原生层退化成「宿主窗口 + 把场景渲染进网页 + 既有编辑器服务」。
2. **网页 dock 框架**：dock 树（分裂节点 / 页签组 / 浮动窗）管所有面板。面板（scene view / 对象检查器 / 资产浏览器 / 控制台…）是可拖拽单元：重排 / 合并页签 / 撕出浮动 / 停靠任意边；布局序列化 JSON 持久化（每用户工作区）。候选库：Golden Layout / FlexLayout / dock-spawn / 自研。
3. **原生渲染进网页**：引擎把场景渲染进网页的 scene 面板（见决策 2）。

**协议/服务层不变**：list-assets / assets-changed / 对象命令 / 线程边界原样保留，dock 框架纯粹是网页 UI 关注点，各面板照常消费协议。多窗 = 多个「原生窗 + 全窗 WebView2」，跨窗拖拽由网页 dock 框架处理。

### 决策 2：原生渲染进网页的两条路（核心难点）

- **路线 A 共享纹理（原推荐方向，spike 后降级）**：引擎离屏渲染到共享纹理（D3D12 DXGI shared handle / Vulkan external memory），web 侧 canvas 经 WebGPU/WebGL 导入显示。优点：零拷贝、实时、面板随便拖（canvas 在网页里移动，原生只管渲染到纹理，与位置无关）。代价：图形互操作工程量大。**spike 结论（2026-11-24）**：D3D12 共享纹理可建可共享，但网页侧没有导入共享句柄的 API（`importExternalTexture` 只收 `HTMLVideoElement`/`VideoFrame`），**这条路在本批 WebView2 上走不通**；Vulkan↔WebGPU **仍未知**（本批排除 Vulkan）。详见「已核实事实」。
- **路线 B 透明叠层**：WebView2 透明背景叠在原生渲染上，scene 面板那块透明露出原生；网页报面板矩形 → 原生设视口。优点：不用纹理互操作，复用现有原生渲染。代价：矩形同步、z-order/透明合成有坑；面板拖动时原生视口要跟着改。

取舍：当前方案原生渲染是一等公民（直接视口）、简单但布局死；自由方案布局全活（网页 dock 框架是已解决问题），代价是把原生渲染塞进网页（A 干净但互操作难，B 省事但耦合矩形）。

### 决策 3：先 spike 验证渲染进网页，再立 dock 框架

自由 dock 的网页侧（dock 框架）是成熟问题，**最大不确定项是原生渲染进网页**（尤其 Vulkan↔WebGPU）。故先做最小 spike 跑通渲染进网页（D3D12 优先，或退路线 B），验证后再立完整 dock 框架 change。不在 spike 里做完整 dock 框架。

## 已核实事实（带来源）

- WebView2 子 HWND 嵌入机制：[EditorWindow.cs:232](../Scripting/Managed/Editor/EditorWindow.cs#L232) `CreateCoreWebView2ControllerAsync(Hwnd)` + Bounds。
- 当前布局硬编码三分区：[EditorWindow.cs:240](../Scripting/Managed/Editor/EditorWindow.cs#L240) ApplyLayout。
- 抓窗可用 `CoreWebView2.CapturePreviewAsync`（不依赖前景，editor-asset-browser 4.2 实测，归档 evidence/4.2-视觉核对.md）——证明 WebView2 内容可被程序化捕获，与渲染进网页的纹理路线互补。
- 单壳窗口约束：`openspec/specs/editor-window-panel/spec.md` 壳要求。
- **D3D12 共享纹理 spike 结论（`editor-web-render-spike`，2026-11-24 实测）**：
  - **共享可行**：`D3D12_HEAP_FLAG_SHARED` + `ID3D12Device::CreateSharedHandle` 建 256×256 共享纹理；
    第二个消费者 `OpenSharedHandle` 读回的字节与本设备读回**逐字节一致**，背景绿/中心红/四角/帧序号位多点匹配
    （`openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/2.1-原生侧正控.txt`）。D3D11 的 `D3D11_RESOURCE_MISC_SHARED` 不适用于 D3D12（desc 无 MiscFlags）。
  - **零拷贝进网页不可行**：标准 WebGPU 没有"导入 DXGI 共享句柄"的入口（规范里 `DXGI` / `sharedHandle` /
    `importSharedTexture` 等检索词 0 命中）；`importExternalTexture` 只收 `HTMLVideoElement` 或 `VideoFrame`；
    WebCodecs 的 `VideoFrame` 构造只收网页图像源或 CPU 字节，不收平台句柄；GPUWeb #5167 已 closed/not_planned
    （维护者：这在浏览器引擎侧，不属网页平台规范）。本机 WebView2 **157.0.4341.60** 同样没有这类入口。
  - **实际可行路径 = CPU 读回**：共享纹理 → C++ Readback → C++→C#（EditorService WebSocket）→ 网页
    `putImageData`；实测三帧同源对账通过，运行读数 `path=cpu-readback`（`openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/4.2-可行性结论.md`、`.../evidence/3.1-同源正控与红态.txt`）。
    每帧 262144 字节的 GPU→CPU 拷贝，只适合低频预览/正控，不是实时方案。
- **Vulkan 共享图像 spike 结论（`editor-web-render-spike` 5.x，2026-11-24 实测）**：
  - **共享可行**：`VK_KHR_external_memory_win32` 的 Win32 句柄（`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`）+
    `vkGetMemoryWin32HandleKHR` 导出 256×256 共享图像；第二消费者 `VkImportMemoryWin32HandleInfoKHR` 导入成独立的
    VkImage/VkDeviceMemory 后读回，字节与本设备读回**逐字节一致**，多点/帧序号位匹配；三帧整图哈希
    （7df2b6c5 / 2dfb06c5 / 90355dc5）与 D3D12 **逐帧相同**（同图案、同 CPU 读回通道）。
    设备：RTX 4070 / 驱动 572.61，`VK_KHR_external_memory` + `VK_KHR_external_memory_win32` 在位
    （`.../evidence/5.1-Vulkan互操作研究.md`、`5.5-Vulkan可行性结论.md`、`5.3-Vulkan绿跑测试日志.txt`）。
  - **零拷贝进网页同样不可行**：网页侧无导入 `VkExternalMemory` / `OPAQUE_WIN32` 的 API（WebGPU 规范这些词 0 命中）；
    另外**引擎自己的 Vulkan 后端**创建设备时只启用 swapchain/GPL、未启用 `VK_KHR_external_memory_win32`
    （本批为"不改后端公共代码"而没有启用），所以即使浏览器侧将来开入口，引擎侧也要先补这一条。
  - **实际路径同样是 CPU 读回**：`vkCmdCopyImageToBuffer` → map → C++→C#（EditorService WebSocket）→ 网页
    `putImageData`；运行读数 `path=cpu-readback`（`.../evidence/5.3-Vulkan绿跑读数.txt`）。

## 被反驳 / 排除的结论

- **「在当前多子 HWND 方案上加 dock 树就能自由 dock」**：排除。每面板独立子 HWND + 独立控制器，跨窗重父化/页签合并难；自由 dock 应走网页全窗 + 网页侧框架，而非原生侧堆子窗。
- **「自由 dock 必须原生侧实现」**：排除。网页 dock 框架（Golden Layout 等）已解决拖拽/页签/浮动/序列化，原生侧做这些是重造轮子。

## 未决问题

1. **Vulkan↔WebGPU 共享纹理互操作**：**已判（本批 Vulkan 扩展，2026-11-24）**——Vulkan 设备侧外部内存
   （Win32 句柄）可导出/可导入，共享图像本机实测跑通；但**零拷贝进网页不可行**：网页侧没有导入 Vulkan 外部内存
   句柄的 API（与 D3D12 同一根因，规范零命中 + `importExternalTexture` 只收视频/VideoFrame + GPUWeb #5167），
   且引擎 Vulkan 后端创建时未启用 `VK_KHR_external_memory_win32`（本批不改后端公共代码）。本批 Vulkan 实现是
   CPU 读回（`path=cpu-readback`）。真零拷贝需要"引擎启用外部内存扩展"与"浏览器暴露引擎侧导入入口"两件同时成立
   （阻塞证据见 `openspec/changes/archive/2026-11-24-editor-web-render-spike/evidence/5.5-Vulkan可行性结论.md`）。
2. **路线 B 透明叠层的 z-order/矩形同步**：WebView2 透明合成在拖动/多窗时的行为需实测。
3. **dock 框架选型**：Golden Layout / FlexLayout / dock-spawn / 自研 的取舍（许可证、维护、与纯 TS 无框架前端的契合）。
4. **多窗宿主**：多个原生窗 + 全窗 WebView2 的创建/关闭/跨窗拖拽协调；与现有「单壳窗口」规范的迁移（MODIFIED 或取代）。
5. **scene 面板的输入**：相机操控/拾取等鼠标键盘事件如何从网页 scene 面板回传原生引擎。
6. **实时渲染路径**：CPU 读回每次 262144 字节（256×256）只够低频；要实时就得做**共享内存多缓冲的 CPU-GPU 桥**，
   或等 WebView2 暴露引擎侧共享纹理导入能力（届时可复用本批的同源对账判据）。

## 升级建议

`editor-web-render-spike`（2026-11-24，**双后端**）已经回答了"原生渲染能不能进网页"：**能**（CPU 读回 + canvas.putImageData，
同源对账可证伪），但**不是零拷贝**；D3D12 共享纹理与 Vulkan 共享图像都可以建、可以共享，缺的是网页侧导入入口
（Vulkan 还多一条：引擎后端要启用外部内存扩展）。
下一步立 `editor-free-dock` 时：先按**路线 B（透明叠层）**或**CPU-GPU 桥**二选一做渲染路径（前者省拷贝、要处理矩形/合成；
后者已验证链路、要做多缓冲降拷贝），零拷贝不作为近期目标。本笔记是种子，**不构成要求**；转 proposal 时把决策 1/2/3
与新结论（D3D12 与 Vulkan 的零拷贝都不可行、两后端共享机制都可用、CPU 读回是当前唯一可行显示路径）写进 design。