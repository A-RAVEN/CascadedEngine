## ADDED Requirements

### Requirement: 原型窗必须用单全窗 WebView2 在网页 canvas 显示原生渲染像素（同源可证伪）

spike MUST 复用 EditorService/EditorWindow 的 WebView2 宿主设施，以**独立进程 + 原型门控（默认关）**起原型窗：一个原生窗 + 一个覆盖全客户区的 WebView2 控制器（区别于现有三分区），加载含 scene 面板 `<canvas>` 的原型网页；控制器关闭 MUST 先于 hostfxr_close；门控 MUST 登记 CSharpHost/README；原型窗用独立 WebView2 数据目录 + 独立端口。D3D12 MUST 经**非绑定面原生助手**（`D3D12_HEAP_FLAG_SHARED` + `CreateSharedHandle`，不改 GPUTexture 绑定面、不改公共分配路径）离屏渲染**固定尺寸特征图案**（背景绿 + 中心红三角 + 四角标记 + 帧序号）到共享纹理，经 CPU 读回→C++→C#→网页字节通道→canvas.putImageData 显示（本批实现正控；零拷贝只研究）。**同源对账**：原生读回与网页 canvas 回传 MUST 对账同一帧（帧序号 + 整图哈希 + 背景/中心/四角多点），不一致 MUST 红（E_WEBCANVAS_MISMATCH）；网页 MUST NOT 本地绘制；断链/缺回传 MUST 红（超时 + E_WEBCANVAS_NOLINE）。显示核对 MUST 同时含 getImageData 多点精确断言**且** CapturePreviewAsync 抓窗 read_image 核对真实显示（非仅位图）。spike MUST **双后端 D3D12 与 Vulkan 各验一遍**（CPU 读回路径后端无关）。Vulkan MUST 经**非绑定面原生助手**（`VK_KHR_external_memory` Win32 外部内存句柄 + `vkGetMemoryWin32HandleKHR`，不改 GPUTexture 绑定面/公共分配路径；设备不支持 externalMemory 则明确记录退 CPU 读回，不静默）离屏渲染同一特征图案，`vkCmdCopyImageToBuffer` 读回经同一通道送 canvas。spike MUST NOT 改动现有 editor-window-panel / editor-asset-browser 实现与 Vulkan/D3D12 后端公共代码。

#### Scenario: 网页 canvas 同源显示原生特征图案

- **WHEN** 原型窗启动、D3D12 渲染特征图案到共享纹理、CPU 读回送进网页 canvas
- **THEN** 网页 canvas 多点（背景绿 + 中心红 (255,0,0,255) + 四角）MUST 与原生读回同帧对账一致（帧序号 + 整图哈希）；getImageData 精确断言且抓窗 read_image 核对真实显示；只判中心一点或网页本地画红 MUST 被识破判红

#### Scenario: Vulkan 后端同源显示原生特征图案

- **WHEN** 原型窗以 --backend vulkan 启动、Vulkan 渲染同一特征图案到共享图像、vkCmdCopyImageToBuffer 读回送进网页 canvas
- **THEN** 网页 canvas 多点 MUST 与 Vulkan 原生读回同帧对账一致（帧序号 + 整图哈希 + 背景/中心/四角）；path=cpu-readback；四红态开关（skip-native-render/hide-canvas/local-red/no-line）各中预期码；Vulkan 独立进程跑，现有用例零影响

#### Scenario: 独立进程原型门控不冲突现有编辑器

- **WHEN** 跑 spike 原型用例
- **THEN** MUST 独立进程 + 原型门控默认关（复用宿主设施但避开一次性状态机同进程冲突，照 TestEditorAssetBrowser 先例）；现有 editor-window-panel / editor-asset-browser 用例 MUST 不受影响（回归零差异）

#### Scenario: 红态可第三方复跑

