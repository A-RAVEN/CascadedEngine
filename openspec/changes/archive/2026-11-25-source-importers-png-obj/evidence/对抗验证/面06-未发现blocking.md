# 面 06：资产浏览器 `.tex→texture` / `.mesh→mesh` 种类映射 —— 未发现 blocking

审查对象：`design.md:36`（「资产浏览器 kind=texture/mesh（扩展名映射 .tex→texture/.mesh→mesh，editor-asset-browser D4 已含）」）、`spec.md:10/24`（「资产浏览器 list-assets 含该条目 kind=texture/mesh registered=true」）、`tasks.md:20`（3.1 同断言）。

## 一、映射真实存在，且已接到 list-assets

`Scripting/Managed/Editor/AssetDisplay.cs`：

```csharp
106:        // 种类映射：键是扩展名的小写形式（比对时不区分大小写，`.shLib` / `.SHLIB` 同一种）。
107:        private static readonly Dictionary<string, string> s_kinds =
108:            new Dictionary<string, string>(StringComparer.Ordinal)
109:            {
110:                [".shlib"] = "shader-library",
111:                [".tex"] = "texture",
112:                [".mesh"] = "mesh",
113:                [".res"] = "resource",
114:            };
```

```csharp
136:        /// <summary>扩展名 → 种类；未知扩展名（含没有扩展名）落 `file`。</summary>
137:        internal static string KindOf(string extension)
...
143:            string key = extension.ToLowerInvariant();
144:            return s_kinds.TryGetValue(key, out string kind) ? kind : FileKind;
```

`list-assets` 的枚举路径真实调用了它：

```csharp
// Scripting/Managed/Editor/EditorService.cs:1112
string kind = AssetDisplayProviders.KindOf(Path.GetExtension(relative));
```

`kind` 进入条目并写进 JSON：`AssetDisplay.cs:22` `internal string Kind = "file";`、`:39` `["kind"] = Kind,`。

## 二、规范出处也对得上

> `openspec/changes/archive/2026-11-24-editor-asset-browser/design.md:59`
> - 种类映射：.shLib→shader-library、.tex→texture、.mesh→mesh、.res→resource、其它→file。

生效规范也写同一映射：`openspec/specs/editor-asset-browser/spec.md` 的资产条目 `kind` 字段（要求与场景）依赖这张表。

## 三、结论

`.tex → texture`、`.mesh → mesh` 的映射真实存在，并且是 `list-assets` 的实际取值路径；本批只需把 ② 文件放进资源根即可产出对应 kind。**本面未发现 blocking**。
