# Design: editor-web-render-spike

## Context

自由 dock 方向（TODO/008）取原生网页方案：网页全窗 + 网页侧 dock 框架 + 原生渲染进网页。本 spike 只验「原生渲染进网页」这一段，D3D12 优先。现有编辑器是「原生壳 + 多 WebView2 子 HWND 分区」（editor-window-panel），渲染占原生视口、网页是子窗——spike 反过来：**网页全窗，原生渲染塞进网页 canvas**。

现有可复用件：D3D12 后端（真实类名 `RenderBackend_D3D12`，目录 D3D12RenderBackend）渲染确定性画面；像素读回惯例 (255,0,0,255) 精确匹配 + 背景/中心两点（Main.cpp:2347-2349，editor-window-panel scene view 证明）；`CoreWebView2.CapturePreviewAsync` 不前置抓窗（editor-asset-browser 4.2 实测）；tester 独立进程用例机制（TestEditorAssetBrowser 先例：CASCADED_EDITOR=1 启服务 + 独立进程）；超时先例 Main.cpp:4362（6000ms）；红态自检开关惯例（CASCADED_EDITOR_SELFTEST）。

## Goals / Non-Goals

**Goals:**
- 单全窗 WebView2 原型窗 + 网页 scene 面板 `<canvas>` 真实显示原生 D3D12 渲染像素（正控可核）。
- 核实 D3D12 共享纹理→WebView2 WebGPU canvas 的互操作可行性，留可核结论（可行路径 / 不可行阻塞证据 + 退路）。

**Non-Goals:** dock 框架 / 多窗 / Vulkan / 迁移现有编辑器 / **零拷贝实现（本批只研究留结论）** / 资产对象面板（见 proposal）。

## Decisions

### D1 独立进程原型模式，复用 WebView2 宿主设施（修订：撤「不复用 EditorService」自造约束）

WebView2 环境/控制器创建只在托管侧（`EditorWindow.cs` 环境 :183 / 控制器 :232，唯一调用点 `EditorService.cs:427`），C++ tester 不链接 WebView2、建不了控制器。故 spike **复用 EditorService/EditorWindow 的 WebView2 宿主设施**，以**独立进程 + 原型门控**跑（照 TestEditorAssetBrowser 先例：`CASCADED_EDITOR=1` 启服务、独立进程避开一次性状态机与 TestEditorWindowPanel 同进程冲突）。新增原型门控（如 `CASCADED_EDITOR_SPIKE=web-render`）让服务起**全客户区单 WebView2 控制器**（Bounds=整客户区，加载 spike.html）而非三分区；门控默认关、现有编辑器用例零影响。**托管侧任务点名**：原型门控读取（Host.cs/EditorService.cs）、全窗控制器创建、**控制器关闭先于 hostfxr_close**（仓库硬判据，0xC0000005 先例）、门控登记 CSharpHost/README（check_csharp_host_readme 第④项）。原型窗用**独立 WebView2 用户数据目录 + 独立端口**（避开 18390/18391/18392 与默认 %TEMP%\cascaded-editor-webview2）。C++ 用例 TestWebRenderSpike 只负责起进程/读回断言，不建控制器。

### D2 D3D12 共享纹理走非绑定面原生助手（修订：正确机制 + 绑定不变）

