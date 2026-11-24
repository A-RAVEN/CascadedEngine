# Tasks: editor-web-render-spike

> 验收口径：每条给「命令 / 产物 + 期望读数」，期望写死、不从被测物推导；红态存 evidence/。
> 构建唯一路径 python build.py --config Debug。编辑器类 MUST 截图视觉验收（CapturePreviewAsync 不前置，AGENTS §2）。
> spike 性质：研究先行 + 最小原型；零拷贝不可行是预期可能结论（留阻塞证据 + 兜底正控），不算失败。

## 1. 互操作研究与原型网页

- [x] 1.1 核实原生 D3D12 共享纹理→WebView2 网页 canvas 互操作路径（按 CLAUDE.md 用 MCP 工具）：候选①零拷贝共享纹理互操作（只研究判定，不实现）②VideoFrame→importExternalTexture 桥（三态：可行/不可行/未尝试+原因）③CPU 读回兜底。importExternalTexture 收 HTMLVideoElement 或 VideoFrame（非裸共享句柄）；零拷贝不成熟引 GPUWeb #5167 出处。验收：evidence/1.1-互操作研究.md 含冻结快照 + 检索词 + 零命中输出 + WebView2 版本三段（版本经 CoreWebView2Environment.BrowserVersionString 获取，给命令）；不可行有具体阻塞（缺失 API 名/版本/文档 URL），非口头断言。
- [x] 1.2 原型网页 EditorWeb/spike.html + src/spike.ts：**vite.config.ts rollupOptions.input 加 spike.html**（现仅 index/assets）+ **build.py 产物校验加 spike.html**（现仅校验 index/assets）；scene 面板 `<canvas>` + CPU 读回胶水（canvas.putImageData）+ **禁止本地绘制**（只展示收到帧）+ getImageData 多点（背景/中心/四角）+ 帧序号 + canvas 尺寸/getBoundingClientRect/visibilityState 经字节通道回传。验收：npm run build 产出 dist/spike.html（grep dist 含 spike.html，非仅「构建过」）；grep spike.html 含 canvas；回传通道 dry-run 固定四元组可核。

## 2. 原生渲染与原型入口

- [x] 2.1 D3D12 共享纹理走**非绑定面原生助手**：新增 D3D12 原生代码（CA_BIND_SKIP 或不进 CODEGEN_ANNOTATED，**不改 GPUTexture 绑定面、不改 MemoryManager 公共分配路径**），用 `D3D12_HEAP_FLAG_SHARED` + `ID3D12Device::CreateSharedHandle` 建共享堆纹理（**非 D3D11_RESOURCE_MISC_SHARED**）；复用 RenderBackend_D3D12 离屏渲染**固定尺寸（写死 256×256）特征图案**（背景绿 (0,255,0,255) + 中心红三角 (255,0,0,255) + 四角标记 + 帧序号）。验收：构建过；共享句柄非空 + 第二消费者 OpenSharedHandle 成功 + 读回同一句柄纹理多点匹配（原生侧正控，背景绿+中心红+四角）；grep GPUTexture 绑定面/公共分配路径零改动。
- [x] 2.2 托管原型门控 + 独立进程入口：**托管侧**新增原型门控（如 CASCADED_EDITOR_SPIKE=web-render，Host.cs/EditorService.cs 读取，默认关）起全客户区单 WebView2 控制器（复用 EditorWindow 设施，Bounds=整客户区，加载 spike.html）；**控制器关闭先于 hostfxr_close**（0xC0000005 先例）；**门控登记 CSharpHost/README**（check_csharp_host_readme 第④项）；**独立 WebView2 数据目录 + 独立端口**（避开 18390/18391/18392 与默认目录）。C++ 用例 TestWebRenderSpike（Main.cpp + --list 登记 23→24，**进 --list 不进全量同进程顺序跑**，独立进程照 TestEditorAssetBrowser 先例）只起进程/读回断言，不建控制器；docs/testing.md 清单/计数同步。验收：--list 24 含 TestWebRenderSpike、全量仍 22；独立进程起原型窗；现有用例零影响；README 含门控（grep）；关闭顺序判据可核。
- [x] 2.3 CPU 读回实现正控（**零拷贝实现是 Non-goal，本批只 CPU 读回**）：C++ 读回共享纹理像素（Readback 绑定面 CA_BIND_SKIP，读像素只在 C++）→**C++→C#→网页字节通道**（复用 EditorService WebSocket 或 AddHostObjectToScript host object，点名其一）→网页 canvas.putImageData。运行读数 MUST 带 **path=cpu-readback** 字段。验收：构建过；3.1 同源正控过；evidence 注明 path=cpu-readback（非零拷贝）。

