// editor-code-source-display 2.2 / 5.3：csharp-script 的源显示提供者（design D2 / D3 / D5）。
//
// 契约（AssetDisplay.cs）：提供者**纯投影** —— 不碰文件系统、不调门面。dll 元数据不存源路径，所以手写
// `*.cs` 的扫描（排除 `obj/` 与生成产物）由主线程（EditorService）做完，源文件列表在那里填进
// `AssetContext.SourceFiles`；这里只投影成 {path, kind} 条目。
//
// 本提供者**不产 dll 自身条目**（「隐程序集显源」）。128 个生成绑定源（`out/build/<preset>/codegen/bindings`）
// 本批不显示，但**明示略去 + 给取回目录**：主线程把该目录（相对工程根）填进
// `AssetContext.OmittedSourceDirectories`，这里投影成 `kind`=source-omitted 的标记条目，不静默隐藏
//（PROTOCOL.md 2.6）。
using System;
using System.Collections.Generic;

namespace Scripting.Managed.Editor
{
    /// <summary>
    /// csharp-script → csharp-source 条目：`path` = 相对**工程根**（`Scripting/Managed/Host.cs`）、
    /// `kind` = csharp-source；另产一条 source-omitted 标记条目承载「生成绑定源已略去」的取回目录。
    /// </summary>
    internal sealed class CSharpSourceDisplayProvider : IDisplayProvider
    {
        internal static readonly CSharpSourceDisplayProvider Shared = new CSharpSourceDisplayProvider();

        public IReadOnlyList<AssetEntry> Describe(AssetContext context)
        {
            if (context == null)
            {
                return Array.Empty<AssetEntry>();
            }
            var entries = new List<AssetEntry>();
            if (context.SourceFiles != null)
            {
                foreach (AssetSourceFile source in context.SourceFiles)
                {
                    if (source == null || string.IsNullOrEmpty(source.Path))
                    {
                        continue;
                    }
                    entries.Add(new AssetEntry
                    {
                        Path = source.Path,
                        Kind = EditorProtocol.AssetKindCSharpSource,
                    });
                }
            }
            if (context.OmittedSourceDirectories != null)
            {
                foreach (string directory in context.OmittedSourceDirectories)
                {
                    if (string.IsNullOrEmpty(directory))
                    {
                        continue;
                    }
                    entries.Add(new AssetEntry
                    {
                        Path = directory,
                        Kind = EditorProtocol.AssetKindSourceOmitted,
                    });
                }
            }
            return entries;
        }
    }
}
