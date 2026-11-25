using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// 一次 OBJ 解析的结果：顶点/索引的原始字节与两个计数（形状直接对得上 <see cref="Mesh"/>）。
    /// </summary>
    internal readonly struct ParsedObjMesh
    {
        internal readonly int VertexCount;
        internal readonly int IndexCount;
        internal readonly byte[] VertexData;
        internal readonly byte[] IndexData;

        internal ParsedObjMesh(int vertexCount, int indexCount, byte[] vertexData, byte[] indexData)
        {
            VertexCount = vertexCount;
            IndexCount = indexCount;
            VertexData = vertexData;
            IndexData = indexData;
        }
    }

    /// <summary>
    /// 纯 C# OBJ 解析器（source-importers-png-obj 任务 1.2 / design D3）：只取几何（位置 + 面索引），
    /// 不看材质、不看法线/UV。
    ///
    /// 规则（判据写死在 tasks / spec 里）：
    ///   * `v x y z` → 位置 float3；顶点按出现序保序，**未被面引用的顶点也保留**（不去重、不裁剪）；
    ///   * `f` 面 → **索引 1 基**，支持负索引（相对当前已出现的顶点数：-1 = 最后一个）；
    ///   * **n 边面扇形三角化 → n - 2 个三角**（立方体的 6 个四边面 = 12 个三角，强制三角化真实现）；
    ///   * 索引 uint32；顶点布局 = 位置 float3 偏移 0 + 颜色 float3 偏移 12、步长 24，与
    ///     `Mesh.VertexStride` / `Mesh.PositionOffset` / `Mesh.ColorOffset` 三个常量一致；
    ///     OBJ 没有顶点色，颜色写死白色 (1,1,1)；
    ///   * 缺顶点 / 索引越界 / 空网格 / 坐标写不成数 → `AssetFormatException`（点名行号与原因）。
    /// </summary>
    internal static class ObjParser
    {
        private const int VertexComponents = 3;
        private const float DefaultVertexColor = 1.0f;

        internal static ParsedObjMesh Parse(byte[] bytes, string sourcePath)
        {
            // 顶点位置按出现序连续存（每个顶点 3 个分量），索引按解析顺序展开成三角。
            var positions = new List<float>();
            var indices = new List<uint>();
            string text = bytes == null ? string.Empty : Encoding.UTF8.GetString(bytes);
            // 去 UTF-8 BOM：带 BOM 时第一行是 "\uFEFFv ..."，按 token 切会认不出 "v"。
            if (text.Length > 0 && text[0] == '\uFEFF')
            {
                text = text.Substring(1);
            }

            int lineNumber = 0;
            foreach (string rawLine in text.Split('\n'))
            {
                ++lineNumber;
                string line = rawLine.Trim();
                if (line.Length == 0 || line[0] == '#')
                {
                    continue;
                }
                string[] tokens = line.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                switch (tokens[0])
                {
                    case "v":
                        ParseVertex(tokens, positions, sourcePath, lineNumber);
                        break;
                    case "f":
                        ParseFace(tokens, positions, indices, sourcePath, lineNumber);
                        break;
                    default:
                        // vt / vn / vp / o / g / s / usemtl / mtllib 等本批不支持也不影响几何：跳过。
                        break;
                }
            }

            int vertexCount = positions.Count / VertexComponents;
            if (vertexCount == 0)
            {
                throw new AssetFormatException(sourcePath, "空网格：文件里没有任何 v 顶点");
            }
            if (indices.Count == 0)
            {
                throw new AssetFormatException(sourcePath, "空网格：文件里没有任何 f 面（生成不出三角）");
            }

            return new ParsedObjMesh(vertexCount, indices.Count,
                BuildVertexData(positions, sourcePath), BuildIndexData(indices));
        }

        private static void ParseVertex(string[] tokens, List<float> positions, string sourcePath, int lineNumber)
        {
            if (tokens.Length < 1 + VertexComponents)
            {
                throw new AssetFormatException(sourcePath,
                    $"第 {lineNumber} 行 v 需要 {VertexComponents} 个坐标，实际给了 {tokens.Length - 1} 个");
            }
            for (int component = 0; component < VertexComponents; ++component)
            {
                if (!float.TryParse(tokens[component + 1], NumberStyles.Float, CultureInfo.InvariantCulture,
                        out float value) ||
                    float.IsNaN(value) || float.IsInfinity(value))
                {
                    throw new AssetFormatException(sourcePath,
                        $"第 {lineNumber} 行 v 的第 {component + 1} 个坐标 \"{tokens[component + 1]}\" 不是有限浮点数");
                }
                positions.Add(value);
            }
            // 第 4 个分量（w）本批忽略：OBJ 的默认 w = 1，不参与本批几何。
        }

        private static void ParseFace(string[] tokens, List<float> positions, List<uint> indices, string sourcePath,
                                      int lineNumber)
        {
            if (tokens.Length < 1 + 3)
            {
                throw new AssetFormatException(sourcePath,
                    $"第 {lineNumber} 行 f 至少需要 3 个顶点，实际给了 {tokens.Length - 1} 个（本批不支持点/线图元）");
            }
            int vertexCount = positions.Count / VertexComponents;
            int cornerCount = tokens.Length - 1;
            var corners = new uint[cornerCount];
            for (int corner = 0; corner < cornerCount; ++corner)
            {
                string token = tokens[corner + 1];
                // 面顶点可以写成 v / v/vt / v//vn / v/vt/vn，本批只取第一段（位置索引）。
                int slash = token.IndexOf('/');
                string indexText = slash >= 0 ? token.Substring(0, slash) : token;
                if (indexText.Length == 0)
                {
                    throw new AssetFormatException(sourcePath,
                        $"第 {lineNumber} 行 f 的第 {corner + 1} 个顶点 \"{token}\" 缺位置索引");
                }
                if (!int.TryParse(indexText, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value))
                {
                    throw new AssetFormatException(sourcePath,
                        $"第 {lineNumber} 行 f 的第 {corner + 1} 个顶点索引 \"{indexText}\" 不是整数");
                }
                long index;
                if (value > 0)
                {
                    index = value - 1L;
                }
                else if (value < 0)
                {
                    index = vertexCount + value;
                }
                else
                {
                    throw new AssetFormatException(sourcePath,
                        $"第 {lineNumber} 行 f 出现索引 0（OBJ 的顶点索引从 1 开始）");
                }
                if (index < 0L || index >= vertexCount)
                {
                    throw new AssetFormatException(sourcePath,
                        $"第 {lineNumber} 行 f 的索引 {value} 越界：当前只有 {vertexCount} 个顶点");
                }
                corners[corner] = (uint)index;
            }
            // 扇形三角化：以第一个角为扇心，(0, i, i+1) 逐个成三角，n 边面出 n - 2 个。
            for (int corner = 1; corner + 1 < cornerCount; ++corner)
            {
                indices.Add(corners[0]);
                indices.Add(corners[corner]);
                indices.Add(corners[corner + 1]);
            }
        }

        private static byte[] BuildVertexData(List<float> positions, string sourcePath)
        {
            int stride = (int)Mesh.VertexStride;
            if (Mesh.PositionOffset + 3u * sizeof(float) > Mesh.VertexStride ||
                Mesh.ColorOffset + 3u * sizeof(float) > Mesh.VertexStride)
            {
                throw new AssetFormatException(sourcePath,
                    $"网格顶点布局常量容不下 位置 float3（偏移 {Mesh.PositionOffset}）+ 颜色 float3" +
                    $"（偏移 {Mesh.ColorOffset}），步长 {Mesh.VertexStride}");
            }
            int vertexCount = positions.Count / VertexComponents;
            byte[] vertexData = new byte[vertexCount * stride];
            for (int vertex = 0; vertex < vertexCount; ++vertex)
            {
                int baseOffset = vertex * stride;
                for (int component = 0; component < 3; ++component)
                {
                    int componentOffset = (int)(component * sizeof(float));
                    WriteFloat(vertexData, baseOffset + (int)Mesh.PositionOffset + componentOffset,
                        positions[vertex * VertexComponents + component]);
                    // OBJ 没有顶点色：写死白色 (1, 1, 1)，与 Mesh 的布局一致。
                    WriteFloat(vertexData, baseOffset + (int)Mesh.ColorOffset + componentOffset,
                        DefaultVertexColor);
                }
            }
            return vertexData;
        }

        private static byte[] BuildIndexData(List<uint> indices)
        {
            byte[] indexData = new byte[indices.Count * sizeof(uint)];
            for (int index = 0; index < indices.Count; ++index)
            {
                BinaryPrimitives.WriteUInt32LittleEndian(indexData.AsSpan(index * sizeof(uint)), indices[index]);
            }
            return indexData;
        }

        private static void WriteFloat(byte[] destination, int offset, float value)
        {
            BinaryPrimitives.WriteSingleLittleEndian(destination.AsSpan(offset), value);
        }
    }
}
