// 场景用例的 C# 侧驱动（csharp-scene-foundation 任务 3.2）。
//
// 为什么在 C# 里：C# 断言只能在真跑 C# 的台子上执行 —— `Test/CoreTests` 是纯 C++ 可执行文件
// （0 个 .cs、无 hostfxr），C# 场景断言唯一的落点是 tester 经 hostfxr 加载的这份托管程序集。
// 于是分工是：**这里真跑**序列化往返 / 失败通道 / 生命周期钩子 / 场景渲染，把结果打成一行
// 机器可读读数（`[托管 读数] probe=...`）；**tester 的 `TestCSharpScene` 判决**这些读数，
// 并自己读回离屏捕获的像素（C# 绑定面没有 Readback，像素断言必须在 C++ 侧）。
//
// 阶段（每次宿主 Tick 走一步，由 tester 逐帧驱动）：
//   tick 1（阶段 0）：无活动场景读数 → 序列化 / 失败通道全部断言 → 注册「新场景」两次（验不重复 OnStart）
//                    并把这份场景存盘（供下一阶段从磁盘读回来）；
//   tick 2（阶段 1）：注销 → 从磁盘 Load → 注册（「保存→清空内存→加载→注册→渲染仍红」）；
//   tick 3（阶段 2）：注册引用不存在着色器路径的场景 → `SceneRuntime.Tick` 明确报错（宿主转非零返回码）；
//   tick 4（阶段 3）：注销 → Tick 回到零行为（读数行 active=0、帧数不涨）。
//
// 这个文件只在 `CASCADED_CSHARP_SCENE_PROBE=1` 时被宿主调用；生产路径（用户脚本注册场景）不经过它。
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Scene
{
    /// <summary>场景用例的 C# 侧驱动：阶段推进 + 机器可读读数行。不参与产品路径。</summary>
    internal static class SceneProbe
    {
        // 场景文件落在 test_output/ 下（与 tester 自己的日志同一目录，evidence 可直接引用）。
        private const string OutputFolder = "test_output";
        private const string RenderScenePath = "test_output/scene_probe_render.json";
        private const string RoundTripPath = "test_output/scene_probe_roundtrip.json";
        private const string ChangedPath = "test_output/scene_probe_roundtrip_changed.json";
        private const string TruncatedPath = "test_output/scene_probe_truncated.json";
        private const string VersionPath = "test_output/scene_probe_version999.json";
        private const string MissingIdPath = "test_output/scene_probe_missing_id.json";
        private const string BadTypePath = "test_output/scene_probe_bad_type.json";
        private const string MissingVersionPath = "test_output/scene_probe_missing_version.json";
        private const string BadResourcePath = "test_output/scene_probe_bad_resource.json";

        /// <summary>故意不存在的着色器路径（tester 侧另写一份同样的常量，两边不共享）。</summary>
        private const string BadShaderPath = "Shaders/Test/csharp_scene_missing_shader";

        /// <summary>文件里允许出现的全部 JSON 字段名（出现别的键就说明混进了句柄之类的字段）。</summary>
        private static readonly string[] AllowedKeys =
        {
            "formatVersion", "scene", "name", "entities", "id", "transform", "position", "rotation",
            "scale", "meshRenderer", "shaderPath", "color", "vertices",
        };

        private static readonly UTF8Encoding Utf8NoBom = new UTF8Encoding(false);

        private static int s_Phase;
        private static int s_TickCount;
        private static int s_CompletedPhases;
        private static int s_RepeatOnStartDelta;

        internal static int TickCount
        {
            get { return s_TickCount; }
        }

        /// <summary>推进阶段。宿主每帧调一次（只在开关打开时）。</summary>
        internal static void Step()
        {
            ++s_TickCount;
            switch (s_Phase)
            {
                case 0:
                    PhaseIdleAndFormat();
                    break;
                case 1:
                    PhaseRegisterLoaded();
                    break;
                case 2:
                    PhaseRegisterBadResource();
                    break;
                case 3:
                    PhaseUnregister();
                    break;
                default:
                    return;
            }
            ++s_Phase;
            ++s_CompletedPhases;
        }

        // ---- 阶段 0：无活动场景读数 + 序列化 / 失败通道断言 + 注册新场景 ----

        private static void PhaseIdleAndFormat()
        {
            EnsureOutputFolder();
            // ① 这一行必须在**任何注册之前**打：OnStart / OnUpdate / 场景帧数都还得是 0
            //（进程里跑到这一步之前也没有任何用例注册过场景）。
            EmitState("scene-idle");
            RunFormatChecks();
            Scene fresh = BuildRenderScene();
            SceneCodec.Save(fresh, RenderScenePath);
            Host.LogFromScene($"[场景 断言] 渲染场景存盘：{RenderScenePath}");
            SceneRuntime.Register(fresh);
            int beforeRepeat = UserScript.StartCount;
            SceneRuntime.Register(fresh); // 同一个对象再注册一次：不应重复调 OnStart
            s_RepeatOnStartDelta = UserScript.StartCount - beforeRepeat;
            Host.LogFromScene($"[场景 断言] 重复注册同一场景的 OnStart 增量：{s_RepeatOnStartDelta}（期望 0）");
        }

        // ---- 阶段 1：注销 → 从磁盘加载 → 注册 ----

        private static void PhaseRegisterLoaded()
        {
            SceneRuntime.Unregister();
            Scene loaded = SceneCodec.Load(RenderScenePath);
            Host.LogFromScene($"[场景 断言] 从磁盘加载渲染场景：{RenderScenePath}（实体 {loaded.Entities.Count} 个）");
            SceneRuntime.Register(loaded);
        }

        // ---- 阶段 2：注册坏资源场景（下一帧渲染时明确报错）----

        private static void PhaseRegisterBadResource()
        {
            Scene bad = SceneCodec.Load(BadResourcePath);
            SceneRuntime.Register(bad);
        }

        // ---- 阶段 3：注销，回到零行为 ----

        private static void PhaseUnregister()
        {
            SceneRuntime.Unregister();
            Host.LogFromScene("[场景 断言] 注销活动场景：此后 Tick 回到零行为");
        }

        // ---- 读数行 ----

        /// <summary>打一行场景读数（宿主在 Tick 之后调；反映本帧完成后的状态）。</summary>
        internal static void EmitState(string probeName)
        {
            Scene active = SceneRuntime.Active;
            string firstId = string.Empty;
            if (active != null && active.Entities != null && active.Entities.Count > 0 &&
                active.Entities[0] != null)
            {
                firstId = active.Entities[0].Id ?? string.Empty;
            }
            string line = "[托管 读数]" +
                $" probe={probeName}" +
                $" tick={s_TickCount}" +
                $" phase={s_CompletedPhases}" +
                $" active={(active == null ? 0 : 1)}" +
                $" onStart={UserScript.StartCount}" +
                $" onUpdate={UserScript.UpdateCount}" +
                $" lastDtMicros={(long)Math.Round(UserScript.LastUpdateDeltaSeconds * 1000000.0)}" +
                $" startScene={UserScript.LastStartSceneName}" +
                $" updateScene={UserScript.LastUpdateSceneName}" +
                $" repeatDelta={s_RepeatOnStartDelta}" +
                $" renderFrames={SceneRuntime.RenderFrameCount}" +
                $" entities={(active == null || active.Entities == null ? 0 : active.Entities.Count)}" +
                $" rendered={SceneRuntime.RenderedEntities}" +
                $" vertices={SceneRuntime.RenderedVertices}" +
                $" shaderGroups={SceneRuntime.ShaderGroups}" +
                $" firstEntityId={firstId}" +
                $" graph={SceneRuntime.GraphToken}" +
                $" graphSelf={SceneRuntime.GraphSelf}" +
                $" capture={SceneRuntime.CaptureToken}" +
                $" captureSelf={SceneRuntime.CaptureSelf}" +
                $" size={SceneRuntime.CaptureWidth}x{SceneRuntime.CaptureHeight}" +
                $" pass={SceneRuntime.PassCount}" +
                $" present={SceneRuntime.PresentCount}" +
                $" v0xMicros={Micros(SceneRuntime.FirstVertexX)}" +
                $" v0yMicros={Micros(SceneRuntime.FirstVertexY)}" +
                $" v0zMicros={Micros(SceneRuntime.FirstVertexZ)}";
            Host.LogFromScene(line);
        }

        private static long Micros(float value)
        {
            return (long)Math.Round(value * 1000000.0);
        }

        // ---- 序列化 / 失败通道断言（全部真跑 C#）----

        private static void RunFormatChecks()
        {
            // ① 往返逐字段相等：两个实体（各带 Transform 与 MeshRenderer）。
            Scene original = BuildRoundTripScene();
            SceneCodec.Save(original, RoundTripPath);
            byte[] firstBytes = File.ReadAllBytes(RoundTripPath);
            Scene loaded = SceneCodec.Load(RoundTripPath);
            CheckTally tally = new CheckTally();
            CompareScenes(tally, original, loaded);

            // ② 改一个 Position 分量再存：两份文件字节必须不同，且加载回来读到的确实是改后的值。
            float changedX = original.Entities[0].Transform.Position.x + 0.5f;
            original.Entities[0].Transform.Position = SceneValues.Vec(changedX,
                original.Entities[0].Transform.Position.y, original.Entities[0].Transform.Position.z);
            SceneCodec.Save(original, ChangedPath);
            byte[] secondBytes = File.ReadAllBytes(ChangedPath);
            int diffBytes = CountByteDifferences(firstBytes, secondBytes);
            Scene changed = SceneCodec.Load(ChangedPath);
            long reloadDeltaMicros = Micros(changed.Entities[0].Transform.Position.x -
                                           loaded.Entities[0].Transform.Position.x);

            // ③ 失败通道：截断文件 / 版本 999 / 缺 id / 字段类型错 / 缺 formatVersion，各须抛 SceneFormatException。
            byte[] validBytes = firstBytes;
            File.WriteAllBytes(TruncatedPath, TruncateBytes(validBytes));
            string trunc = CatchType("截断文件", () => SceneCodec.Load(TruncatedPath));
            File.WriteAllBytes(VersionPath, Utf8NoBom.GetBytes(
                Variant(BuildTemplate(), "\"formatVersion\": 1", "\"formatVersion\": 999")));
            string version = CatchType("版本999", () => SceneCodec.Load(VersionPath));
            File.WriteAllBytes(MissingIdPath, Utf8NoBom.GetBytes(
                Variant(BuildTemplate(), " \"id\": \"", " \"xid\": \"")));
            string missingId = CatchType("缺id", () => SceneCodec.Load(MissingIdPath));
            File.WriteAllBytes(BadTypePath, Utf8NoBom.GetBytes(
                Variant(BuildTemplate(), "\"position\": [0,0,0]", "\"position\": 0")));
            string badType = CatchType("字段类型错", () => SceneCodec.Load(BadTypePath));
            File.WriteAllBytes(MissingVersionPath, Utf8NoBom.GetBytes(
                Variant(BuildTemplate(), "\"formatVersion\": 1, ", string.Empty)));
            string missingVersion = CatchType("缺formatVersion", () => SceneCodec.Load(MissingVersionPath));

            // ④ Save 的 IO 失败：目录不存在 → IOException（消息带路径名，不是 SceneFormatException）。
            string saveIo = CatchType("保存到不存在的目录", () =>
                {
                    SceneCodec.Save(original, "test_output/不存在目录/scene_probe_io.json");
                    return null;
                });

            // ⑤ Load 不查资源存在性：坏着色器路径的文件必须能正常加载（资源校验在渲染期）。
            Scene badResource = Scene.Create("坏资源场景");
            Entity badEntity = badResource.AddEntity("坏资源实体");
            badEntity.MeshRenderer = MeshRenderer.Create(BadShaderPath, SceneValues.Vec(1.0f, 0.0f, 0.0f));
            SceneCodec.Save(badResource, BadResourcePath);
            string badResLoad = "ok";
            int badResEntities = 0;
            try
            {
                Scene reloadedBad = SceneCodec.Load(BadResourcePath);
                badResEntities = reloadedBad.Entities.Count;
            }
            catch (Exception ex)
            {
                badResLoad = ex.GetType().Name;
                Host.LogFromScene($"[场景 断言] 坏资源名场景竟然加载失败：{ex.GetType().Name}：{ex.Message}");
            }

            // ⑥ 文件里只有已知字段名（没有任何句柄字段）。
            HashSet<string> keys = new HashSet<string>(StringComparer.Ordinal);
            using (JsonDocument document = JsonDocument.Parse(validBytes))
            {
                CollectKeys(document.RootElement, keys);
            }
            int unknownKeys = 0;
            foreach (string key in keys)
            {
                if (Array.IndexOf(AllowedKeys, key) < 0)
                {
                    ++unknownKeys;
                    Host.LogFromScene($"[场景 断言] 文件里出现未登记字段：{key}（可能混进了句柄）");
                }
            }

            Host.LogFromScene(
                "[托管 读数]" +
                " probe=scene-format" +
                $" rtChecked={tally.Checked}" +
                $" rtMismatch={tally.Mismatch}" +
                $" file1Bytes={firstBytes.Length}" +
                $" file2Bytes={secondBytes.Length}" +
                $" diffBytes={diffBytes}" +
                $" reloadDeltaMicros={reloadDeltaMicros}" +
                $" trunc={trunc}" +
                $" version={version}" +
                $" missingId={missingId}" +
                $" badType={badType}" +
                $" missingVersion={missingVersion}" +
                $" saveIo={saveIo}" +
                $" badResLoad={badResLoad}" +
                $" badResEntities={badResEntities}" +
                $" keysTotal={keys.Count}" +
                $" keysUnknown={unknownKeys}");
        }

        /// <summary>往返对拍用的场景：两个实体各带 Transform 与 MeshRenderer，取值互不相同。</summary>
        private static Scene BuildRoundTripScene()
        {
            Scene scene = Scene.Create("往返场景");
            Entity first = scene.AddEntity("实体甲");
            first.Transform.Position = SceneValues.Vec(0.25f, -0.5f, 0.75f);
            first.Transform.Rotation = SceneValues.Vec(0.125f, 0.25f, 0.5f);
            first.Transform.Scale = SceneValues.Vec(2.0f, 3.0f, 4.0f);
            first.MeshRenderer = MeshRenderer.Create("Shaders/Test/TestSimpleTriangle",
                SceneValues.Vec(1.0f, 0.0f, 0.0f));

            Entity second = scene.AddEntity("实体乙");
            second.Transform.Position = SceneValues.Vec(-0.75f, 0.5f, 0.25f);
            second.Transform.Rotation = SceneValues.Vec(-0.125f, 0.0f, 0.0625f);
            second.Transform.Scale = SceneValues.Vec(0.5f, 0.25f, 1.5f);
            second.MeshRenderer = MeshRenderer.Create("Shaders/Test/TestNaiveTriangle",
                SceneValues.Vec(0.0f, 1.0f, 0.25f));
            // 顶点也换成非默认值：证明 vertices 真进了文件（不是恒等于默认三角形）。
            second.MeshRenderer.Vertices = new[]
            {
                -0.5f, -0.125f, 0.25f,
                 0.125f, -0.25f, 0.5f,
                 0.0f,  0.375f, -0.25f,
            };
            return scene;
        }

        /// <summary>渲染用例的场景：一个主三角（非单位 Transform）+ 一个角上的三角 + 一个没有渲染成分的实体。</summary>
        private static Scene BuildRenderScene()
        {
            Scene scene = Scene.Create("场景甲");
            Entity main = scene.AddEntity("主三角");
            // Position (0.25,0,0) + Scale 2：把默认三角形（中心已在原点左侧）整体放大并右移，
            // 中心 (0,0) 仍被覆盖 → 离屏捕获中心必须是纯红。v0 = (-0.25*2+0.25, -0.25*2, 0) = (-0.25,-0.5,0)。
            main.Transform.Position = SceneValues.Vec(0.25f, 0.0f, 0.0f);
            // Rotation 非零：序列化保留、渲染**暂不施加**（design D6）—— 顶点读数据此可判「没有施加旋转」。
            main.Transform.Rotation = SceneValues.Vec(0.0f, 0.0f, 1.5707964f);
            main.Transform.Scale = SceneValues.Vec(2.0f, 2.0f, 2.0f);
            main.MeshRenderer = MeshRenderer.Create("Shaders/Test/TestSimpleTriangle",
                SceneValues.Vec(1.0f, 0.0f, 0.0f));

            Entity corner = scene.AddEntity("角三角");
            corner.Transform.Position = SceneValues.Vec(0.9f, 0.9f, 0.0f);
            corner.Transform.Scale = SceneValues.Vec(0.1f, 0.1f, 0.1f);
            corner.MeshRenderer = MeshRenderer.Create("Shaders/Test/TestSimpleTriangle",
                SceneValues.Vec(0.0f, 1.0f, 0.0f));

            scene.AddEntity("没有渲染成分");
            return scene;
        }

        // ---- 小工具 ----

        private static void EnsureOutputFolder()
        {
            try
            {
                Directory.CreateDirectory(OutputFolder);
            }
            catch (Exception ex)
            {
                Host.LogFromScene("[场景 断言] test_output 目录建不出来：" + ex.GetType().Name + "：" + ex.Message);
            }
        }

        private static byte[] TruncateBytes(byte[] bytes)
        {
            // 截到大约三分之二：JSON 一定不完整（解析必然失败），但开头是合法的。
            int keep = bytes.Length * 2 / 3;
            byte[] truncated = new byte[keep];
            Array.Copy(bytes, truncated, keep);
            return truncated;
        }

        private static string BuildTemplate()
        {
            return "{ \"formatVersion\": 1, \"scene\": { \"name\": \"夹具\", \"entities\": [ " +
                   "{ \"id\": \"11111111-1111-1111-1111-111111111111\", \"name\": \"实体\", " +
                   "\"transform\": { \"position\": [0,0,0], \"rotation\": [0,0,0], \"scale\": [1,1,1] }, " +
                   "\"meshRenderer\": null } ] } }";
        }

        private static string Variant(string template, string from, string to)
        {
            if (template.IndexOf(from, StringComparison.Ordinal) < 0)
            {
                // 模板被改坏了：宁可当场炸掉（宿主会把它变成非零返回码），也不要静默测了个没改的文件。
                throw new InvalidOperationException($"场景探针夹具错误：模板里找不到片段 {from}");
            }
            return template.Replace(from, to);
        }

        private static int CountByteDifferences(byte[] left, byte[] right)
        {
            int count = 0;
            int common = left.Length < right.Length ? left.Length : right.Length;
            for (int index = 0; index < common; ++index)
            {
                if (left[index] != right[index])
                {
                    ++count;
                }
            }
            return count + (left.Length > right.Length ? left.Length - right.Length : right.Length - left.Length);
        }

        private static string CatchType(string label, Func<object> action)
        {
            try
            {
                action();
                Host.LogFromScene($"[场景 断言] {label} → 没有抛异常（期望报错）");
                return "none";
            }
            catch (Exception ex)
            {
                Host.LogFromScene($"[场景 断言] {label} → {ex.GetType().Name}：{ex.Message}");
                return ex.GetType().Name;
            }
        }

        private static void CollectKeys(JsonElement element, HashSet<string> keys)
        {
            switch (element.ValueKind)
            {
                case JsonValueKind.Object:
                    foreach (JsonProperty property in element.EnumerateObject())
                    {
                        keys.Add(property.Name);
                        CollectKeys(property.Value, keys);
                    }
                    break;
                case JsonValueKind.Array:
                    foreach (JsonElement item in element.EnumerateArray())
                    {
                        CollectKeys(item, keys);
                    }
                    break;
                default:
                    break;
            }
        }

        /// <summary>逐字段对拍：checked 是被实际比较过的字段个数（漏比一个字段，读数就会变小）。</summary>
        private sealed class CheckTally
        {
            internal int Checked;
            internal int Mismatch;

            internal void Text(string field, string actual, string expected)
            {
                ++Checked;
                if (!string.Equals(actual, expected, StringComparison.Ordinal))
                {
                    ++Mismatch;
                    Host.LogFromScene($"[场景 断言] 字段不符：{field} 读到「{actual}」，期望「{expected}」");
                }
            }

            internal void Int(string field, int actual, int expected)
            {
                ++Checked;
                if (actual != expected)
                {
                    ++Mismatch;
                    Host.LogFromScene($"[场景 断言] 字段不符：{field} 读到 {actual}，期望 {expected}");
                }
            }

            internal void Bool(string field, bool actual, bool expected)
            {
                ++Checked;
                if (actual != expected)
                {
                    ++Mismatch;
                    Host.LogFromScene($"[场景 断言] 字段不符：{field} 读到 {actual}，期望 {expected}");
                }
            }

            internal void Float(string field, float actual, float expected)
            {
                ++Checked;
                if (actual != expected)
                {
                    ++Mismatch;
                    Host.LogFromScene($"[场景 断言] 字段不符：{field} 读到 {actual}，期望 {expected}");
                }
            }

            internal void Float3(string field, float3 actual, float3 expected)
            {
                Float(field + ".x", actual.x, expected.x);
                Float(field + ".y", actual.y, expected.y);
                Float(field + ".z", actual.z, expected.z);
            }

            internal void Floats(string field, float[] actual, float[] expected)
            {
                Int(field + ".Length", actual == null ? 0 : actual.Length, expected == null ? 0 : expected.Length);
                if (actual == null || expected == null || actual.Length != expected.Length)
                {
                    return;
                }
                for (int index = 0; index < expected.Length; ++index)
                {
                    Float(field + "[" + index + "]", actual[index], expected[index]);
                }
            }
        }

        private static void CompareScenes(CheckTally tally, Scene expected, Scene actual)
        {
            tally.Text("scene.name", actual.Name, expected.Name);
            tally.Int("scene.entities.Count", actual.Entities.Count, expected.Entities.Count);
            if (actual.Entities.Count != expected.Entities.Count)
            {
                return;
            }
            for (int index = 0; index < expected.Entities.Count; ++index)
            {
                Entity left = expected.Entities[index];
                Entity right = actual.Entities[index];
                string prefix = "entities[" + index + "].";
                tally.Text(prefix + "id", right.Id, left.Id);
                tally.Text(prefix + "name", right.Name, left.Name);
                tally.Float3(prefix + "transform.position", right.Transform.Position, left.Transform.Position);
                tally.Float3(prefix + "transform.rotation", right.Transform.Rotation, left.Transform.Rotation);
                tally.Float3(prefix + "transform.scale", right.Transform.Scale, left.Transform.Scale);
                tally.Bool(prefix + "meshRenderer!=null", right.MeshRenderer != null, left.MeshRenderer != null);
                if (left.MeshRenderer == null || right.MeshRenderer == null)
                {
                    continue;
                }
                tally.Text(prefix + "meshRenderer.shaderPath", right.MeshRenderer.ShaderPath,
                    left.MeshRenderer.ShaderPath);
                tally.Float3(prefix + "meshRenderer.color", right.MeshRenderer.Color, left.MeshRenderer.Color);
                tally.Floats(prefix + "meshRenderer.vertices", right.MeshRenderer.Vertices,
                    left.MeshRenderer.Vertices);
            }
        }
    }
}
