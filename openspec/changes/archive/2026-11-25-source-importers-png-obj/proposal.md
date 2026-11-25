# Proposal: source-importers-png-obj

## Why

资产层地基（注册表门面 + Texture2D/Mesh 资产 + 自读加载）与导入器机制（resource-importer-refactor 的拉取式 ResourceImporter 反向接口）已就绪，资产浏览器（editor-asset-browser）也能浏览 ② 阶段文件——但 **CAAssets 里只有两个 shLib + 三个探针残留 .res，没有真实贴图/网格**。路线图第一梯队①欠的「源导入」正是这块：把 ① 源文件（PNG 贴图 / OBJ 网格）导入成 ② 编辑器资产。本批接 **PNG/OBJ 源导入器**，让资产浏览器有真实内容、为后续场景编辑备网格/材质。复用已就绪的导入器机制与资产工厂，**不新增原生接口、不改绑定面**。

## What Changes

- **PNG 贴图导入器**（C# `ResourceImporterBase`）：扫源目录 `.png`，纯 C# 解码（多 IDAT 拼接 + DeflateStream raw deflate + adler32/CRC 校验 + 五种滤波重建）→ RGBA8 像素 → **直接 new `Texture2D`** 交迭代器，产出到 ② `Textures/*.tex`。
- **OBJ 网格导入器**（C# `ResourceImporterBase`）：扫源目录 `.obj`，纯 C# 解析（v/f 行，**四边面扇形三角化**，1 基/负索引，uint32 索引）→ **直接 new `Mesh`**（位置 float3 + 默认白色 float3，stride 24 与 SceneRenderer 布局一致），产出到 ② `Meshes/*.mesh`。
- **导入与注册分离 + 测试驱动接入**（沿用 resource-importer 规范）：导入器纯转换不碰注册表（直接 new 资产，**不经工厂/AssetDatabase**——那会在 Produce 内调注册表）；C# 无 AddImporter 绑定，**照 TestCSharpImporter 先例测试驱动**：C# 造导入器句柄写读数→C++ ResolveImpl→`ResourceImportingSystem.AddImporter`→`ScanSourceDirectory`（属 ResourceImportingSystem）拉取→CreateProxy 注册→SerializeAll 落盘。门控默认关，**测试用临时资源根隔离**（防着色器空库覆盖真实 shLib/污染 CAAssets）。
- **失败可见**：坏 PNG（魔数/滤波/长度/CRC 不符）/坏 OBJ（缺顶点/索引越界）抛 `AssetFormatException` 点名路径与原因，不静默灌垃圾。
- **源与 ② 资产同进工程、路径镜像，从源重建**（2026-11-25 用户裁定）：① 源文件（工程内 `CAResources/`）与 ② 资产（CAAssets）都提交进工程；**② 路径镜像源相对路径 + 扩展名替换**（`CAResources/Images/A/Test.png`→`CAAssets/Images/A/Test.tex`）；源是重建原点，资源变动重扫源目录即重建 ② 产物。② 仍是共享真相（否 Unity 式每机重导），源同进工程供重建。
- **资产浏览器联动**：导入后 ② 文件 kind=texture/mesh、registered=true，editor-asset-browser 列展示（不改浏览器代码，只补真实内容）。

## Capabilities

### New Capabilities

- `source-importers-png-obj`: PNG/OBJ 源导入器——纯 C# 解码/解析、拉取式导入产出 Texture2D/Mesh、导入注册分离、失败可见、② 落盘与资产浏览器联动。

### Modified Capabilities

- 无（复用 resource-importer / csharp-resource-factory / csharp-resource-registry 既有规范，不改其要求；导入器是新增 C# 实现，不动接口/生成器）。

## Impact

- **代码**：新增 `Scripting/Managed/Assets/Importers/`（PngTextureImporter.cs / ObjMeshImporter.cs / PngDecoder.cs / ObjParser.cs）；C# 门控 + 句柄出口 + 绑反向表；C++ 测试接线（ResolveImpl→AddImporter→ScanSourceDirectory，照 TestCSharpImporter）；测试夹具（五滤波 PNG + 含四边面 OBJ）+ tester 用例 TestSourceImporters；CSharpHost/README 门控登记。
- **不动**：ResourceImporter/ResourceImporterProductions 接口与生成器；GPUTexture/Mesh 绑定面；Texture2D/Mesh 资产格式（CATX/CAMH）；editor-asset-browser 实现；绑定产物（逐字节不变）。
- **依赖**：复用导入器机制（直接 new 资产 + 管线 CreateProxy，不经工厂/AssetDatabase）；纯 C# 解码无新外部依赖（PNG 用 System.IO.Compression.DeflateStream）；stb_image_write 仅测试夹具生成（tester 已链）。

## Non-goals

- **其它源格式**（FBX/glTF/JPEG/DDS）：后续批；本批只 PNG（RGBA8）+ OBJ（位置+索引）。
- **材质/贴图引用绑定**（OBJ mtl）：本批 OBJ 只几何，材质留后续。
- **法线/UV 顶点布局**：Mesh 现格式是位置+色（stride 24），本批不改布局；法线/UV 待资产格式扩。
- **增量导入/内容哈希身份**：身份重设计批；本批全量重导入替换（沿用管线）。
- **导入 UI 按钮**：本批门控触发导入，编辑器导入按钮留后续。