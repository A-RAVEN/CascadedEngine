# EditorWeb：编辑器面板前端

**它做什么**：浏览器端面板，连接编辑器服务（`ws://localhost:<port>/ws`）后提供连接状态、标题编辑、
宽高编辑、撤销、事件日志与失效横幅。控件与协议命令一一对应（`Scripting/Managed/Editor/PROTOCOL.md`
§5）；对象失效后输入全部禁用。

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
| `src/protocol.ts` | `PROTOCOL.md` 的 TypeScript 镜像（消息形状、错误码、版本号） |
| `src/main.ts` | 面板逻辑：hello/welcome、命令请求/结果配对、事件日志、失效禁用 |
| `index.html` | 面板 DOM 与样式（深色、无框架） |
| `vite.config.ts` | `base: './'`（静态文件从子目录提供）、产物 `dist/` |
| `tsconfig.json` | 严格模式、`noEmit`（类型检查由 `tsc --noEmit` 做） |

## 面板与命令的对应（走读记录）

| 控件 | 协议 |
|---|---|
| 连接状态 | `hello` / `welcome`（`welcome.objects` 渲染初值） |
| 标题输入 + 应用 | `set-title`（`args.title`） |
| 宽/高输入 + 应用尺寸 | `set-size`（`args.width/height`；宽必须 > 面板宽，否则服务回 `E_INVALID_ARG`） |
| 撤销 | `undo`（服务端单层撤销槽，空时 `E_UNDO_EMPTY`） |
| 事件日志 | 所有 `event`（`object-changed` / `object-invalidated`）与错误 `result` |
| 失效横幅 | `object-invalidated` → 显示横幅并禁用全部输入与按钮 |

## 实测构建输出

```text
> tsc --noEmit && vite build
vite v8.3.8 building client environment for production...
✓ 6 modules transformed.
dist/index.html                3.37 kB │ gzip: 1.58 kB
dist/assets/index-DmsRUtI8.js  3.60 kB │ gzip: 1.83 kB
✓ built in 41ms
```
