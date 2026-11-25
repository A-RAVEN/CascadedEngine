// editor-asset-browser 任务 2.2：显示提供者（design D4）。
//
// 接口形状按**资产中心**定：输入是资产标识（相对路径）与提供者需要的上下文，输出是显示条目列表。
// v1 退化为"一文件 = 一个资产"（默认提供者产一条）；v2 溯源落地后（程序集展开成源树 / 1→N 衍生物归组）
// 只需按种类注册新提供者，接口不用推翻。
//
// editor-code-source-display 1.1 / 1.2：注册键从「仅种类」扩到「种类 + 路径」（精确绑定 → 种类默认 →
// 默认提供者），并新增「注册显示资源」表 —— 路径不在资产根里被枚举到的资源（C# 的 dll 在 bin/）也能
// 引入显示条目。IO 全部由主线程做完填进 <see cref="AssetContext"/>（含源文件信息 SourceFiles），
// 提供者只投影、不碰文件系统、不调门面。
//
// editor-code-source-display 5.1（2026-11-25 用户裁定）：条目**只有 path + kind 两个字段**。大小 / 修改时间 /
// 注册状态 / 子条目 / 编号这类固定元数据是强加字段，全部删掉；提供者决定显示什么（含不存在的历史路径、
// 虚拟条目），浏览器忠实渲染。
//
// **显示逻辑不挂到 IResource 核心接口上**：反序列化接口不背编辑器义务，提供者纯编辑器侧框架代码
//（本文件只在 Scripting/Managed/Editor/ 下）。
using System;
using System.Collections.Generic;

namespace Scripting.Managed.Editor
{
    /// <summary>
    /// 一条资产条目（PROTOCOL.md 2.6）：**只有 `path`（资源路径）+ `kind`（资源类型）两个字段**。
    /// `path` 同时是条目的稳定标识；JSON 形状由这里的 <see cref="ToJson"/> 决定，两侧镜像按字段名逐字对齐。
    /// </summary>
    internal sealed class AssetEntry
    {
        internal string Path = string.Empty;
        internal string Kind = "file";

        internal Dictionary<string, object> ToJson()
        {
            return new Dictionary<string, object>
            {
                ["path"] = Path,
                ["kind"] = Kind,
            };
        }
    }

    /// <summary>
    /// 一个源文件的信息（editor-code-source-display D3 / 5.3）：**主线程**读 shLib 字节 / 扫源目录之后填好
    /// （只有源路径 + 存在性），源显示提供者据此决定条目的 kind，不碰文件系统。
    ///
    /// `Path` 是**相对工程根**的正斜杠路径（如 `CAResources/Shaders/FinalBlit.slang`、
    /// `Scripting/Managed/Host.cs`）—— 源文件在资源根（CAAssets）之外，无法相对资源根。
    /// </summary>
    internal sealed class AssetSourceFile
    {
        internal string Path = string.Empty;
        /// <summary>源文件当前是否存在（缺失的历史路径仍要产条目，kind 用 missing 变体表达）。</summary>
        internal bool Exists;
    }

    /// <summary>
    /// 描述一个资产所需的上下文（资产中心的输入）：由编辑器服务在枚举时填好；提供者不碰文件系统、
    /// 不调门面（那些都在主线程命令里已经做完）。
    ///
    /// 字段都是提供者**投影时真正需要的输入**：`RelativePath` + `Kind`（默认提供者用）、`SourceFiles`
    /// （主线程填好的源文件列表）、`OmittedSourceDirectories`（明示略去的源目录，如生成绑定目录 —— 取回路径）。
    /// </summary>
    internal sealed class AssetContext
    {
        internal string RelativePath = string.Empty;
        internal string Kind = "file";
        /// <summary>该资产映射到的源文件（主线程填好；数据型资产为空列表）。</summary>
        internal List<AssetSourceFile> SourceFiles = new List<AssetSourceFile>();
        /// <summary>本批明示略去的源目录（相对工程根，如 `out/build/<preset>/codegen/bindings`）。</summary>
        internal List<string> OmittedSourceDirectories = new List<string>();
    }

    /// <summary>
    /// 显示提供者：把一个资产投影成显示条目列表。默认实现只产一条；程序集展开 / 衍生物归组在这里产出
    /// 多个条目（`path` 保持稳定标识）。
    /// </summary>
    internal interface IDisplayProvider
    {
        IReadOnlyList<AssetEntry> Describe(AssetContext context);
    }

