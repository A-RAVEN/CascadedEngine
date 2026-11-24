// Cascaded 渲染原型页（editor-web-render-spike 1.2）：把服务端推来的原生读回帧显示在 canvas 上，
// 并把 canvas 自己读回的对账数据回传。
//
// 硬约束（design D4 / spec 要求 1）：
//   - **不本地绘制**：唯一的绘制调用是 `putImageData` 展示收到的帧。除了自检开关 local-red
//     （只用来证明"网页本地画红"会被同源对账识破），本页没有任何自造像素。
//   - 回传 MUST 带：帧序号、整图哈希（与 C++ 侧同一算法）、背景/中心/四角多点采样、
//     canvas 像素尺寸、getBoundingClientRect、可见性状态。少一项，隐藏画布这类显示红态就抓不住。
//   - 通道复用 EditorService 的 WebSocket（/ws）；消息形状见 Scripting/Managed/Editor/PROTOCOL.md §7。
//   - 纯换算（base64 / 哈希 / 采样 / 回传消息）在 spikeChannel.ts，本文件只留 DOM 与 WebSocket 胶水。

import type { SpikeFrameMessage, SpikeHelloMessage } from './spikeChannel';
import { buildReport, CANVAS_SIZE, decodeBase64 } from './spikeChannel';

function must<T extends Element>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (element === null) {
    throw new Error(`渲染原型页缺少元素：${selector}`);
  }
  return element;
}

const statusEl = must<HTMLSpanElement>('#connection-status');
const frameStatusEl = must<HTMLSpanElement>('#frame-status');
const canvas = must<HTMLCanvasElement>('#scene-canvas');
const logEl = must<HTMLUListElement>('#report-log');

const context = canvas.getContext('2d', { willReadFrequently: true });
if (context === null) {
  throw new Error('渲染原型页拿不到 canvas 2d 上下文');
}

// 本批用 canvas 2D，不依赖 WebGPU；这里只把"这个 WebView2 版本有没有 navigator.gpu"作为环境
// 事实回传（研究结论需要它）。DOM 类型库不含 WebGPU 类型，按可选属性读，避免引 @webgpu/types。
const gpuNavigator = navigator as Navigator & { gpu?: unknown };
const webgpuAvailable = typeof gpuNavigator.gpu !== 'undefined' ? 1 : 0;
const selftest = new URLSearchParams(location.search).get('selftest') ?? '';

function setStatus(text: string, className: string): void {
  statusEl.textContent = text;
  statusEl.className = className;
}

function logLine(text: string, isError = false): void {
  const item = document.createElement('li');
  item.textContent = text;
  if (isError) {
    item.className = 'error';
  }
  logEl.appendChild(item);
  while (logEl.childElementCount > 40) {
    logEl.removeChild(logEl.firstElementChild as Element);
  }
}

/** 隐藏画布自检：只在 selftest=hide-canvas 时执行（正常模式绝不隐藏）。 */
if (selftest === 'hide-canvas') {
  canvas.style.display = 'none';
  logLine('自检开关 selftest=hide-canvas：画布已 display:none（显示类红态）');
}

let socket: WebSocket | null = null;
let helloSeen = false;
let receivedFrames = 0;

function sendReport(frame: SpikeFrameMessage): void {
  // 像素尺寸 / 位置 / 可见性三类读数都必须回传（D4：显示核对不是"只看哈希"）。
  const rect = canvas.getBoundingClientRect();
  const source = context!.getImageData(0, 0, canvas.width, canvas.height);
  const report = buildReport(
    { frame: frame.frame, width: frame.width, height: frame.height },
    {
      width: canvas.width,
      height: canvas.height,
      rectX: rect.x,
      rectY: rect.y,
      rectWidth: rect.width,
      rectHeight: rect.height,
      visibility: document.visibilityState,
      display: getComputedStyle(canvas).display,
    },
    source.data,
    webgpuAvailable,
    window.devicePixelRatio,
  );
  if (socket !== null && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(report));
  }
  const center = report.samples.find((sample) => sample.name === 'center');
  logLine(
    `帧 ${report.frame}：hash=${report.hash} center=(${center?.r},${center?.g},${center?.b},${center?.a}) ` +
      `canvas=${report.canvasWidth}x${report.canvasHeight} rect=${report.rectWidth}x${report.rectHeight} ` +
      `display=${report.display}`,
  );
}

function drawFrame(frame: SpikeFrameMessage): void {
  const bytes = decodeBase64(frame.pixels);
  const expected = frame.width * frame.height * 4;
  if (frame.width !== CANVAS_SIZE || frame.height !== CANVAS_SIZE || bytes.length !== expected) {
    logLine(`帧 ${frame.frame} 尺寸不符：${frame.width}x${frame.height}，字节 ${bytes.length}/${expected}`, true);
    return;
  }
  if (selftest === 'local-red') {
    // 自检开关 local-red：网页本地画红（正常模式禁止的行为，只用来证明同源对账能识破）。
    context!.fillStyle = '#ff0000';
    context!.fillRect(0, 0, canvas.width, canvas.height);
  } else {
    // 唯一合法的绘制：把服务端推来的原生读回帧原样上屏。
    context!.putImageData(new ImageData(new Uint8ClampedArray(bytes), frame.width, frame.height), 0, 0);
  }
  receivedFrames += 1;
  frameStatusEl.textContent = `已收到 ${receivedFrames} 帧（最近帧序号 ${frame.frame}，路径 ${frame.path}）`;
  if (selftest === 'no-line') {
    // 自检开关 no-line：回传通道断链——页面照收帧并上屏，但从不回传，服务端必须 6000ms 超时判红。
    // （页面仍连接、仍握手：这样"断链"红态与"页面根本没起来"能区分开，红态读数不会混成一个原因。）
    return;
  }
  sendReport(frame);
}

function handleMessage(raw: string): void {
  let parsed: { type?: string };
  try {
    parsed = JSON.parse(raw) as { type?: string };
  } catch {
    return;
  }
  if (parsed.type === 'spike-frame') {
    drawFrame(parsed as SpikeFrameMessage);
    return;
  }
  if (parsed.type === 'welcome') {
    if (helloSeen) {
      return;
    }
    helloSeen = true;
    const hello: SpikeHelloMessage = {
      type: 'spike-hello',
      webgpu: webgpuAvailable,
      userAgent: navigator.userAgent,
    };
    socket?.send(JSON.stringify(hello));
    logLine(`已握手：webgpu=${webgpuAvailable} selftest=${selftest.length === 0 ? '-' : selftest}`);
  }
}

function connect(): void {
  if (selftest === 'no-line') {
    logLine('自检开关 selftest=no-line：回传通道断链（仍连接并握手，但不回传任何报告）');
  }
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = `${scheme}://${location.host}/ws`;
  setStatus('连接中…', 'pending');
  const ws = new WebSocket(url);
  socket = ws;
  ws.onopen = () => {
    setStatus('已连接', 'ok');
    ws.send(JSON.stringify({ type: 'hello', protocolVersion: 1 }));
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

connect();
