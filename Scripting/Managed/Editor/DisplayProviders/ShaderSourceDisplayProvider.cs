// editor-code-source-display 2.1 / 5.3：shader-library 的源显示提供者（design D2 / D3 / D5）。
//
// 契约（AssetDisplay.cs）：提供者**纯投影** —— 不碰文件系统、不调门面。shLib 字节里的源路径串由主线程
// （EditorService）读出来、映射到 `CAResources/<源路径>.slang`，源文件是否存在也在那里填进
// `AssetContext.SourceFiles`；这里只把这些信息投影成 {path, kind} 条目。
//
// 本提供者**不产 shLib 自身条目**：代码型资产只显源文件（用户裁定「隐程序集显源」）。缺源文件的历史路径
// 仍产条目（kind=shader-source-missing），不静默跳过；两个 shLib 的源条目由 `EnumerateAssets` 按 path 去重。
using System;
using System.Collections.Generic;

namespace Scripting.Managed.Editor
{
    /// <summary>
    /// shader-library → shader-source 条目：`path` = 相对**工程根**（`CAResources/Shaders/FinalBlit.slang`）、
    /// `kind` = shader-source；源文件不存在时 `kind` = shader-source-missing（条目仍产出）。
    /// </summary>
    internal sealed class ShaderSourceDisplayProvider : IDisplayProvider
    {
        internal static readonly ShaderSourceDisplayProvider Shared = new ShaderSourceDisplayProvider();

        public IReadOnlyList<AssetEntry> Describe(AssetContext context)
        {
            if (context == null || context.SourceFiles == null || context.SourceFiles.Count == 0)
            {
                return Array.Empty<AssetEntry>();
            }
            var entries = new List<AssetEntry>(context.SourceFiles.Count);
            foreach (AssetSourceFile source in context.SourceFiles)
            {
                if (source == null || string.IsNullOrEmpty(source.Path))
                {
                    continue;
                }
                // 缺源的历史路径用 kind 变体表达（PROTOCOL.md 2.6）：仍产条目，调用方从 path 就能看到略去了什么。
                entries.Add(new AssetEntry
                {
                    Path = source.Path,
                    Kind = source.Exists
                        ? EditorProtocol.AssetKindShaderSource
                        : EditorProtocol.AssetKindShaderSourceMissing,
                });
            }
            return entries;
        }
    }
}
