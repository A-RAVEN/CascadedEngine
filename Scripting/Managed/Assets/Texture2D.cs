using System;
using System.Buffers.Binary;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 贴图资源（csharp-resource-asset-foundation design D5）：持像素字节与标量元数据。
    ///
    /// `Serialize()` 产出**本程序集自定义**的带版本格式（不是 CodeGen 的 `TResource` 格式；
    /// C++ 的 `TResource` 读不了它，加载方也在 C#，闭环自洽）。格式小端、逐字段顺序写、无对齐填充：
    ///   magic "CATX"（4 字节）
    ///   + formatVersion = 1（uint32）
    ///   + width（int32）/ height（int32）/ format（int32）
    ///   + 像素长度（uint64）
    ///   + 像素字节
    /// 固定头部 28 字节；解析器按同一份偏移逐字段读回（见 `Parse`）。
    /// </summary>
    public sealed class Texture2D : ResourceBase
    {
        /// <summary>当前格式版本；解析时不是这个值就必须显式失败。</summary>
        public const int FormatVersion = 1;

        /// <summary>文件魔数 `CATX`（贴图）。</summary>
        public static readonly byte[] Magic = { (byte)'C', (byte)'A', (byte)'T', (byte)'X' };

        /// <summary>固定头部字节数：魔数 4 + 版本 4 + 三个 int32 12 + 长度 8。</summary>
        public const int HeaderSize = 28;

        public int Width;
        public int Height;

        /// <summary>本批只支持 RGBA8 一张格式（`E_R8G8B8A8_UNORM`，底层值 6）。</summary>
        public ETextureFormat Format = ETextureFormat.E_R8G8B8A8_UNORM;

        /// <summary>像素字节（RGBA8 时长度必须是 宽 × 高 × 4）。</summary>
        public byte[] Pixels = Array.Empty<byte>();

        /// <summary>解析结果（先解析、后经门面新建并填数据；见 design D6）。</summary>
        internal readonly struct Payload
        {
            public readonly int Width;
            public readonly int Height;
            public readonly ETextureFormat Format;
            public readonly byte[] Pixels;

            public Payload(int width, int height, ETextureFormat format, byte[] pixels)
            {
                Width = width;
                Height = height;
                Format = format;
                Pixels = pixels;
            }
        }

        public override byte[] Serialize()
        {
            byte[] pixels = Pixels ?? Array.Empty<byte>();
            byte[] bytes = new byte[HeaderSize + pixels.Length];
            Magic.CopyTo(bytes, 0);
            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), (uint)FormatVersion);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(8), Width);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(12), Height);
            BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(16), (int)Format);
            BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(20), (ulong)pixels.Length);
            pixels.CopyTo(bytes, HeaderSize);
            return bytes;
        }

        /// <summary>
        /// 解析一个贴图文件。魔数不符 / 版本不是 1 / 长度越界 / 格式不支持都抛
        /// `AssetFormatException` 并点名路径与原因 —— MUST NOT 静默返回空对象或把垃圾灌进资产。
        /// </summary>
        internal static Payload Parse(byte[] bytes, string sourcePath)
        {
            if (bytes == null || bytes.Length < HeaderSize)
            {
                throw new AssetFormatException(sourcePath,
                    $"文件长度是 {(bytes == null ? 0 : bytes.Length)} 字节，不足贴图固定头部 {HeaderSize} 字节");
            }
            for (int index = 0; index < Magic.Length; ++index)
            {
                if (bytes[index] != Magic[index])
                {
                    throw new AssetFormatException(sourcePath,
                        "魔数不是 CATX（贴图文件头不符）");
                }
            }
            uint version = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4));
            if (version != (uint)FormatVersion)
            {
                throw new AssetFormatException(sourcePath,
                    $"formatVersion 是 {version}，只支持 {FormatVersion}");
            }
            int width = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(8));
            int height = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12));
            int formatValue = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(16));
            ulong pixelLength = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(20));
            if (width < 0 || height < 0)
            {
                throw new AssetFormatException(sourcePath,
                    $"尺寸非法：宽 {width}、高 {height}（不允许负数）");
            }
            if (formatValue != (int)ETextureFormat.E_R8G8B8A8_UNORM)
            {
                throw new AssetFormatException(sourcePath,
                    $"格式值 {formatValue} 不受支持（本批只支持 R8G8B8A8_UNORM = " +
                    $"{(int)ETextureFormat.E_R8G8B8A8_UNORM}）");
            }
            ulong expectedLength = (ulong)width * (ulong)height * 4UL;
            if (pixelLength != expectedLength)
            {
                throw new AssetFormatException(sourcePath,
                    $"像素长度 {pixelLength} 与 宽×高×4 = {expectedLength} 不符（RGBA8）");
            }
            ulong remaining = (ulong)(bytes.Length - HeaderSize);
            if (pixelLength != remaining)
            {
                throw new AssetFormatException(sourcePath,
                    $"像素长度 {pixelLength} 越界：文件剩余 {remaining} 字节");
            }
            byte[] pixels = new byte[(int)pixelLength];
            Array.Copy(bytes, HeaderSize, pixels, 0, pixels.Length);
            return new Payload(width, height, (ETextureFormat)formatValue, pixels);
        }
    }
}
