// Cascaded 资产浏览器（底部通栏独立 dock，editor-asset-browser 3.1）。
//
// 独立页面：由编辑器服务的**第二个 WebView2 控制器**加载（design D6 / D9），与右侧对象检查器页面
// （index.html + main.ts）分离。只做资产浏览：连接后拉 `list-assets`、渲染表格、收到 `assets-changed`
// 重拉、空态与错误显示。协议类型全部来自 protocol.ts（唯一事实源是 PROTOCOL.md）。

import type {
  AssetChangedFields,
  AssetEntry,
  CommandMessage,
  EventMessage,
  ResultMessage,
  ServerMessage,
} from './protocol';
import { PROTOCOL_VERSION } from './protocol';

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

function formatBytes(bytes: number): string {
  return `${bytes.toLocaleString('zh-CN')} B`;
}

function formatModifiedUtc(text: string): string {
  // 服务端给的是 ISO 8601 UTC（例 2024-03-04T05:06:07.0000000Z）；显示到秒，解析失败就原样显示。
  const at = text.indexOf('.');
  if (at > 0 && text.endsWith('Z')) {
    return `${text.slice(0, at)}Z`;
  }
  return text;
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
    const cells: Array<[string, string]> = [
      [entry.path, ''],
      [entry.kind, ''],
      [formatBytes(entry.sizeBytes), ''],
      [formatModifiedUtc(entry.modifiedUtc), ''],
      [entry.registered ? '已注册' : '未注册', entry.registered ? 'registered-true' : 'registered-false'],
    ];
    for (const [text, className] of cells) {
      const cell = document.createElement('td');
      cell.textContent = text;
      if (className !== '') {
        cell.className = className;
      }
      row.appendChild(cell);
    }
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
