// Cascaded 编辑器面板（editor-window-panel 2.1）。
// 只做一件事：把 PROTOCOL.md 的命令/事件接到一个小面板上（连接状态、标题编辑、宽高编辑、撤销、
// 事件日志、失效横幅）。不引入运行时框架；所有 DOM 结构在 index.html，逻辑在这里。

import type {
  CommandMessage,
  CommandName,
  EditorObject,
  EventMessage,
  ObjectChangedFields,
  ResultMessage,
  ServerMessage,
  WelcomeMessage,
} from './protocol';
import { PROTOCOL_VERSION } from './protocol';

function must<T extends Element>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (element === null) {
    throw new Error(`面板缺少元素：${selector}`);
  }
  return element;
}

const statusEl = must<HTMLSpanElement>('#connection-status');
const bannerEl = must<HTMLDivElement>('#invalidated-banner');
const titleInput = must<HTMLInputElement>('#title-input');
const widthInput = must<HTMLInputElement>('#width-input');
const heightInput = must<HTMLInputElement>('#height-input');
const applyTitleButton = must<HTMLButtonElement>('#apply-title');
const applySizeButton = must<HTMLButtonElement>('#apply-size');
const undoButton = must<HTMLButtonElement>('#undo');
const reconnectButton = must<HTMLButtonElement>('#reconnect');
const logList = must<HTMLUListElement>('#event-log');
const objectSummary = must<HTMLParagraphElement>('#object-summary');

let socket: WebSocket | null = null;
let nextRequestId = 1;
let current: EditorObject | null = null;
let invalidated = false;

function log(message: string, className = ''): void {
  const item = document.createElement('li');
  item.textContent = `${new Date().toLocaleTimeString()} ${message}`;
  if (className !== '') {
    item.className = className;
  }
  logList.prepend(item);
  while (logList.childElementCount > 200) {
    logList.lastElementChild?.remove();
  }
}

function setStatus(text: string, className: string): void {
  statusEl.textContent = text;
  statusEl.className = className;
}

function setInputsEnabled(enabled: boolean): void {
  const value = enabled && !invalidated;
  titleInput.disabled = !value;
  widthInput.disabled = !value;
  heightInput.disabled = !value;
  applyTitleButton.disabled = !value;
  applySizeButton.disabled = !value;
  undoButton.disabled = !value;
}

function render(): void {
  if (current === null) {
    objectSummary.textContent = '当前没有可编辑对象';
    return;
  }
  objectSummary.textContent = `对象 ${current.id}：${current.title}（${current.width}x${current.height}）`;
  titleInput.value = current.title;
  widthInput.value = String(current.width);
  heightInput.value = String(current.height);
}

function applyObject(next: EditorObject): void {
  current = next;
  render();
}

function send(command: CommandName, args?: CommandMessage['args']): void {
  if (socket === null || socket.readyState !== WebSocket.OPEN) {
    log('连接未打开，命令未发送', 'warn');
    return;
  }
  if (current === null || invalidated) {
    log('没有可编辑对象或对象已失效，命令未发送', 'warn');
    return;
  }
  const message: CommandMessage = {
    type: 'command',
    requestId: nextRequestId++,
    command,
    objectId: current.id,
    ...(args === undefined ? {} : { args }),
  };
  socket.send(JSON.stringify(message));
}

function handleResult(message: ResultMessage): void {
  if (!message.ok) {
    log(`命令失败：${message.error.code} ${message.error.message}`, 'error');
    return;
  }
  if (message.value.object !== undefined) {
    applyObject(message.value.object);
  }
  log(`命令成功（requestId=${message.requestId}）`);
}

function handleEvent(message: EventMessage): void {
  if (message.event === 'assets-changed') {
    // 资产变化由底部资产浏览器 dock 处理（独立页面 assets.ts）；对象检查器只记一行，不重拉。
    log('资产列表已变化（由底部资产浏览器接管）');
    return;
  }
  if (message.event === 'object-invalidated') {
    invalidated = true;
    bannerEl.hidden = false;
    setInputsEnabled(false);
    log(`对象 ${message.objectId ?? 0} 已失效：输入已禁用`, 'error');
    return;
  }
  if (message.objectId === undefined) {
    log('对象事件缺少 objectId，已忽略', 'warn');
    return;
  }
  if (current !== null && message.objectId === current.id) {
    applyObject({ ...current, ...(message.fields as ObjectChangedFields) });
  }
  log(`对象 ${message.objectId} 变化：${JSON.stringify(message.fields)}`);
}

function handleMessage(raw: string): void {
  let parsed: ServerMessage;
  try {
    parsed = JSON.parse(raw) as ServerMessage;
  } catch {
    log(`无法解析的服务端消息：${raw}`, 'error');
    return;
  }
  switch (parsed.type) {
    case 'welcome': {
      const welcome = parsed as WelcomeMessage;
      log(`welcome：protocolVersion=${welcome.protocolVersion}，对象 ${welcome.objects.length} 个`);
      if (welcome.objects.length > 0) {
        applyObject(welcome.objects[0]);
      } else {
        current = null;
        render();
      }
      setInputsEnabled(current !== null);
      break;
    }
    case 'result':
      handleResult(parsed);
      break;
    case 'event':
      handleEvent(parsed);
      break;
    default:
      log(`未知消息类型：${JSON.stringify(parsed)}`, 'error');
      break;
  }
}

function connect(): void {
  if (socket !== null) {
    socket.close();
    socket = null;
  }
  invalidated = false;
  bannerEl.hidden = true;
  current = null;
  render();
  setInputsEnabled(false);
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = `${scheme}://${location.host}/ws`;
  setStatus('连接中…', 'pending');
  log(`连接 ${url}`);
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
    log('WebSocket 错误', 'error');
  };
  ws.onclose = (event: CloseEvent) => {
    setStatus('已断开', 'warn');
    setInputsEnabled(false);
    log(`连接关闭：code=${event.code} reason=${event.reason}`);
  };
}

applyTitleButton.addEventListener('click', () => {
  send('set-title', { title: titleInput.value });
});

applySizeButton.addEventListener('click', () => {
  const width = Number(widthInput.value);
  const height = Number(heightInput.value);
  if (!Number.isInteger(width) || !Number.isInteger(height)) {
    log('宽高必须是整数', 'warn');
    return;
  }
  send('set-size', { width, height });
});

undoButton.addEventListener('click', () => {
  send('undo');
});

reconnectButton.addEventListener('click', () => {
  connect();
});

connect();
