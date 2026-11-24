// editor-asset-browser 任务 2.2：显示提供者（design D4）。
//
// 接口形状按**资产中心**定：输入是资产标识（v1 = 相对路径）与文件元数据，输出是显示条目树（含
// children）。v1 退化为"一文件 = 一个资产"（默认提供者产一条、children 恒空）；v2 溯源落地后（程序集
// 展开成源树 / 1→N 衍生物归组）只需按种类注册新提供者，接口不用推翻。
//
// **显示逻辑不挂到 IResource 核心接口上**：反序列化接口不背编辑器义务，提供者纯编辑器侧框架代码
//（本文件只在 Scripting/Managed/Editor/ 下）。
using System;
using System.Collections.Generic;

namespace Scripting.Managed.Editor
{
    /// <summary>
    /// 一条资产条目（PROTOCOL.md 2.6）：v1 `children` 恒为空、`nodeId` = `path` = 相对资源根的正斜杠路径。
    /// 七个字段全部必填；JSON 形状由这里的 <see cref="ToJson"/> 决定，两侧镜像按字段名逐字对齐。
    /// </summary>
    internal sealed class AssetEntry
    {
        internal string NodeId = string.Empty;
        internal string Path = string.Empty;
        internal string Kind = "file";
        internal long SizeBytes;
        internal string ModifiedUtc = string.Empty;
        internal bool Registered;
        internal List<AssetEntry> Children = new List<AssetEntry>();

        internal Dictionary<string, object> ToJson()
        {
            var children = new List<object>();
            foreach (AssetEntry child in Children)
            {
                children.Add(child.ToJson());
            }
            return new Dictionary<string, object>
            {
                ["nodeId"] = NodeId,
                ["path"] = Path,
                ["kind"] = Kind,
                ["sizeBytes"] = SizeBytes,
                ["modifiedUtc"] = ModifiedUtc,
                ["registered"] = Registered,
                ["children"] = children,
            };
        }
    }

    /// <summary>
    /// 描述一个资产所需的上下文（资产中心的输入）：v1 是"相对路径 + 文件元数据 + 资源根"，
    /// 由编辑器服务在枚举时填好；提供者不碰文件系统、不调门面（那些都在主线程命令里已经做完）。
    /// </summary>
    internal sealed class AssetContext
    {
        internal string ResourceRoot = string.Empty;
        internal string RelativePath = string.Empty;
        internal string Kind = "file";
        internal long SizeBytes;
        internal string ModifiedUtc = string.Empty;
        internal bool Registered;
    }

    /// <summary>
    /// 显示提供者：把一个资产投影成显示条目树。v1 的实现只产一条；v2 的程序集展开 / 衍生物归组在这里
    /// 产出多个条目（父条目把子条目挂进 `children`），`nodeId` 仍取稳定标识。
    /// </summary>
    internal interface IDisplayProvider
    {
        IReadOnlyList<AssetEntry> Describe(AssetContext context);
    }

    /// <summary>默认提供者：一文件一条目，`children` 恒空（v1 的 1:1 退化）。</summary>
    internal sealed class DefaultFileDisplayProvider : IDisplayProvider
    {
        internal static readonly DefaultFileDisplayProvider Shared = new DefaultFileDisplayProvider();

        public IReadOnlyList<AssetEntry> Describe(AssetContext context)
        {
            if (context == null || string.IsNullOrEmpty(context.RelativePath))
            {
                return Array.Empty<AssetEntry>();
            }
            var entry = new AssetEntry
            {
                NodeId = context.RelativePath,
                Path = context.RelativePath,
                Kind = context.Kind,
                SizeBytes = context.SizeBytes,
                ModifiedUtc = context.ModifiedUtc,
                Registered = context.Registered,
            };
            return new[] { entry };
        }
    }

    /// <summary>
    /// 按种类注册的提供者表 + 扩展名到种类的映射（design D4）。没登记的种类落默认提供者；
    /// 未知扩展名落 `file` 种类而不是报错（资产根里出现新产物时浏览器不该整体失败）。
    /// </summary>
    internal static class AssetDisplayProviders
    {
        internal const string FileKind = "file";

        private static readonly Dictionary<string, IDisplayProvider> s_providers =
            new Dictionary<string, IDisplayProvider>(StringComparer.Ordinal);

        // 种类映射：键是扩展名的小写形式（比对时不区分大小写，`.shLib` / `.SHLIB` 同一种）。
        private static readonly Dictionary<string, string> s_kinds =
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                [".shlib"] = "shader-library",
                [".tex"] = "texture",
                [".mesh"] = "mesh",
                [".res"] = "resource",
            };

        /// <summary>按种类登记提供者（v2 的程序集 / 缩略图提供者从这里注册；同种类再登记 = 替换）。</summary>
        internal static void Register(string kind, IDisplayProvider provider)
        {
            if (string.IsNullOrEmpty(kind) || provider == null)
            {
                return;
            }
            s_providers[kind] = provider;
        }

        /// <summary>取该种类的提供者；没登记就回默认提供者（一文件一条目）。</summary>
        internal static IDisplayProvider Resolve(string kind)
        {
            if (!string.IsNullOrEmpty(kind) && s_providers.TryGetValue(kind, out IDisplayProvider provider))
            {
                return provider;
            }
            return DefaultFileDisplayProvider.Shared;
        }

        /// <summary>扩展名 → 种类；未知扩展名（含没有扩展名）落 `file`。</summary>
        internal static string KindOf(string extension)
        {
            if (string.IsNullOrEmpty(extension))
            {
                return FileKind;
            }
            string key = extension.ToLowerInvariant();
            return s_kinds.TryGetValue(key, out string kind) ? kind : FileKind;
        }
    }
}