    /// <summary>默认提供者：一文件一条目（`path` = 相对资源根路径、`kind` = 种类映射读数）。</summary>
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
                Path = context.RelativePath,
                Kind = context.Kind,
            };
            return new[] { entry };
        }
    }

    /// <summary>
    /// 按种类 + 路径注册的提供者表 + 扩展名到种类的映射（design D4 / editor-code-source-display D1）。
    ///
    /// 取用顺序：**精确 (kind, path) → 种类默认（path 空）→ 默认提供者**。没登记的种类落默认提供者；
    /// 未知扩展名落 `file` 种类而不是报错（资产根里出现新产物时浏览器不该整体失败）。
    /// </summary>
    internal static class AssetDisplayProviders
    {
        internal const string FileKind = "file";
        internal const string ShaderLibraryKind = "shader-library";
        internal const string CSharpScriptKind = "csharp-script";

        /// <summary>种类默认提供者（`Register(kind, provider)`，path 空）。</summary>
        private static readonly Dictionary<string, IDisplayProvider> s_providers =
            new Dictionary<string, IDisplayProvider>(StringComparer.Ordinal);

        /// <summary>精确 (kind, path) 绑定。</summary>
        private static readonly Dictionary<(string Kind, string Path), IDisplayProvider> s_pathProviders =
            new Dictionary<(string Kind, string Path), IDisplayProvider>();

        /// <summary>注册显示资源：路径不一定出现在资产根枚举里（C# 的 dll 在 bin/），枚举时逐条引入。</summary>
        private static readonly List<DisplayResource> s_displayResources = new List<DisplayResource>();
        private static readonly HashSet<(string Kind, string Path)> s_displayResourceKeys =
            new HashSet<(string Kind, string Path)>();

        /// <summary>一条注册显示资源（种类 + 路径 + 提供者）。</summary>
        internal sealed class DisplayResource
        {
            internal string Kind = string.Empty;
            internal string Path = string.Empty;
            internal IDisplayProvider Provider;
        }

        // 种类映射：键是扩展名的小写形式（比对时不区分大小写，`.shLib` / `.SHLIB` 同一种）。
        private static readonly Dictionary<string, string> s_kinds =
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                [".shlib"] = ShaderLibraryKind,
                [".tex"] = "texture",
                [".mesh"] = "mesh",
                [".res"] = "resource",
            };

        /// <summary>按种类登记提供者（等价于 path 空的种类默认；v2 的程序集 / 缩略图提供者从这里注册）。</summary>
        internal static void Register(string kind, IDisplayProvider provider)
        {
            Register(kind, string.Empty, provider);
        }

        /// <summary>
        /// 按种类 + 路径登记提供者（path 空＝种类默认）。同键再登记 = 替换；字符串按序数比对
        /// （Windows 侧路径已由枚举/注册点统一成正斜杠形式）。
        /// </summary>
        internal static void Register(string kind, string path, IDisplayProvider provider)
        {
            if (string.IsNullOrEmpty(kind) || provider == null)
            {
                return;
            }
            string keyPath = NormalizeProviderPath(path);
            if (keyPath.Length == 0)
            {
                s_providers[kind] = provider;
                return;
            }
            s_pathProviders[(kind, keyPath)] = provider;
        }

        /// <summary>
        /// 注册一个「显示资源」：既按精确 (kind, path) 绑定提供者，又登记进枚举引入表 —— 该路径即使不在
        /// 资产根里（dll 在 bin/），浏览器也会额外对它调 Describe 引入条目。同键重复注册只算一次。
        /// </summary>
        internal static void RegisterDisplayResource(string kind, string path, IDisplayProvider provider)
        {
            if (string.IsNullOrEmpty(kind) || string.IsNullOrEmpty(path) || provider == null)
            {
                return;
            }
            string keyPath = NormalizeProviderPath(path);
            if (keyPath.Length == 0)
            {
                return;
            }
            Register(kind, keyPath, provider);
            if (!s_displayResourceKeys.Add((kind, keyPath)))
            {
                return;
            }
            s_displayResources.Add(new DisplayResource { Kind = kind, Path = keyPath, Provider = provider });
        }

        /// <summary>枚举时要额外引入的注册显示资源（按注册顺序）。</summary>
        internal static IReadOnlyList<DisplayResource> DisplayResources => s_displayResources;

        /// <summary>取该种类 + 路径的提供者：精确 (kind,path) → 种类默认 → 默认提供者。</summary>
        internal static IDisplayProvider Resolve(string kind, string path)
        {
            if (!string.IsNullOrEmpty(kind))
            {
                string keyPath = NormalizeProviderPath(path);
                if (keyPath.Length != 0 &&
                    s_pathProviders.TryGetValue((kind, keyPath), out IDisplayProvider exact))
                {
                    return exact;
                }
                if (s_providers.TryGetValue(kind, out IDisplayProvider byKind))
                {
                    return byKind;
                }
            }
            return DefaultFileDisplayProvider.Shared;
        }

        /// <summary>取该种类的提供者（path 空）：精确→种类默认→默认提供者链的退化形态。</summary>
        internal static IDisplayProvider Resolve(string kind)
        {
            return Resolve(kind, string.Empty);
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

        private static string NormalizeProviderPath(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return string.Empty;
            }
            string normalized = path.Replace('\\', '/');
            while (normalized.StartsWith("./", StringComparison.Ordinal))
            {
                normalized = normalized.Substring(2);
            }
            return normalized.Trim('/');
        }
    }

    /// <summary>
    /// 源显示的门控与**生产注册点**（editor-code-source-display D1 / D4）。
    ///
    /// 门控 `CASCADED_EDITOR_SOURCE_DISPLAY=off`（默认 on，登记 CSharpHost/README）：off 时**不做任何源显示
    /// 注册**，代码型程序集（shLib / dll）落默认提供者仍显 —— 红态「未注册落默认」可在同一进程里翻开关
    /// 观察（枚举时每次读门控，验收要同进程差分）。
    ///
    /// 生产注册点**钉死真实路径**：`(shader-library, "VulkanShaderLibrary.shLib")` /
    /// `(shader-library, "D3D12ShaderLibrary.shLib")` 绑 `ShaderSourceDisplayProvider`；
    /// `(csharp-script, "Scripting.Managed.dll")` 作为注册显示资源引入（dll 在 bin/，不在资产根）。
    /// 探针 `ShaderLibraries/Browser.shLib` 路径不同 → 精确绑定不命中 → 落默认提供者仍显，
    /// 既有 `TestEditorAssetBrowser` 的数据型断言不受影响。
    /// </summary>
    internal static class EditorSourceDisplay
    {
        internal const string OffValue = "off";
        internal const string VulkanShaderLibraryPath = "VulkanShaderLibrary.shLib";
        internal const string D3D12ShaderLibraryPath = "D3D12ShaderLibrary.shLib";
        internal const string CSharpAssemblyPath = "Scripting.Managed.dll";

        private static bool s_providersInstalled;

        /// <summary>开关读数：默认 on；只有值等于 `off`（不区分大小写）才关。每次枚举都会重读。</summary>
        internal static bool Enabled =>
            !string.Equals(Environment.GetEnvironmentVariable("CASCADED_EDITOR_SOURCE_DISPLAY"), OffValue,
                StringComparison.OrdinalIgnoreCase);

        /// <summary>
        /// 按当前开关注册源显示（幂等）。off 时什么都不注册并返回 false —— 之后即使把开关翻回 on 也不会有
        /// 半注册状态（注册全部集中在一次调用里）。
        /// </summary>
        internal static bool EnsureRegistered()
        {
            if (!Enabled)
            {
                return false;
            }
            if (s_providersInstalled)
            {
                return true;
            }
            s_providersInstalled = true;
            AssetDisplayProviders.Register(AssetDisplayProviders.ShaderLibraryKind, VulkanShaderLibraryPath,
                ShaderSourceDisplayProvider.Shared);
            AssetDisplayProviders.Register(AssetDisplayProviders.ShaderLibraryKind, D3D12ShaderLibraryPath,
                ShaderSourceDisplayProvider.Shared);
            AssetDisplayProviders.RegisterDisplayResource(AssetDisplayProviders.CSharpScriptKind, CSharpAssemblyPath,
                CSharpSourceDisplayProvider.Shared);
            return true;
        }
    }
}
