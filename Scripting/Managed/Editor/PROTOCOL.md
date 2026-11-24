# 编辑器命令协议 v1

本文件是编辑器服务（`Scripting/Managed/Editor/`）与面板前端（`EditorWeb/src/protocol.ts`）两侧类型的
**唯一事实源**。C# 记录与 TS 类型逐字段镜像本文件；改协议必须先改本文件，再改两侧（任务判据：两侧
字段名与本文件逐条 grep 对拍）。协议版本号随不兼容改动递增；v1 是首切片。

## 1. 传输与会话

- 端点：`ws://localhost:<port>/ws`（`<port>` 默认 18390，`CASCADED_EDITOR_PORT` 可配）。
- 帧：**文本帧**，UTF-8 编码的 JSON 对象。二进制帧不支持（收到即按 `E_BAD_MESSAGE` 处理并断开）。
- 一个连接一条会话：客户端先发 `hello`，收到 `welcome` 之后才能发 `command`；`hello` 之前到达的
  `command` 按 `E_BAD_MESSAGE` 处理并断开。
- 事件（`event`）可随时到达，与命令结果（`result`）交错；两侧都按 `requestId` 配对结果，事件不占
  `requestId`。**`result` 只发给发起该 `requestId` 的那条连接；`event` 广播给所有已连接客户端**。
- 线程边界（对客户端的可见语义）：命令在**引擎主线程的下一次 Tick** 执行；`result` 在命令执行完成
  之后才发出。因此"收到 ok"等价于"引擎侧改动已生效"，客户端不得把本地显示当成成功。

## 2. 消息形状

所有消息都是带 `type` 字段的 JSON 对象。字段名区分大小写。

### 2.1 C→S `hello`

```json
{ "type": "hello", "protocolVersion": 1 }
```

### 2.2 S→C `welcome`

```json
{ "type": "welcome", "protocolVersion": 1,
  "objects": [ { "id": 1, "title": "编辑器窗口", "width": 1280, "height": 720 } ] }
```

`objects` 是当前仍有效的对象快照（首切片 0 或 1 个）。

### 2.3 C→S `command`

```json
{ "type": "command", "requestId": 7, "command": "set-title", "objectId": 1,
  "args": { "title": "新标题" } }
```

- `requestId`：整数，由客户端分配，连接内不要求唯一，但客户端应保证在收到对应结果前不重复，否则
  结果无法区分。服务端原样回填。
- `command`：`get-object` / `list-objects` / `set-title` / `set-size` / `undo` / `list-assets`。
- `objectId`：对象编号。对象作用域命令（`get-object` / `set-title` / `set-size` / `undo`）必填；
  **非对象作用域命令**（`list-objects` / `list-assets`）不使用该字段（缺省即可，给了也被忽略）。
- `args`：
  - `get-object`：无（可省略）。
  - `list-objects`：无（可省略）。
  - `list-assets`：无（可省略）—— 资产枚举不接受参数：根路径来自资源注册表门面，没有分页/过滤参数。
  - `set-title`：`{ "title": <string> }`。
  - `set-size`：`{ "width": <int>, "height": <int> }`，两者都必须 **> 0**；宽度还必须是
    **视口宽**（即 `宽度 > 面板宽`，面板宽默认 320、`CASCADED_EDITOR_PANEL_WIDTH` 可配），高度必须
    **> dock 高**（即 `高度 > 底部资产浏览器 dock 的高`，dock 高默认 240、`CASCADED_EDITOR_DOCK_HEIGHT`
    可配），否则 `E_INVALID_ARG`（杜绝视口宽 ≤ 0 或视口高 = 高 − dock 高 ≤ 0）。
  - `undo`：无（可省略）。

### 2.4 S→C `result`

成功：

```json
{ "type": "result", "requestId": 7, "ok": true, "value": { ... } }
```

失败：

```json
{ "type": "result", "requestId": 7, "ok": false,
  "error": { "code": "E_OBJECT_INVALIDATED", "message": "对象 1 已失效（目标窗口已关闭）" } }
```

`value` 的形状：

| 命令 | `value` |
|---|---|
| `get-object` | `{ "object": { id, title, width, height } }` |
| `list-objects` | `{ "objects": [ { id, title, width, height }, ... ] }` |
| `set-title` | `{ "object": { id, title, width, height } }`（执行后的状态） |
| `set-size` | `{ "object": { id, title, width, height } }`（执行后的状态） |
| `undo` | `{ "object": { id, title, width, height } }`（撤销后的状态） |
| `list-assets` | `{ "assets": [ AssetEntry, ... ] }`（条目形状见 2.6；目录存在但无文件时为空数组） |

