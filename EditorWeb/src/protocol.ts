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
 * 资产条目（PROTOCOL.md 2.6）：**只有 path + kind 两个字段**（2026-11-25 用户裁定：条目不加强制的固定元数据，
 * 提供者决定显示什么、浏览器忠实渲染）。**路径基准按 kind 区分**（editor-code-source-display）：数据型条目 path
 * 相对资源根；源种类（shader-source / csharp-source / shader-source-missing）path 相对工程根；source-omitted 的
 * path 是取回目录（相对工程根）。path 同时是条目的稳定标识。
 */
export interface AssetEntry {
  path: string;
  kind: string;
}

/**
 * 源种类（PROTOCOL.md 2.6，与 C# 的 EditorProtocol.AssetKindShaderSource / AssetKindCSharpSource /
 * AssetKindShaderSourceMissing 逐字一致）：这些 path 相对**工程根**（源文件在资源根之外），前端不得按资源根解读。
 */
export const SOURCE_KINDS = ['shader-source', 'csharp-source', 'shader-source-missing'] as const;

/**
 * 标记种类（PROTOCOL.md 2.6，与 C# 的 EditorProtocol.AssetKindShaderSourceMissing / AssetKindSourceOmitted
 * 逐字一致）：shader-source-missing = 历史上编译进库、现在找不到源文件的路径（仍产条目）；
 * source-omitted = 本批明示略去的源目录（path 是取回目录）。
 */
export const SHADER_SOURCE_MISSING_KIND = 'shader-source-missing';
export const SOURCE_OMITTED_KIND = 'source-omitted';

/** 判断条目是否属于「源种类」（其 path 是相对工程根的）。 */
export function isSourceKind(kind: string): boolean {
  return (SOURCE_KINDS as readonly string[]).includes(kind);
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
