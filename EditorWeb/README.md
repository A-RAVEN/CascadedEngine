# EditorWeb：编辑器面板前端

**它做什么**：三个独立页面 —— ① 右面板**对象检查器**（`index.html` + `src/main.ts`）连接编辑器服务
（`ws://localhost:<port>/ws`）后提供连接状态、标题编辑、宽高编辑、撤销、事件日志与失效横幅；
② 底部通栏**资产浏览器 dock**（`assets.html` + `src/assets.ts`，editor-asset-browser）只读列出资源根
下的资产文件（路径 / 种类 / 大小 / 修改时间 / 已注册），带刷新按钮与空态/错误显示；③ **渲染原型页**
（`spike.html` + `src/spike.ts`，editor-web-render-spike）由**全客户区单 WebView2 控制器**加载，把服务端
推来的原生读回帧 `putImageData` 到 256×256 的 scene canvas 上，再把 canvas 自读回的 帧序号/整图哈希/
七点采样/canvas 尺寸与可见性经同一条 WebSocket 回传（`spike-hello` / `spike-frame` / `spike-report`，
形状见 `Scripting/Managed/Editor/PROTOCOL.md` §7）。前两个页面各自由一个 WebView2 控制器加载，互不引用；
控件与协议命令的对应见 `PROTOCOL.md` §5。对象失效后对象检查器的输入全部禁用。

**它不做什么**：不带任何运行时框架（无 React/Vue）；不直接访问引擎，只通过 WebSocket 命令/事件；
不做本地乐观更新 —— 收到 `result.ok` 或 `object-changed` 才更新显示（命令在引擎主线程的下一次
`Tick` 执行）。

## 要求

- Node.js **>= 20**（本机实测 v22.20.0）与 npm（实测 10.9.3）。
- `package-lock.json` **必须入库**：`build.py` 用 `npm ci` 还原依赖（没有 lockfile 会直接报错）。

## 开发与构建

```powershell
cd EditorWeb
npm install        # 首次/加依赖时；提交 package-lock.json
npm run dev        # 本地开发服务器（调试用，serve 的 dist 不参与构建）
npm run build      # tsc --noEmit + vite build → dist/
```

`python build.py --config Debug` 会自动跑 `npm ci` + `npm run build`，并把 `dist/` 整目录拷到
`out/build/x64-debug/bin/editor-web/`（编辑器服务的静态文件根）。PATH 里没有 node/npm 时只打印警告
并跳过（不生成 `bin/editor-web/`，服务返回资源缺失错误页），不会让引擎构建失败。

## 文件

| 文件 | 作用 |
|---|---|
| `src/protocol.ts` | `PROTOCOL.md` 的 TypeScript 镜像（消息形状、错误码、版本号），两个页面共用 |
| `src/main.ts` | 对象检查器逻辑：hello/welcome、命令请求/结果配对、事件日志、失效禁用 |
| `index.html` | 对象检查器 DOM 与样式（深色、无框架；由右上 WebView2 控制器加载） |
| `src/assets.ts` | 资产浏览器 dock 逻辑：连接后拉 `list-assets`、渲染 `AssetEntry` 行、`assets-changed` 重拉、空态/错误 |
| `assets.html` | 资产浏览器 dock DOM 与样式（底部通栏独立页面；由第二个 WebView2 控制器加载） |
| `src/spike.ts` | 渲染原型页逻辑：连 `/ws`、收 `spike-frame` → `putImageData` → `getImageData` 回传对账数据；**不本地绘制**（自检开关 `?selftest=hide-canvas/local-red/no-line` 只用来构造红态） |
| `src/spikeChannel.ts` | 渲染原型的**纯**通道逻辑（base64 解码、FNV-1a 32 整图哈希、七点采样、回传消息组装）——不碰 DOM，可脱离浏览器做 dry-run |
| `spike.html` | 渲染原型页 DOM 与样式（scene 面板 + 256×256 canvas；由全客户区单控制器加载） |
| `vite.config.ts` | `base: './'`（静态文件从子目录提供）、多页面入口（`index.html` + `assets.html` + `spike.html`）、产物 `dist/` |
| `tsconfig.json` | 严格模式、`noEmit`（类型检查由 `tsc --noEmit` 做） |

## 面板与命令的对应（走读记录）

**对象检查器（`index.html` + `main.ts`）**：

| 控件 | 协议 |
|---|---|
| 连接状态 | `hello` / `welcome`（`welcome.objects` 渲染初值） |
| 标题输入 + 应用 | `set-title`（`args.title`） |
| 宽/高输入 + 应用尺寸 | `set-size`（`args.width/height`；宽必须 > 面板宽、高必须 > dock 高（默认 240），否则服务回 `E_INVALID_ARG`） |
| 撤销 | `undo`（服务端单层撤销槽，空时 `E_UNDO_EMPTY`） |
| 事件日志 | 所有 `event`（`object-changed` / `object-invalidated` / `assets-changed`）与错误 `result` |
| 失效横幅 | `object-invalidated` → 显示横幅并禁用全部输入与按钮 |

**资产浏览器 dock（`assets.html` + `assets.ts`）**：

| 控件 | 协议 |
|---|---|
| 连接状态 + 表格 | `hello` / `welcome` 后自动发 `list-assets`，用 `value.assets` 渲染五行（路径/种类/大小/修改时间/已注册） |
| 刷新按钮 | 重发 `list-assets` |
| 资产变化 | `assets-changed`（`fields.added/removed`）→ 自动重发 `list-assets` 重拉 |
| 空态 / 错误条 | `ok=true` 空数组 → 空态文案（不是错误）；`E_ASSET_ROOT` → 错误条显示码与消息 |

## 实测构建输出

多页面（`index.html` + `assets.html`）之后的一次 `npm run build` 实测（**加 `spike.html` 之前的历史读数**；
加第三个入口后产物应另有 `dist/spike.html` 与它自己的 js，`build.py` 现在校验三个页面都在）：

```text
> tsc --noEmit && vite build
vite v8.3.8 building client environment for production...
✓ 9 modules transformed.
dist/index.html                                 3.47 kB │ gzip: 1.61 kB
dist/assets.html                                3.63 kB │ gzip: 1.78 kB
dist/assets/modulepreload-polyfill-P2Xu9kJm.js  0.67 kB │ gzip: 0.38 kB
dist/assets/assets-DcTGLRXq.js                  2.38 kB │ gzip: 1.34 kB
dist/assets/index-TFnG6Smo.js                   3.14 kB │ gzip: 1.67 kB
✓ built in 117ms
```

`build.py` 会把 `dist/` 整目录拷到 `bin/editor-web/`，并校验三个页面（`index.html` / `assets.html` /
`spike.html`）都在（缺任一页即构建失败）。
