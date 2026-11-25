# 本面未发现 blocking：资产浏览器 kind 映射（.tex/.mesh）与既有规范对得上

## 面与问题

攻击面 ⑤：本 change 的「资产浏览器 kind=texture/mesh」与 `editor-asset-browser` 规范及实现是否对得上。结论：**未发现 blocking**。扩展名映射、registered 读数、显示提供者三条都对得上，且 proposal 声明「不改浏览器代码，只补真实内容」，与既有「本批 MUST 只读，MUST NOT 提供编辑/删除/重命名/导入」不冲突。

## 一致性的逐字引文

本 change `specs/source-importers-png-obj/spec.md` 第 10 行：

> - **THEN** ② `Textures/*.tex` MUST 落盘且 HasResource 真；Texture2D.Parse 读回宽/高/格式/像素 MUST 等于写死期望（逐字节）；资产浏览器 list-assets MUST 含该条目 kind=texture registered=true

同文件第 24 行：

> - **THEN** ② `Meshes/*.mesh` MUST 落盘且 HasResource 真；Mesh.Parse 读回 VertexCount/IndexCount/VertexData/IndexData MUST 等于写死期望；资产浏览器 list-assets MUST 含该条目 kind=mesh registered=true

`openspec/specs/editor-asset-browser/spec.md` 第 44 行：

> 种类映射 MUST 覆盖已知扩展名（.shLib/.tex/.mesh/.res），未知扩展名 MUST 落默认种类而非报错。

`Scripting/Managed/Editor/PROTOCOL.md` 第 124 行：

> | `kind` | string | 种类（由扩展名映射，大小写不敏感）：`.shLib`→`shader-library`、`.tex`→`texture`、`.mesh`→`mesh`、`.res`→`resource`、其它→`file` |

`Scripting/Managed/Editor/AssetDisplay.cs`：

> [".shlib"] = "shader-library",
> [".tex"] = "texture",
> [".mesh"] = "mesh",
> [".res"] = "resource",

`Scripting/Managed/Editor/EditorService.cs`：

> bool registered = registry.HasResource(relative);

`proposal.md` 第 13 行：

> **资产浏览器联动**：导入后 ② 文件 kind=texture/mesh、registered=true，editor-asset-browser 列展示（不改浏览器代码，只补真实内容）。

`openspec/specs/editor-asset-browser/spec.md` 第 11 行（末尾）：

> 本批 MUST 只读，MUST NOT 提供编辑/删除/重命名/导入。

（导入由宿主门控/管线完成，不新增浏览器按钮或命令；「MUST NOT 提供导入」指的是浏览器界面不提供导入动作，与宿主侧的导入器不冲突。）

## grep / 命令与真实输出

```powershell
Select-String -Path 'Scripting/Managed/Editor/AssetDisplay.cs' -Pattern '\["\.tex"\]|\["\.mesh"\]' | ForEach-Object { "AssetDisplay.cs:$($_.LineNumber): $($_.Line.Trim())" }
```

```
AssetDisplay.cs:111: [".tex"] = "texture",
AssetDisplay.cs:112: [".mesh"] = "mesh",
```

```powershell
Select-String -Path 'Scripting/Managed/Editor/PROTOCOL.md' -Pattern '\.tex|\.mesh' | ForEach-Object { "PROTOCOL.md:$($_.LineNumber): $($_.Line.Trim())" }
```

```
PROTOCOL.md:115: { "nodeId": "CSharpProbe/Probe.tex", "path": "CSharpProbe/Probe.tex", "kind": "texture",
PROTOCOL.md:123: | `path` | string | 相对资源根的正斜杠路径（例：`CSharpProbe/Probe.tex`） |
PROTOCOL.md:124: | `kind` | string | 种类（由扩展名映射，大小写不敏感）：`.shLib`→`shader-library`、`.tex`→`texture`、`.mesh`→`mesh`、`.res`→`resource`、其它→`file` |
```

```powershell
Select-String -Path 'Scripting/Managed/Editor/EditorService.cs' -Pattern 'HasResource\(relative\)' | ForEach-Object { "EditorService.cs:$($_.LineNumber): $($_.Line.Trim())" }
```

```
EditorService.cs:1113: bool registered = registry.HasResource(relative);
```

```powershell
Select-String -Path 'openspec/specs/editor-asset-browser/spec.md' -Pattern '种类映射 MUST 覆盖|MUST 只读' | ForEach-Object { "editor-asset-browser/spec.md:$($_.LineNumber)" }
```

```
editor-asset-browser/spec.md:11
editor-asset-browser/spec.md:44
```

## 非 blocking 观察

1. **list-assets 断言需要编辑器服务**：tasks 3.1 要求 `TestSourceImporters` 断言「资产浏览器 list-assets 含 kind=texture/mesh 条目」，但 `list-assets` 是编辑器服务的 WebSocket 命令，只有 `CASCADED_EDITOR=1` 起服务后才能发；既有 `TestEditorAssetBrowser` 是独立进程、自己起服务并当客户端。新用例要么自己也起一次服务（等于第三个编辑器进程），要么把这条断言并进 `TestEditorAssetBrowser` 的预置数据。`docs/testing.md` 第 36/245 行的「编辑器服务一次性，两个编辑器用例不同进程」只是现状描述，并未禁止第三个，所以不判 blocking；但 tasks 应写明由谁起服务、端口与清理，否则 3.1 的这条断言无法执行。
2. **`registered` 真/假都出现**：既有 `editor-asset-browser` 规范要求「registered MUST 等于 HasResource 读数且真值与假值都出现（恒假实现不得过）」。新导入的资产只贡献「真」值，这条不冲突；但 3.1 若只断言新条目的 registered=true，不能替代既有的真/假双向判据（该判据仍由 `TestEditorAssetBrowser` 负责）。
3. **视觉验收的预置行**：既有规范场景要求 dock 里 MUST 含五个既有文件的相对路径（`VulkanShaderLibrary.shLib` 等），措辞是「MUST 含」，新增的 `Textures/*.tex`、`Meshes/*.mesh` 行不违反它；但 3.2 的 `read_image` 核对记录里应明确「五条既有行仍在 + 新增行出现」，避免只看新增行。

## 复核方式

1. 改后 `AssetDisplay.cs` 的映射与 `PROTOCOL.md` 第 124 行逐字对照（不区分大小写：`.tex`/`.TEX` 同为 texture）。
2. 导出一个 `.tex` 后发 `list-assets`，条目 `kind` 必须是 `texture`、`registered` 必须是 `true`；把该文件删掉但保留注册表条目（或反之）时，`registered` 必须跟着变。
3. 视觉验收截图必须同时看到既有五条行与新增 texture/mesh 行（见上面非 blocking 观察 3）。
