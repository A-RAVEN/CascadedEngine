using System;
using System.Collections.Generic;
using System.IO;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// 源导入器的共同骨架（source-importers-png-obj design D1 / D4）：拉取式 <c>Produce()</c> 扫源根下
    /// 指定扩展名的文件，逐个交给子类解码/解析，产出**直接 new 的资产**交迭代器。
    ///
    /// 硬边界（判据写在 tasks / spec 里）：
    ///   * **不调注册表**：注册 / 释放 / 新建 / 取或加载 / 取或新建这五个注册表写入口，在本目录里
    ///     一个都不许出现 —— 注册、替换与落盘由管线 `ResourceImportingSystem::ScanSourceDirectory`
    ///     （CreateProxy 注册 + SerializeAll）做；
    ///   * 不经资产工厂 / 资产数据库门面（后者的 Create 第一句就是注册表写入口，等于在 Produce 里注册）；
    ///   * 一条产出 = 一条资源，`NextPath` 是**镜像路径**（源相对路径 + 扩展名替换，design D7）；
    ///   * 坏文件抛 `AssetFormatException`（点名源相对路径与原因），并把异常类型/消息/路径写进读数文件
    ///     —— 管线边界会把它变形为 1012，C++ 只能从读数通道观测（design D5）。
    /// </summary>
    internal abstract class SourceImporterBase : ResourceImporterBase
    {
        /// <summary>每成功一轮 Produce 交出的产出（读数抓句柄用；重导入第二轮是新的一份）。</summary>
        internal readonly List<SourceProductions> Rounds = new List<SourceProductions>();

        /// <summary>Produce 被管线调用的总次数（含失败轮）。</summary>
        internal int ProduceCount;

        /// <summary>抓到的 AssetFormatException 次数（红态观测通道的另一半：证明"确实抛过"）。</summary>
        internal int ErrorCount { get; private set; }

        /// <summary>读数键前缀：`png` / `obj`。</summary>
        internal abstract string Prefix { get; }

        /// <summary>源扩展名（小写，含点）：`.png` / `.obj`。</summary>
        internal abstract string SourceExtension { get; }

        /// <summary>② 资产扩展名（小写，含点）：`.tex` / `.mesh`。</summary>
        internal abstract string AssetExtension { get; }

        internal abstract string Tags { get; }

        /// <summary>目标根的标识串：只作标识，管线落盘用的是 NextPath + 资源根（不参与写盘）。</summary>
        internal abstract string DestDirectoryLabel { get; }

        public override string GetTags()
        {
            return Tags;
        }

        // C# 反向实现读不到管线传入的扫描根（ResourceImporter.h 的说明），所以目录必须自己声明。
        public override string GetSourceDirectory()
        {
            return SourceImporterEnvironment.SourceDirectory();
        }

        public override string GetDestDirectory()
        {
            return DestDirectoryLabel;
        }

        public override ResourceImporterProductionsBase Produce()
        {
            ++ProduceCount;
            var productions = new SourceProductions();
            try
            {
                string sourceRoot = GetSourceDirectory();
                List<string> files = EnumerateSourceFiles(sourceRoot);
                foreach (string relativePath in files)
                {
                    byte[] bytes = ReadSourceBytes(sourceRoot, relativePath);
                    ProduceOne(relativePath, bytes, productions);
                }
            }
            catch (AssetFormatException error)
            {
                RecordError(error);
                throw;
            }

            int round = Rounds.Count + 1;
            Rounds.Add(productions);
            SourceImporterEnvironment.Write(Prefix + "_produce_count", ProduceCount);
            SourceImporterEnvironment.Write(Prefix + "_round" + round + "_count", productions.Count);
            for (int index = 0; index < productions.Count; ++index)
            {
                SourceImporterEnvironment.Write(Prefix + "_round" + round + "_path_" + index,
                    productions.Paths[index]);
            }
            return productions;
        }

        /// <summary>解一个源文件，把 (镜像路径, 资产) 追加进产出。</summary>
        protected abstract void ProduceOne(string relativePath, byte[] bytes, SourceProductions productions);

        private List<string> EnumerateSourceFiles(string sourceRoot)
        {
            try
            {
                return SourceImporterEnvironment.EnumerateSourceFiles(sourceRoot, SourceExtension);
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
            {
                throw new AssetFormatException(sourceRoot,
                    $"枚举源目录失败：{error.GetType().Name}: {error.Message}", error);
            }
        }

        private static byte[] ReadSourceBytes(string sourceRoot, string relativePath)
        {
            string fullPath = Path.Combine(sourceRoot, relativePath.Replace('/', Path.DirectorySeparatorChar));
            try
            {
                return File.ReadAllBytes(fullPath);
            }
            catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
            {
                throw new AssetFormatException(relativePath,
                    $"读取源文件失败：{error.GetType().Name}: {error.Message}", error);
            }
        }

        private void RecordError(AssetFormatException error)
        {
            ++ErrorCount;
            SourceImporterEnvironment.Write(Prefix + "_error_count", ErrorCount);
            SourceImporterEnvironment.Write(Prefix + "_error_type", error.GetType().Name);
            SourceImporterEnvironment.Write(Prefix + "_error_message", error.Message);
            SourceImporterEnvironment.Write(Prefix + "_error_asset_path", error.AssetPath);
            SourceImporterEnvironment.Write(Prefix + "_error_produce", ProduceCount);
        }
    }
}
