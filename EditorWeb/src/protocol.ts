// 编辑器命令协议 v1 的 TypeScript 镜像 —— 唯一事实源是
// Scripting/Managed/Editor/PROTOCOL.md；字段名与错误码必须逐字一致（改协议先改那份，再改本文件与 C# 记录）。
// 本文件只放类型与常量，不含 DOM / 网络逻辑。

export const PROTOCOL_VERSION = 1 as const;
export const DEFAULT_PORT = 18390;

/** 可编辑对象：服务创建的目标窗口登记为编号 1。 */
export interface EditorObject {
  id: number;
  title: string;
  width: number;
  height: number;
}

export type CommandName = 'get-object' | 'list-objects' | 'set-title' | 'set-size' | 'undo' | 'list-assets';

/**
 * 资产条目（PROTOCOL.md 2.6）：v1 一文件一条目，children 恒为空数组、nodeId = path =
 * 相对资源根的正斜杠路径；v2 的程序集展开 / 衍生物归组只往 children 里挂子条目，nodeId 仍稳定。
 */
export interface AssetEntry {
  nodeId: string;
  path: string;
  kind: string;
  sizeBytes: number;
  /** ISO 8601 UTC（FileInfo.LastWriteTimeUtc 的 "o" 格式）。 */
  modifiedUtc: string;
  /** 该路径在资源注册表里已有条目；未注册的文件同样列出，只是这里为 false。 */
  registered: boolean;
  children: AssetEntry[];
}

/** C→S hello：连接后第一条消息，收到 welcome 之前不能发 command。 */
export interface HelloMessage {
  type: 'hello';
  protocolVersion: typeof PROTOCOL_VERSION;
}

/** C→S command：requestId 由客户端分配（≥1），服务端原样回填。 */
export interface CommandMessage {
  type: 'command';
  requestId: number;
  command: CommandName;
  objectId?: number;
  args?: {
    title?: string;
    width?: number;
    height?: number;
  };
}

/** S→C welcome：objects 是当前仍有效的对象快照（首切片 0 或 1 个）。 */
export interface WelcomeMessage {
  type: 'welcome';
  protocolVersion: number;
  objects: EditorObject[];
}

export type ErrorCode =
  | 'E_PROTOCOL_VERSION'
  | 'E_UNKNOWN_OBJECT'
  | 'E_OBJECT_INVALIDATED'
  | 'E_INVALID_ARG'
  | 'E_UNDO_EMPTY'
  | 'E_BAD_MESSAGE'
  | 'E_ASSET_ROOT';

export interface ResultOk {
  type: 'result';
  requestId: number;
  ok: true;
  value: {
    object?: EditorObject;
    objects?: EditorObject[];
    assets?: AssetEntry[];
  };
}

export interface ResultError {
  type: 'result';
  requestId: number;
  ok: false;
  error: { code: ErrorCode | string; message: string };
}

export type ResultMessage = ResultOk | ResultError;

/** 对象事件的字段：只含本次变化的字段（title / width / height 的子集）。 */
export type ObjectChangedFields = Partial<Pick<EditorObject, 'title' | 'width' | 'height'>>;

/** assets-changed 的字段：本次枚举相对上次快照的条目增删数（不携带条目本体）。 */
export interface AssetChangedFields {
  added: number;
  removed: number;
}

/**
 * S→C event（PROTOCOL.md 2.5）：object-changed 的 fields 只含本次变化字段；object-invalidated 的
 * fields 为 {}；assets-changed 的 fields 是 { added, removed }。**objectId 只在对象事件上存在**
 *（assets-changed 不带，客户端不得因缺它而拒绝这条事件）。
 */
export interface EventMessage {
  type: 'event';
  event: 'object-changed' | 'object-invalidated' | 'assets-changed';
  objectId?: number;
  fields: ObjectChangedFields | AssetChangedFields;
}

export type ServerMessage = WelcomeMessage | ResultMessage | EventMessage;

/** 面板控件与协议命令的一一对应（PROTOCOL.md §5）。 */
export const UI_TO_COMMAND = {
  status: 'hello / welcome',
  title: 'set-title',
  size: 'set-size',
  undo: 'undo',
  log: 'event',
  invalidatedBanner: 'object-invalidated',
  assets: 'list-assets',
} as const;
