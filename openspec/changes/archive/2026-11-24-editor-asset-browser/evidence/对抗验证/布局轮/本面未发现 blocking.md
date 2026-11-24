# 真实存在性面：本面未发现 blocking

- 攻击面：真实存在性（工件点名的文件 / 符号 / 行号是否真实存在）
- 审查对象：`openspec/changes/editor-asset-browser/` 的 proposal.md / design.md / tasks.md / specs/editor-asset-browser/spec.md / specs/editor-window-panel/spec.md
- 审查快照：proposal.md `67C6262848BBE41B`（00:13:10）、design.md `23F83364A67108FC`（00:10:13）、tasks.md `18F17F295C208209`（00:12:18）、specs/editor-asset-browser/spec.md `65F2330C8240EAE0`（00:10:30）、specs/editor-window-panel/spec.md `CC9BF09EAD59C25C`（00:10:44）
- 结论：**未发现 blocking**。任务点名的既有文件与符号全部真实存在，声称的行号与真实行号偏移在 ±2 行以内；预期新增的 `EditorWeb/assets.html`、`EditorWeb/src/assets.ts`、`CASCADED_EDITOR_DOCK_HEIGHT` 当前不存在，与工件「新增」措辞一致。另提出 4 条非阻断主张（全部 misleading），见文末清单。
- 本面只写 evidence 下的文件，**未改任何工件**；允许的只读校验 `openspec validate editor-asset-browser` 通过：

```text
Change 'editor-asset-browser' is valid
EXIT=0
```

## 一、六项指定核对（命令与真实输出摘录）

### ① EditorWindow.cs 的布局符号

命令：

```powershell
Select-String -Path 'Scripting/Managed/Editor/EditorWindow.cs' -Pattern 'internal int Client\w+|internal int PanelWidth|internal int ViewportWidth|m_ConfiguredPanelWidth|internal void ApplyLayout|m_Controller\.Bounds' |
  ForEach-Object { "$($_.LineNumber):$($_.Line.Trim())" }
```

真实输出：

```text
26:internal int ClientWidth;
27:internal int ClientHeight;
28:internal int PanelWidth;
29:internal int ViewportWidth;
35:private readonly int m_ConfiguredPanelWidth;
52:m_ConfiguredPanelWidth = panelWidth;
202:m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ClientHeight);
240:internal void ApplyLayout(int clientWidth, int clientHeight)
248:if (m_ConfiguredPanelWidth < 1 || m_ConfiguredPanelWidth >= clientWidth)
250:Fail("CASCADED_EDITOR_PANEL_WIDTH=" + m_ConfiguredPanelWidth + " 超出范围（1..客户区宽-1=" +
254:int panel = m_ConfiguredPanelWidth;
261:m_Controller.Bounds = new System.Drawing.Rectangle(ViewportWidth, 0, PanelWidth, ClientHeight);
```

判定：`ApplyLayout` 声称 :240、实际 :240（偏移 0）；`ClientHeight`/`PanelWidth`/`ViewportWidth` 三个字段真实存在（27/28/29）；`m_ConfiguredPanelWidth` 真实存在（35）；`m_Controller.Bounds` 真实存在，但有**两处**（202/261），工件只点名 ApplyLayout 一处——见 `主张-存在-001`。

### ② EditorService.cs 的布局符号与 s_scene.Resize 参数

命令：

```powershell
Select-String -Path 'Scripting/Managed/Editor/EditorService.cs' -Pattern 'DefaultPanelWidth|TryReadPanelWidth|private static void SyncLayout|s_scene\.Resize|" panel="|" viewport="' |
  ForEach-Object { "$($_.LineNumber):$($_.Line.Trim())" }
Select-String -Path 'Scripting/Managed/Editor/EditorSceneView.cs' -Pattern 'internal bool Resize'
```

真实输出：

```text
61:internal const int DefaultPanelWidth = 320;
191:if (!TryReadPanelWidth(out configError))
344:private static bool TryReadPanelWidth(out string error)
350:s_panelWidth = DefaultPanelWidth;
584:private static void SyncLayout(int width, int height, string source)
597:!s_scene.Resize(s_window.ViewportWidth, s_window.ClientHeight))
1598:" panel=" + (s_window == null ? "-" : s_window.ViewportWidth + ",0," + s_window.PanelWidth +
1600:" viewport=" + (s_window == null ? "-" : "0,0," + s_window.ViewportWidth + "," +
Scripting/Managed/Editor/EditorSceneView.cs
164:internal bool Resize(int viewportWidth, int viewportHeight)
```

判定：

