# 面 4（第二 WebView2 控制器加载 assets.html）：本面未发现 blocking

- 攻击面：工件一致性（布局修订第 ④ 点：第二控制器加载 assets.html 的说法在 design 的 D6/D9、tasks 3.1/3.2 是否一致）
- 结论：**四处一致**，未发现矛盾。下面贴出四处原文与两条外部读数（前端构建产物如何进静态根、静态服务如何取文件），说明这条链路在工件层面自洽。

## 四处原文（逐字）

> `openspec/changes/editor-asset-browser/design.md:68`（D6）
> 资产浏览器是**独立面板单元**：独立页面 `EditorWeb/assets.html` + 独立入口 `src/assets.ts`（复用 `protocol.ts`），由底部 dock 的第二个 WebView2 控制器加载（D9）。

> `openspec/changes/editor-asset-browser/design.md:75`（D9）
> - **资产浏览器 dock**（底部通栏）：**第二个 WebView2 控制器**，Bounds = (0, ClientHeight − DockHeight, ClientWidth, DockHeight)，加载 assets.html。

> `openspec/changes/editor-asset-browser/tasks.md:22`（3.1）
> - [ ] 3.1 资产浏览器独立页面（底部 dock，纯 TS）：新增 `EditorWeb/assets.html` + `EditorWeb/src/assets.ts`（复用 protocol.ts）……vite 多页面入口配 assets.html。验收：npm run build 过（产出 index.html + assets.html）；grep index.html 无 asset-section、assets.html 含 asset-table/refresh-assets……

> `openspec/changes/editor-asset-browser/tasks.md:23`（3.2）
> ……`EditorService.cs` 创建第二个 WebView2 控制器加载 assets.html（静态根同 editor-web）……

> `openspec/changes/editor-asset-browser/proposal.md:9`
> ……独立页面 `EditorWeb/assets.html` + `src/assets.ts`（纯 TS），由底部 dock 的第二个 WebView2 控制器加载……

> `openspec/changes/editor-asset-browser/specs/editor-asset-browser/spec.md:5`
> ……独立 WebView2 控制器 + 独立页面……（不点名文件名，但与 assets.html 不冲突）

一致点：文件名（assets.html）、入口（src/assets.ts）、加载者（第二个 WebView2 控制器 / 底部 dock）、静态根（同 editor-web）四要素在四处说法相同；没有一处把它写成右面板页面或 index.html 内嵌。

## 外部读数（证明「静态根同 editor-web」成立，不是空要求）

```text
PS> Select-String -Path 'build.py' -SimpleMatch 'shutil.copytree(dist, target)','dist / "index.html"'
build.py:1095:     if not (dist / "index.html").is_file():
build.py:1100:     shutil.copytree(dist, target)
```

`build.py` 是**整目录 copytree**（`dist` → `bin/editor-web`），所以只要 vite 多页构建产出 `dist/assets.html`，它自动出现在静态根，不需要额外拷贝步骤——与 tasks 3.2「静态根同 editor-web」一致，工件没有漏掉构建搬运任务。

```text
PS> Select-String -Path 'Scripting\Managed\Editor\EditorService.cs' -SimpleMatch 'string relative = path.TrimStart'
EditorService.cs:1285: string relative = path.TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
```

静态服务按请求路径拼到静态根下取文件（`/assets.html` → `<静态根>/assets.html`），不是固定白名单，所以第二控制器请求 `/assets.html` 不需要额外路由改动；`/` 仍映射 index.html（`EditorService.cs:1281-1284`）。

当前 `EditorWeb/vite.config.ts` 只有单页默认入口（`base: './'`、`outDir: 'dist'`，无 `rollupOptions.input`），tasks 3.1 的「vite 多页面入口配 assets.html」正是需要新增的改动，两处不冲突。

## 附注（不计入主张，超出本面范围）

spec 的 editor-window-panel「资源缺失明确错误页」场景要求「bin/editor-web/ 无面板或 dock 资源时请求对应页 → MUST 返回说明『资源未构建』的错误页」。现状 `ServeStatic` 只用 `index.html` 是否存在当哨兵（`EditorService.cs:1264-1280`），缺 `assets.html` 而 `index.html` 在时会走 404 纯文本分支（`EditorService.cs:1297-1300`）。tasks 3.2 的实现描述里写了「dock 静态资源缺失明确错误页」，但验收清单里没有对应判据（3.2 验收只有构建、三矩形、窗口计数、超范围报错、README grep）。这属于验收覆盖缺口，不是四处说法互相矛盾，留给负责验收面的 agent 判定。

## 本面判定

未发现 blocking 级工件矛盾；不改工件。