## 3. 正控与视觉验收

- [x] 3.1 同源正控（能失败，D3D12 独立进程，**不要求 Vulkan**）：原生读回共享纹理与网页 canvas 回传**对账同一帧**（帧序号 + 整图哈希 + 背景绿/中心红/四角多点），不一致 → E_WEBCANVAS_MISMATCH；断链/缺回传 → 超时（取 Main.cpp:4362 的 6000ms）+ E_WEBCANVAS_NOLINE；网页禁止本地画红（对照红态识破）。**红态构造开关**（照 CASCADED_EDITOR_SELFTEST 惯例）：不启原生渲染 / 隐藏画布（display:none/0 尺寸）/ 断链 各能红。验收：D3D12 独立进程正控过；三红态各自能红（evidence/3.1-*.txt 含开关+命令+失败码，照归档 4.1-首次运行红态.txt 格式）。
- [x] 3.2 截图视觉验收（主循环，编辑器硬要求）：交互打开原型窗，CapturePreviewAsync 抓窗（不前置/不最小化/不移动用户窗口，AGENTS §2）+ read_image 核对网页 canvas 区显示原生红三角。验收：截图存 evidence/3.2-截图.png + read_image 核对记录；抓不到/不符判不通过。

## 4. 回归与结论

- [x] 4.1 回归零差异（主循环跑）：现有 22 用例同进程两后端 name+status 与基线（归档 2026-11-24-editor-asset-browser/evidence/5.1-全量-vulkan.json/-d3d12.json）零差异；TestWebRenderSpike 独立进程过；CoreTests exit 0 + PASSED 26（evidence/4.1-CoreTests.txt）；绑定基线 `python Tools/CodeGen/check_binding_baseline.py --baseline openspec/changes/archive/2026-11-23-csharp-resource-asset-foundation/evidence/新锚.txt --product-dir out/build/x64-debug/codegen/bindings` 缺失0/不一致0/多余0（evidence/4.1-基线.txt，共享纹理走非绑定面）；--list 24、全量 22、docs/testing.md 同步。验收：零差异 + CoreTests 26 + 基线 0/0/0 + 计数一致。
- [x] 4.2 可行性结论留档 + 回写 TODO/008：evidence/4.2-可行性结论.md 含研究冻结快照 + 实际 path 运行读数 + 零拷贝可行与否判定（**MUST 与 2.3 实际 path 一致**，判可行须给该帧无 Readback 证据；不可行含阻塞证据 + 退路 B/桥）；回写 TODO/008（D3D12 零拷贝结论 + **明示 Vulkan↔WebGPU 仍未知**，本批排除 Vulkan）与升级建议。验收：结论与 path 一致（矛盾即红）、可核（运行读数或阻塞证据），TODO/008 已更新含 Vulkan 仍未知。
- [x] 4.3 编译验证（最后）：python build.py --config Debug 失败修到成功；成功后复跑 4.1 读数不变。验收：BUILD SUCCESSFUL + 复跑一致（evidence/4.3-构建.txt）。

## 5. Vulkan 后端扩展（2026-11-24 用户裁定双后端）