- `SyncLayout` 声称 :584、实际 :584（偏移 0）。
- `DefaultPanelWidth` 声称 :61、实际 :61（偏移 0）。
- `TryReadPanelWidth` 声称 :344、实际 :344（偏移 0）。
- 读数行声称 :1598：`" panel="` 确实从 :1598 开始；`" viewport="` 在 :1600（同一表达式内，偏移 +2 行仍属同一读数行）。
- `s_scene.Resize` 的签名真实为 **两参数** `Resize(int viewportWidth, int viewportHeight)`；当前调用 `s_scene.Resize(s_window.ViewportWidth, s_window.ClientHeight)` 也是两参数，design 要求的 `(ViewportWidth, ClientHeight−DockHeight)` 与签名相符，不需要改方法签名。

### ③ assets.html / src/assets.ts 当前是否存在（应为「新增」）

命令与真实输出：

```powershell
'EditorWeb/assets.html','EditorWeb/src/assets.ts' | ForEach-Object { "$_ -> Test-Path " + (Test-Path -LiteralPath $_) }
Get-ChildItem -Recurse -File -Include 'assets.html','assets.ts' -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -notmatch 'node_modules' } | ForEach-Object { $_.FullName }
```

```text
EditorWeb/assets.html -> Test-Path False
EditorWeb/src/assets.ts -> Test-Path False
（全仓排除 node_modules 后零命中）
```

工件措辞：`proposal.md:28` 写「**新增** `assets.html`/`src/assets.ts`」、`tasks.md:22`（3.1）写「**新增** `EditorWeb/assets.html` + `EditorWeb/src/assets.ts`」——是「新增」，不是「已存在」。构建产物目录 `EditorWeb/dist/` 当前只有 `index.html` 与 `assets/index-BtQx6xeJ.js`，`out/build/x64-debug/bin/editor-web/` 同样只有这两个文件。判定：**与事实一致，不计缺陷**。

### ④ index.html 当前是否含 asset-section

命令与真实输出（节选）：

```powershell
Select-String -Path EditorWeb/index.html -Pattern 'asset-section|asset-table|refresh-assets|asset-empty|asset-error'
```

```text
82:#asset-section { margin-top: 14px; border-top: 1px solid var(--line); padding-top: 10px; }
83:#asset-section h2 { margin: 0; font-size: 13px; font-weight: 600; }
157:<section id="asset-section" aria-label="资产">
160:<button id="refresh-assets" type="button">刷新资产</button>
163:<p id="asset-empty">尚未加载资产列表。</p>
164:<table id="asset-table" hidden>
183:<div id="asset-error" hidden></div>
```

`EditorWeb/src/main.ts` 资产相关匹配 53 处（`#refresh-assets`、`#asset-table`、`#asset-rows`、`list-assets`、`assets-changed` 等）。判定：**当前确实含资产区**；`tasks.md:22`（3.1）要求「从 index.html/main.ts 移出资产区（右面板只留对象检查器，删 #asset-section 及相关 main.ts 逻辑）」，与当前状态一致，不计缺陷。

### ⑤ vite 多页面入口可行性

命令与真实输出：

```powershell
Get-Content EditorWeb/vite.config.ts -Raw
(Get-Content EditorWeb/package.json -Raw | ConvertFrom-Json).scripts
Get-Content EditorWeb/tsconfig.json -Raw
```

```text
vite.config.ts（全文）：
import { defineConfig } from 'vite';
export default defineConfig({
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
});

package.json scripts：
dev     : vite
build   : tsc --noEmit && vite build
preview : vite preview

tsconfig.json：include = ["src", "vite.config.ts"]，strict，noEmit
```

判定：`vite.config.ts` 与 `package.json` 都真实存在，当前构建脚本是 `tsc --noEmit && vite build`；配置里没有 `rollupOptions.input`（命令 `Select-String -Path EditorWeb/vite.config.ts -Pattern 'rollupOptions|input'` 命中 0 条）。加 `build.rollupOptions.input = { main: 'index.html', assets: 'assets.html' }` 即可产出两个页面，`tsconfig.json` 的 `include: ["src", ...]` 会覆盖新增的 `src/assets.ts`——**多页面入口可行**。构建产物由 `build.py` 的 `shutil.copytree(dist, target)` 整目录拷进 `bin/editor-web/`（`build.py:1100`），新增 `assets.html` 会被一起拷走；但 `build.py:1095` 的产物校验只查 `dist/index.html`，不查 `dist/assets.html`（此项只作为观察，未计为主张）。

### ⑥ PrintWindow 是否 user32 真实 API

- 本机 Windows SDK 10.0.26100.0：`WinUser.h:4617` 声明 `PrintWindow(HWND, HDC, UINT)`；`WinUser.h:4610` 定义 `PW_RENDERFULLCONTENT 0x00000002`。
- 微软官方文档（2026-11-24 抓取）：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow>，要求表写明 `Header: winuser.h`、`Library: User32.lib`、`DLL: User32.dll`。

