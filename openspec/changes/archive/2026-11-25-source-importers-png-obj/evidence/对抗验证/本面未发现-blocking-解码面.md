# 本面未发现 blocking：PNG 纯 C#（DeflateStream）与「不动绑定面」一致

## 面与问题

攻击面 ②：PNG 解码「纯 C#（DeflateStream）」与 proposal/design 的「不新增原生接口、不改绑定面」是否一致，有没有偷偷要 native stb。结论：**未发现 blocking**。四处工件口径一致，且现有工程不需要新增原生接口或依赖。

## 一致性的逐字引文

`proposal.md` 第 9 行：

> **PNG 贴图导入器**（C# `ResourceImporterBase`）：扫源目录 `.png`，纯 C# 解码（zlib deflate via `DeflateStream` + PNG 滤波重建）→ RGBA8 像素 → `Texture2D` 资产（复用 Texture2DFactory/AssetDatabase），产出到 ② `CAAssets/Textures/*.tex`。

`proposal.md` 第 29 行：

> **依赖**：复用导入器机制 + 资产工厂 + AssetDatabase；纯 C# 解码无新外部依赖（PNG 用 System.IO.Compression.DeflateStream）。

`design.md` 第 9 行：

> CAAssets 现状：两 shLib + 三探针 .res，无真实贴图/网格。stb 在 C++ 侧（stbi_write_png），但本批解码走纯 C#（不动绑定面）。

`design.md` D2：

> `PngDecoder.cs`：解析 PNG 签名 + IHDR（宽/高/位深 8/颜色类型 6 RGBA 或 2 RGB→补 alpha）+ IDAT（zlib 流，跳 2 字节 zlib 头用 `DeflateStream` 解压，忽略 4 字节 adler32）+ 逐扫描线滤波重建（None/Sub/Up/Average/Paeth 五种）→ RGBA8 像素（宽×高×4）。只支持 8 位深非隔行（Adam7 隔行/16 位/调色板明确抛 AssetFormatException 点名不支持）。解码结果填 `Texture2D`（Width/Height/Format=E_R8G8B8A8_UNORM/Pixels）。**不调 native stb**（避免绑定面改动）；正确性由已知 PNG 夹具的写死像素期望核。

`specs/source-importers-png-obj/spec.md` 第 5 行：

> ……**纯 C# 解码**（DeflateStream zlib + PNG 五种滤波重建，8 位 RGBA/RGB→RGBA8，不调 native stb、不动绑定面）……

`tasks.md` 1.1：

> PNG 解码器 `Scripting/Managed/Assets/Importers/PngDecoder.cs`：签名+IHDR（宽/高/位深 8/颜色类型 6 RGBA 或 2 RGB→补 alpha）+IDAT（跳 2 字节 zlib 头用 DeflateStream 解压）+逐扫描线五种滤波重建（None/Sub/Up/Average/Paeth）→RGBA8（宽×高×4）……

## grep / 命令与真实输出

```powershell
Select-String -Path 'openspec/changes/source-importers-png-obj/design.md','openspec/changes/source-importers-png-obj/proposal.md','openspec/changes/source-importers-png-obj/tasks.md','openspec/changes/source-importers-png-obj/specs/source-importers-png-obj/spec.md' -Pattern 'stb|stbi' | ForEach-Object { ... }
```

```
design.md:9:  CAAssets 现状：两 shLib + 三探针 .res，无真实贴图/网格。stb 在 C++ 侧（stbi_write_png），但本批解码走纯 C#（不动绑定面）。
design.md:28: `PngDecoder.cs`：……**不调 native stb**（避免绑定面改动）；正确性由已知 PNG 夹具的写死像素期望核。
specs\source-importers-png-obj\spec.md:5: ……不调 native stb、不动绑定面……
```

（仅三处提到 stb，全部是「C++ 侧有 stb / 本批不调」的排除式表述；没有任何一处要求新增 stb 绑定或原生导出。）

`Scripting/Managed/Scripting.Managed.csproj` 全文核对（第 8–32 行）：`TargetFramework net8.0`、唯一的 `PackageReference` 是 `Microsoft.Web.WebView2`，没有压缩库包。

DeflateStream 所在程序集在共享框架里（本机实测）：

```powershell
$root = 'C:\Program Files\dotnet\shared\Microsoft.NETCore.App'
$ver = Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1
"$($ver.FullName)"
Test-Path (Join-Path $ver.FullName 'System.IO.Compression.dll')
```

```
C:\Program Files\dotnet\shared\Microsoft.NETCore.App\9.0.23
True
```

（`net8.0` + `RollForward LatestMinor`，本机滚动到 9.0.23；`System.IO.Compression.dll` 是共享框架的一部分，加 `DeflateStream` 不需要新的 `PackageReference`，也不需要任何 C++ 侧改动 ⇒ 与「不动绑定面」一致。）

## 非 blocking 观察（不判 blocking，但建议在 tasks 1.1 写清）

design D2 只说「IDAT（zlib 流，跳 2 字节 zlib 头用 `DeflateStream` 解压，忽略 4 字节 adler32）」，**没有说多个 IDAT 块要按顺序拼接 payload**。PNG 规范允许 IDAT 任意分块（真实大图几乎必然是多个 IDAT）；spec 把「不支持的 PNG」限定为 16 位 / 隔行 / 调色板，因此一个合法的 8 位 RGBA、非隔行、多 IDAT 的 PNG 按 spec 必须成功。若 tasks 1.1 照着「单个 IDAT」写，2.1 的 4×4 夹具仍会绿（小图单块），但 3.2 的「真实 PNG」会失败。

建议在 tasks 1.1 加一句：「按 IHDR 之后所有 IDAT 块顺序拼接 payload，再跳 2 字节 zlib 头解压；adler32 只忽略流尾 4 字节」，并加一条红态夹具（把同一张 4×4 图拆成两个 IDAT 块，解码结果必须与单块版逐字节相同）。

## 复核方式

1. `Select-String -Path 'openspec/changes/source-importers-png-obj/*.md','openspec/changes/source-importers-png-obj/specs/**/*.md' -Pattern 'stb'` 的命中应仍只有上面三处排除式表述。
2. 构建后检查 `out/build/x64-debug/codegen/bindings` 与 `Scripting.Managed.csproj`：没有因 PNG 解码新增的接口编号 / PackageReference。
3. 若采纳多 IDAT 建议，跑红态：两块 IDAT 的夹具解码必须与单块一致；只读第一块会得到截断像素，测试必须变红。