- [x] 5.1 Vulkan 互操作研究：核 `VK_KHR_external_memory`（Win32 外部内存句柄）→WebView2 WebGPU 零拷贝可行性（按 CLAUDE.md 用 MCP 工具，冻结快照+检索词+零命中+WebView2 版本）；核本机 Vulkan 设备 externalMemory 扩展支持（vkEnumerateDeviceExtensionProperties）。验收：evidence/5.1-Vulkan互操作研究.md 含零拷贝可行/不可行判定（不可行有阻塞证据 API/版本/文档）+ 设备 externalMemory 支持读数；非口头断言。
- [x] 5.2 Vulkan 非绑定面助手 + CPU 读回实现：新增 Vulkan 原生代码（CA_BIND_SKIP/不进 CODEGEN_ANNOTATED，**不改 GPUTexture 绑定面/公共分配路径**），`VK_KHR_external_memory` Win32 句柄建共享图像（设备不支持则明确记录退 CPU 读回，不静默）；复用 RenderBackend_Vulkan 离屏渲染同一 256×256 特征图案；`vkCmdCopyImageToBuffer`→map→读像素经同一 C++→C#→网页通道送 canvas；运行读数 path=cpu-readback。验收：构建过；句柄非空+第二消费者导入成功+读回同一图像多点匹配（原生侧正控）；grep GPUTexture 绑定面/公共分配零改动；5.3 正控过。
- [x] 5.3 Vulkan 同源正控 + 四红态（--backend vulkan 独立进程）：原生读回与网页 canvas 同帧对账（帧序号+整图哈希+多点）spikeMatched=3；四红态开关 skip-native-render→E_SPIKE_PATTERN / hide-canvas→E_WEBCANVAS_DISPLAY / local-red→E_WEBCANVAS_MISMATCH / no-line→E_WEBCANVAS_NOLINE 各中。验收：vulkan 绿跑 pass + 四红态各中码（evidence/5.3-*.json/txt 含开关+命令+失败码）。
- [x] 5.4 Vulkan 截图视觉验收（主循环）：--backend vulkan 交互原型窗，CASCADED_EDITOR_SPIKE_CAPTURE 抓窗（不前置，AGENTS §2）+ read_image 核对 canvas 显示原生特征图案。验收：evidence/5.4-Vulkan截图.png + read_image 核对记录；抓不到/不符判不通过。
- [x] 5.5 Vulkan 可行性结论 + 回写 TODO/008：evidence/5.5-Vulkan可行性结论.md 含 Vulkan 零拷贝判定（**与实际 path 一致**，判可行须给该帧无 Readback 证据；不可行含阻塞证据+退路）；回写 TODO/008 Vulkan↔WebGPU 结论（替换「仍未知」）。验收：结论与 path 一致、可核，TODO/008 Vulkan 结论已更新。

## 修订记录（对抗验证首轮，五面 46 条主张全采纳，主循环复核按根因归并修订）

> 五面：验收 14（7 blocking）/一致 11（6 blocking）/影响 15（6 blocking）/溯源 3（1 blocking）/存在 3。多处同根，按根因归并处置如下。

1. **共享纹理机制错 + 绑定面互斥**（溯源-001/存在-001/一致-003/影响-004/005/006/008）：`D3D11_RESOURCE_MISC_SHARED` 是 D3D11 标志（D3D12_RESOURCE_DESC 无 MiscFlags），D3D12 正确机制 `D3D12_HEAP_FLAG_SHARED`+`CreateSharedHandle`；GPUTexture 在可绑面（编号 6），加共享句柄会重生成绑定产物与「逐字节不变」互斥。
   - 改前：design D2/tasks 2.1「D3D11_RESOURCE_MISC_SHARED 或 D3D12 shared handle」「复用 D3D12RenderBackend」「共享句柄产出可核」无落点。
   - 改后：D2/tasks 2.1 改**非绑定面原生助手**（CA_BIND_SKIP/不进 CODEGEN_ANNOTATED，HEAP_FLAG_SHARED+CreateSharedHandle，不改 GPUTexture 绑定面/公共分配路径）；真实类名 RenderBackend_D3D12；句柄可核=非空+第二消费者 OpenSharedHandle+读回同一句柄多点匹配。依据：攻击方 grep 全仓 0 命中 + d3d12.h 机制。
