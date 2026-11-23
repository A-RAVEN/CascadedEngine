using System;
using System.Globalization;
using System.IO;
using System.Text;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.CsScript;
using Scripting.Generated.Bindings.GraphicsBackend;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 资源资产探针（csharp-resource-asset-foundation 4.1）：环境变量门控 + 阶段机 + 读数文件，
    /// 与 `ResourceProbe` 同一套做法。默认关（不设 `CASCADED_CSHARP_ASSET_PROBE=1` 时 Tick 立即返回、
    /// 零行为、零日志、零状态改变）。
    ///
    /// 它验证两件事：① C# 经宿主门面 `IResourceRegistry` 自助完成「注册工厂 → 新建 → 填数据 → 落盘 →
    /// 自读加载 → 替换」；② 失败面（坏句柄 / 未注册类型 / 同路径 / 损坏版本 / 缺失文件 / 序列化抛异常
    /// 两段式）的错误码与文本都能被 C# 侧读到。C++ 侧 `TestCSharpAssets` 读同一份读数文件并断言
    /// 写死期望（不从被测物推导）。
    ///
    /// 读数通道为什么用文件：跨边界只有 Tick 的 int 回值，而本用例要传句柄（ulong）、十六进制字节、
    /// 错误码与文本；与 `CASCADED_CSHARP_RESOURCE_READINGS` / `CASCADED_EDITOR_READINGS_FILE` 同一套。
    /// </summary>
    internal static unsafe class ResourceAssetProbe
    {
        private const string ProbeVariable = "CASCADED_CSHARP_ASSET_PROBE";
        private const string PhaseVariable = "CASCADED_CSHARP_ASSET_PHASE";
        private const string ReadingsVariable = "CASCADED_CSHARP_ASSET_READINGS";

        // 路径全部相对资源根（C++ 侧设成临时目录）；探针与 C++ 用例各自写死同一份。
        internal const string TexturePath = "Assets/Probe.tex";
        internal const string MeshPath = "Assets/Probe.mesh";
        internal const string BrokenPath = "Assets/ProbeBroken.tex";
        internal const string TruncatedPath = "Assets/ProbeTruncated.tex";
        internal const string MissingPath = "Assets/ProbeMissing.tex";
        internal const string ThrowingPath = "Assets/ProbeThrowing.res";
        internal const string ThrowingText = "资产序列化故意抛出的异常";
        internal const string ThrowingTypeName = "ThrowingAsset";

        // 写死数据（C++ 侧另写一份期望值，不共享常量表）
        internal const int TextureWidth = 2;
        internal const int TextureHeight = 2;
        internal const int TextureFormatValue = 6; // E_R8G8B8A8_UNORM
        internal const int MeshVertexCount = 3;
        internal const int MeshIndexCount = 3;

        private static bool s_tablesBound;
        private static string s_lastPhase = "\u0000";
        private static IResourceRegistry s_registry;
        private static AssetDatabase s_database;
        private static Texture2D s_texture;
        private static Mesh s_mesh;

        /// <summary>门控开关（Host 只在它为真时才确保渲染表绑定）。</summary>
        internal static bool Enabled =>
            Environment.GetEnvironmentVariable(ProbeVariable) == "1";

        // ---- 读数写入 ----
        private static void Write(string key, object value)
        {
            string path = Environment.GetEnvironmentVariable(ReadingsVariable);
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            string text = key + "=" + Convert.ToString(value, CultureInfo.InvariantCulture) + "\n";
            // 不带 BOM：原生侧按行首 `key=` 解析，第一行带 BOM 会让第一条读数读不到。
            File.AppendAllText(path, text, new UTF8Encoding(false));
        }

        private static void Fail(string text)
        {
            Write("probe_error", text);
            throw new InvalidOperationException("[资源资产探针] " + text);
        }

        // 原生侧 `SerializeAll` 的写批在 IOManager 的流缓存里保持着目标文件打开（提交后不关流，
        // 实测），所以探针对这些文件的读写都显式用 FileShare.ReadWrite；默认 `File.*` 的共享模式
        // 会与那个写句柄冲突。
        private static byte[] ReadSharedBytes(string fullPath)
        {
            using var stream = new FileStream(fullPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            if (stream.Length > int.MaxValue)
            {
                throw new IOException($"文件过大：{stream.Length} 字节");
            }
            byte[] bytes = new byte[(int)stream.Length];
            int read = 0;
            while (read < bytes.Length)
            {
                int count = stream.Read(bytes, read, bytes.Length - read);
                if (count == 0)
                {
                    break;
                }
                read += count;
            }
            if (read != bytes.Length)
            {
                throw new IOException($"读取不完整：期望 {bytes.Length} 字节，实际 {read} 字节");
            }
            return bytes;
        }

        private static void WriteSharedBytes(string fullPath, byte[] bytes)
        {
            using var stream = new FileStream(fullPath, FileMode.Create, FileAccess.Write,
                                              FileShare.ReadWrite);
            stream.Write(bytes, 0, bytes.Length);
        }

        // ---- 表绑定 ----
        private delegate void BindTableDelegate(byte* table);

        private static void BindTables(EngineApiMirror* api)
        {
            if (s_tablesBound)
            {
                return;
            }
            uint facadeId = EngineApiBindings.CsScript__ResourceRegistryInterfaceId;
            uint facadeSize = api->GetInterfaceApi(facadeId, null, EngineApiBindings.TableVersion, 0, null, null);
            if (facadeSize == 0)
            {
                Fail($"取门面函数表失败：编号 {facadeId} 没有登记（kBindingPolicies 缺行？）");
            }
            byte* facadeTable = stackalloc byte[(int)facadeSize];
            void* facadeInstance = null;
            uint facadeWritten = api->GetInterfaceApi(facadeId, null, EngineApiBindings.TableVersion, facadeSize,
                                                      facadeTable, &facadeInstance);
            if (facadeWritten != facadeSize || facadeInstance == null)
            {
                Fail($"取门面函数表失败：写入 {facadeWritten} / {facadeSize} 字节，实例 {(long)facadeInstance}");
            }
            IResourceRegistryBindings.Bind(facadeTable);
            s_registry = new IResourceRegistry(new IResourceRegistryLease { Self = facadeInstance });
            s_database = new AssetDatabase(s_registry);
            Write("facade_size", facadeSize);
            Write("facade_instance", facadeInstance == null ? 0 : 1);
            BindReverse(api, EngineApiBindings.ResourceManagement__ResourceFactoryInterfaceId, "IResourceFactory",
                        table => IResourceFactoryBindings.Bind(table));
            BindReverse(api, EngineApiBindings.ResourceManagement__ResourceInterfaceId, "IResource",
                        table => IResourceBindings.Bind(table));
            s_tablesBound = true;
        }

        private static void BindReverse(EngineApiMirror* api, uint id, string name, BindTableDelegate bind)
        {
            uint size = api->GetInterfaceApi(id, null, EngineApiBindings.TableVersion, 0, null, null);
            if (size == 0)
            {
                Fail($"取 {name} 反向函数表失败：编号 {id} 没有登记（kBindingPolicies 缺行？）");
            }
            byte* table = stackalloc byte[(int)size];
            uint written = api->GetInterfaceApi(id, null, EngineApiBindings.TableVersion, size, table, null);
            if (written != size)
            {
                Fail($"取 {name} 反向函数表失败：写入 {written} 字节、期望 {size} 字节");
            }
            bind(table);
        }

        // ---- 写死数据 ----
        private static byte[] BuildYellowPixels()
        {
            byte[] pixels = new byte[TextureWidth * TextureHeight * 4];
            for (int texel = 0; texel < TextureWidth * TextureHeight; ++texel)
            {
                pixels[texel * 4 + 0] = 255;
                pixels[texel * 4 + 1] = 255;
                pixels[texel * 4 + 2] = 0;
                pixels[texel * 4 + 3] = 255;
            }
            return pixels;
        }

        private static byte[] BuildGreenPixels()
        {
            byte[] pixels = new byte[TextureWidth * TextureHeight * 4];
            for (int texel = 0; texel < TextureWidth * TextureHeight; ++texel)
            {
                pixels[texel * 4 + 0] = 0;
                pixels[texel * 4 + 1] = 255;
                pixels[texel * 4 + 2] = 0;
                pixels[texel * 4 + 3] = 255;
            }
            return pixels;
        }

        private static byte[] BuildMeshVertices()
        {
            float[] values =
            {
                -0.25f, -0.25f, 0.25f, 1.0f, 0.0f, 0.0f,
                 0.25f, -0.25f, 0.25f, 1.0f, 0.0f, 0.0f,
                 0.00f,  0.50f, 0.50f, 1.0f, 0.0f, 0.0f,
            };
            byte[] bytes = new byte[values.Length * sizeof(float)];
            Buffer.BlockCopy(values, 0, bytes, 0, bytes.Length);
            return bytes;
        }

        private static byte[] BuildMeshIndices()
        {
            uint[] values = { 0u, 1u, 2u };
            byte[] bytes = new byte[values.Length * sizeof(uint)];
            Buffer.BlockCopy(values, 0, bytes, 0, bytes.Length);
            return bytes;
        }

        // ---- 阶段机 ----
        /// <summary>每个 Tick 调一次（Host.Tick 里，门控开时）。默认关 = 零行为。</summary>
        internal static void Tick(EngineApiMirror* api, ScriptRenderBackend backend)
        {
            if (api == null || !Enabled)
            {
                return;
            }
            string phase = Environment.GetEnvironmentVariable(PhaseVariable) ?? string.Empty;
            if (phase.Length == 0 || phase == s_lastPhase)
            {
                return;
            }
            s_lastPhase = phase;
            try
            {
                BindTables(api);
                switch (phase)
                {
                    case "register-factories":
                        RegisterFactories();
                        break;
                    case "create-and-save":
                        CreateAndSave();
                        break;
                    case "release-and-load":
                        ReleaseAndLoad();
                        break;
                    case "replace":
                        Replace();
                        break;
                    case "failure":
                        FailureSurface();
                        break;
                    case "render":
                        Render(backend);
                        break;
                    default:
                        Fail($"未知的阶段：{phase}");
                        break;
                }
                Write("phase", phase);
            }
            catch (Exception ex)
            {
                Write("probe_error", ex.GetType().Name + ": " + ex.Message);
                throw;
            }
        }

        private static void RegisterFactories()
        {
            // 工厂对象由 `IResourceFactoryBindings.Create` 分配的 GCHandle 保活（强句柄交给注册表），
            // 不需要 C# 侧另存字段；句柄处置权在 register/unregister 路径。
            var textureFactory = new Texture2DFactory();
            ulong textureHandle = IResourceFactoryBindings.Create(textureFactory);
            s_registry.RegisterFactory(Texture2DFactory.TypeName, textureHandle);
            var meshFactory = new MeshFactory();
            ulong meshHandle = IResourceFactoryBindings.Create(meshFactory);
            s_registry.RegisterFactory(MeshFactory.TypeName, meshHandle);
            var throwingFactory = new ThrowingAssetFactory();
            ulong throwingHandle = IResourceFactoryBindings.Create(throwingFactory);
            s_registry.RegisterFactory(ThrowingTypeName, throwingHandle);
            Write("texture_factory_handle", textureHandle);
            Write("mesh_factory_handle", meshHandle);
            Write("throwing_factory_handle", throwingHandle);
            Write("texture_factory_type", Texture2DFactory.TypeName);
            Write("has_texture_factory", s_registry.HasFactory(Texture2DFactory.TypeName) ? 1 : 0);
            Write("has_mesh_factory", s_registry.HasFactory(MeshFactory.TypeName) ? 1 : 0);
            Write("facade_root", s_registry.GetResourceRootPath());
            Write("pending_count", AssetFactoryBase.PendingCount);
        }

        private static void CreateAndSave()
        {
            s_texture = (Texture2D)s_database.Create(Texture2DFactory.TypeName, TexturePath);
            s_texture.Width = TextureWidth;
            s_texture.Height = TextureHeight;
            s_texture.Format = ETextureFormat.E_R8G8B8A8_UNORM;
            s_texture.Pixels = BuildYellowPixels();
            Write("texture_handle", IResourceBindings.GetBorrowHandle(s_texture));
            Write("texture_width", s_texture.Width);
            Write("texture_height", s_texture.Height);
            Write("texture_format", (int)s_texture.Format);
            Write("texture_pixel_len", s_texture.Pixels.Length);

            s_mesh = (Mesh)s_database.Create(MeshFactory.TypeName, MeshPath);
            s_mesh.VertexData = BuildMeshVertices();
            s_mesh.IndexData = BuildMeshIndices();
            s_mesh.VertexCount = MeshVertexCount;
            s_mesh.IndexCount = MeshIndexCount;
            Write("mesh_handle", IResourceBindings.GetBorrowHandle(s_mesh));
            Write("mesh_vertex_count", s_mesh.VertexCount);
            Write("mesh_index_count", s_mesh.IndexCount);
            Write("mesh_vertex_len", s_mesh.VertexData.Length);
            Write("mesh_index_len", s_mesh.IndexData.Length);
            Write("pending_count", AssetFactoryBase.PendingCount);

            s_database.SaveAll();
            Write("saved", 1);
        }

        private static void ReleaseAndLoad()
        {
            Write("release_texture", s_registry.ReleaseResource(TexturePath) ? 1 : 0);
            Write("release_mesh", s_registry.ReleaseResource(MeshPath) ? 1 : 0);
            Write("has_texture_after_release", s_registry.HasResource(TexturePath) ? 1 : 0);
            Write("has_mesh_after_release", s_registry.HasResource(MeshPath) ? 1 : 0);

            s_texture = (Texture2D)s_database.Load(TexturePath);
            s_mesh = (Mesh)s_database.Load(MeshPath);
            Write("load_width", s_texture.Width);
            Write("load_height", s_texture.Height);
            Write("load_format", (int)s_texture.Format);
            Write("load_pixels_hex", Convert.ToHexString(s_texture.Pixels));
            Write("load_vertex_hex", Convert.ToHexString(s_mesh.VertexData));
            Write("load_index_hex", Convert.ToHexString(s_mesh.IndexData));
            Write("load_vertex_count", s_mesh.VertexCount);
            Write("load_index_count", s_mesh.IndexCount);
            Write("load_texture_handle", IResourceBindings.GetBorrowHandle(s_texture));
            Write("load_mesh_handle", IResourceBindings.GetBorrowHandle(s_mesh));
            Write("pending_count", AssetFactoryBase.PendingCount);
        }

        private static void Replace()
        {
            // 第一次加载回来的对象句柄：第二次加载替换后，它的代理必须已析构（C++ 侧按 1001 判）。
            ulong oldTextureHandle = IResourceBindings.GetBorrowHandle(s_texture);
            Write("replace_old_handle", oldTextureHandle);
            Write("replace_old_pixels_hex", Convert.ToHexString(s_texture.Pixels));

            // 直接用候选资产把绿色数据序列化后覆盖原文件（磁盘上的第二次数据不同）。
            string fullPath = s_registry.GetResourceFullPath(TexturePath);
            var greenAsset = new Texture2D
            {
                Width = TextureWidth,
                Height = TextureHeight,
                Format = ETextureFormat.E_R8G8B8A8_UNORM,
                Pixels = BuildGreenPixels(),
            };
            WriteSharedBytes(fullPath, greenAsset.Serialize());
            s_texture = (Texture2D)s_database.Load(TexturePath);
            Write("replace_loaded_pixels_hex", Convert.ToHexString(s_texture.Pixels));
            Write("replace_has_resource", s_registry.HasResource(TexturePath) ? 1 : 0);
            Write("replace_new_handle", IResourceBindings.GetBorrowHandle(s_texture));
            Write("pending_count", AssetFactoryBase.PendingCount);
        }

        private static void FailureSurface()
        {
            // ① 坏句柄：0
            CaptureRegistryFailure("bad_handle", () => s_registry.RegisterFactory("BadHandleProbe", 0UL));

            // ② 已释放的句柄：先造一个工厂句柄、退役它，再拿它注册
            var tempFactory = new Texture2DFactory();
            ulong tempHandle = IResourceFactoryBindings.Create(tempFactory);
            Write("released_prepare_retire_code", IResourceFactoryBindings.Release(tempHandle));
            CaptureRegistryFailure("released_handle",
                () => s_registry.RegisterFactory(Texture2DFactory.TypeName, tempHandle));

            // ③ 未注册类型
            CaptureRegistryFailure("unregistered",
                () => s_registry.CreateResource("NoSuchAssetType", "Assets/NoSuch.res"));

            // ④ 同路径直接新建：门面必须在注册表 get_or_create 之前拒绝，旧对象不被析构
            ulong meshHandle = IResourceBindings.GetBorrowHandle(s_mesh);
            CaptureRegistryFailure("same_path",
                () => s_registry.CreateResource(MeshFactory.TypeName, MeshPath));
            Write("same_path_has_resource", s_registry.HasResource(MeshPath) ? 1 : 0);
            Write("same_path_old_alive", IResourceBindings.Resolve(meshHandle) != IntPtr.Zero ? 1 : 0);
            Write("same_path_vertex_hex", Convert.ToHexString(s_mesh.VertexData));

            // ⑤ 损坏版本：先有一个合法条目，再把文件版本改成 2；加载必须抛 AssetFormatException 且旧条目仍在
            var broken = (Texture2D)s_database.Create(Texture2DFactory.TypeName, BrokenPath);
            broken.Width = 1;
            broken.Height = 1;
            broken.Format = ETextureFormat.E_R8G8B8A8_UNORM;
            broken.Pixels = new byte[] { 1, 2, 3, 4 };
            s_database.SaveAll();
            ulong brokenHandle = IResourceBindings.GetBorrowHandle(broken);
            string brokenFull = s_registry.GetResourceFullPath(BrokenPath);
            byte[] validBytes = ReadSharedBytes(brokenFull);
            byte[] corrupted = (byte[])validBytes.Clone();
            corrupted[4] = 2;
            corrupted[5] = 0;
            corrupted[6] = 0;
            corrupted[7] = 0;
            WriteSharedBytes(brokenFull, corrupted);
            CaptureAssetFormatFailure("corrupt", () => s_database.Load(BrokenPath));
            Write("corrupt_has_resource", s_registry.HasResource(BrokenPath) ? 1 : 0);
            Write("corrupt_old_alive", IResourceBindings.Resolve(brokenHandle) != IntPtr.Zero ? 1 : 0);
            Write("corrupt_file_hex", Convert.ToHexString(ReadSharedBytes(brokenFull)));

            // ⑥ 截短文件：保留固定头部 28 字节 + 载荷的一半（8 字节），声明长度 16 > 剩余 8 → 越界
            var truncated = (Texture2D)s_database.Create(Texture2DFactory.TypeName, TruncatedPath);
            truncated.Width = TextureWidth;
            truncated.Height = TextureHeight;
            truncated.Format = ETextureFormat.E_R8G8B8A8_UNORM;
            truncated.Pixels = BuildYellowPixels();
            s_database.SaveAll();
            ulong truncatedHandle = IResourceBindings.GetBorrowHandle(truncated);
            string truncatedFull = s_registry.GetResourceFullPath(TruncatedPath);
            byte[] truncatedBytes = ReadSharedBytes(truncatedFull);
            byte[] halfPayload = new byte[Texture2D.HeaderSize + truncated.Pixels.Length / 2];
            Array.Copy(truncatedBytes, halfPayload, halfPayload.Length);
            WriteSharedBytes(truncatedFull, halfPayload);
            CaptureAssetFormatFailure("truncated", () => s_database.Load(TruncatedPath));
            Write("truncated_has_resource", s_registry.HasResource(TruncatedPath) ? 1 : 0);
            Write("truncated_old_alive", IResourceBindings.Resolve(truncatedHandle) != IntPtr.Zero ? 1 : 0);
            Write("truncated_file_len", halfPayload.Length);

            // ⑦ 缺失文件：先建合法条目（**不落盘** —— 原生写批的流缓存会一直开着文件，删不掉；
            // 这里要的正是"磁盘上不存在 + 注册表有旧条目"），加载必须抛 AssetFormatException（包装 IO）
            // 且旧条目仍在。
            var missing = (Texture2D)s_database.Create(Texture2DFactory.TypeName, MissingPath);
            missing.Width = 1;
            missing.Height = 1;
            missing.Format = ETextureFormat.E_R8G8B8A8_UNORM;
            missing.Pixels = new byte[] { 4, 3, 2, 1 };
            ulong missingHandle = IResourceBindings.GetBorrowHandle(missing);
            string missingFull = s_registry.GetResourceFullPath(MissingPath);
            Write("missing_file_exists_before_load", File.Exists(missingFull) ? 1 : 0);
            CaptureAssetFormatFailure("missing", () => s_database.Load(MissingPath));
            Write("missing_has_resource", s_registry.HasResource(MissingPath) ? 1 : 0);
            Write("missing_old_alive", IResourceBindings.Resolve(missingHandle) != IntPtr.Zero ? 1 : 0);
            Write("missing_file_exists", File.Exists(missingFull) ? 1 : 0);

            // ⑧ Serialize 异常两段式：首次落盘该资源缺席（文件保持写前字节、其余照写），
            //    第二次经该代理的调用抛 ReverseBindingError(1012)，门面转 1014 并保留原始文本。
            _ = (ThrowingAsset)s_database.Create(ThrowingTypeName, ThrowingPath);
            string throwingFull = s_registry.GetResourceFullPath(ThrowingPath);
            byte[] sentinel = { 0x5A, 0x5A, 0x5A, 0x5A };
            WriteSharedBytes(throwingFull, sentinel);
            try
            {
                s_database.SaveAll();
                Write("serialize_first_threw", 0);
                Write("serialize_first_text", "没有抛异常");
            }
            catch (Exception ex)
            {
                Write("serialize_first_threw", 1);
                Write("serialize_first_text", ex.GetType().Name + ": " + ex.Message);
            }
            byte[] afterFirst = File.Exists(throwingFull) ? ReadSharedBytes(throwingFull) : Array.Empty<byte>();
            Write("serialize_after_hex", Convert.ToHexString(afterFirst));
            Write("serialize_texture_exists",
                  File.Exists(s_registry.GetResourceFullPath(TexturePath)) ? 1 : 0);
            try
            {
                s_database.SaveAll();
                Write("serialize_second_code", 0);
                Write("serialize_second_text", "没有抛异常");
            }
            catch (IResourceRegistryException ex)
            {
                Write("serialize_second_type", ex.GetType().Name);
                Write("serialize_second_code", ex.Code);
                Write("serialize_second_text", ex.Message);
            }
            catch (Exception ex)
            {
                Write("serialize_second_type", ex.GetType().Name);
                Write("serialize_second_code", -1);
                Write("serialize_second_text", ex.GetType().Name + ": " + ex.Message);
            }
            Write("pending_count", AssetFactoryBase.PendingCount);
        }

        private static void CaptureRegistryFailure(string prefix, Action action)
        {
            try
            {
                action();
                Write(prefix + "_type", "没有抛异常");
                Write(prefix + "_code", 0);
                Write(prefix + "_text", "没有抛异常");
            }
            catch (IResourceRegistryException ex)
            {
                Write(prefix + "_type", ex.GetType().Name);
                Write(prefix + "_code", ex.Code);
                Write(prefix + "_text", ex.Message);
            }
            catch (Exception ex)
            {
                Write(prefix + "_type", ex.GetType().Name);
                Write(prefix + "_code", -1);
                Write(prefix + "_text", ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static void CaptureAssetFormatFailure(string prefix, Action action)
        {
            try
            {
                action();
                Write(prefix + "_type", "没有抛异常");
                Write(prefix + "_text", string.Empty);
            }
            catch (AssetFormatException ex)
            {
                Write(prefix + "_type", ex.GetType().Name);
                Write(prefix + "_text", ex.Message);
            }
            catch (Exception ex)
            {
                Write(prefix + "_type", ex.GetType().Name);
                Write(prefix + "_text", ex.Message);
            }
        }

        // ---- 渲染消费（csharp-resource-asset-foundation 4.3）----
        // Texture2D 的像素字节 → 既有正向绑定 `CreateGPUTexture`（经糖层 CreateTexture）→ 纹理三角形；
        // Mesh 的顶点/索引字节 → 图内缓冲绑定（AllocBuffer + ScheduleData）→ 索引绘制。
        // 两张图各自一个离屏捕获目标；读数文件写出图对象地址，C++ 侧按图附件取回离屏纹理做像素判据。
        private const uint RenderWidth = 320u;
        private const uint RenderHeight = 240u;
        private const string RenderTextureName = "assetRenderTexture2x2";
        private const string RenderShaderPath = "Shaders/Test/TestCSharpTextureSampling";
        private const string MeshShaderPath = "Shaders/Test/TestSimpleTriangle";
        // 流名与缓冲名逐字相同 —— 与既有纹理/三角形探针的写法对齐（排除命名差异这个变量）。
        private const string TextureStreamName = "assetTextureVertices";
        private const string TextureBufferName = "assetTextureVertices";
        private const string MeshStreamName = "assetMeshVertices";
        private const string MeshVertexBufferName = "assetMeshVertices";
        private const string MeshIndexBufferName = "assetMeshIndices";

        private static ScriptRenderTarget s_textureTarget;
        private static ScriptRenderTarget s_meshTarget;
        private static ScriptRenderGraph s_textureGraph;
        private static ScriptRenderGraph s_meshGraph;
        private static ScriptTexture s_renderTexture;
        private static ScriptShaderStruct s_textureParams;

        private static void Check(int status, ScriptRenderBackend backend, string step)
        {
            if (status == 0)
            {
                return;
            }
            Fail($"{step} 被拒绝：{backend.GetLastError()}");
        }

        private static TextureSamplerDescriptor LinearClamp()
        {
            return new TextureSamplerDescriptor
            {
                magFilterMode = ETextureSamplerFilterMode.eLinear,
                minFilterMode = ETextureSamplerFilterMode.eLinear,
                mipmapFilterMode = ETextureSamplerFilterMode.eLinear,
                addressModeU = ETextureSamplerAddressMode.eClampToEdge,
                addressModeV = ETextureSamplerAddressMode.eClampToEdge,
                addressModeW = ETextureSamplerAddressMode.eClampToEdge,
                boarderColor = ETextureSamplerBorderColor.eTransparentBlack,
                integerFormat = false,
            };
        }

        private static byte[] BuildTextureTriangleVertices()
        {
            float[] values =
            {
                -0.25f, -0.25f, 0.25f, 0.0f, 0.0f,
                 0.25f, -0.25f, 0.25f, 1.0f, 0.0f,
                 0.00f,  0.50f, 0.50f, 0.5f, 1.0f,
            };
            byte[] bytes = new byte[values.Length * sizeof(float)];
            Buffer.BlockCopy(values, 0, bytes, 0, bytes.Length);
            return bytes;
        }

        private static void Render(ScriptRenderBackend backend)
        {
            if (backend == null)
            {
                Fail("渲染阶段：渲染后端表没有绑定（CRenderBackend 未登记？）");
            }
            s_textureTarget = backend.CreateCaptureTarget(RenderWidth, RenderHeight);
            s_meshTarget = backend.CreateCaptureTarget(RenderWidth, RenderHeight);
            if (s_textureTarget == null || s_meshTarget == null)
            {
                Fail("渲染阶段：CreateCaptureTarget 返回空：" + backend.GetLastError());
            }

            // ① 纹理三角形：贴图字节来自资产（替换阶段之后是绿色），经 CreateTexture → CreateGPUTexture。
            s_textureGraph = backend.CreateGraph();
            if (s_textureGraph == null)
            {
                Fail("渲染阶段：CreateGraph 返回空：" + backend.GetLastError());
            }
            Check(s_textureGraph.AddBuffer(TextureBufferName,
                      new GPUBufferDescriptor { count = 3UL, stride = 20UL }, BuildTextureTriangleVertices()),
                  backend, "AddBuffer(纹理顶点)");
            Check(s_textureGraph.SetPositionTextureStream(TextureStreamName, 20u, 0u, 12u), backend,
                  "SetPositionTextureStream");
            s_renderTexture = backend.CreateTexture(RenderTextureName, (uint)s_texture.Width,
                                                    (uint)s_texture.Height, s_texture.Pixels);
            if (s_renderTexture == null)
            {
                Fail("渲染阶段：CreateTexture 返回空：" + backend.GetLastError());
            }
            s_textureParams = backend.CreateShaderStruct("TextureSampleBlock");
            if (s_textureParams == null)
            {
                Fail("渲染阶段：CreateShaderStruct(TextureSampleBlock) 返回空：" + backend.GetLastError());
            }
            if (s_textureParams.SetImage("testTexture", s_renderTexture) != 0)
            {
                Fail("渲染阶段：SetImage 被拒绝：" + s_textureParams.GetLastError());
            }
            if (s_textureParams.SetSampler("testSampler", LinearClamp()) != 0)
            {
                Fail("渲染阶段：SetSampler 被拒绝：" + s_textureParams.GetLastError());
            }
            Check(s_textureGraph.AddTrianglePass(s_textureTarget, RenderShaderPath, TextureStreamName,
                      TextureBufferName, 3u, 0.0f, 0.0f, 1.0f, 1.0f),
                  backend, "AddTrianglePass(纹理)");
            Check(s_textureGraph.SetPassParam(0u, "textureBlock", s_textureParams), backend,
                  "SetPassParam(纹理)");
            Check(backend.SubmitGraph(s_textureGraph), backend, "SubmitGraph(纹理)");

            // ② 网格索引绘制：顶点字节来自 Mesh（位置 float3@0 + 颜色 float3@12，步长 24），索引 uint32。
            s_meshGraph = backend.CreateGraph();
            if (s_meshGraph == null)
            {
                Fail("渲染阶段：CreateGraph(网格) 返回空：" + backend.GetLastError());
            }
            Check(s_meshGraph.AddBuffer(MeshVertexBufferName,
                      new GPUBufferDescriptor
                      {
                          count = (ulong)s_mesh.VertexCount, stride = Mesh.VertexStride,
                      }, s_mesh.VertexData),
                  backend, "AddBuffer(网格顶点)");
            Check(s_meshGraph.AddBuffer(MeshIndexBufferName,
                      new GPUBufferDescriptor
                      {
                          count = (ulong)s_mesh.IndexCount, stride = 4UL,
                      }, s_mesh.IndexData),
                  backend, "AddBuffer(网格索引)");
            Check(s_meshGraph.SetPositionColorStream(MeshStreamName, Mesh.VertexStride, Mesh.PositionOffset,
                      Mesh.ColorOffset),
                  backend, "SetPositionColorStream");
            Check(s_meshGraph.AddIndexedTrianglePass(s_meshTarget, MeshShaderPath, MeshStreamName,
                      MeshVertexBufferName, MeshIndexBufferName, (uint)s_mesh.IndexCount, 0.0f, 0.0f, 1.0f,
                      1.0f),
                  backend, "AddIndexedTrianglePass");
            Check(backend.SubmitGraph(s_meshGraph), backend, "SubmitGraph(网格)");

            backend.WaitIdle();
            Write("graph_tex_self", s_textureGraph.NativeSelf);
            Write("graph_mesh_self", s_meshGraph.NativeSelf);
            Write("capture_width", RenderWidth);
            Write("capture_height", RenderHeight);
            Write("pending_count", AssetFactoryBase.PendingCount);
        }

        /// <summary>
        /// 关闭路径释放渲染阶段创建的表面对象（与其它探针同一口径：必须在宿主释放后端表面之前调）。
        /// 幂等；未启用探针时是空操作。
        /// </summary>
        internal static void ReleaseSurfaces()
        {
            s_textureParams?.Dispose();
            s_textureParams = null;
            s_renderTexture?.Dispose();
            s_renderTexture = null;
            s_textureGraph?.Dispose();
            s_textureGraph = null;
            s_meshGraph?.Dispose();
            s_meshGraph = null;
            s_textureTarget?.Dispose();
            s_textureTarget = null;
            s_meshTarget?.Dispose();
            s_meshTarget = null;
        }

        /// <summary>抛异常的资产：只用于两段式失败面。</summary>
        private sealed class ThrowingAsset : ResourceBase
        {
            public override byte[] Serialize()
            {
                throw new InvalidOperationException(ThrowingText);
            }
        }

        /// <summary>抛异常资产的工厂：Create 时入队，由 AssetDatabase 出队。</summary>
        private sealed class ThrowingAssetFactory : AssetFactoryBase
        {
            public override string GetResourceType()
            {
                return ThrowingTypeName;
            }

            public override ResourceBase Create()
            {
                var asset = new ThrowingAsset();
                Enqueue(asset);
                return asset;
            }
        }
    }
}
