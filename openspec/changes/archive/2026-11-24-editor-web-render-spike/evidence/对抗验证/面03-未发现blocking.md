# 面 03：`CoreWebView2.CapturePreviewAsync` —— 未发现阻断项

## 一、API 真实

WebView2 包（本仓库 `Scripting.Managed.csproj` 钉的 `Microsoft.Web.WebView2` 1.0.4370.26）的 XML 文档：

```text
C:\Users\76956\.nuget\packages\microsoft.web.webview2\1.0.4370.26\lib\net462\Microsoft.Web.WebView2.Core.xml
1581:         <member name="M:Microsoft.Web.WebView2.Core.CoreWebView2.CapturePreviewAsync(Microsoft.Web.WebView2.Core.CoreWebView2CapturePreviewImageFormat,System.IO.Stream)">
```

## 二、仓库里真实使用

`Scripting\Managed\Editor\EditorWindow.cs`：

```csharp
466:                 var stream = new MemoryStream();
467:                 m_DockCaptureStream = stream;
468:                 m_DockCaptureTask = m_DockController.CoreWebView2.CapturePreviewAsync(
469:                     CoreWebView2CapturePreviewImageFormat.Png, stream);
```

调用对象是 dock 控制器（`m_DockController`），不是主面板控制器；方法名、枚举 `CoreWebView2CapturePreviewImageFormat.Png`、`Stream` 参数都与 XML 文档一致。

## 三、归档里的实测先例

`openspec/changes/archive/2026-11-24-editor-asset-browser/evidence/4.2-视觉核对.md` 原文：

> 抓窗方法：`CASCADED_EDITOR_SELFTEST=capture-dock=<png>`，dock 第二 WebView2 控制器 `CoreWebView2.CapturePreviewAsync` 落盘（不依赖前景，被挡也能抓）。**全程未置前/未最小化/未移动用户窗口**（AGENTS §2，2026-11-24 用户明令禁止前置窗口）。正控通过：PNG 真实渲染出资产行（非空白），证明 CapturePreviewAsync 对 WebView2 合成内容有效（PrintWindow 未采用，其对 WebView2 未验证）。

同目录的 `4.2-截图.png`、`4.2-刷新前.png`、`4.2-刷新后.png`、`4.2-空态.png` 都在。`design.md` 写的“editor-asset-browser 4.2 实测”对得上。

## 四、注记（不构成阻断）

- XML 文档同一条目写明：在第一次 `ContentLoading` 之前调用会失败；spike 的新控制器需要等页面内容加载后再抓，这属于实现细节，不是 API 不存在。
- 现有代码只把 `CapturePreviewAsync` 接到 dock 控制器；spike 的原型控制器要自己加一条抓窗路径。API 本身可用，`design.md` D4/spec 的“CapturePreviewAsync 抓窗”有真实依据。

## 五、结论

本面未发现阻断项。`CoreWebView2.CapturePreviewAsync` 真实存在，仓库有使用，归档有实测正控记录。
