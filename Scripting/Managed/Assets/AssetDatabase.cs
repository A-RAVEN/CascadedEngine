using System;
using System.IO;
using Scripting.Generated.Bindings.CsScript;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 资产库助手（csharp-resource-asset-foundation design D6）：新建 / 自读加载 / 落盘。
    ///
    /// 加载顺序是**先读文件与解析、成功后才释放旧条目**（design D6 / tasks 3.3）：
    /// 解析失败时旧资产必须保持不动 —— 与导入器管线「先产出、产出失败旧条目不动、成功后才释放注册」
    /// 同构（`ResourceImportingSystem.cpp` L80-92）。MUST NOT 先释放再读文件：那会让损坏 / 缺失文件
    /// 静默毁掉旧资产。
    ///
    /// 加载**不走**引擎 `GetOrLoadResource` / `Deserialize`（反向读仍是显式排除）；
    /// 读文件用 C# IO，解析按魔数分派，新建走门面 `CreateResource`，数据由工厂队列取回后填。
    /// </summary>
    public sealed class AssetDatabase
    {
        private readonly IResourceRegistry m_registry;

        public AssetDatabase(IResourceRegistry registry)
        {
            m_registry = registry ?? throw new ArgumentNullException(nameof(registry));
        }

        /// <summary>门面表面（测试要读 `HasResource` 等读数时用）。</summary>
        public IResourceRegistry Registry => m_registry;

        /// <summary>新建：门面 `CreateResource`（注册表查工厂 → Create → 注册）→ 从工厂队列取出对象。</summary>
        public ResourceBase Create(string typeName, string path)
        {
            m_registry.CreateResource(typeName, path);
            return AssetFactoryBase.Dequeue(typeName);
        }

        /// <summary>全量落盘：门面 `SerializeAll`（引擎经反向 `Serialize()` 写盘，无条件全量）。</summary>
        public void SaveAll()
        {
            m_registry.SerializeAll();
        }

        /// <summary>
        /// 自读加载（design D6）：`GetResourceFullPath` → C# 读文件 → 按魔数解析；
        /// 全部成功后才释放旧条目（若有）→ `CreateResource` → 出队 → 填数据。
        /// 文件缺失 / IO 错误 / 魔数或版本或长度不符一律抛 `AssetFormatException` 并点名路径与原因。
        /// </summary>
        public ResourceBase Load(string path)
        {
            string fullPath = m_registry.GetResourceFullPath(path);
            byte[] bytes = ReadAllBytes(path, fullPath);
            ParsedAsset parsed = Parse(path, bytes);
            // 到这里文件已经完整解析成功：旧条目这时才可以释放（失败路径已经抛出去了）。
            if (m_registry.HasResource(path))
            {
                m_registry.ReleaseResource(path);
            }
            ResourceBase resource = Create(parsed.TypeName, path);
            parsed.Apply(resource);
            return resource;
        }

        private static byte[] ReadAllBytes(string path, string fullPath)
        {
            try
            {
                // 原生侧的写批在 IOManager 的流缓存里保持着文件打开（写批提交后不关流，实测），
                // 所以这里显式用 FileShare.ReadWrite 读 —— 默认 `File.ReadAllBytes` 的共享模式
                // （FileShare.Read）会与已有的写句柄冲突，报「文件被另一个进程使用」。
                using var stream = new FileStream(fullPath, FileMode.Open, FileAccess.Read,
                                                  FileShare.ReadWrite);
                if (stream.Length > int.MaxValue)
                {
                    throw new IOException($"文件过大：{stream.Length} 字节（资产文件不可能是这个量级）");
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
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                // 文件缺失 / 权限 / 设备错误都包装成资产格式错误：裸抛 FileNotFoundException 会让
                // "异常类型必须是 AssetFormatException" 这条判据被喂绿（tasks 3.4 修订记录）。
                throw new AssetFormatException(path, "读取文件失败：" + ex.Message, ex);
            }
        }

        private static ParsedAsset Parse(string path, byte[] bytes)
        {
            if (bytes == null || bytes.Length < 4)
            {
                throw new AssetFormatException(path,
                    $"文件长度是 {(bytes == null ? 0 : bytes.Length)} 字节，连 4 字节魔数都不足");
            }
            if (StartsWith(bytes, Texture2D.Magic))
            {
                return new ParsedAsset { Kind = AssetKind.Texture2D, Texture = Texture2D.Parse(bytes, path) };
            }
            if (StartsWith(bytes, Mesh.Magic))
            {
                return new ParsedAsset { Kind = AssetKind.Mesh, MeshPayload = Mesh.Parse(bytes, path) };
            }
            throw new AssetFormatException(path,
                $"魔数不是 CATX 也不是 CAMH（实际前四字节 {bytes[0]:X2} {bytes[1]:X2} {bytes[2]:X2} {bytes[3]:X2}）");
        }

        private static bool StartsWith(byte[] bytes, byte[] magic)
        {
            if (bytes.Length < magic.Length)
            {
                return false;
            }
            for (int index = 0; index < magic.Length; ++index)
            {
                if (bytes[index] != magic[index])
                {
                    return false;
                }
            }
            return true;
        }

        private enum AssetKind
        {
            Texture2D,
            Mesh,
        }

        private sealed class ParsedAsset
        {
            public AssetKind Kind;
            public Texture2D.Payload Texture;
            public Mesh.Payload MeshPayload;

            public string TypeName =>
                Kind == AssetKind.Texture2D ? Texture2DFactory.TypeName : MeshFactory.TypeName;

            public void Apply(ResourceBase resource)
            {
                if (Kind == AssetKind.Texture2D)
                {
                    if (resource is not Texture2D texture)
                    {
                        throw new InvalidOperationException(
                            $"工厂返回的对象不是 Texture2D（实际 {resource?.GetType().Name ?? "null"}）");
                    }
                    texture.Width = Texture.Width;
                    texture.Height = Texture.Height;
                    texture.Format = Texture.Format;
                    texture.Pixels = Texture.Pixels;
                    return;
                }
                if (resource is not Mesh mesh)
                {
                    throw new InvalidOperationException(
                        $"工厂返回的对象不是 Mesh（实际 {resource?.GetType().Name ?? "null"}）");
                }
                mesh.VertexCount = MeshPayload.VertexCount;
                mesh.IndexCount = MeshPayload.IndexCount;
                mesh.VertexData = MeshPayload.VertexData;
                mesh.IndexData = MeshPayload.IndexData;
            }
        }
    }
}
