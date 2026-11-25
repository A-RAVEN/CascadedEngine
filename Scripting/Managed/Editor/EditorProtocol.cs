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
        // editor-web-render-spike 2.2 / 2.3：渲染原型通道的三类消息（PROTOCOL.md §7）。
        // 只有 CASCADED_EDITOR_SPIKE=web-render 门控开启时才被识别；关着时按未知类型回 E_BAD_MESSAGE。
        SpikeHello = 3,
        SpikeFrame = 4,
        SpikeReport = 5,
    }

    /// <summary>一个采样点的 RGBA（原生帧与网页回传共用同一形状）。</summary>
    internal sealed class SpikeSampleValue
    {
        internal string Name = string.Empty;
        internal int R;
        internal int G;
        internal int B;
        internal int A;
    }

    /// <summary>`spike-frame`：C++ 原生读回的一帧（PROTOCOL.md 7.2）。</summary>
    internal sealed class SpikeFramePayload
    {
        internal int Frame;
        internal int Width;
        internal int Height;
        internal string Path = string.Empty;
        internal string Hash = string.Empty;
        internal string Pixels = string.Empty;
        internal List<SpikeSampleValue> Samples = new List<SpikeSampleValue>();
        /// <summary>原始 JSON 文本（服务端原样转发给原型页，不重新序列化）。</summary>
        internal string RawJson = string.Empty;
    }

    /// <summary>`spike-report`：原型页从 canvas 自读回的对账数据（PROTOCOL.md 7.3）。</summary>
    internal sealed class SpikeReportPayload
    {
        internal int Frame;
        internal int Width;
        internal int Height;
        internal int CanvasWidth;
        internal int CanvasHeight;
        internal double RectWidth;
        internal double RectHeight;
        internal string Display = string.Empty;
        internal string Visibility = string.Empty;
        internal string Hash = string.Empty;
        internal string HashAlgorithm = string.Empty;
        internal int Webgpu;
        internal List<SpikeSampleValue> Samples = new List<SpikeSampleValue>();
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
        internal SpikeFramePayload Frame;
        internal SpikeReportPayload Report;
        internal int Webgpu;
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

        // ---- 资产条目种类（editor-code-source-display 5.1；PROTOCOL.md 2.6 与 protocol.ts 三处镜像）----
        // 条目只有 path + kind 两个字段（2026-11-25 用户裁定）：数据型种类由扩展名映射
        //（AssetDisplayProviders.KindOf），源类型 / 标记类型由显示提供者产出；源条目的 path 基准是工程根
        //（数据型是资源根），前端按这些 kind 区分渲染、不混基准。
        internal const string AssetKindShaderSource = "shader-source";
        internal const string AssetKindCSharpSource = "csharp-source";
        internal const string AssetKindShaderSourceMissing = "shader-source-missing";
        internal const string AssetKindSourceOmitted = "source-omitted";

        private static readonly HashSet<string> s_knownCommands = new HashSet<string>
        {
            "get-object", "list-objects", "set-title", "set-size", "undo", "list-assets",
        };

        /// <summary>
        /// 渲染原型通道是否开启（由 EditorService 在服务启动时按 CASCADED_EDITOR_SPIKE=web-render 设置）。
        /// 关着时 `spike-hello` / `spike-frame` / `spike-report` 都按未知消息类型处理（E_BAD_MESSAGE），
        /// 既有面板客户端的可见行为不变。
        /// </summary>
        internal static bool SpikeMessagesEnabled;

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
                // 渲染原型通道（PROTOCOL.md §7）：门控关着时走下面的"未知消息类型"，与既有行为一致。
                if (SpikeMessagesEnabled && type == "spike-hello")
                {
                    return TryParseSpikeHello(root, out inbound, out errorCode, out errorMessage);
                }
                if (SpikeMessagesEnabled && type == "spike-frame")
                {
                    return TryParseSpikeFrame(json, root, out inbound, out errorCode, out errorMessage);
                }
                if (SpikeMessagesEnabled && type == "spike-report")
                {
                    return TryParseSpikeReport(root, out inbound, out errorCode, out errorMessage);
                }
                errorCode = ErrorBadMessage;
                errorMessage = "未知消息类型：" + type;
                return false;
            }
        }

        // ---- 渲染原型通道（editor-web-render-spike 2.3；形状见 PROTOCOL.md §7）----

        private static bool TryParseSpikeHello(JsonElement root, out EditorInbound inbound, out string errorCode,
            out string errorMessage)
        {
            inbound = null;
            errorCode = ErrorBadMessage;
            errorMessage = string.Empty;
            int webgpu = 0;
            if (root.TryGetProperty("webgpu", out JsonElement gpuElement) &&
                (gpuElement.ValueKind != JsonValueKind.Number || !gpuElement.TryGetInt32(out webgpu)))
            {
                errorMessage = "spike-hello 的 webgpu 必须是整数";
                return false;
            }
            inbound = new EditorInbound { Kind = EditorInboundKind.SpikeHello, Webgpu = webgpu };
            errorCode = string.Empty;
            return true;
        }

        private static bool TryParseSpikeSamples(JsonElement root, List<SpikeSampleValue> samples,
            out string errorMessage)
        {
            errorMessage = string.Empty;
            if (!root.TryGetProperty("samples", out JsonElement samplesElement) ||
                samplesElement.ValueKind != JsonValueKind.Array)
            {
                errorMessage = "缺少 samples 数组";
                return false;
            }
            foreach (JsonElement item in samplesElement.EnumerateArray())
            {
                if (item.ValueKind != JsonValueKind.Object)
                {
                    errorMessage = "samples 的元素必须是对象";
                    return false;
                }
                if (!item.TryGetProperty("name", out JsonElement nameElement) ||
                    nameElement.ValueKind != JsonValueKind.String)
                {
                    errorMessage = "采样点缺少字符串 name";
                    return false;
                }
                var sample = new SpikeSampleValue { Name = nameElement.GetString() ?? string.Empty };
                if (!TryReadChannel(item, "r", out sample.R) || !TryReadChannel(item, "g", out sample.G) ||
                    !TryReadChannel(item, "b", out sample.B) || !TryReadChannel(item, "a", out sample.A))
                {
                    errorMessage = "采样点 " + sample.Name + " 的 r/g/b/a 必须是 0..255 的整数";
                    return false;
                }
                if (sample.Name.Length == 0)
                {
                    errorMessage = "采样点的 name 不能为空";
                    return false;
                }
                samples.Add(sample);
            }
            if (samples.Count == 0)
            {
                errorMessage = "samples 不能是空数组";
                return false;
            }
            return true;
        }

        private static bool TryReadChannel(JsonElement element, string name, out int value)
        {
            value = 0;
            if (!element.TryGetProperty(name, out JsonElement channel) ||
                channel.ValueKind != JsonValueKind.Number || !channel.TryGetInt32(out value))
            {
                return false;
            }
            return value >= 0 && value <= 255;
        }

        private static bool TryParseSpikeFrame(string json, JsonElement root, out EditorInbound inbound,
            out string errorCode, out string errorMessage)
        {
            inbound = null;
            errorCode = ErrorBadMessage;
            errorMessage = string.Empty;
            var frame = new SpikeFramePayload { RawJson = json };
            if (!root.TryGetProperty("frame", out JsonElement frameElement) ||
                frameElement.ValueKind != JsonValueKind.Number || !frameElement.TryGetInt32(out frame.Frame) ||
                frame.Frame < 1)
            {
                errorMessage = "spike-frame 缺少 ≥1 的整数 frame";
                return false;
            }
            if (!root.TryGetProperty("width", out JsonElement widthElement) ||
                widthElement.ValueKind != JsonValueKind.Number || !widthElement.TryGetInt32(out frame.Width) ||
                !root.TryGetProperty("height", out JsonElement heightElement) ||
                heightElement.ValueKind != JsonValueKind.Number || !heightElement.TryGetInt32(out frame.Height))
            {
                errorMessage = "spike-frame 缺少整数 width / height";
                return false;
            }
            if (frame.Width != SpikeChannel.TextureSize || frame.Height != SpikeChannel.TextureSize)
            {
                errorMessage = "spike-frame 的尺寸必须是 " + SpikeChannel.TextureSize + "×" +
                               SpikeChannel.TextureSize + "（本批写死）：" + frame.Width + "×" + frame.Height;
                return false;
            }
            if (!root.TryGetProperty("path", out JsonElement pathElement) ||
                pathElement.ValueKind != JsonValueKind.String)
            {
                errorMessage = "spike-frame 缺少字符串 path";
                return false;
            }
            frame.Path = pathElement.GetString() ?? string.Empty;
            if (!root.TryGetProperty("hash", out JsonElement hashElement) ||
                hashElement.ValueKind != JsonValueKind.String)
            {
                errorMessage = "spike-frame 缺少字符串 hash";
                return false;
            }
            frame.Hash = hashElement.GetString() ?? string.Empty;
            if (!IsHex8(frame.Hash))
            {
                errorMessage = "spike-frame 的 hash 必须是 8 位十六进制：" + frame.Hash;
                return false;
            }
            if (!root.TryGetProperty("pixels", out JsonElement pixelsElement) ||
                pixelsElement.ValueKind != JsonValueKind.String)
            {
                errorMessage = "spike-frame 缺少字符串 pixels";
                return false;
            }
            frame.Pixels = pixelsElement.GetString() ?? string.Empty;
            // 256×256×4 = 262144 字节 → 标准 base64 定长 349528 字符；长度不符说明帧被截断或编码方式不对，
            // 在形状校验就拦下（否则页面解码失败只会表现成"没回传"，红态原因会指向错误的机制）。
            const int expectedBase64Length = 349528;
            if (frame.Pixels.Length != expectedBase64Length)
            {
                errorMessage = "spike-frame 的 pixels 长度不符：期望 " + expectedBase64Length + " 字符（262144 字节的 base64），实际 " +
                               frame.Pixels.Length;
                return false;
            }
            if (!TryParseSpikeSamples(root, frame.Samples, out errorMessage))
            {
                return false;
            }
            inbound = new EditorInbound { Kind = EditorInboundKind.SpikeFrame, Frame = frame };
            errorCode = string.Empty;
            return true;
        }

        private static bool TryParseSpikeReport(JsonElement root, out EditorInbound inbound, out string errorCode,
            out string errorMessage)
        {
            inbound = null;
            errorCode = ErrorBadMessage;
            errorMessage = string.Empty;
            var report = new SpikeReportPayload();
            if (!root.TryGetProperty("frame", out JsonElement frameElement) ||
                frameElement.ValueKind != JsonValueKind.Number || !frameElement.TryGetInt32(out report.Frame))
            {
                errorMessage = "spike-report 缺少整数 frame";
                return false;
            }
            if (!ReadIntField(root, "width", out report.Width) || !ReadIntField(root, "height", out report.Height))
            {
                errorMessage = "spike-report 缺少整数 width / height";
                return false;
            }
            if (!ReadIntField(root, "canvasWidth", out report.CanvasWidth) ||
                !ReadIntField(root, "canvasHeight", out report.CanvasHeight))
            {
                errorMessage = "spike-report 缺少整数 canvasWidth / canvasHeight";
                return false;
            }
            if (!ReadDoubleField(root, "rectWidth", out report.RectWidth) ||
                !ReadDoubleField(root, "rectHeight", out report.RectHeight))
            {
                errorMessage = "spike-report 缺少数字 rectWidth / rectHeight";
                return false;
            }
            if (!ReadStringField(root, "display", out report.Display) ||
                !ReadStringField(root, "visibility", out report.Visibility))
            {
                errorMessage = "spike-report 缺少字符串 display / visibility";
                return false;
            }
            if (!ReadStringField(root, "hash", out report.Hash) ||
                !ReadStringField(root, "hashAlgorithm", out report.HashAlgorithm))
            {
                errorMessage = "spike-report 缺少字符串 hash / hashAlgorithm";
                return false;
            }
            if (root.TryGetProperty("webgpu", out JsonElement gpuElement) &&
                gpuElement.ValueKind == JsonValueKind.Number && !gpuElement.TryGetInt32(out report.Webgpu))
            {
                report.Webgpu = 0;
            }
            if (!TryParseSpikeSamples(root, report.Samples, out errorMessage))
            {
                return false;
            }
            inbound = new EditorInbound { Kind = EditorInboundKind.SpikeReport, Report = report };
            errorCode = string.Empty;
            return true;
        }

        private static bool ReadIntField(JsonElement root, string name, out int value)
        {
            value = 0;
            return root.TryGetProperty(name, out JsonElement element) && element.ValueKind == JsonValueKind.Number &&
                   element.TryGetInt32(out value);
        }

        private static bool ReadDoubleField(JsonElement root, string name, out double value)
        {
            value = 0.0;
            return root.TryGetProperty(name, out JsonElement element) && element.ValueKind == JsonValueKind.Number &&
                   element.TryGetDouble(out value);
        }

        private static bool ReadStringField(JsonElement root, string name, out string value)
        {
            value = string.Empty;
            if (!root.TryGetProperty(name, out JsonElement element) || element.ValueKind != JsonValueKind.String)
            {
                return false;
            }
            value = element.GetString() ?? string.Empty;
            return true;
        }

        private static bool IsHex8(string text)
        {
            if (text.Length != 8)
            {
                return false;
            }
            foreach (char character in text)
            {
                bool hex = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                           (character >= 'A' && character <= 'F');
                if (!hex)
                {
                    return false;
                }
            }
            return true;
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
