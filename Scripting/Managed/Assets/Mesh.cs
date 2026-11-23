using System;
using System.Buffers.Binary;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 网格资源（csharp-resource-asset-foundation design D5）：持顶点字节、索引字节与两个计数。
    ///
    /// 顶点布局**必须**与 `Scene/SceneRenderer.cs` 的三个常量逐字一致：
    ///   位置 float3 偏移 0 + 颜色 **float3** 偏移 12、步长 24（颜色不是 float4）。
    /// 改这里等于改场景着色器输入布局 —— 三个常量就是对照点，测试按写死值核对。
    ///
    /// `Serialize()` 产出本程序集自定义格式（小端、逐字段顺序写、无对齐填充）：
    ///   magic "CAMH"（4 字节）
    ///   + formatVersion = 1（uint32）
    ///   + 顶点数（int32）/ 索引数（int32）
    ///   + 顶点字节长度（uint64）/ 索引字节长度（uint64）
    ///   + 顶点字节 / 索引字节
    /// 固定头部 32 字节；解析器按同一份偏移逐字段读回（见 `Parse`）。
    /// </summary>
    public sealed class Mesh : ResourceBase
    {
        /// <summary>当前格式版本；解析时不是这个值就必须显式失败。</summary>
        public const int FormatVersion = 1;

        /// <summary>文件魔数 `CAMH`（网格）。</summary>
        public static readonly byte[] Magic = { (byte)'C', (byte)'A', (byte)'M', (byte)'H' };

        // 与 SceneRenderer.cs 的 VertexStride / PositionOffset / ColorOffset 逐字相同（uint）。
        public const uint VertexStride = 24;
        public const uint PositionOffset = 0;
        public const uint ColorOffset = 12;

        /// <summary>固定头部字节数：魔数 4 + 版本 4 + 两个计数 8 + 两个长度 16。</summary>
        public const int HeaderSize = 32;

        /// <summary>顶点字节：每个顶点 = 位置 float3（偏移 0）+ 颜色 float3（偏移 12），共 24 字节。</summary>
        public byte[] VertexData = Array.Empty<byte>();

        /// <summary>索引字节：uint32 索引。</summary>
        public byte[] IndexData = Array.Empty<byte>();

        public int VertexCount;
        public int IndexCount;

        /// <summary>解析结果（先解析、后经门面新建并填数据；见 design D6）。</summary>
        internal readonly struct Payload
        {
            public readonly int VertexCount;
            public readonly int IndexCount;
            public readonly byte[] VertexData;
            public readonly byte[] IndexData;

            public Payload(int vertexCount, int indexCount, byte[] vertexData, byte[] indexData)
            {
                VertexCount = vertexCount;
                IndexCount = indexCount;
                VertexData = vertexData;
                IndexData = indexData;
            }
        }

        public override byte[] Serialize()
        {
            byte[] vertexData = VertexData ?? Array.Empty<byte>();
            byte[] indexData = IndexData ?? Array.Empty<byte>();
            byte[] bytes = new byte[HeaderSize + vertexData.Length + indexData.Length];
            Magic.CopyTo(bytes, 0);
            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), (uint)FormatVersion);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(8), VertexCount);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(12), IndexCount);
            BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(16), (ulong)vertexData.Length);
            BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(24), (ulong)indexData.Length);
            vertexData.CopyTo(bytes, HeaderSize);
            indexData.CopyTo(bytes, HeaderSize + vertexData.Length);
            return bytes;
        }

        /// <summary>
        /// 解析一个网格文件。魔数不符 / 版本不是 1 / 长度越界 / 计数与字节长度不一致都抛
        /// `AssetFormatException` 并点名路径与原因。
        /// </summary>
        internal static Payload Parse(byte[] bytes, string sourcePath)
        {
            if (bytes == null || bytes.Length < HeaderSize)
            {
                throw new AssetFormatException(sourcePath,
                    $"文件长度是 {(bytes == null ? 0 : bytes.Length)} 字节，不足网格固定头部 {HeaderSize} 字节");
            }
            for (int index = 0; index < Magic.Length; ++index)
            {
                if (bytes[index] != Magic[index])
                {
                    throw new AssetFormatException(sourcePath,
                        "魔数不是 CAMH（网格文件头不符）");
                }
            }
            uint version = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4));
            if (version != (uint)FormatVersion)
            {
                throw new AssetFormatException(sourcePath,
                    $"formatVersion 是 {version}，只支持 {FormatVersion}");
            }
            int vertexCount = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(8));
            int indexCount = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12));
            ulong vertexLength = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(16));
            ulong indexLength = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(24));
            if (vertexCount < 0 || indexCount < 0)
            {
                throw new AssetFormatException(sourcePath,
                    $"计数非法：顶点 {vertexCount}、索引 {indexCount}（不允许负数）");
            }
            ulong expectedVertexLength = (ulong)vertexCount * VertexStride;
            if (vertexLength != expectedVertexLength)
            {
                throw new AssetFormatException(sourcePath,
                    $"顶点字节长度 {vertexLength} 与 顶点数×步长 = {expectedVertexLength} 不符（步长 {VertexStride}）");
            }
            ulong expectedIndexLength = (ulong)indexCount * 4UL;
            if (indexLength != expectedIndexLength)
            {
                throw new AssetFormatException(sourcePath,
                    $"索引字节长度 {indexLength} 与 索引数×4 = {expectedIndexLength} 不符（uint32 索引）");
            }
            ulong remaining = (ulong)(bytes.Length - HeaderSize);
            ulong declared = vertexLength + indexLength;
            if (declared != remaining)
            {
                throw new AssetFormatException(sourcePath,
                    $"两段载荷合计 {declared} 越界：文件剩余 {remaining} 字节");
            }
            byte[] vertexData = new byte[(int)vertexLength];
            byte[] indexData = new byte[(int)indexLength];
            Array.Copy(bytes, HeaderSize, vertexData, 0, vertexData.Length);
            Array.Copy(bytes, HeaderSize + vertexData.Length, indexData, 0, indexData.Length);
            return new Payload(vertexCount, indexCount, vertexData, indexData);
        }
    }
}