对象快照 `object` 的字段类型：`id`、`width`、`height` 为整数，`title` 为字符串。
资产条目 `AssetEntry` 的字段与取值范围见 2.6。

### 2.5 S→C `event`

```json
{ "type": "event", "event": "object-changed", "objectId": 1, "fields": { "title": "新标题" } }
```

- `objectId`：`object-changed` / `object-invalidated` **必填**；`assets-changed` **不带该字段**（资产
  变化不是某个对象的变化，客户端不得因缺 `objectId` 而拒绝这条事件）。
- `object-changed`：`fields` 只含本次变化的字段（`title` / `width` / `height` 的子集）。来源：命令
  执行成功，或窗口尺寸回调（外部改窗口尺寸）。`undo` 重放也产生它。
- `object-invalidated`：`fields` 为空对象 `{}`。对象已移出注册表；此后同编号命令回
  `E_OBJECT_INVALIDATED`。
- `assets-changed`：广播给所有已连接客户端，`fields` 为 `{ "added": <int>, "removed": <int> }` ——
  本次资产枚举相对**上一次枚举快照**的条目增删数（按 `path` 比对；首次枚举没有上一条快照，不发该
  事件）。来源：某次 `list-assets` 枚举发现与上次快照有差异（v1 没有文件监听定时器，不主动轮询）。
  客户端收到后应重发 `list-assets` 重拉列表；`added` / `removed` 只用于提示"有变化"，不携带条目本体。

```json
{ "type": "event", "event": "assets-changed", "fields": { "added": 1, "removed": 0 } }
```

### 2.6 资产条目 `AssetEntry`

`list-assets` 的 `value.assets` 里每一项的形状（字段名区分大小写，七个字段全部必填）：

```json
{ "nodeId": "CSharpProbe/Probe.tex", "path": "CSharpProbe/Probe.tex", "kind": "texture",
  "sizeBytes": 142, "modifiedUtc": "2024-03-04T05:06:07.0000000Z", "registered": true,
  "children": [] }
```

| 字段 | 类型 | 取值 |
|---|---|---|
| `nodeId` | string | 稳定条目编号。**v1 = `path` = 相对资源根的正斜杠路径**（见下）；v2 溯源落地后仍保持稳定 |
| `path` | string | 相对资源根的正斜杠路径（例：`CSharpProbe/Probe.tex`） |
| `kind` | string | 种类（由扩展名映射，大小写不敏感）：`.shLib`→`shader-library`、`.tex`→`texture`、`.mesh`→`mesh`、`.res`→`resource`、其它→`file` |
| `sizeBytes` | int | 文件字节数（`FileInfo.Length`，非负整数） |
| `modifiedUtc` | string | 修改时间，**ISO 8601 UTC**（`FileInfo.LastWriteTimeUtc` 的 `"o"` 格式，例：`2024-03-04T05:06:07.0000000Z`） |
| `registered` | bool | 该路径在资源注册表里**已有条目**（门面 `HasResource(path)` 读数）；未注册的文件同样列出，只是本字段为 `false` |
| `children` | AssetEntry[] | 子条目。**v1 恒为空数组**；为 v2 的程序集展开 / 衍生物归组预留（届时父条目在 `children` 里挂子条目，`nodeId` 仍稳定） |

约定：

- 资产枚举只做**只读**：不导入、不改文件、不注册/释放资源条目。
- 目录存在但无文件 = 合法空态：`ok=true` 且 `assets` 为空数组；门面未绑、根为空串、根目录不存在、
  枚举 IO 错误 = `ok=false` 且错误码 `E_ASSET_ROOT`（**不得**用空数组冒充成功）。

## 3. 错误码

| 码 | 触发条件 | 是否入引擎队列 |
|---|---|---|
| `E_PROTOCOL_VERSION` | `hello.protocolVersion` 不是 1（缺失按 0 处理）。服务端发失败 `result`（`requestId = 0`）后**关闭连接** | 否 |
| `E_UNKNOWN_OBJECT` | `objectId` 从未存在过 | 否（零原生调用） |
| `E_OBJECT_INVALIDATED` | `objectId` 曾存在、现已失效 | 否（零原生调用） |
| `E_INVALID_ARG` | 参数非法：`set-size` 的宽或高 ≤ 0、宽 ≤ 面板宽、类型不是整数；`set-title` 的 `title` 不是字符串 | 否（零原生调用） |
| `E_UNDO_EMPTY` | `undo` 时该对象撤销槽为空 | 否（零原生调用） |
| `E_BAD_MESSAGE` | 形状错误：不是 JSON 对象、缺 `type`、未知 `type`、缺 `requestId`、未知 `command`、缺必需 `objectId`、`args` 形状不符 | 否（零原生调用） |
| `E_ASSET_ROOT` | 资产根不可用：资源注册表门面未绑定、根路径为空串、根目录不存在、枚举 IO 错误（含读文件信息失败）、门面底层抛错（错误码 1014）。`message` 必须点名是哪一种原因 | 是（`list-assets` 的枚举在引擎主线程命令执行内完成；错误在该 `requestId` 的结果里回） |

