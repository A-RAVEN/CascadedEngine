// editor-window-panel 任务 1.4：协议 v1 的 C# 侧（唯一事实源是 Editor/PROTOCOL.md）。
//
// 分工：**监听线程只做形状校验**（JSON 能否解析、type 是否认识、requestId/command/objectId 是否齐、
// args 形状是否符合 PROTOCOL.md 的字段类型），校验通过才入队；语义校验（宽高必须 > 0、对象是否存在）
// 在主线程执行命令时做。形状错误直接回 E_BAD_MESSAGE，**不入引擎队列**（D4）。
//
// 这里只用 BCL 的 System.Text.Json（D3：零 NuGet 服务依赖）。
using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Scripting.Managed.Editor
{
    internal enum EditorInboundKind
    {
        Hello = 1,
        Command = 2,
    }

    /// <summary>一条已通过形状校验的命令（字段含义见 PROTOCOL.md 第 2.3 节）。</summary>
    internal sealed class EditorCommand
    {
        internal int RequestId;
        internal string Name = string.Empty;
        internal int ObjectId;
        internal string Title = string.Empty;
        internal int Width;
        internal int Height;
    }

    /// <summary>入队项：hello 或 command。客户端引用只用来把结果写回该客户端的发送队列。</summary>
    internal sealed class EditorInbound
    {
        internal EditorInboundKind Kind;
        internal EditorClient Client;
        internal int ProtocolVersion;
        internal EditorCommand Command;
    }

    /// <summary>对象快照（PROTOCOL.md 2.4 的 object 记录）：值全部来自主线程对真实窗口的读回。</summary>
    internal sealed class EditorObjectSnapshot
    {
        internal int Id;
        internal string Title = string.Empty;
        internal int Width;
        internal int Height;

        internal Dictionary<string, object> ToJson()
        {
            return new Dictionary<string, object>
            {
                ["id"] = Id,
                ["title"] = Title,
                ["width"] = Width,
                ["height"] = Height,
            };
        }
    }

    internal static class EditorProtocol
    {
        internal const int Version = 1;
        internal const string ErrorProtocolVersion = "E_PROTOCOL_VERSION";
        internal const string ErrorUnknownObject = "E_UNKNOWN_OBJECT";
        internal const string ErrorObjectInvalidated = "E_OBJECT_INVALIDATED";
        internal const string ErrorInvalidArg = "E_INVALID_ARG";
        internal const string ErrorUndoEmpty = "E_UNDO_EMPTY";
        internal const string ErrorBadMessage = "E_BAD_MESSAGE";
        // editor-asset-browser 1.3：资产根不可用（门面未绑 / 根空串 / 目录不存在 / 枚举 IO 错误 /
        // 门面底层抛错）。不用 E_INVALID_ARG 兜底：那个码的触发定义是 set-* 的参数非法（PROTOCOL.md 第 3 节）。
        internal const string ErrorAssetRoot = "E_ASSET_ROOT";

        private static readonly HashSet<string> s_knownCommands = new HashSet<string>
        {
            "get-object", "list-objects", "set-title", "set-size", "undo", "list-assets",
        };

        private static bool IsObjectScoped(string command)
        {
            return command == "get-object" || command == "set-title" || command == "set-size" || command == "undo";
        }

        /// <summary>
        /// 形状校验。成功返回 true 并填 inbound；失败返回 false 并给出协议错误码与消息
        ///（requestId 尽量回填：能解析到就回填，解析不到为 0 —— PROTOCOL.md 第 3 节的约定）。
        /// </summary>
        internal static bool TryParse(string json, out EditorInbound inbound, out int requestId,
            out string errorCode, out string errorMessage)
        {
            inbound = null;
            requestId = 0;
            errorCode = string.Empty;
            errorMessage = string.Empty;
            if (string.IsNullOrWhiteSpace(json))
            {
                errorCode = ErrorBadMessage;
                errorMessage = "空消息不是 JSON 对象";
                return false;
            }
            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(json);
            }
            catch (JsonException ex)
            {
                errorCode = ErrorBadMessage;
                errorMessage = "JSON 解析失败：" + ex.Message;
                return false;
            }
            using (document)
            {
                JsonElement root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object)
                {
                    errorCode = ErrorBadMessage;
                    errorMessage = "消息必须是 JSON 对象";
                    return false;
                }
                if (!root.TryGetProperty("type", out JsonElement typeElement) ||
                    typeElement.ValueKind != JsonValueKind.String)
                {
                    errorCode = ErrorBadMessage;
                    errorMessage = "缺少字符串字段 type";
                    return false;
                }
                string type = typeElement.GetString() ?? string.Empty;
                if (type == "hello")
                {
                    int version = 0;
                    if (root.TryGetProperty("protocolVersion", out JsonElement versionElement))
                    {
                        if (versionElement.ValueKind != JsonValueKind.Number ||
                            !versionElement.TryGetInt32(out version))
                        {
                            errorCode = ErrorBadMessage;
                            errorMessage = "protocolVersion 必须是整数";
                            return false;
                        }
                    }
                    inbound = new EditorInbound { Kind = EditorInboundKind.Hello, ProtocolVersion = version };
                    return true;
                }
                if (type == "command")
                {
                    return TryParseCommand(root, out inbound, out requestId, out errorCode, out errorMessage);
                }
                errorCode = ErrorBadMessage;
                errorMessage = "未知消息类型：" + type;
                return false;
            }
        }

        private static bool TryParseCommand(JsonElement root, out EditorInbound inbound, out int requestId,
            out string errorCode, out string errorMessage)
        {
            inbound = null;
            requestId = 0;
            errorCode = ErrorBadMessage;
            errorMessage = string.Empty;

            if (!root.TryGetProperty("requestId", out JsonElement requestElement) ||
                requestElement.ValueKind != JsonValueKind.Number ||
                !requestElement.TryGetInt32(out requestId))
            {
                errorMessage = "命令缺少整数 requestId";
                return false;
            }
            if (!root.TryGetProperty("command", out JsonElement commandElement) ||
                commandElement.ValueKind != JsonValueKind.String)
            {
                errorMessage = "命令缺少字符串 command";
                return false;
            }
            string command = commandElement.GetString() ?? string.Empty;
            if (!s_knownCommands.Contains(command))
            {
                errorMessage = "未知命令：" + command;
                return false;
            }

            var parsed = new EditorCommand { RequestId = requestId, Name = command };
            if (IsObjectScoped(command))
            {
                if (!root.TryGetProperty("objectId", out JsonElement objectElement) ||
                    objectElement.ValueKind != JsonValueKind.Number ||
                    !objectElement.TryGetInt32(out parsed.ObjectId))
                {
                    errorMessage = "命令 " + command + " 缺少整数 objectId";
                    return false;
                }
            }
            else if (root.TryGetProperty("objectId", out JsonElement ignoredObject) &&
                     ignoredObject.ValueKind == JsonValueKind.Number)
            {
                // list-objects / list-assets 不使用 objectId；给了数字就当 0（文档写明"给了也被忽略"）。
                parsed.ObjectId = 0;
            }

            if (root.TryGetProperty("args", out JsonElement argsElement))
            {
                if (argsElement.ValueKind != JsonValueKind.Object)
                {
                    errorMessage = "args 必须是 JSON 对象";
                    return false;
                }
                if (command == "set-title")
                {
                    // 形状错误与非法参数分开：args 不是对象 = 形状错（E_BAD_MESSAGE）；
                    // title 缺失/不是字符串 = 非法参数（E_INVALID_ARG，PROTOCOL.md 第 3 节）。
                    if (!argsElement.TryGetProperty("title", out JsonElement titleElement) ||
                        titleElement.ValueKind != JsonValueKind.String)
                    {
                        errorCode = ErrorInvalidArg;
                        errorMessage = "set-title 的 args.title 必须是字符串";
                        return false;
                    }
                    parsed.Title = titleElement.GetString() ?? string.Empty;
                }
                else if (command == "set-size")
                {
                    if (!argsElement.TryGetProperty("width", out JsonElement widthElement) ||
                        widthElement.ValueKind != JsonValueKind.Number ||
                        !widthElement.TryGetInt32(out parsed.Width) ||
                        !argsElement.TryGetProperty("height", out JsonElement heightElement) ||
                        heightElement.ValueKind != JsonValueKind.Number ||
                        !heightElement.TryGetInt32(out parsed.Height))
                    {
                        errorCode = ErrorInvalidArg;
                        errorMessage = "set-size 的 args.width / args.height 必须是整数";
                        return false;
                    }
                }
            }
            else if (command == "set-title" || command == "set-size")
            {
                errorCode = ErrorBadMessage;
                errorMessage = "命令 " + command + " 缺少 args 对象";
                return false;
            }

            inbound = new EditorInbound
            {
                Kind = EditorInboundKind.Command,
                Command = parsed,
            };
            errorCode = string.Empty;
            return true;
        }

        // ---- 出站消息构造（形态见 PROTOCOL.md 第 2 节）----

        internal static string BuildWelcome(IEnumerable<EditorObjectSnapshot> objects)
        {
            var list = new List<object>();
            foreach (EditorObjectSnapshot snapshot in objects)
            {
                list.Add(snapshot.ToJson());
            }
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "welcome",
                ["protocolVersion"] = Version,
                ["objects"] = list,
            });
        }

        internal static string BuildObjectResult(int requestId, EditorObjectSnapshot snapshot)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "result",
                ["requestId"] = requestId,
                ["ok"] = true,
                ["value"] = new Dictionary<string, object> { ["object"] = snapshot.ToJson() },
            });
        }

        internal static string BuildObjectsResult(int requestId, IEnumerable<EditorObjectSnapshot> objects)
        {
            var list = new List<object>();
            foreach (EditorObjectSnapshot snapshot in objects)
            {
                list.Add(snapshot.ToJson());
            }
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "result",
                ["requestId"] = requestId,
                ["ok"] = true,
                ["value"] = new Dictionary<string, object> { ["objects"] = list },
            });
        }

        internal static string BuildError(int requestId, string code, string message)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "result",
                ["requestId"] = requestId,
                ["ok"] = false,
                ["error"] = new Dictionary<string, object> { ["code"] = code, ["message"] = message },
            });
        }

        // ---- 资产浏览（editor-asset-browser 1.3；形状见 PROTOCOL.md 2.4 / 2.5 / 2.6）----

        /// <summary>`list-assets` 的成功结果：`value.assets` 为条目数组（空数组 = 合法空态）。</summary>
        internal static string BuildAssetsResult(int requestId, IEnumerable<AssetEntry> assets)
        {
            var list = new List<object>();
            foreach (AssetEntry entry in assets)
            {
                list.Add(entry.ToJson());
            }
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "result",
                ["requestId"] = requestId,
                ["ok"] = true,
                ["value"] = new Dictionary<string, object> { ["assets"] = list },
            });
        }

        /// <summary>
        /// `assets-changed` 广播事件：**不带 objectId**（资产变化不是某个对象的变化），`fields` 只有本次
        /// 枚举相对上次快照的增删条数；客户端收到后重发 `list-assets` 重拉条目本体。
        /// </summary>
        internal static string BuildAssetsChangedEvent(int added, int removed)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "event",
                ["event"] = "assets-changed",
                ["fields"] = new Dictionary<string, object> { ["added"] = added, ["removed"] = removed },
            });
        }

        internal static string BuildChangedEvent(int objectId, Dictionary<string, object> fields)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "event",
                ["event"] = "object-changed",
                ["objectId"] = objectId,
                ["fields"] = fields,
            });
        }

        internal static string BuildInvalidatedEvent(int objectId)
        {
            return JsonSerializer.Serialize(new Dictionary<string, object>
            {
                ["type"] = "event",
                ["event"] = "object-invalidated",
                ["objectId"] = objectId,
                ["fields"] = new Dictionary<string, object>(),
            });
        }
    }
}
