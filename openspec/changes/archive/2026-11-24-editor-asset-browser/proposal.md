# Proposal: editor-asset-browser

## Why

资产层地基已落地（`csharp-resource-asset-foundation`：注册表门面 + C# 贴图/网格资产 + 自读加载），但编辑器里**看不到资产**——资源根目录（CAAssets）下躺着着色器库与导入产物，没有任何界面展示它们。这是路线图第一梯队④「编辑器场景编辑 / 资源导入 UI」的第一块，也是把资产层用 UI 验一遍的纵向切片。本批做**只读资产浏览器**：浏览 ② 阶段（编辑器资产）共享文件，刷新可见。方向上下文（三阶段模型 / ② 共享真相 / 代码型程序集 / 显示提供者）见 design.md，源自 2026-11-23 探索讨论。

## What Changes

- **底部通栏独立资产浏览器 dock**（2026-11-24 用户裁定取惯例布局，与右检查器分离）：独立页面 `EditorWeb/assets.html` + `src/assets.ts`（纯 TS），由底部 dock 的第二个 WebView2 控制器加载；列出资源根下的 ② 文件，列＝相对路径 / 种类（扩展名映射）/ 大小 / 修改时间 / 是否已注册；刷新按钮重新枚举；空根显示空态而非报错。
- **原生壳三分区**（D9）：单壳窗口内场景视口左上 + 对象检查器右上 + 资产浏览器底部通栏；`EditorWindow.ApplyLayout` 重算三分区，`EditorService` 加第二 WebView2 控制器，dock 高 `CASCADED_EDITOR_DOCK_HEIGHT` 可配（**默认 240**，照面板宽先例，运行期固定）；第二控制器失败/关闭独立处置 + 红态开关 `no-dock-controller`；assets.html 缺失明确错误页；set-size 高度判据改「高 > dock 高」；交互资源根覆盖 `CASCADED_EDITOR_ASSET_ROOT` + 抓窗钩子 `capture-dock`（CapturePreviewAsync）；既有 `TestEditorWindowPanel` 布局断言随三分区更新（像素判据不放松 + 视口高显式断言）。
- **协议增量（仍 v1，加法）**：`list-assets` 命令（非对象作用域，返回资产条目树）+ `assets-changed` 事件 + `AssetEntry` 类型。先改 `Scripting/Managed/Editor/PROTOCOL.md`（唯一事实源），再镜像到 `EditorWeb/src/protocol.ts` 与 `EditorProtocol.cs`。条目带稳定 `nodeId`（v1 = 相对路径），树形 children 字段为 v2 投影预留。
- **EditorService 资产枚举**：绑定 `IResourceRegistry` 门面（编号 13，经 Host 助手）取 `GetResourceRootPath` + `HasResource`；C# 侧 `Directory.EnumerateFiles` 扫根目录，`FileInfo` 取大小/时间，扩展名定种类，`HasResource` 定已注册标志。**不新增任何原生接口**（门面现成）。枚举在主线程命令执行内，监听线程仍只做 I/O。
- **显示提供者抽象**（C# 编辑器侧）：`IDisplayProvider` 接口 + 按种类注册表 + `DefaultFileDisplayProvider`（一文件一条目）。接口形状**资产中心**（输入资产/导入单元，输出显示树），v1 退化为文件=资产 1:1，为 v2 程序集展开 / 衍生物归组留口。**不改 IResource 核心接口**（显示是编辑器关注点）。
- **tester 第 23 用例 `TestEditorAssetBrowser`** + **截图视觉验收**（编辑器任务硬要求）：C++ 侧断言 list-assets 结果与真实文件一致；真实打开界面截图 + read_image 核对资产列表渲染。

## Capabilities

### New Capabilities

- `editor-asset-browser`: 编辑器只读资产浏览——② 文件枚举与列展示、list-assets/assets-changed 协议、显示提供者抽象（资产中心、按种类注册）、线程边界与失败可见、视觉 + 读回验收。

### Modified Capabilities

- `editor-window-panel`: ① 命令协议要求扩命令集（加 `list-assets`，非对象作用域）、事件集（加 `assets-changed`）、错误码集（加 `E_ASSET_ROOT`）——生效规范原为封闭枚举（五命令/六错误码），加法扩展即修改该要求；既有五条命令/六条错误码形状语义不变，协议版本仍 1。② **单窗口分栏壳要求改三分区**（底部通栏资产浏览器 dock + 第二 WebView2 控制器，dock 高可配）——仍单壳窗口、线程边界不变；既有布局断言随三分区更新（§7 不放松）。

## Impact

- **代码**：`Scripting/Managed/Editor/PROTOCOL.md`（协议增量）；`EditorProtocol.cs`（AssetEntry/list-assets/assets-changed 构建与校验）；`EditorService.cs`（门面绑定 + 枚举 + 命令 + 事件 + 第二 WebView2 控制器 + 三分区 SyncLayout + 读数行 dock 矩形）；`EditorWindow.cs`（ApplyLayout 三分区 + dock 高可配）；新增 `Scripting/Managed/Editor/AssetDisplay.cs`（IDisplayProvider + 注册表 + 默认提供者）；`Host.cs`（门面绑定助手）；`EditorWeb/src/protocol.ts` + 新增 `assets.html`/`src/assets.ts`（底部 dock 独立页面）+ `index.html`/`main.ts`（移出资产区）；`Test/GPUBackendTester/Main.cpp`（第 23 用例 + TestEditorWindowPanel 布局断言更新）。
- **不动**：GeneralResources / 门面接口 / 生成器 / 既有绑定产物（逐字节不变）；IResource 核心接口；CoreTests；窗口属性面板的对象检查器/改标题/改尺寸/撤销/scene view 功能（布局断言随三分区更新，功能保留）。
- **依赖**：复用 `csharp-resource-asset-foundation` 的门面（编号 13）与资源根；无新外部依赖。

## Non-goals

- **程序集→源投影**（shLib 展开成源文件树）：v2，需 slang 反射 + 源映射元数据。
- **衍生物归组**（模型 1→N：LOD/proxy/impostor/BLAS）：v2+，需溯源/身份链。
- **缩略图**（贴图预览）：v3，需像素懒加载。
- **导入按钮 / 源文件解码器**（PNG/OBJ）：另立批。
- **溯源元数据 / 内容哈希 / 依赖记录**（身份重设计）：另立批；本批 nodeId 用相对路径，不依赖身份链。
- **① 源文件浏览 / ③ 运行时资产**：不浏览。
- **USD 依赖**：不引（探索结论：太重）。
- **资产编辑 / 删除 / 重命名**：本批只读。
- **多窗自由 dock / 拖拽分隔条调高**：后续 dock 框架 change（dock 树驱动布局、面板可跨窗搬运）；本批底部通栏固定三分区 + 独立面板单元是其铺垫，dock 高运行期可配固定。