// Cascaded 资产浏览器（底部通栏独立 dock，editor-asset-browser 3.1）。
//
// 独立页面：由编辑器服务的**第二个 WebView2 控制器**加载（design D6 / D9），与右侧对象检查器页面
// （index.html + main.ts）分离。只做资产浏览：连接后拉 `list-assets`、渲染表格、收到 `assets-changed`
// 重拉、空态与错误显示。协议类型全部来自 protocol.ts（唯一事实源是 PROTOCOL.md）。
//
// editor-code-source-display 5.2：条目**只有 path + kind 两个字段**（用户裁定），表格通用渲染**两列（路径/种类）**；
// **按 kind 区分路径基准**，不把两种基准混成一列读数 —— 数据型条目的 path 相对资源根、源种类
//（shader-source / csharp-source / shader-source-missing）的 path 相对工程根；source-omitted 的 path 是取回目录。

import type {
  AssetChangedFields,
  AssetEntry,
  CommandMessage,
  EventMessage,
  ResultMessage,
  ServerMessage,
} from './protocol';
import { isSourceKind, PROTOCOL_VERSION, SOURCE_OMITTED_KIND } from './protocol';

/** 源类型 / 标记类型的中文类型名（PROTOCOL.md 2.6、§5）；未知类型原样显示。 */
const KIND_LABELS: Record<string, string> = {
  'shader-source': '着色器源文件',
  'shader-source-missing': '着色器源文件（源文件缺失）',
  'csharp-source': 'C# 源文件',
  'source-omitted': '生成绑定源（已略去）',
};

/** 路径基准说明（挂在路径单元的 title 上：数据型相对资源根、源型相对工程根，不混基准）。 */
const PATH_BASE_DATA = '路径相对资源根';
const PATH_BASE_SOURCE = '路径相对工程根';
const PATH_BASE_OMITTED = '取回目录（路径相对工程根）';

function must<T extends Element>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (element === null) {
    throw new Error(`资产浏览器页面缺少元素：${selector}`);
  }
  return element;
}

const statusEl = must<HTMLSpanElement>('#connection-status');
const refreshButton = must<HTMLButtonElement>('#refresh-assets');
const table = must<HTMLTableElement>('#asset-table');
const rows = must<HTMLTableSectionElement>('#asset-rows');
const emptyEl = must<HTMLParagraphElement>('#asset-empty');
const errorEl = must<HTMLDivElement>('#asset-error');
const summaryEl = must<HTMLSpanElement>('#asset-summary');

let socket: WebSocket | null = null;
let nextRequestId = 1;
/** 最近一次 list-assets 的 requestId：只渲染这条请求的结果，避免旧结果覆盖新列表。 */
let latestRequestId = 0;

function setStatus(text: string, className: string): void {
  statusEl.textContent = text;
  statusEl.className = className;
}

function requestAssets(): void {
  if (socket === null || socket.readyState !== WebSocket.OPEN) {
    return;
  }
  const requestId = nextRequestId++;
  latestRequestId = requestId;
  // 非对象作用域命令：不带 objectId（PROTOCOL.md 2.3）。
  const message: CommandMessage = { type: 'command', requestId, command: 'list-assets' };
  socket.send(JSON.stringify(message));
}

function renderAssets(assets: AssetEntry[]): void {
  rows.replaceChildren();
  errorEl.hidden = true;
  summaryEl.textContent = `共 ${assets.length} 条`;
  if (assets.length === 0) {
    // 空根是合法空态（ok=true + 空数组），不显示成错误。
    table.hidden = true;
    emptyEl.hidden = false;
    emptyEl.textContent = '资源根下没有资产文件（空根是合法空态，不是错误）。';
    return;
  }
  for (const entry of assets) {
    const row = document.createElement('tr');
    const isSource = isSourceKind(entry.kind);
    const isOmitted = entry.kind === SOURCE_OMITTED_KIND;
    const pathBase = isOmitted ? PATH_BASE_OMITTED : isSource ? PATH_BASE_SOURCE : PATH_BASE_DATA;
    const kindText = KIND_LABELS[entry.kind] ?? entry.kind;
    // 条目只有 path + kind 两个字段（PROTOCOL.md 2.6）：表格只有路径 / 类型两列，源型在路径单元标注基准。
    const cells: Array<[string, string, string?]> = [
      [entry.path, isSource || isOmitted ? 'source-path' : '', pathBase],
      [kindText, isSource || isOmitted ? 'source-marker' : '', isSource ? PATH_BASE_SOURCE : undefined],
    ];
    for (const [text, className, title] of cells) {
      const cell = document.createElement('td');
      cell.textContent = text;
      if (className !== '') {
        cell.className = className;
      }
      if (title !== undefined) {
        cell.title = title;
      }
      row.appendChild(cell);
    }
    row.className = isSource || isOmitted ? 'source-row' : 'data-row';
    rows.appendChild(row);
  }
  emptyEl.hidden = true;
  table.hidden = false;
}

function renderAssetError(code: string, message: string): void {
  rows.replaceChildren();
  table.hidden = true;
  emptyEl.hidden = true;
  summaryEl.textContent = '资产根不可用';
  errorEl.hidden = false;
  errorEl.textContent = `资产根不可用：${code} ${message}`;
}

function handleResult(message: ResultMessage): void {
  if (!message.ok) {
    if (message.requestId === latestRequestId) {
      renderAssetError(message.error.code, message.error.message);
    }
    return;
  }
  if (message.value.assets !== undefined && message.requestId === latestRequestId) {
    renderAssets(message.value.assets);
  }
}

function handleEvent(message: EventMessage): void {
  // 只关心资产变化：事件只提示增删条数，条目本体靠重拉 list-assets 取（PROTOCOL.md 2.5）。
  if (message.event !== 'assets-changed') {
    return;
  }
  const fields = message.fields as AssetChangedFields;
  summaryEl.textContent = `资产变化：新增 ${fields.added}、移除 ${fields.removed} 条，重拉中…`;
  requestAssets();
}

function handleMessage(raw: string): void {
  let parsed: ServerMessage;
  try {
    parsed = JSON.parse(raw) as ServerMessage;
  } catch {
    return;
  }
  switch (parsed.type) {
    case 'welcome':
      // 连接后立即拉一次资产列表（PROTOCOL.md §5）。
      requestAssets();
      break;
    case 'result':
      handleResult(parsed);
      break;
    case 'event':
      handleEvent(parsed);
      break;
    default:
      break;
  }
}

function connect(): void {
  if (socket !== null) {
    socket.close();
    socket = null;
  }
  latestRequestId = 0;
  rows.replaceChildren();
  table.hidden = true;
  errorEl.hidden = true;
  emptyEl.hidden = false;
  emptyEl.textContent = '尚未加载资产列表。';
  summaryEl.textContent = '';
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = `${scheme}://${location.host}/ws`;
  setStatus('连接中…', 'pending');
  const ws = new WebSocket(url);
  socket = ws;
  ws.onopen = () => {
    setStatus('已连接', 'ok');
    ws.send(JSON.stringify({ type: 'hello', protocolVersion: PROTOCOL_VERSION }));
  };
  ws.onmessage = (event: MessageEvent<string>) => {
    handleMessage(event.data);
  };
  ws.onerror = () => {
    setStatus('连接错误', 'error');
  };
  ws.onclose = (event: CloseEvent) => {
    setStatus(`已断开（code=${event.code}）`, 'warn');
  };
}

refreshButton.addEventListener('click', () => {
  requestAssets();
});

connect();