约定：

- **握手错误**与**无法解析出 `requestId` 的错误**用 `requestId = 0` 回 `result`（0 保留给这类错误；
  客户端分配的 `requestId` 应 ≥ 1）。
- 形状错误在任何引擎调用之前判定：被 `E_BAD_MESSAGE` / `E_INVALID_ARG` / `E_UNKNOWN_OBJECT` /
  `E_OBJECT_INVALIDATED` 拒绝的命令，引擎绑定调用次数增量为 **0**（原生调用计数器读数为判据）。
- `E_ASSET_ROOT` 的错误**必须在处理内产生并回到该 `requestId`**：不许让枚举异常穿出到引擎外侧的
  兜底捕获（那会让客户端收不到任何 `result` 而静默挂起）。
- 同一连接内错误不关闭连接（`E_PROTOCOL_VERSION` 与二进制帧除外）。

## 4. 对象与撤销语义

- 编号：从 1 开始的单调递增整数，**不复用**；服务创建的目标窗口登记为编号 1。壳/面板不是可编辑对象。
- 失效触发：目标窗口 close 回调，或执行命令前发现租约已失效。失效立即移出注册表并向所有已连接
  客户端广播一次 `object-invalidated`。
- 撤销槽：每对象**单层**。`set-title` / `set-size` 成功执行时记录（命令, 旧值），`undo` 取槽重放旧值
  （走同一执行路径、产生 `object-changed`）并清槽。撤销非 `set-*` 的外部变化（例如窗口尺寸回调）不在
  v1 范围内；`undo` 只重放最近一次由命令成功写入的值。
- 对象失效后：`undo` 回 `E_OBJECT_INVALIDATED`，零原生调用。

## 5. 与面板 UI 的对应

**右侧对象检查器**（`index.html` + `main.ts`）的控件与命令一一对应：连接状态 → `hello`/`welcome`；
标题编辑+应用 → `set-title`；宽高编辑+应用 → `set-size`；撤销按钮 → `undo`；事件日志 → `event`；
失效横幅 → `object-invalidated`（此后禁用输入）。面板启动时先发 `hello`，用 `welcome.objects` 渲染初值。

**底部通栏资产浏览器 dock**（`assets.html` + `assets.ts`，editor-asset-browser）：这是**独立面板单元**
——底部通栏的第二块 WebView2 由编辑器服务的第二个控制器加载，与右侧对象检查器分离（仍同一个原生壳
窗口）。dock 内标题 + 表格 → `list-assets`（`value.assets` 渲染五行：路径 / 种类 / 大小 / 修改时间 /
已注册）；点**刷新**按钮 → 重发 `list-assets` 重拉；收到 `assets-changed` 事件 → 重发 `list-assets`
重拉（事件只提示有变化，条目本体靠重拉取）；空根（`assets` 为空数组）→ dock 显示空态文案，不是报错；
`E_ASSET_ROOT` → dock 显示错误码与该消息并保留表格外的错误提示（不得显示成空列表）。dock 高由
`CASCADED_EDITOR_DOCK_HEIGHT` 配置（默认 240，运行期固定）。

## 6. 示例往返

```text
C→S  {"type":"hello","protocolVersion":1}
S→C  {"type":"welcome","protocolVersion":1,"objects":[{"id":1,"title":"编辑器窗口","width":1280,"height":720}]}
C→S  {"type":"command","requestId":1,"command":"set-title","objectId":1,"args":{"title":"场景视图"}}
S→C  {"type":"event","event":"object-changed","objectId":1,"fields":{"title":"场景视图"}}
S→C  {"type":"result","requestId":1,"ok":true,"value":{"object":{"id":1,"title":"场景视图","width":1280,"height":720}}}
C→S  {"type":"command","requestId":2,"command":"undo","objectId":1}
S→C  {"type":"result","requestId":2,"ok":true,"value":{"object":{"id":1,"title":"编辑器窗口","width":1280,"height":720}}}
C→S  {"type":"command","requestId":3,"command":"undo","objectId":1}
S→C  {"type":"result","requestId":3,"ok":false,"error":{"code":"E_UNDO_EMPTY","message":"对象 1 的撤销槽为空"}}
```

