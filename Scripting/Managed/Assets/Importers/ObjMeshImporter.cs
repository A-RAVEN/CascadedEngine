namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// OBJ 网格源导入器（source-importers-png-obj 任务 2.2 / design D1、D3、D7）：
    /// 扫源目录下的 `.obj` → 纯 C# 解析（位置 + 面索引，四边面扇形三角化）→ **直接 new
    /// <see cref="Mesh"/>** 交迭代器（顶点布局位置 float3 + 写死白色 float3、stride 24）。
    ///
    /// 落盘路径由管线决定：`NextPath` 是源相对路径镜像 + 扩展名替换（`Models/B.obj` →
    /// `Models/B.mesh`）。**不调注册表、不经资产工厂 / 资产数据库门面**。
    /// </summary>
    internal sealed class ObjMeshImporter : SourceImporterBase
    {
        internal override string Prefix => "obj";

        internal override string SourceExtension => ".obj";

        internal override string AssetExtension => ".mesh";

        internal override string Tags => "CSharp;Source;OBJ";

        // 标识串：管线只用 NextPath + 资源根落盘，GetDestDirectory 不参与写盘（见 ResourceImporter.h）。
        internal override string DestDirectoryLabel => "Meshes";

        protected override void ProduceOne(string relativePath, byte[] bytes, SourceProductions productions)
        {
            ParsedObjMesh parsed = ObjParser.Parse(bytes, relativePath);
            var mesh = new Mesh
            {
                VertexData = parsed.VertexData,
                IndexData = parsed.IndexData,
                VertexCount = parsed.VertexCount,
                IndexCount = parsed.IndexCount,
            };
            productions.Add(SourceImporterEnvironment.MirrorPath(relativePath, SourceExtension, AssetExtension),
                mesh);
        }
    }
}
