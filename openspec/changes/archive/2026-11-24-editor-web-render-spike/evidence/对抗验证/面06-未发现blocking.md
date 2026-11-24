# 面 06：`getImageData` / host object / WebSocket 回传 —— 未发现阻断项

## 一、`canvas.getImageData` 真实，但只在 2D 上下文

`EditorWeb\node_modules\@typescript\typescript-win32-x64\lib\lib.dom.d.ts`（本仓库 `typescript` 7.0.2 的类型库）：

```text
10350:     getImageData(sx: number, sy: number, sw: number, sh: number, settings?: ImageDataSettings): ImageData;
10352:     putImageData(imageData: ImageData, dx: number, dy: number): void;
10436: interface CanvasRenderingContext2D extends CanvasCompositing, ... CanvasImageData, ...
```

`GPUCanvasContext`（WebGPU canvas 的上下文）只有：

```text
15339: interface GPUCanvasContext {
15345:     readonly canvas: HTMLCanvasElement | OffscreenCanvas;
15351:     configure(configuration: GPUCanvasConfiguration): void;
15357:     getConfiguration(): GPUCanvasConfiguration | null;
15363:     getCurrentTexture(): GPUTexture;
```

没有 `getImageData` / `putImageData`。

注记（不构成阻断）：`design.md` D3 的路径①是“共享纹理→WebGPU canvas”，路径③是“CPU 读回→`canvas.putImageData`”。`putImageData`/`getImageData` 只在 2D 上下文；若 1.1 最终选路径①（WebGPU canvas），网页侧要读中心像素不能直接对同一个 WebGPU canvas 调 `getImageData`，得先把画面画到 2D canvas 再读，或改用 `CapturePreviewAsync` 抓窗。`spec.md` 的判据是“getImageData 回传 C++ 断言**或** CapturePreviewAsync 抓窗 read_image 核对”，有第二条兜底，所以不是阻断；`tasks.md` 1.2 把 `getImageData` 写成无条件步骤，实现到路径①时需要按这条注记调整。

## 二、`CoreWebView2.AddHostObjectToScript` 真实

WebView2 包 XML：

```text
C:\Users\76956\.nuget\packages\microsoft.web.webview2\1.0.4370.26\lib\net462\Microsoft.Web.WebView2.Core.xml
1643:         <member name="M:Microsoft.Web.WebView2.Core.CoreWebView2.AddHostObjectToScript(System.String,System.Object)">
1647:             Host objects are exposed as host object proxies via <c>window.chrome.webview.hostObjects.{name}</c>. Host object proxies are promises and will resolve to an object representing the host object. Only the COM visible objects/properties/methods can be accessed from script.
1649:             The app can control which part of .NET objects are exposed using <see cref="T:System.Runtime.InteropServices.ComVisibleAttribute" />.
```

API 真实；要求对象 COM 可见（.NET 侧用 `[ComVisible(true)]` 等）。本仓库当前没有任何 `AddHostObjectToScript` 调用（全仓零命中），spike 若走 host object 是新增代码；`Scripting.Managed.csproj` 是 net8.0，具备 COM 可见对象的基础，但工件没有把“新增 COM 可见宿主对象类”写进任务，属于实现细节。

## 三、`CoreWebView2.WebMessageReceived` 真实

同 XML：

```text
1494:         <member name="E:Microsoft.Web.WebView2.Core.CoreWebView2.WebMessageReceived">
1496:             WebMessageReceived is raised when the <see cref="P:Microsoft.Web.WebView2.Core.CoreWebView2Settings.IsWebMessageEnabled" /> setting is set and the top-level document of the WebView runs <c>window.chrome.webview.postMessage</c> or <c>window.chrome.webview.postMessageWithAdditionalObjects</c>.
```

`IsWebMessageEnabled` 默认 true（同 XML 6749 行附近写明“默认值是 true”）。API 真实；本仓库零使用。

## 四、WebSocket 回传有现成先例

前端：

```text
EditorWeb\src\main.ts
37: let socket: WebSocket | null = null;
185:   const ws = new WebSocket(url);

EditorWeb\src\assets.ts
33: let socket: WebSocket | null = null;
170:   const ws = new WebSocket(url);
```

tester 侧也有进程内最小 WebSocket 客户端：

```text
Test\GPUBackendTester\Main.cpp
1: // editor-window-panel 3.1：进程内最小 WebSocket 客户端（Winsock 手写 RFC6455 客户端握手 + 掩码文本帧，
3499: // ---- editor-window-panel 3.1 / 3.2：进程内最小 WebSocket 客户端与像素/屏幕捕获助手 ----
3561: 				"GET /ws HTTP/1.1\r\nHost: localhost:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
```

`design.md` D3 的路径③“经 WebView2 host object / WebSocket 传字节”两条都有真实基础：host object 是 WebView2 官方 API（新增使用），WebSocket 是仓库已有客户端/服务端通信方式。

## 五、结论

本面未发现阻断项。`getImageData`（2D 上下文）、`AddHostObjectToScript`、`WebMessageReceived` 都是真实 API；WebSocket 有现成先例。唯一注记是 `getImageData` 不能直接读 WebGPU canvas，路径①下要用 2D 中转或 CapturePreviewAsync 兜底，而 spec 的“或”已经留了兜底。