判定：**PrintWindow 是 user32 真实 API**，这一项通过。但工件把「用 PrintWindow 按 HWND 抓窗」写成 4.2 的硬验收判据、同时点名 `desktop-screenshot` 技能；该技能脚本只用 `CopyFromScreen`、没有 PrintWindow 实现——见 `主张-存在-003`。

## 二、附加核对（不计主张，供主循环参考）

1. `CAAssets/` 真实内容与 `tasks.md:29`（4.2）写死的预期行对得上：`VulkanShaderLibrary.shLib` 145710 B、`D3D12ShaderLibrary.shLib` 293737 B、`CSharpProbe/Contrast.res` 4 B、`CSharpProbe/Imported_A.res` 2 B、`CSharpProbe/Imported_B.res` 2 B（`Get-ChildItem CAAssets -Recurse -File` 实测）。4.2 文本把后两条简写成 `Imported_A.res`/`Imported_B.res`，实际相对路径带 `CSharpProbe/` 前缀，执行截图核对时以真实路径为准。
2. `CASCADED_EDITOR_PANEL_WIDTH` 的上界校验不在被引用的 `EditorService.cs:61/344`：`:344-359` 的 `TryReadPanelWidth` 只校验「整数且 ≥1」，`1..客户区宽−1` 的上界在 `EditorWindow.cs:248-253`。工件若照被引用两行给 dock 高只加正数校验，`tasks.md:23` 的「CASCADED_EDITOR_DOCK_HEIGHT 超范围明确报错」会缺少落点（此项并入 `主张-存在-002` 的取证范围，未单列 claim）。
3. `docs/testing.md:34/42` 已含第 23 用例 `TestEditorAssetBrowser` 的说明（5.2 已勾），与 `Main.cpp:9751` 的 `--list` 输出行一致；`openspec validate editor-asset-browser` 通过。
4. 本面没有发现「工件点名了不存在的文件/符号/行号」的 blocking 级问题；发现的是「点名不完整 / 缺少可执行手段 / 缺少数字基准」三类 misleading。

## 三、本面主张清单（4 条，全部 misleading，无 blocking）

| 编号 | severity | 一句话 |
|---|---|---|
| `主张-存在-001` | misleading | `m_Controller.Bounds` 有两处赋值（EditorWindow.cs:202/261），工件只点名 ApplyLayout（:240），按字面实现首帧检查器会压住 dock。 |
| `主张-存在-002` | misleading | `CASCADED_EDITOR_DOCK_HEIGHT` 的「默认值」在工件里没有数字，5.5 的「与 3.2 一致」无基准、不能失败。 |
| `主张-存在-003` | misleading | PrintWindow 是真实 API，但被点名的 `desktop-screenshot` 脚本只用 `CopyFromScreen`、无 HWND/PrintWindow 实现，4.2 按工件无法直接执行。 |
| `主张-存在-004` | misleading | `CSharpHost/README.md:123/125/389` 与 `EditorWeb/README.md:35/41-48` 仍是右面板资产区/两分区旧事实，tasks 5.5 未点名、验收不查旧句删除。 |

每条的完整取证见同目录 `证据-存在-001.md` … `证据-存在-004.md`。

## 四、与并行攻击面的重叠（供主循环去重）

| 本面主张 | 并行面同根主张 | 关系 |
|---|---|---|
| `主张-存在-001`（m_Controller.Bounds 第二处 :202） | `主张-影响-002`（初始建图 / 读数行 / set-size 高度未点名，EditorService.cs:391-392 等） | 互补：同属「初始路径没随 dock 高改」，但本条是 EditorWindow.cs:202 的控制器 Bounds，影响-002 是 EditorService 的初始建图与读数行。 |
| `主张-存在-002`（dock 高默认值没有数字） | `主张-一致-002`（同一缺口，标 blocking）、`主张-验收-001`（同根：读数行取值自洽无法判错） | 同一缺口，本条从「存在性/可对拍基准」角度取证；severity 本条标 misleading，是否升 blocking 由主循环合并时定。 |
| `主张-存在-003`（PrintWindow 抓窗无实现） | `主张-溯源-003`（把未验证能力写成事实）、`主张-验收-004`（三项观察不能变红，标 blocking） | 同一根因三角取证；本条补的是「被点名的 desktop-screenshot 脚本没有 HWND/PrintWindow 实现」这一存在性事实。 |
| `主张-存在-004`（README 旧事实未被 5.5 点名） | 并行面未检索到同根主张 | 本面独有。 |

另：`主张-溯源-002` 与本面「附加核对 2」同根（`CASCADED_EDITOR_PANEL_WIDTH` 先例的 :61/344 只覆盖默认值与正数校验，上界在 EditorWindow.cs:248）；本面未单列 claim，主循环可直接采用溯源-002。

