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
- `command`：`get-object` / `list-objects` / `set-title` / `set-size` / `undo`。
- `objectId`：对象编号。对象作用域命令（`get-object` / `set-title` / `set-size` / `undo`）必填；
  `list-objects` 不使用该字段（缺省即可，给了也被忽略）。
- `args`：
  - `get-object`：无（可省略）。
  - `list-objects`：无（可省略）。
  - `set-title`：`{ "title": <string> }`。
  - `set-size`：`{ "width": <int>, "height": <int> }`，两者都必须 **> 0**；宽度还必须是
    **视口宽**（即 `宽度 > 面板宽`，面板宽默认 320、`CASCADED_EDITOR_PANEL_WIDTH` 可配），否则
    `E_INVALID_ARG`（杜绝视口宽 ≤ 0）。
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

对象快照 `object` 的字段类型：`id`、`width`、`height` 为整数，`title` 为字符串。

### 2.5 S→C `event`

```json
{ "type": "event", "event": "object-changed", "objectId": 1, "fields": { "title": "新标题" } }
```

- `object-changed`：`fields` 只含本次变化的字段（`title` / `width` / `height` 的子集）。来源：命令
  执行成功，或窗口尺寸回调（外部改窗口尺寸）。`undo` 重放也产生它。
- `object-invalidated`：`fields` 为空对象 `{}`。对象已移出注册表；此后同编号命令回
  `E_OBJECT_INVALIDATED`。

## 3. 错误码

| 码 | 触发条件 | 是否入引擎队列 |
|---|---|---|
| `E_PROTOCOL_VERSION` | `hello.protocolVersion` 不是 1（缺失按 0 处理）。服务端发失败 `result`（`requestId = 0`）后**关闭连接** | 否 |
| `E_UNKNOWN_OBJECT` | `objectId` 从未存在过 | 否（零原生调用） |
| `E_OBJECT_INVALIDATED` | `objectId` 曾存在、现已失效 | 否（零原生调用） |
| `E_INVALID_ARG` | 参数非法：`set-size` 的宽或高 ≤ 0、宽 ≤ 面板宽、类型不是整数；`set-title` 的 `title` 不是字符串 | 否（零原生调用） |
| `E_UNDO_EMPTY` | `undo` 时该对象撤销槽为空 | 否（零原生调用） |
| `E_BAD_MESSAGE` | 形状错误：不是 JSON 对象、缺 `type`、未知 `type`、缺 `requestId`、未知 `command`、缺必需 `objectId`、`args` 形状不符 | 否（零原生调用） |

约定：

- **握手错误**与**无法解析出 `requestId` 的错误**用 `requestId = 0` 回 `result`（0 保留给这类错误；
  客户端分配的 `requestId` 应 ≥ 1）。
- 形状错误在任何引擎调用之前判定：被 `E_BAD_MESSAGE` / `E_INVALID_ARG` / `E_UNKNOWN_OBJECT` /
  `E_OBJECT_INVALIDATED` 拒绝的命令，引擎绑定调用次数增量为 **0**（原生调用计数器读数为判据）。
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

面板控件与命令一一对应：连接状态 → `hello`/`welcome`；标题编辑+应用 → `set-title`；宽高编辑+应用 →
`set-size`；撤销按钮 → `undo`；事件日志 → `event`；失效横幅 → `object-invalidated`（此后禁用输入）。
面板启动时先发 `hello`，用 `welcome.objects` 渲染初始值。

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
