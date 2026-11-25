using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// 一次 PNG 解码的结果：RGBA8 像素（长度恒为 宽 × 高 × 4）。
    /// </summary>
    internal readonly struct DecodedPng
    {
        internal readonly int Width;
        internal readonly int Height;
        internal readonly byte[] Pixels;

        internal DecodedPng(int width, int height, byte[] pixels)
        {
            Width = width;
            Height = height;
            Pixels = pixels;
        }
    }

    /// <summary>
    /// 纯 C# PNG 解码器（source-importers-png-obj 任务 1.1 / design D2）：不调 native stb，只用
    /// `System.IO.Compression.DeflateStream` 解压，产出 RGBA8。
    ///
    /// 规格边界（判据写死在 tasks / spec 里）：
    ///   * 签名 + IHDR + **全部 IDAT 数据字段按序拼成一条 zlib 流**（块界任意）+ IEND；
    ///   * zlib 头 2 字节跳过（校验 CM/FCHECK/无预置字典），正文用 raw deflate 解；
    ///   * **逐块 CRC32 校验**：损坏但还能 inflate 的 PNG 也必须被拒（不许静默导错像素）；
    ///   * **adler32 尾校验**：整段解压数据的校验和与 zlib 流末尾声明的不符就失败；
    ///   * 五种扫描线滤波重建（None / Sub / Up / Average / Paeth）；
    ///   * 颜色类型 2（RGB，补 alpha = 255）/ 6（RGBA）→ RGBA8；
    ///   * 颜色类型 0（灰度）/ 3（调色板）/ 4（灰度 + alpha）、位深不是 8、Adam7 隔行：明确抛
    ///     `AssetFormatException` 并点名，不猜、不静默。
    ///
    /// 失败一律抛 `AssetFormatException`（点名源相对路径与原因），消息里不出现"可能/大概"。
    /// </summary>
    internal static class PngDecoder
    {
        private static readonly byte[] Signature = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

        // IHDR 的数据段恒为 13 字节：宽 4 + 高 4 + 位深 1 + 颜色类型 1 + 压缩 1 + 滤波 1 + 隔行 1。
        private const int HeaderDataLength = 13;

        private static uint[] s_crcTable;

        internal static DecodedPng Decode(byte[] bytes, string sourcePath)
        {
            if (bytes == null || bytes.Length < Signature.Length)
            {
                throw new AssetFormatException(sourcePath,
                    $"文件长度是 {(bytes == null ? 0 : bytes.Length)} 字节，连 PNG 签名 {Signature.Length} 字节都不足");
            }
            for (int index = 0; index < Signature.Length; ++index)
            {
                if (bytes[index] != Signature[index])
                {
                    throw new AssetFormatException(sourcePath,
                        "PNG 签名不符（文件头不是 89 50 4E 47 0D 0A 1A 0A）");
                }
            }

            int width = 0;
            int height = 0;
            int bitDepth = 0;
            int colorType = -1;
            bool sawHeader = false;
            bool sawEnd = false;
            using var idat = new MemoryStream();

            int offset = Signature.Length;
            while (offset < bytes.Length)
            {
                if (bytes.Length - offset < 8)
                {
                    throw new AssetFormatException(sourcePath,
                        $"块头被截断：偏移 {offset} 处只剩 {bytes.Length - offset} 字节（块头要 8 字节）");
                }
                uint length = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset));
                string type = ChunkTypeText(bytes, offset + 4);
                if (length > int.MaxValue)
                {
                    throw new AssetFormatException(sourcePath,
                        $"块 {type} 声明的长度 {length} 超过可处理范围");
                }
                int dataOffset = offset + 8;
                long chunkEnd = (long)dataOffset + length + 4L; // 数据 + 4 字节 CRC
                if (chunkEnd > bytes.Length)
                {
                    throw new AssetFormatException(sourcePath,
                        $"块 {type} 被截断：需要 {length} 字节数据 + 4 字节 CRC，文件只剩 {bytes.Length - dataOffset} 字节");
                }
                // 逐块 CRC32（覆盖"类型 + 数据"）：损坏但还能 inflate 的 PNG 在这里必须被拒。
                uint storedCrc = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(dataOffset + (int)length));
                uint actualCrc = Crc32(bytes, offset + 4, (int)length + 4);
                if (storedCrc != actualCrc)
                {
                    throw new AssetFormatException(sourcePath,
                        $"块 {type} 的 CRC32 不符：文件写 {storedCrc:X8}，按内容算 {actualCrc:X8}（文件已损坏）");
                }

                if (!sawHeader)
                {
                    if (type != "IHDR")
                    {
                        throw new AssetFormatException(sourcePath,
                            $"第一个块必须是 IHDR，实际是 {type}");
                    }
                    if (length != HeaderDataLength)
                    {
                        throw new AssetFormatException(sourcePath,
                            $"IHDR 数据长度是 {length} 字节，必须是 {HeaderDataLength} 字节");
                    }
                    ReadHeader(bytes, dataOffset, sourcePath, out width, out height, out bitDepth, out colorType);
                    sawHeader = true;
                }
                else if (type == "IHDR")
                {
                    throw new AssetFormatException(sourcePath, "出现了第二个 IHDR 块（格式非法）");
                }
                else if (type == "IDAT")
                {
                    // 全部 IDAT 按序拼接：块界可以任意切，拼起来的才是那条 zlib 流。
                    idat.Write(bytes, dataOffset, (int)length);
                }
                else if (type == "IEND")
                {
                    sawEnd = true;
                    offset = (int)chunkEnd;
                    break;
                }
                // 其它块（PLTE / tEXt / gAMA / pHYs …）按需跳过：本批不用它们，但上面的 CRC 已经校验过。
                offset = (int)chunkEnd;
            }

            if (!sawHeader)
            {
                throw new AssetFormatException(sourcePath, "没有 IHDR 块（文件不是合法 PNG）");
            }
            if (!sawEnd)
            {
                throw new AssetFormatException(sourcePath, "没有 IEND 块（文件被截断）");
            }
            if (idat.Length == 0)
            {
                throw new AssetFormatException(sourcePath, "没有任何 IDAT 数据（图像数据缺失）");
            }

            int channels = colorType == 6 ? 4 : 3;
            byte[] raw = InflateZlibStream(idat.ToArray(), sourcePath, height, width, channels);
            return Unfilter(raw, sourcePath, width, height, channels, colorType);
        }

        private static void ReadHeader(byte[] bytes, int dataOffset, string sourcePath, out int width, out int height,
                                       out int bitDepth, out int colorType)
        {
            uint rawWidth = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(dataOffset));
            uint rawHeight = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(dataOffset + 4));
            bitDepth = bytes[dataOffset + 8];
            colorType = bytes[dataOffset + 9];
            int compressionMethod = bytes[dataOffset + 10];
            int filterMethod = bytes[dataOffset + 11];
            int interlaceMethod = bytes[dataOffset + 12];

            if (rawWidth == 0u || rawHeight == 0u)
            {
                throw new AssetFormatException(sourcePath,
                    $"尺寸非法：宽 {rawWidth}、高 {rawHeight}（PNG 不允许 0）");
            }
            if (rawWidth > int.MaxValue || rawHeight > int.MaxValue)
            {
                throw new AssetFormatException(sourcePath,
                    $"尺寸超出可处理范围：宽 {rawWidth}、高 {rawHeight}");
            }
            width = (int)rawWidth;
            height = (int)rawHeight;

            if (bitDepth != 8)
            {
                throw new AssetFormatException(sourcePath,
                    $"位深是 {bitDepth} 位，只支持 8 位（16 位 PNG 不在本批支持范围）");
            }
            switch (colorType)
            {
                case 2:
                case 6:
                    break;
                case 0:
                    throw new AssetFormatException(sourcePath,
                        "颜色类型 0（灰度）不支持（本批只支持 RGB / RGBA）");
                case 3:
                    throw new AssetFormatException(sourcePath,
                        "颜色类型 3（调色板）不支持（本批只支持 RGB / RGBA）");
                case 4:
                    throw new AssetFormatException(sourcePath,
                        "颜色类型 4（灰度 + alpha）不支持（本批只支持 RGB / RGBA）");
                default:
                    throw new AssetFormatException(sourcePath,
                        $"颜色类型 {colorType} 不支持（本批只支持 2 = RGB / 6 = RGBA）");
            }
            if (compressionMethod != 0)
            {
                throw new AssetFormatException(sourcePath,
                    $"压缩方法是 {compressionMethod}，只支持 0（deflate）");
            }
            if (filterMethod != 0)
            {
                throw new AssetFormatException(sourcePath,
                    $"滤波方法是 {filterMethod}，只支持 0（五种自适应滤波）");
            }
            if (interlaceMethod != 0)
            {
                throw new AssetFormatException(sourcePath,
                    interlaceMethod == 1
                        ? "Adam7 隔行扫描不支持（只支持非隔行 PNG）"
                        : $"隔行方法是 {interlaceMethod}，只支持 0（非隔行）");
            }
        }

        /// <summary>
        /// 把拼接好的 zlib 流解回扫描线原始字节：跳过 2 字节 zlib 头（校验 CMF/FLG），正文走 raw
        /// deflate，最后 4 字节 adler32 校验整段解压数据。
        /// </summary>
        private static byte[] InflateZlibStream(byte[] zlib, string sourcePath, int height, int width, int channels)
        {
            if (zlib.Length < 6)
            {
                throw new AssetFormatException(sourcePath,
                    $"zlib 流只有 {zlib.Length} 字节，不足 2 字节头 + 空正文 + 4 字节 adler32 尾");
            }
            byte cmf = zlib[0];
            byte flg = zlib[1];
            if ((cmf & 0x0F) != 8)
            {
                throw new AssetFormatException(sourcePath,
                    $"zlib 头声明的压缩方法是 {(cmf & 0x0F)}，只支持 8（deflate）");
            }
            if (((cmf << 8) | flg) % 31 != 0)
            {
                throw new AssetFormatException(sourcePath, "zlib 头校验（FCHECK）不符");
            }
            if ((flg & 0x20) != 0)
            {
                throw new AssetFormatException(sourcePath, "zlib 头声明了预置字典，本解码器不支持");
            }

            uint declaredAdler = BinaryPrimitives.ReadUInt32BigEndian(zlib.AsSpan(zlib.Length - 4));
            byte[] raw;
            try
            {
                using var input = new MemoryStream(zlib, 2, zlib.Length - 6, writable: false);
                using var deflate = new DeflateStream(input, CompressionMode.Decompress);
                using var output = new MemoryStream();
                deflate.CopyTo(output);
                raw = output.ToArray();
            }
            catch (InvalidDataException error)
            {
                throw new AssetFormatException(sourcePath,
                    "deflate 正文解不开（zlib 流损坏或截断）", error);
            }

            long expectedLength = (long)height * (1L + (long)width * channels);
            if (raw.Length != expectedLength)
            {
                throw new AssetFormatException(sourcePath,
                    $"解压后 {raw.Length} 字节，按 宽 × 高 × (1 + 通道) 应为 {expectedLength} 字节（图像数据长度不符）");
            }
            uint actualAdler = Adler32(raw);
            if (actualAdler != declaredAdler)
            {
                throw new AssetFormatException(sourcePath,
                    $"adler32 尾校验不符：文件写 {declaredAdler:X8}，按解压内容算 {actualAdler:X8}（图像数据已损坏）");
            }
            return raw;
        }

        /// <summary>逐扫描线反滤波 → RGBA8（RGB 的 alpha 写死 255）。</summary>
        private static DecodedPng Unfilter(byte[] raw, string sourcePath, int width, int height, int channels,
                                           int colorType)
        {
            int stride = width * channels;
            byte[] pixels = new byte[(int)((long)width * height * 4)];
            byte[] previous = new byte[stride];
            byte[] current = new byte[stride];
            int source = 0;
            for (int y = 0; y < height; ++y)
            {
                byte filter = raw[source++];
                Array.Copy(raw, source, current, 0, stride);
                source += stride;
                switch (filter)
                {
                    case 0: // None
                        break;
                    case 1: // Sub
                        for (int index = channels; index < stride; ++index)
                        {
                            current[index] = (byte)(current[index] + current[index - channels]);
                        }
                        break;
                    case 2: // Up
                        for (int index = 0; index < stride; ++index)
                        {
                            current[index] = (byte)(current[index] + previous[index]);
                        }
                        break;
                    case 3: // Average
                        for (int index = 0; index < stride; ++index)
                        {
                            int left = index >= channels ? current[index - channels] : 0;
                            int up = previous[index];
                            current[index] = (byte)(current[index] + ((left + up) >> 1));
                        }
                        break;
                    case 4: // Paeth
                        for (int index = 0; index < stride; ++index)
                        {
                            int left = index >= channels ? current[index - channels] : 0;
                            int up = previous[index];
                            int upLeft = index >= channels ? previous[index - channels] : 0;
                            current[index] = (byte)(current[index] + Paeth(left, up, upLeft));
                        }
                        break;
                    default:
                        throw new AssetFormatException(sourcePath,
                            $"第 {y} 行的滤波类型是 {filter}，只支持 0-4（None/Sub/Up/Average/Paeth）");
                }

                int destination = y * width * 4;
                if (colorType == 6)
                {
                    Array.Copy(current, 0, pixels, destination, stride);
                }
                else
                {
                    // 颜色类型 2（RGB）→ RGBA8：alpha 写死 255。
                    for (int x = 0; x < width; ++x)
                    {
                        pixels[destination + x * 4] = current[x * 3];
                        pixels[destination + x * 4 + 1] = current[x * 3 + 1];
                        pixels[destination + x * 4 + 2] = current[x * 3 + 2];
                        pixels[destination + x * 4 + 3] = 255;
                    }
                }

                byte[] swap = previous;
                previous = current;
                current = swap;
            }
            return new DecodedPng(width, height, pixels);
        }

        private static int Paeth(int left, int up, int upLeft)
        {
            int estimate = left + up - upLeft;
            int distanceLeft = Math.Abs(estimate - left);
            int distanceUp = Math.Abs(estimate - up);
            int distanceUpLeft = Math.Abs(estimate - upLeft);
            if (distanceLeft <= distanceUp && distanceLeft <= distanceUpLeft)
            {
                return left;
            }
            return distanceUp <= distanceUpLeft ? up : upLeft;
        }

        private static string ChunkTypeText(byte[] bytes, int offset)
        {
            char[] chars = new char[4];
            for (int index = 0; index < 4; ++index)
            {
                byte value = bytes[offset + index];
                chars[index] = value >= 0x20 && value < 0x7F ? (char)value : '?';
            }
            return new string(chars);
        }

        private static uint[] CrcTable()
        {
            if (s_crcTable != null)
            {
                return s_crcTable;
            }
            uint[] table = new uint[256];
            for (uint index = 0; index < 256u; ++index)
            {
                uint value = index;
                for (int bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1) : value >> 1;
                }
                table[index] = value;
            }
            s_crcTable = table;
            return table;
        }

        /// <summary>CRC-32（IEEE 802.3，PNG 用的那一条）：覆盖"块类型 + 数据"。</summary>
        internal static uint Crc32(byte[] bytes, int offset, int length)
        {
            uint[] table = CrcTable();
            uint crc = 0xFFFFFFFFu;
            for (int index = 0; index < length; ++index)
            {
                crc = (crc >> 8) ^ table[(crc ^ bytes[offset + index]) & 0xFFu];
            }
            return crc ^ 0xFFFFFFFFu;
        }

        private static uint Adler32(byte[] bytes)
        {
            const uint modulus = 65521u;
            uint low = 1u;
            uint high = 0u;
            for (int index = 0; index < bytes.Length; ++index)
            {
                low = (low + bytes[index]) % modulus;
                high = (high + low) % modulus;
            }
            return (high << 16) | low;
        }
    }
}
