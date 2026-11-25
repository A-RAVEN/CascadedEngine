using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// 源导入器的环境约定（source-importers-png-obj 任务 2.1 / 2.2 / 2.3）：
    ///   * 门控 `CASCADED_SOURCE_IMPORTERS=1`（默认关零行为，只由 `Host.Tick` 读）；
    ///   * 阶段机 `CASCADED_SOURCE_IMPORTERS_PHASE`（测试在两次 Tick 之间改它，驱动读数）；
    ///   * 读数文件 `CASCADED_SOURCE_IMPORTERS_READINGS`（句柄 / Produce 次数 / 红态异常，C++ 读同一份）；
    ///   * 源根 `CASCADED_SOURCE_IMPORTERS_DIR`（C# 反向实现读不到管线传入的扫描根，见
    ///     `ResourceImporter.h` 的说明；测试传临时绝对路径，生产默认工程内 `CAResources/`）。
    /// </summary>
    internal static class SourceImporterEnvironment
    {
        internal const string GateVariable = "CASCADED_SOURCE_IMPORTERS";
        internal const string PhaseVariable = "CASCADED_SOURCE_IMPORTERS_PHASE";
        internal const string ReadingsVariable = "CASCADED_SOURCE_IMPORTERS_READINGS";
        internal const string SourceDirectoryVariable = "CASCADED_SOURCE_IMPORTERS_DIR";

        /// <summary>
        /// 默认源根：工程内 `CAResources/`（design D7：① 源与 ② 资产都进工程，源是重建原点）。
        /// 反向实现读不到管线传进来的扫描根，所以 `GetSourceDirectory()` 必须返回非空值。
        /// </summary>
        internal const string DefaultSourceDirectory = "CAResources";

        internal static string SourceDirectory()
        {
            string declared = Environment.GetEnvironmentVariable(SourceDirectoryVariable);
            return string.IsNullOrEmpty(declared) ? DefaultSourceDirectory : declared;
        }

        /// <summary>追加一条 `key=value` 读数（不带 BOM：原生侧按行首 `key=` 解析）。</summary>
        internal static void Write(string key, object value)
        {
            string path = Environment.GetEnvironmentVariable(ReadingsVariable);
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            string text = key + "=" + Convert.ToString(value, CultureInfo.InvariantCulture) + "\n";
            File.AppendAllText(path, text, new UTF8Encoding(false));
        }

        /// <summary>
        /// 源相对路径 → ② 资产相对路径：换扩展名 + 分隔符统一成正斜杠（design D7 的镜像规则，
        /// 例 `Images/A/Test.png` → `Images/A/Test.tex`）。
        /// </summary>
        internal static string MirrorPath(string relativeSourcePath, string sourceExtension, string assetExtension)
        {
            string withoutExtension = relativeSourcePath.Substring(0, relativeSourcePath.Length - sourceExtension.Length);
            return withoutExtension.Replace('\\', '/') + assetExtension;
        }

        /// <summary>
        /// 枚举源根下指定扩展名的文件，返回**相对源根的正斜杠路径**并按序数排序（导入必须确定：
        /// 同一份源每次产出的 NextPath 顺序逐字相同）。
        /// </summary>
        internal static List<string> EnumerateSourceFiles(string sourceRoot, string extension)
        {
            var files = new List<string>();
            foreach (string file in Directory.EnumerateFiles(sourceRoot, "*", SearchOption.AllDirectories))
            {
                if (string.Equals(Path.GetExtension(file), extension, StringComparison.OrdinalIgnoreCase))
                {
                    files.Add(Path.GetRelativePath(sourceRoot, file).Replace('\\', '/'));
                }
            }
            files.Sort(StringComparer.Ordinal);
            return files;
        }
    }

    /// <summary>
    /// 一次 <c>Produce()</c> 的产出迭代器（拉取式，照 `ResourceImporterProbe` 的写法）：
    /// 管线先问 <c>HasNext()</c>，为真才取一条（<c>NextPath()</c> + <c>NextResource()</c>）。
    /// 产出在 <c>Produce()</c> 里一次算完（解码/解析失败要能整轮报错，不能把错误拖到取条时）。
    /// </summary>
    internal sealed class SourceProductions : ResourceImporterProductionsBase
    {
        private readonly List<string> m_paths = new List<string>();
        private readonly List<ResourceBase> m_resources = new List<ResourceBase>();
        private int m_index;

        internal int Count => m_paths.Count;

        internal IReadOnlyList<string> Paths => m_paths;

        internal IReadOnlyList<ResourceBase> Resources => m_resources;

        internal void Add(string path, ResourceBase resource)
        {
            m_paths.Add(path);
            m_resources.Add(resource);
        }

        public override bool HasNext()
        {
            return m_index < m_paths.Count;
        }

        public override string NextPath()
        {
            return m_paths[m_index];
        }

        public override ResourceBase NextResource()
        {
            ResourceBase resource = m_resources[m_index];
            ++m_index;
            return resource;
        }
    }
}
