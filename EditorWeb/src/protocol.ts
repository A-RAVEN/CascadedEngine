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

export type CommandName = 'get-object' | 'list-objects' | 'set-title' | 'set-size' | 'undo';

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
  | 'E_BAD_MESSAGE';

export interface ResultOk {
  type: 'result';
  requestId: number;
  ok: true;
  value: {
    object?: EditorObject;
    objects?: EditorObject[];
  };
}

export interface ResultError {
  type: 'result';
  requestId: number;
  ok: false;
  error: { code: ErrorCode | string; message: string };
}

export type ResultMessage = ResultOk | ResultError;

/** S→C event：object-changed 的 fields 只含本次变化字段；object-invalidated 的 fields 为 {}。 */
export interface EventMessage {
  type: 'event';
  event: 'object-changed' | 'object-invalidated';
  objectId: number;
  fields: Partial<Pick<EditorObject, 'title' | 'width' | 'height'>>;
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
} as const;