2. **独立入口与 WebView2 仅 C# 侧矛盾**（一致-004/005/影响-001/002/003/存在-002）：CreateCoreWebView2ControllerAsync 只在托管 EditorWindow，唯一调用点 EditorService；C++ tester 建不了控制器；「不复用 EditorService」与先例（TestEditorAssetBrowser 复用服务+独立进程）冲突；关闭/门控登记缺失。
   - 改前：D1/tasks 2.2「C++ tester 建控制器」「不复用 EditorService 一次性状态机」无托管任务/关闭/登记。
   - 改后：D1/tasks 2.2 撤「不复用」自造约束（§9），改**复用宿主设施 + 独立进程 + 原型门控默认关**；补托管门控/全窗控制器/**关闭先于 hostfxr_close**（0xC0000005 先例）/README 登记/独立数据目录+端口；C++ 用例只起进程/读回。依据：EditorWindow.cs:183/232、EditorService.cs:427、Host.cs:309、check_csharp_host_readme 第④项。
3. **零拷贝范围浮动**（一致-001/002/010/影响-013）：proposal Non-goals「零拷贝后续」vs design/tasks「可行则实现」；spec「或尚未核实」漏洞；零拷贝与 CPU 兜底验收不可区分。
   - 改前：D3/tasks 2.3「可行则实现」；spec 要求2 场景1「或尚未核实」；无 path 字段。
   - 改后：D3/tasks 2.3/spec 要求2 **零拷贝只研究留结论、实现是 Non-goal**，CPU 读回为实现正控；运行读数带 **path=cpu-readback**；结论 MUST 与实际 path 一致（矛盾即红，判可行须给该帧无 Readback 证据）；撤「或尚未核实」。
4. **正控不可证伪**（验收-001/002/003/008/010/影响-013/014/015/溯源-003/一致-007/008）：读位图非显示画面、页面可本地画红、无同源绑定、只判中心、无尺寸、getImageData「或」目视不等价、无失败码/超时/红态开关。
   - 改前：D4/tasks 3.1「中心像素==(255,0,0,255) 或纯色」「或抓窗目视」「断链 MUST 红」无机制。
   - 改后：D4/tasks 3.1/spec 要求1 **固定尺寸 256×256 特征图案**（背景绿+中心红+四角+帧序号）；**同源对账**（帧序号+整图哈希+多点，E_WEBCANVAS_MISMATCH）；**禁止网页本地画红**；显示核对 getImageData 多点精确**且**抓窗 read_image（非或）；断链超时 6000ms（Main.cpp:4362 先例）+E_WEBCANVAS_NOLINE；**红态开关**（不启渲染/隐藏画布/断链，照 SELFTEST 惯例，evidence 含开关+命令+失败码）。
5. **字节通道无归属**（影响-007/一致-006）：Readback 绑定面 CA_BIND_SKIP（读像素只 C++），WebView2/host object 只 C#，通道选型留未决。
   - 改后：D3/tasks 2.3 点名 **C++→C#→网页通道**（复用 EditorService WebSocket 或 AddHostObjectToScript，择一）。
6. **研究结论软词**（验收-005/006/007/012/014/溯源-002/一致-011）：「可核」无判据、版本无获取命令、importExternalTexture 漏 HTMLVideoElement、零拷贝不可行无出处、基线锚未钉死、--list 计数漏。
   - 改后：1.1 冻结快照+检索词+零命中+版本三段（BrowserVersionString 命令）；importExternalTexture 收 HTMLVideoElement/VideoFrame + GPUWeb#5167 出处；4.1 基线锚钉死（新锚.txt+product-dir+命令）；--list 24/全量 22/docs/testing.md 同步；4.2 结论与 path 一致+Vulkan 仍未知。
7. **spike.html 构建入口缺失**（存在-003）：vite.config.ts input 仅 index/assets，build.py 仅校验两页面。
   - 改后：tasks 1.2 点名 vite.config.ts rollupOptions.input 加 spike.html + build.py 校验加 spike.html；验收 grep dist/spike.html（非仅「构建过」）。
8. **同跑环境/登记**（影响-010/011/012/一致-009/011）：数据目录/端口撞、--list/全量/docs 登记缺、「两后端」越 Vulkan Non-goal。
   - 改后：D1/tasks 2.2 独立数据目录+独立端口；tasks 3.1 删「两后端」改 D3D12；--list 24/不进全量/docs 同步（见 6）。