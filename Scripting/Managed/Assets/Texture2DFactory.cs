using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// 贴图工厂（csharp-resource-asset-foundation design D4/D5）：`GetResourceType` 写死类型名，
    /// `Create()` 造 `Texture2D` 并入队等 `AssetDatabase` 取走填数据。
    /// 编译期依赖 `Texture2D`（tasks 3.2：与 3.1 不并行）。
    /// </summary>
    public sealed class Texture2DFactory : AssetFactoryBase
    {
        /// <summary>注册表按这个类型名查工厂；`AssetDatabase.Load` 也用它新建贴图。</summary>
        public const string TypeName = "Texture2D";

        public override string GetResourceType()
        {
            return TypeName;
        }

        public override ResourceBase Create()
        {
            var texture = new Texture2D();
            Enqueue(texture);
            return texture;
        }
    }
}
