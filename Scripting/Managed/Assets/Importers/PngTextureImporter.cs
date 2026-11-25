using Scripting.Generated.Bindings;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// PNG 贴图源导入器（source-importers-png-obj 任务 2.1 / design D1、D2、D7）：
    /// 扫源目录下的 `.png` → 纯 C# 解码成 RGBA8 → **直接 new <see cref="Texture2D"/>** 交迭代器。
    ///
    /// 落盘路径由管线决定：`NextPath` 是源相对路径镜像 + 扩展名替换（`Images/A/Test.png` →
    /// `Images/A/Test.tex`），资源根下的同名镜像路径就是 ② 资产位置。**不调注册表、不经资产工厂 /
    /// 资产数据库门面** —— 注册与落盘全部由 `ResourceImportingSystem::ScanSourceDirectory` 做。
    /// </summary>
    internal sealed class PngTextureImporter : SourceImporterBase
    {
        internal override string Prefix => "png";

        internal override string SourceExtension => ".png";

        internal override string AssetExtension => ".tex";

        internal override string Tags => "CSharp;Source;PNG";

        // 标识串：管线只用 NextPath + 资源根落盘，GetDestDirectory 不参与写盘（见 ResourceImporter.h）。
        internal override string DestDirectoryLabel => "Textures";

        protected override void ProduceOne(string relativePath, byte[] bytes, SourceProductions productions)
        {
            DecodedPng decoded = PngDecoder.Decode(bytes, relativePath);
            var texture = new Texture2D
            {
                Width = decoded.Width,
                Height = decoded.Height,
                Format = ETextureFormat.E_R8G8B8A8_UNORM,
                Pixels = decoded.Pixels,
            };
            productions.Add(SourceImporterEnvironment.MirrorPath(relativePath, SourceExtension, AssetExtension),
                texture);
        }
    }
}
