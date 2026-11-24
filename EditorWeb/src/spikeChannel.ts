// 渲染原型的**纯**通道逻辑（editor-web-render-spike 1.2）：不碰 DOM，只做"帧字节 ↔ 对账数据"的换算。
//
// 单独成文件的目的：① spike.ts 只留 DOM/WebSocket 胶水；② 这条通道可以脱离浏览器做 dry-run ——
// 用固定帧（frame=7、256×256、背景绿 + 中心红）跑一遍 `decodeBase64 → buildReport`，把
// 帧序号 / 整图哈希 / 七点采样 / canvas 读数这四元组逐字打出来核对（验收里的"回传通道 dry-run"）。
//
// 算法必须与 C++ 侧一致：整图哈希 = FNV-1a 32 位、逐字节、RGBA8 行主序、8 位小写十六进制
// （C++ 实现见 D3D12RenderBackend/private/WebRenderSpike/D3D12WebRenderSpike.cpp 的 ComputeSpikeHash）。

/** 特征图案固定尺寸（D2 写死 256×256，原生共享纹理与 canvas 同尺寸）。 */
export const CANVAS_SIZE = 256;

/** 服务端推来的帧（C++ 读到共享纹理像素 → 服务 → 本页）。 */
export interface SpikeFrameMessage {
  type: 'spike-frame';
  frame: number;
  width: number;
  height: number;
  /** 实际路径。本批固定 cpu-readback（零拷贝是 Non-goal）。 */
  path: string;
  /** RGBA8 紧凑字节的 base64。 */
  pixels: string;
  /** 原生侧对同一份读回字节算的整图哈希（8 位小写十六进制）。 */
  hash: string;
}

/** 多点采样的一项：坐标 + 该点 RGBA（来自本页 getImageData）。 */
export interface SpikeSample {
  name: string;
  x: number;
  y: number;
  r: number;
  g: number;
  b: number;
  a: number;
}

/** canvas 的布局类读数（显示红态靠它们判）。 */
export interface CanvasReadings {
  width: number;
  height: number;
  rectX: number;
  rectY: number;
  rectWidth: number;
  rectHeight: number;
  visibility: string;
  display: string;
}

/** 回传给服务端的对账数据。 */
export interface SpikeReportMessage {
  type: 'spike-report';
  frame: number;
  width: number;
  height: number;
  canvasWidth: number;
  canvasHeight: number;
  rectX: number;
  rectY: number;
  rectWidth: number;
  rectHeight: number;
  visibility: string;
  display: string;
  hash: string;
  hashAlgorithm: 'fnv1a32';
  webgpu: number;
  devicePixelRatio: number;
  samples: SpikeSample[];
}

/** 页面连上并准备好后先报一次（服务端据此判断"页面已就绪"，再开始推帧）。 */
export interface SpikeHelloMessage {
  type: 'spike-hello';
  webgpu: number;
  userAgent: string;
}

/**
 * 采样点（与 C++ 侧 `kSpikeSamplePoints` 逐点一致，两边各写一份、由同源对账兜底）：
 * 背景（绿）、中心（红三角内）、四角标记（红）。全部落在特征图案里位置固定的区域，
 * 不取帧序号位（那个随帧变化）。
 */
export const SAMPLE_POINTS: ReadonlyArray<{ name: string; x: number; y: number }> = [
  { name: 'bg', x: 20, y: 20 },
  { name: 'bg2', x: 236, y: 128 },
  { name: 'center', x: 128, y: 128 },
  { name: 'corner-tl', x: 8, y: 8 },
  { name: 'corner-tr', x: 248, y: 8 },
  { name: 'corner-bl', x: 8, y: 248 },
  { name: 'corner-br', x: 248, y: 248 },
];

/** 整图哈希算法（C++ 侧 `ComputeSpikeHash` 必须逐位一致）：FNV-1a 32 位，逐字节，RGBA8 行主序。 */
export function fnv1a32(bytes: Uint8ClampedArray | Uint8Array): string {
  let hash = 0x811c9dc5;
  for (let index = 0; index < bytes.length; index += 1) {
    hash ^= bytes[index];
    // 乘 16777619 用 Math.imul：32 位整数乘，避免双精度乘法丢低位。
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash.toString(16).padStart(8, '0');
}

/** base64 → 字节（帧像素）。 */
export function decodeBase64(text: string): Uint8Array {
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

/**
 * 用 canvas 自读回的像素与布局读数生成回传消息。`source` 必须恰好是 canvas 的
 * RGBA8 紧凑字节（`getImageData(0, 0, width, height)` 的 `data`），采样按 `canvas.width` 行主序取。
 */
export function buildReport(
  frame: { frame: number; width: number; height: number },
  canvas: CanvasReadings,
  source: Uint8ClampedArray,
  webgpu: number,
  devicePixelRatio: number,
): SpikeReportMessage {
  const samples: SpikeSample[] = SAMPLE_POINTS.map((point) => {
    const offset = (point.y * canvas.width + point.x) * 4;
    return {
      name: point.name,
      x: point.x,
      y: point.y,
      r: source[offset],
      g: source[offset + 1],
      b: source[offset + 2],
      a: source[offset + 3],
    };
  });
  return {
    type: 'spike-report',
    frame: frame.frame,
    width: frame.width,
    height: frame.height,
    canvasWidth: canvas.width,
    canvasHeight: canvas.height,
    rectX: canvas.rectX,
    rectY: canvas.rectY,
    rectWidth: canvas.rectWidth,
    rectHeight: canvas.rectHeight,
    visibility: canvas.visibility,
    display: canvas.display,
    hash: fnv1a32(source),
    hashAlgorithm: 'fnv1a32',
    webgpu,
    devicePixelRatio,
    samples,
  };
}