事件与结果的先后不保证固定（实现按"先执行、后回结果"的顺序发送，事件可能在结果之前到达同一连接）；
客户端以 `requestId` 配对结果、以事件更新显示。

## 7. 渲染原型通道（editor-web-render-spike，复用本协议的同一条 WebSocket）

渲染原型页（`spike.html` + `spike.ts`）与 C++ 驱动共用编辑器服务的 `/ws`：**帧从 C++ 驱动进、经服务转发
给原型页；原型页的对账数据回服务**。三类消息都带 `type`、都不占 `requestId`（不是命令，执行语义不在
主线程命令表里；转发与对账在服务的主线程 Tick 内做）。只有 `CASCADED_EDITOR_SPIKE=web-render` 门控开启时
才接受这三类消息；门控关闭时按 `E_BAD_MESSAGE`（未知消息类型）处理，既有面板行为不变。

### 7.1 C→S `spike-hello`（原型页就绪）

```json
{ "type": "spike-hello", "webgpu": 1, "userAgent": "Mozilla/5.0 ..." }
```

原型页收到 `welcome` 后立即发一次。服务收到后把 `spikePage=1` 写进读数行 —— C++ 驱动据此判断"页面已就绪，
可以开始推帧"，避免把最早几帧推给一个还没连上的页面。

### 7.2 C→S `spike-frame`（原生帧，服务转发给原型页）

```json
{ "type": "spike-frame", "frame": 1, "width": 256, "height": 256, "path": "cpu-readback",
  "hash": "1a2b3c4d", "pixels": "<RGBA8 紧凑字节的 base64>",
  "samples": [ { "name": "bg", "r": 0, "g": 255, "b": 0, "a": 255 },
               { "name": "center", "r": 255, "g": 0, "b": 0, "a": 255 } ] }
```

- `pixels` 是 C++ 从共享纹理读回的 RGBA8 紧凑字节（256×256×4 = 262144 字节 → base64 349528 字符）。
- `hash` 是 C++ 对同一份字节算的整图哈希（FNV-1a 32 位，逐字节，8 位小写十六进制）；原型页用同一算法
  对 `getImageData` 结果算一次，服务端比对两者。
- `samples` 是 C++ 在同一份读回字节上取的采样点（与回传的七点同名同坐标）；服务端逐点比对网页回传值。
- 服务收到后**转发给除发送者以外的所有已连接客户端**（即原型页），并登记"帧 N 的原生哈希 + 采样 +
  6000ms 回传截止时间"。

### 7.3 C→S `spike-report`（原型页对账数据）

```json
{ "type": "spike-report", "frame": 1, "width": 256, "height": 256,
  "canvasWidth": 256, "canvasHeight": 256,
  "rectX": 16, "rectY": 178, "rectWidth": 256, "rectHeight": 256,
  "visibility": "visible", "display": "block",
  "hash": "1a2b3c4d", "hashAlgorithm": "fnv1a32", "webgpu": 1, "devicePixelRatio": 1,
  "samples": [ { "name": "bg", "x": 20, "y": 20, "r": 0, "g": 255, "b": 0, "a": 255 },
               { "name": "center", "x": 128, "y": 128, "r": 255, "g": 0, "b": 0, "a": 255 } ] }
```

- `samples` 至少含 `bg` / `bg2` / `center` / `corner-tl` / `corner-tr` / `corner-bl` / `corner-br` 七点
  （位置与 C++ 侧采样表逐点一致）。
- `canvasWidth` / `canvasHeight` 是 canvas 的**像素**尺寸；`rect*` 来自 `getBoundingClientRect`；
  `visibility` 是 `document.visibilityState`；`display` 是 canvas 的 computed `display`。
  这三类读数缺一项，隐藏画布（display:none / 0 尺寸）这类显示红态就无法判红。

### 7.4 失败码与超时

| 码 | 触发 |
|---|---|
| `E_WEBCANVAS_MISMATCH` | 同帧对账不一致：帧序号 / 整图哈希 / 任一点采样 / `hashAlgorithm` 不符 |
| `E_WEBCANVAS_DISPLAY` | 显示类读数不合法：canvas 像素尺寸不是 256×256、`rectWidth`/`rectHeight` ≤ 0、`display=none`、`visibility=hidden` |
| `E_WEBCANVAS_NOLINE` | 收到帧后 6000ms 内没有收到同帧号的 `spike-report`（断链 / 缺回传） |

超时 6000ms 取既有先例（`Test/GPUBackendTester/Main.cpp` 的 6000ms 帧等待）。