- **WHEN** 自检开关构造 不启原生渲染 / 隐藏画布（display:none/0 尺寸）/ 断链
- **THEN** 各 MUST 红（E_WEBCANVAS_MISMATCH / E_WEBCANVAS_NOLINE / 显示类失败）；evidence MUST 含开关 + 命令 + 失败码（照归档 4.1-首次运行红态.txt 格式）

### Requirement: 互操作可行性结论必须可核且与实际路径一致

spike MUST 核实 WebView2/Chromium 原生 D3D12 共享纹理→网页 canvas 路径（按 CLAUDE.md 用 MCP 工具，冻结快照 + 检索词 + 零命中输出 + WebView2 版本三段，版本经 BrowserVersionString 获取）。**零拷贝实现是 Non-goal**：本批只研究零拷贝可行性留结论，实现以 CPU 读回兜底正控为准。运行读数 MUST 带 `path=` 字段；可行性结论 MUST 与实际 path 一致（CPU 读回实现 + 「零拷贝可行」判定矛盾 MUST 红）；判「零拷贝可行」MUST 给该帧无 Readback 证据。零拷贝不可行 MUST 给具体阻塞（缺失 API 名 / WebView2 版本 / 文档 URL，引 GPUWeb #5167 等出处），MUST NOT 口头断言。结论 MUST 留档 evidence 并回写 TODO/008（D3D12 结论 + 明示 Vulkan↔WebGPU 仍未知）。

#### Scenario: CPU 读回实现正控可跑通

- **WHEN** 本批实现（CPU 读回→canvas.putImageData，path=cpu-readback）
- **THEN** MUST 显示原生像素并通过同源正控；evidence MUST 注明 path=cpu-readback 非零拷贝；MUST NOT 因「零拷贝尚未核实」跳过研究结论

#### Scenario: 不可行结论有阻塞证据且与 path 一致

- **WHEN** 判定零拷贝共享纹理→WebGPU 在 WebView2 不可行
- **THEN** evidence MUST 含具体阻塞（缺失 API 名 / WebView2 版本 / 文档 URL）+ 退路建议（路线 B / CPU-GPU 桥）；判定 MUST 与实际 path 一致（矛盾即红）；MUST NOT 仅口头断言

### Requirement: spike 必须视觉验收不前置且回归零差异

spike MUST 有真实打开原型窗的截图 + 主循环 read_image 核对网页 canvas 显示原生渲染（编辑器类硬要求）。抓窗 MUST 用 CapturePreviewAsync（不依赖前景），MUST NOT 置前/最小化/移动用户窗口（C:\Users\76956\.dsh\AGENTS.md §2）。新用例 MUST 进 --list（23→24）但**不进全量同进程顺序跑**（独立进程，照 TestEditorAssetBrowser），docs/testing.md 清单/计数同步。现有 tester 用例 + CoreTests MUST 全绿；绑定产物 MUST 逐字节不变（spike 共享纹理走非绑定面，不碰生成器/GPUTexture 绑定面）。

#### Scenario: 截图视觉验收不前置

- **WHEN** 交互打开原型窗，网页 canvas 显示原生渲染
- **THEN** CapturePreviewAsync 抓窗 PNG MUST 经 read_image 核对 canvas 区显示原生画面；MUST NOT 置前/最小化/移动用户窗口；缺截图或画面不符 MUST 判不通过

#### Scenario: 回归与绑定零差异

- **WHEN** spike 完成后跑现有用例 + CoreTests + 绑定基线比对
- **THEN** 现有 22 用例同进程 name+status MUST 与基线（归档 2026-11-24-editor-asset-browser/evidence/5.1-全量-*.json）零差异、CoreTests MUST exit 0 + PASSED 26、绑定产物 MUST 逐字节不变（check_binding_baseline.py --baseline 归档新锚.txt --product-dir out/build/x64-debug/codegen/bindings，缺失0/不一致0/多余0）；--list MUST 24 含 TestWebRenderSpike、全量仍 22