D3D12 共享纹理的正确机制是 **`D3D12_HEAP_FLAG_SHARED` + `ID3D12Device::CreateSharedHandle`**（`D3D11_RESOURCE_MISC_SHARED` 是 D3D11 标志，D3D12_RESOURCE_DESC 无 MiscFlags，不适用——首轮误写已纠）。现有 `GPUTexture` 在可绑面（interface-ids.lock 编号 6），给它加共享句柄方法/字段会重生成绑定产物、与「绑定逐字节不变」互斥。故 spike **不改 GPUTexture 绑定面**，共享纹理经**独立的非绑定原生助手**（新增 D3D12 原生代码，CA_BIND_SKIP 或不进 CODEGEN_ANNOTATED，直接拿 ID3D12Device/ID3D12Resource 建共享堆纹理 + CreateSharedHandle），渲染复用 RenderBackend_D3D12（真实类名，非 D3D12RenderBackend）离屏渲染到该纹理。**不改公共分配路径**（MemoryManager::AllocGPUResource 不动，避免波及所有纹理）。画面**固定尺寸（写死，如 256×256）+ 特征图案**：背景绿 (0,255,0,255) + 中心红三角 (255,0,0,255) + 四角标记 + 每帧帧序号（供同源对账，D4）。共享句柄产出可核 = 句柄非空 + 第二消费者 OpenSharedHandle 成功 + 读回同一句柄纹理像素匹配。

### D3 互操作：零拷贝只研究留结论，CPU 读回为实现正控（修订：范围钉死）

**零拷贝实现是 Non-goal（后续）**——本 spike 只**研究**零拷贝可行性留可核结论，**实现以路径③ CPU 读回兜底为准**（撤首轮「可行则实现」浮动范围）。标准 WebGPU web API 无直接「导入 DXGI 共享句柄」入口（`importExternalTexture` 收 **HTMLVideoElement 或 VideoFrame**，不收裸共享句柄；零拷贝互操作不成熟有 GPUWeb issue #5167 依据，design 引用出处）。候选路径研究（按 CLAUDE.md 用 MCP 工具核文档/版本，冻结快照 + 检索词 + 零命中输出 + WebView2 版本三段，版本经 `CoreWebView2Environment.BrowserVersionString` 获取）：
- **路径①零拷贝**：Chromium/WebView2 实验性共享纹理互操作——**只研究判定可行/不可行**，不可行给具体阻塞（缺失 API 名/版本/文档 URL），**不在本批实现**。
- **路径②VideoFrame 桥**：评估三态（可行/不可行/未尝试+原因），不强行。
- **路径③CPU 读回（实现正控）**：C++ 读回共享纹理像素（Readback 绑定面是 CA_BIND_SKIP，读像素只在 C++）→**经 C++→C#→网页字节通道**（复用 EditorService WebSocket 或 AddHostObjectToScript host object，点名其一）→网页 `canvas.putImageData`。证「原生渲染能显示进网页 canvas」，非零拷贝。
- **path= 字段**：运行读数 MUST 带 `path=cpu-readback`（或实际路径），4.2 可行性结论 MUST 与实际 path 一致（CPU 读回实现 + 「零拷贝可行」判定矛盾即红）。

### D4 正控判据（能失败，修订：同源绑定 + 多点 + 显示核对 + 红态开关）

核心命题「原生像素进了网页」MUST 可证伪：
- **同源对账**：原生侧读回共享纹理与网页 canvas 回传 MUST 对账同一帧——**帧序号 + 整图哈希 + 四角/中心/背景多点采样**（背景绿 + 中心红 + 四角标记），不一致 → `E_WEBCANVAS_MISMATCH`。只判中心一点不够（翻转/缩放/部分传输可过）。
- **禁止网页本地画红**：网页只展示收到的帧，MUST NOT 本地绘制；对照红态（自检开关不启原生渲染/预填红）MUST 被同源对账识破。
- **显示核对（且，非或）**：网页 `canvas.getImageData` 多点回传 C++ 精确断言 **且** CapturePreviewAsync 抓窗 + read_image 核对 canvas 区**真实显示**（非仅位图）；回传带 canvas 尺寸/getBoundingClientRect/visibilityState；显示类红态（自检开关隐藏画布 display:none/0 尺寸）MUST 红。
- **断链判红**：帧序号 + 超时（取既有 Main.cpp:4362 的 6000ms 先例）+ 缺回传行 → `E_WEBCANVAS_NOLINE`。
- **红态构造开关**（照仓库 SELFTEST 惯例）：不启原生渲染 / 隐藏画布 / 断链 各能红，第三方可复跑（evidence 含开关+命令+失败码，照归档 4.1-首次运行红态.txt 格式）。
- 截图视觉验收（编辑器硬要求）：CapturePreviewAsync 不前置（AGENTS §2）。

