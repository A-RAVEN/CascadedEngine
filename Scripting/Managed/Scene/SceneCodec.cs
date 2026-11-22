// 场景文件编解码（csharp-scene-foundation 任务 1.2 / 1.3，design D2 / D3）。
//
// 技术选型（design D2）：System.Text.Json 的 Utf8JsonWriter（写）/ JsonDocument（读）**手写**，
// 逐字段显式读写与校验。
//   · **不用** CodeGen 给 C# 发射的 codec：它只服务跨 ABI 的 C++ 序列化块记录、`internal` 可见、
//     块头是类型 / 布局哈希而不是文件版本、反序列化单向；
//   · **不用** `JsonSerializer.Serialize(生成镜像)`：生成镜像是 public **字段**，STJ 默认不序列化字段，
//     直接序列化会得到空对象（调查实测陷阱）。
//
// 文件形状（formatVersion = 1）：
//   { "formatVersion": 1,
//     "scene": { "name": "...",
//                "entities": [ { "id": "<uuid 字符串>", "name": "...",
//                                "transform": { "position": [x,y,z], "rotation": [x,y,z], "scale": [x,y,z] },
//                                "meshRenderer": { "shaderPath": "...", "color": [r,g,b],
//                                                  "vertices": [9 个 float] } } ] } }
// `meshRenderer` 为 null 表示该实体没有渲染成分。
//
// 失败通道（design D3，沿用 io-error-channel 的教训）：
//   · 格式 / 结构问题一律抛 `SceneFormatException` 并点名具体字段，**不返回半个场景、
//     不静默补默认值**；
//   · 文件读写本身的失败（目录不存在 / 读不到 / 写不进）抛 `IOException`，消息里带路径名；
//   · **Load 只做格式 / 结构校验，不查资源存在性**（Load 是纯文件操作，没有后端入口）；
//     着色器路径是否登记由注册 / 渲染期校验（见 SceneRenderer，design D4）。
//
// 资源引用只按**名字**持久化：文件里没有任何句柄数值字段（design D4）。
using System;
using System.IO;
using System.Text.Json;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Scene
{
    /// <summary>场景 JSON 编解码。所有字段显式读写；任何结构问题都抛 <see cref="SceneFormatException"/>。</summary>
    public static class SceneCodec
    {
        /// <summary>当前支持的文件版本。与 CodeGen 的 FormatVersion 无关（design D2）。</summary>
        public const int FormatVersion = 1;

        /// <summary>把场景写到 <paramref name="path"/>（UTF-8 JSON，缩进可读）。</summary>
        public static void Save(Scene scene, string path)
        {
            if (scene == null)
            {
                throw new ArgumentNullException(nameof(scene), "保存场景：场景对象不能为空");
            }
            if (string.IsNullOrEmpty(path))
            {
                throw new ArgumentException("保存场景：文件路径不能为空", nameof(path));
            }

            // 先在内存里编完再落盘：编码途中的编程错误（空实体 / 顶点数不对）不会留下半个文件。
            byte[] bytes;
            using (MemoryStream stream = new MemoryStream())
            {
                using (Utf8JsonWriter writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
                {
                    WriteDocument(writer, scene);
                }
                bytes = stream.ToArray();
            }

            try
            {
                File.WriteAllBytes(path, bytes);
            }
            catch (IOException ex)
            {
                throw new IOException($"保存场景失败：{path}（{ex.GetType().Name}: {ex.Message}）", ex);
            }
            catch (UnauthorizedAccessException ex)
            {
                throw new IOException($"保存场景失败：{path}（{ex.GetType().Name}: {ex.Message}）", ex);
            }
            catch (ArgumentException ex)
            {
                throw new IOException($"保存场景失败：{path}（路径非法：{ex.Message}）", ex);
            }
        }

        /// <summary>从 <paramref name="path"/> 读回场景。结构不合法就抛，不返回半个场景。</summary>
        public static Scene Load(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                throw new ArgumentException("加载场景：文件路径不能为空", nameof(path));
            }

            byte[] bytes;
            try
            {
                bytes = File.ReadAllBytes(path);
            }
            catch (IOException ex)
            {
                throw new IOException($"加载场景失败：{path}（{ex.GetType().Name}: {ex.Message}）", ex);
            }
            catch (UnauthorizedAccessException ex)
            {
                throw new IOException($"加载场景失败：{path}（{ex.GetType().Name}: {ex.Message}）", ex);
            }

            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(bytes);
            }
            catch (JsonException ex)
            {
                throw new SceneFormatException($"场景文件解析失败：{path}（JSON 不完整或格式错：{ex.Message}）", ex);
            }
            using (document)
            {
                return ReadDocument(document.RootElement, path);
            }
        }

        // ---- 写 ----

        private static void WriteDocument(Utf8JsonWriter writer, Scene scene)
        {
            writer.WriteStartObject();
            writer.WriteNumber("formatVersion", FormatVersion);
            writer.WritePropertyName("scene");
            writer.WriteStartObject();
            writer.WriteString("name", scene.Name ?? string.Empty);
            writer.WritePropertyName("entities");
            writer.WriteStartArray();
            if (scene.Entities != null)
            {
                foreach (Entity entity in scene.Entities)
                {
                    WriteEntity(writer, entity);
                }
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        private static void WriteEntity(Utf8JsonWriter writer, Entity entity)
        {
            if (entity == null)
            {
                throw new ArgumentException("保存场景：实体列表里有空项（null），不替你跳过");
            }
            if (string.IsNullOrEmpty(entity.Id))
            {
                // 空 id 写出去，Load 一定按「必填字段 id」拒绝 —— 与其留一份读不回来的文件，不如当场报。
                throw new ArgumentException("保存场景：实体缺少 id（持久身份不能为空）");
            }
            if (entity.Transform == null)
            {
                throw new ArgumentException($"保存场景：实体 {entity.Id} 缺少 Transform（不替你补默认值）");
            }
            writer.WriteStartObject();
            writer.WriteString("id", entity.Id);
            writer.WriteString("name", entity.Name ?? string.Empty);
            writer.WritePropertyName("transform");
            WriteTransform(writer, entity.Transform);
            writer.WritePropertyName("meshRenderer");
            if (entity.MeshRenderer == null)
            {
                writer.WriteNullValue();
            }
            else
            {
                WriteMeshRenderer(writer, entity);
            }
            writer.WriteEndObject();
        }

        private static void WriteTransform(Utf8JsonWriter writer, Transform transform)
        {
            writer.WriteStartObject();
            writer.WritePropertyName("position");
            WriteFloat3(writer, transform.Position);
            writer.WritePropertyName("rotation");
            WriteFloat3(writer, transform.Rotation);
            writer.WritePropertyName("scale");
            WriteFloat3(writer, transform.Scale);
            writer.WriteEndObject();
        }

        private static void WriteMeshRenderer(Utf8JsonWriter writer, Entity entity)
        {
            MeshRenderer renderer = entity.MeshRenderer;
            if (string.IsNullOrEmpty(renderer.ShaderPath))
            {
                throw new ArgumentException($"保存场景：实体 {entity.Id} 的 MeshRenderer 缺少 ShaderPath（资源按名字持久化）");
            }
            if (renderer.Vertices == null || renderer.Vertices.Length != MeshRenderer.VertexFloatCount)
            {
                // 首切片一个实体一个三角形：9 个 float（3 顶点 × 3 分量）。长度不对写出去就读不回来。
                throw new ArgumentException(
                    $"保存场景：实体 {entity.Id} 的内联顶点个数是 {(renderer.Vertices == null ? 0 : renderer.Vertices.Length)}，" +
                    $"期望 {MeshRenderer.VertexFloatCount}（3 顶点 × 3 分量）");
            }
            writer.WriteStartObject();
            writer.WriteString("shaderPath", renderer.ShaderPath);
            writer.WritePropertyName("color");
            WriteFloat3(writer, renderer.Color);
            writer.WritePropertyName("vertices");
            writer.WriteStartArray();
            foreach (float value in renderer.Vertices)
            {
                writer.WriteNumberValue(value);
            }
            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        private static void WriteFloat3(Utf8JsonWriter writer, float3 value)
        {
            writer.WriteStartArray();
            writer.WriteNumberValue(value.x);
            writer.WriteNumberValue(value.y);
            writer.WriteNumberValue(value.z);
            writer.WriteEndArray();
        }

        // ---- 读 ----

        private static Scene ReadDocument(JsonElement root, string path)
        {
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw Format($"场景文件根节点不是 JSON 对象：{path}");
            }
            JsonElement versionElement = RequireProperty(root, "formatVersion", "顶层", path);
            if (versionElement.ValueKind != JsonValueKind.Number || !versionElement.TryGetInt32(out int version))
            {
                throw Format($"formatVersion 不是整数：{path}");
            }
            if (version != FormatVersion)
            {
                throw Format($"formatVersion 不支持：文件是 {version}，本程序只支持 {FormatVersion}（{path}）");
            }

            JsonElement sceneElement = RequireProperty(root, "scene", "顶层", path);
            if (sceneElement.ValueKind != JsonValueKind.Object)
            {
                throw Format($"字段 scene 不是 JSON 对象：{path}");
            }
            Scene scene = new Scene();
            scene.Name = ReadString(sceneElement, "name", "scene", path);
            JsonElement entities = RequireProperty(sceneElement, "entities", "scene", path);
            if (entities.ValueKind != JsonValueKind.Array)
            {
                throw Format($"字段 scene.entities 不是数组：{path}");
            }
            int index = 0;
            foreach (JsonElement item in entities.EnumerateArray())
            {
                scene.Entities.Add(ReadEntity(item, index, path));
                ++index;
            }
            return scene;
        }

        private static Entity ReadEntity(JsonElement element, int index, string path)
        {
            string label = $"entities[{index}]";
            if (element.ValueKind != JsonValueKind.Object)
            {
                throw Format($"字段 {label} 不是 JSON 对象：{path}");
            }
            Entity entity = new Entity();
            entity.Id = ReadString(element, "id", label, path);
            if (entity.Id.Length == 0)
            {
                throw Format($"必填字段 {label}.id 是空字符串：{path}");
            }
            entity.Name = ReadString(element, "name", label, path);
            JsonElement transform = RequireProperty(element, "transform", label, path);
            if (transform.ValueKind != JsonValueKind.Object)
            {
                throw Format($"字段 {label}.transform 不是 JSON 对象：{path}");
            }
            entity.Transform = new Transform
            {
                Position = ReadFloat3(transform, "position", label + ".transform", path),
                Rotation = ReadFloat3(transform, "rotation", label + ".transform", path),
                Scale = ReadFloat3(transform, "scale", label + ".transform", path),
            };
            if (!element.TryGetProperty("meshRenderer", out JsonElement renderer) ||
                renderer.ValueKind == JsonValueKind.Null)
            {
                entity.MeshRenderer = null;
                return entity;
            }
            if (renderer.ValueKind != JsonValueKind.Object)
            {
                throw Format($"字段 {label}.meshRenderer 既不是 JSON 对象也不是 null：{path}");
            }
            string rendererLabel = label + ".meshRenderer";
            MeshRenderer mesh = new MeshRenderer();
            mesh.ShaderPath = ReadString(renderer, "shaderPath", rendererLabel, path);
            if (mesh.ShaderPath.Length == 0)
            {
                throw Format($"必填字段 {rendererLabel}.shaderPath 是空字符串：{path}");
            }
            mesh.Color = ReadFloat3(renderer, "color", rendererLabel, path);
            JsonElement vertices = RequireProperty(renderer, "vertices", rendererLabel, path);
            mesh.Vertices = ReadFloatArray(vertices, MeshRenderer.VertexFloatCount,
                rendererLabel + ".vertices", path);
            entity.MeshRenderer = mesh;
            return entity;
        }

        private static JsonElement RequireProperty(JsonElement parent, string name, string label, string path)
        {
            if (!parent.TryGetProperty(name, out JsonElement value))
            {
                throw Format($"缺少必填字段 {label}.{name}：{path}");
            }
            return value;
        }

        private static string ReadString(JsonElement parent, string name, string label, string path)
        {
            JsonElement value = RequireProperty(parent, name, label, path);
            if (value.ValueKind != JsonValueKind.String)
            {
                throw Format($"字段 {label}.{name} 不是字符串：{path}");
            }
            return value.GetString() ?? string.Empty;
        }

        private static float3 ReadFloat3(JsonElement parent, string name, string label, string path)
        {
            JsonElement value = RequireProperty(parent, name, label, path);
            float[] values = ReadFloatArray(value, 3, label + "." + name, path);
            return SceneValues.Vec(values[0], values[1], values[2]);
        }

        private static float[] ReadFloatArray(JsonElement element, int expectedLength, string label, string path)
        {
            if (element.ValueKind != JsonValueKind.Array)
            {
                throw Format($"字段 {label} 不是数组：{path}");
            }
            int length = element.GetArrayLength();
            if (length != expectedLength)
            {
                throw Format($"字段 {label} 的元素个数是 {length}，期望 {expectedLength}：{path}");
            }
            float[] values = new float[expectedLength];
            int index = 0;
            foreach (JsonElement item in element.EnumerateArray())
            {
                if (item.ValueKind != JsonValueKind.Number || !item.TryGetSingle(out float value))
                {
                    throw Format($"字段 {label} 的第 {index} 个元素不是 float 数字：{path}");
                }
                values[index] = value;
                ++index;
            }
            return values;
        }

        private static SceneFormatException Format(string message)
        {
            return new SceneFormatException(message);
        }
    }
}
