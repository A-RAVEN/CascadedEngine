using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// 网格工厂（csharp-resource-asset-foundation design D4/D5）：`GetResourceType` 写死类型名，
    /// `Create()` 造 `Mesh` 并入队等 `AssetDatabase` 取走填数据。
    /// 编译期依赖 `Mesh`（tasks 3.2：与 3.1 不并行）。
    /// </summary>
    public sealed class MeshFactory : AssetFactoryBase
    {
        /// <summary>注册表按这个类型名查工厂；`AssetDatabase.Load` 也用它新建网格。</summary>
        public const string TypeName = "Mesh";

        public override string GetResourceType()
        {
            return TypeName;
        }

        public override ResourceBase Create()
        {
            var mesh = new Mesh();
            Enqueue(mesh);
            return mesh;
        }
    }
}