### D5 可行性结论留档（修订：与 path 一致 + 双后端各自结论）

evidence 留：互操作研究冻结快照（检索词/零命中/文档 URL/WebView2 版本）、实际 path 运行读数、零拷贝可行与否判定（**MUST 与实际 path 一致**，判「可行」须给该帧无 Readback 证据）+ 退路建议（B 透明叠层 / CPU-GPU 桥）。回写 TODO/008：**D3D12 与 Vulkan 各自零拷贝结论**（D3D12 共享可行/零拷贝进网页不可行；Vulkan 外部内存→WebGPU 判定）。

### D6 Vulkan 共享图像走非绑定面原生助手（双后端扩展，2026-11-24 用户裁定）

Vulkan 与 D3D12 共用网页/通道/同源对账判据（D4），仅原生助手不同。Vulkan 共享图像机制 **`VK_KHR_external_memory`（Win32 外部内存句柄 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`）+ `vkGetMemoryWin32HandleKHR`**（设备须启用 externalMemory 扩展；若设备不支持则明确记录并退 CPU 读回正控，不静默）。经**独立非绑定 Vulkan 助手**（新增 Vulkan 原生代码，CA_BIND_SKIP/不进 CODEGEN_ANNOTATED，**不改 GPUTexture 绑定面/公共分配路径**）建共享图像，复用 RenderBackend_Vulkan 离屏渲染同一特征图案（256×256 背景绿+中心红+四角+帧序号）。**CPU 读回走 `vkCmdCopyImageToBuffer`→map→读像素**（Vulkan 读回惯例），经同一 C++→C#→网页通道送 canvas。共享句柄可核 = 句柄非空 + 第二消费者 `vkGetMemoryWin32HandleKHR`/导入成功 + 读回同一图像多点匹配。**Vulkan↔WebGPU 零拷贝只研究**（路径①，外部内存句柄能否进 WebView2 WebGPU， likely 不可行，留阻塞证据），实现仍以 CPU 读回为正控（path=cpu-readback）。红态开关与 D3D12 同（skip-native-render/hide-canvas/local-red/no-line），Vulkan 独立进程跑（--backend vulkan）。

## Risks / Trade-offs

- [零拷贝互操作在 WebView2 不可行] → spike 兜底路径③证概念 + 留阻塞证据，退路 B/桥；不强行零拷贝。这是 spike 的预期可能结论，不是失败。
- [WebGPU 在 WebView2 需特性开关/版本不足] → 研究任务先核版本与开关，不可用则路径③（canvas 2D putImageData 不依赖 WebGPU）。
- [CPU 读回慢] → spike 只证「能显示」，性能非目标；明示非零拷贝。
- [原型窗与现有编辑器冲突] → 复用宿主设施但独立进程 + 原型门控默认关 + 独立数据目录/端口（D1），现有用例零影响。
- [共享纹理改绑定面] → 走非绑定原生助手（D2），GPUTexture 绑定面不动，绑定产物逐字节不变。

## Migration Plan

纯实验增量：独立原型入口 + 原型网页 + 共享纹理渲染 + 读回正控。回滚 = 撤增量；现有编辑器/规范/绑定产物不受影响（绑定逐字节不变是硬判据）。spike 结论喂后续 `editor-free-dock` change。

## Open Questions

1. WebView2 当前版本是否暴露零拷贝共享纹理→WebGPU 互操作（研究任务核）。
2. 路径③的字节传输通道选型（host object vs WebSocket）与帧率（spike 取最简单可行）。
3. 若零拷贝不可行，后续 dock 框架取路线 B（透明叠层）还是 CPU/GPU 桥——spike 给建议，后续 change 定。