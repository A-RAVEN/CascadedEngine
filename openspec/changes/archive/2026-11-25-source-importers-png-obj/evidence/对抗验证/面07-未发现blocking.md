# 面 07：`PngDecoder.cs` / `ObjParser.cs` / `PngTextureImporter.cs` / `ObjMeshImporter.cs` —— 未发现 blocking

审查对象：`design.md:20`（D1 新增两个导入器）、`design.md:28`（D2 `PngDecoder.cs`）、`design.md:32`（D3 `ObjParser.cs`）、`tasks.md:9-15`（1.1 / 1.2 / 2.1 / 2.2）。

## 一、四个文件当前全都不存在（符合“应新增”）

```powershell
foreach ($f in @(
  'Scripting/Managed/Assets/Importers/PngDecoder.cs',
  'Scripting/Managed/Assets/Importers/ObjParser.cs',
  'Scripting/Managed/Assets/Importers/PngTextureImporter.cs',
  'Scripting/Managed/Assets/Importers/ObjMeshImporter.cs')) { "$f -> " + (Test-Path -LiteralPath $f) }
```

```
Scripting/Managed/Assets/Importers/PngDecoder.cs -> False
Scripting/Managed/Assets/Importers/ObjParser.cs -> False
Scripting/Managed/Assets/Importers/PngTextureImporter.cs -> False
Scripting/Managed/Assets/Importers/ObjMeshImporter.cs -> False
```

全仓同名文件与符号也是 0：

```powershell
foreach ($n in @('PngDecoder.cs','ObjParser.cs','PngTextureImporter.cs','ObjMeshImporter.cs')) {
  "$n -> " + @(Get-ChildItem -Path . -Recurse -File -Filter $n -ErrorAction SilentlyContinue).Count
}
# PngDecoder.cs -> 0 / ObjParser.cs -> 0 / PngTextureImporter.cs -> 0 / ObjMeshImporter.cs -> 0

@(Get-ChildItem -Path . -Recurse -Directory -Filter 'Importers' -ErrorAction SilentlyContinue).Count
# 0
```

`Select-String` 在 `Scripting/Managed` 全目录检索 `PngDecoder|ObjParser|PngTextureImporter|ObjMeshImporter` 也是 0 命中。工件把这些文件都写在「新增 / 新建」语境（design D1-D3、tasks 1.1/1.2/2.1/2.2），所以不存在=正确，不是缺陷。

## 二、新用例名与新门控当前也不存在（同样符合“应新增”）

| 符号 | 检索命令 | 命中 |
|---|---|---|
| `TestSourceImporters` | `Select-String -Path 'Test/GPUBackendTester/Main.cpp' -Pattern 'TestSourceImporters'` | 0 |
| `CASCADED_SOURCE_IMPORTERS` | `Select-String -Path 'Scripting/Managed/**/*.cs','Scripting/Managed/*.cs' -Pattern 'CASCADED_SOURCE_IMPORTERS'` | 0 |

## 三、附注（不构成阻断）

- 默认源目录 `SourceAssets` 当前不存在（`Test-Path 'SourceAssets'` → False）。design D1 把它定为默认值、env `CASCADED_SOURCE_IMPORTERS_DIR` 可覆盖；tasks 3.1 用临时源目录预置夹具，所以验收不依赖这个默认目录。生产默认目录要不要预建、C# 侧如何定位「仓库根」，工件没有写；这属于约定缺口，不影响本面「四个新增文件应不存在」的结论。
- 新增文件放在 `Scripting/Managed/Assets/Importers/` 下能被 `Scripting.Managed.csproj` 自动收编（SDK 默认 glob `**/*.cs`，项目文件里没有排除项），前提是构建时先跑 CodeGen 生成绑定（既有流程）。

## 四、结论

四个新增文件当前全都不存在，与新用例名、新门控一起符合「本批新增」的预期，**本面未发现 blocking**。
