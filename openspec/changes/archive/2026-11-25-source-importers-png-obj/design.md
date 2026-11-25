# Design: source-importers-png-obj

## Context

导入器机制（resource-importer-refactor）：`ResourceImporter` 反向接口（GetTags/GetSourceDirectory/GetDestDirectory + Produce→`ResourceImporterProductions` 迭代器 HasNext/NextPath/NextResource），导入器纯转换不碰注册表，管线 `ResourceImportingSystem.ScanSourceDirectory`（属 ResourceImportingSystem 非 ResourceManagingSystem）拉取→包 shared_ptr→注册→SerializeAll 落盘→重导入替换。C# 侧 `ResourceImporterBase`/`ResourceImporterProductionsBase`（先例 `ResourceImporterProbe.cs`：门控 `CASCADED_CSHARP_IMPORTER_PROBE=1`、源目录自声明 env、Produce 造资源入迭代器）。

资产（csharp-resource-asset-foundation）：`Texture2D`（RGBA8 像素，CATX 格式，HeaderSize 28）、`Mesh`（VertexData 位置 float3+色 float3 stride 24 / IndexData uint32，CAMH 格式，HeaderSize 32，布局与 SceneRenderer 三常量逐字一致）；`Texture2DFactory`/`MeshFactory`（Create 入队）+ `AssetDatabase`（出队填数据）+ `AssetFormatException`（坏文件点名路径原因）。

CAAssets 现状：两 shLib + 三探针 .res，无真实贴图/网格。stb 在 C++ 侧（stbi_write_png），但本批解码走纯 C#（不动绑定面）。

## Goals / Non-Goals

**Goals:** PNG/OBJ 源导入器把 ① 源文件导成 ② Texture2D/Mesh（注册+落盘+资产浏览器可见），纯 C# 解码/解析，失败可见，复用导入器机制与资产工厂。
**Non-Goals:** 其它格式 / mtl 材质 / 法线 UV 布局 / 增量身份 / 导入 UI（见 proposal）。

## Decisions

### D1 两个 C# 导入器，测试驱动接入管线（修订：C# 无 AddImporter 入口，照 TestCSharpImporter 先例）

新增 `Scripting/Managed/Assets/Importers/PngTextureImporter.cs` 与 `ObjMeshImporter.cs`，各继承 `ResourceImporterBase`（照 ResourceImporterProbe 模式）。**C# 侧没有 AddImporter/ScanSourceDirectory 绑定**（AddImporter 是 `ResourceImportingSystem` 的 C++ 入口，ResourceImportingSystem.h 无 CA_BIND_REVERSE），故**接入管线走测试驱动**（照 TestCSharpImporter 先例 Main.cpp:8852-8887）：
- C# 门控 `CASCADED_SOURCE_IMPORTERS=1`（默认关零行为）下，绑三张反向表（ResourceImporter/ResourceImporterProductions/IResource，照 ResourceImporterProbe.BindReverseTables）+ `ResourceImporterBindings.Create(importer)` 造强句柄，把句柄写进读数文件（env `CASCADED_SOURCE_IMPORTERS_READINGS`）。
- **C++ 测试侧**读句柄 → `ResolveImpl` → `pImportingSystem->AddImporter` → `pImportingSystem->ScanSourceDirectory`（注意：ScanSourceDirectory 属 `ResourceImportingSystem` 非 ResourceManagingSystem）。
- `GetSourceDirectory` 返回 env `CASCADED_SOURCE_IMPORTERS_DIR`（**默认工程内源根 `CAResources/`**，Images/ 等源已在此；C++ 测试传临时绝对路径隔离；C# 读不到管线扫描根，ResourceImporter.h:27-28 要求非空）；`GetDestDirectory` 返回标识字符串（**管线落盘只用 NextPath+资源根，GetDestDirectory 不参与写盘**，修订首轮因果说反）；`GetTags` 标 "CSharp;Source;PNG"/"OBJ"。
- `Produce` 扫源目录对应扩展名，逐个解码/解析造资产入迭代器。**NextPath = 源相对路径镜像 + 扩展名替换**（`Images/A/Test.png`→`Images/A/Test.tex`、`Models/B.obj`→`Models/B.mesh`，相对源根 CAResources；落资源根 CAAssets 下同名镜像路径），NextResource=资产。**导入器不调注册表**（直接 new 资产，D4）。
- **资源根隔离**：测试用 `SetResourceRootPath(临时资源根)` + 临时源目录，ScanSourceDirectory 会对所有导入器 Produce + SerializeAll 按资源根写全部——临时根隔离避免着色器导入器空库覆盖真实 CAAssets/shLib、新资产污染用户 CAAssets。

### D2 PNG 解码纯 C#（修订：规格补全 + 完整性校验 + 五滤波夹具）

`PngDecoder.cs`：解析 PNG 签名 + IHDR + **全部 IDAT 数据字段按序拼接成一条 zlib 流**（块界任意，RFC 2083）+ IEND。zlib 流跳 2 字节头用 `DeflateStream`（raw deflate，无头尾）解压，**校验 adler32 尾**；**逐 IDAT 块校验 CRC32**（损坏但可 inflate 的 PNG MUST 拒，不静默导错像素）。逐扫描线滤波重建五种（None/Sub/Up/Average/Paeth）。颜色类型支持 **8 位 2(RGB)/6(RGBA)**（RGB 补 alpha=255 写死）；**0(灰度)/4(灰度+alpha)/3(调色板)/16 位/Adam7 隔行明确抛 AssetFormatException 点名不支持**（修订首轮 0/4 落未定义分支）。解码→RGBA8（宽×高×4）填 Texture2D。**不调 native stb**（不动绑定面）。**五滤波覆盖夹具**：用 stbi_write_force_png_filter=0..4（tester 已链 stb_image_write）生成五张各含一种滤波的 PNG，逐张解码像素==写死期望（修订首轮 4×4 与「五滤波各覆盖」算术互斥，4 行最多 4 滤波字节）。

### D3 OBJ 解析纯 C#（修订：四边面三角化夹具 + 索引规则钉死）

`ObjParser.cs`：逐行解析 `v x y z`（位置 float3）、`f` 面（**索引 1 基**；支持负索引=相对当前顶点数；忽略 vt/vn 本批不支持）。**面三角化扇形拆**（n 边面→n-2 三角），**夹具 MUST 含四边面**（如立方体 6 个 quad 面→12 三角，强制三角化真实现，修订首轮「8 顶点 12 三角面」可免实现三角化）；未引用顶点保留（不去重，索引按 v 出现序）。顶点布局**位置 float3 偏移 0 + 色 float3 偏移 12 步长 24**（与 Mesh 三常量逐字一致）；OBJ 无顶点色，色用**写死默认白 (1,1,1)**（钉死，修订首轮「如白」未定）。产出 Mesh（VertexData/IndexData/VertexCount/IndexCount，uint32 索引）。缺顶点/索引越界/空网格抛 AssetFormatException 点名。

### D4 资产创建路径钉死：直接 new + 管线 CreateProxy（修订：删工厂/AssetDatabase 分支）

**导入器直接 `new Texture2D()`/`new Mesh()` 填数据交迭代器**（NextResource 交出），由管线 `CreateProxy` 注册（单一强所有者，resource-importer:24）。**删首轮「经 Texture2DFactory/MeshFactory + AssetDatabase」分支**——AssetDatabase.Create 第一句即 `m_registry.CreateResource`（AssetDatabase.cs:34），在 Produce 内调注册表违反「导入器不调注册表」，且工厂只在 CASCADED_CSHARP_ASSET_PROBE 探针注册（净进程无工厂抛错）、第二轮撞门面守卫 1014、双份所有权。直接 new 路径由 TestCSharpImporter 证明可行（探针 ProbeResource 直接 new）。管线 ScanSourceDirectory 拉取→CreateProxy 注册（② 路径=NextPath）→SerializeAll 落盘 CATX/CAMH（按资源根，GetDestDirectory 不参与）。重导入替换沿用管线（第二轮新对象，旧代理退役）。② 落临时资源根的**镜像路径**（源相对路径+扩展名替换，如 `Images/A/Test.tex`/`Models/B.mesh`，非扁平 Textures/），资产浏览器 kind=texture/mesh（.tex/.mesh 映射 AssetDisplay.cs:111-112 已含）、registered=HasResource。

### D5 失败可见 + 红态观测通道（修订：管线边界异常变形）

坏 PNG（签名/IHDR/滤波/长度/CRC/adler32 不符、不支持位深/隔行/调色板/灰度）/坏 OBJ（缺顶点/索引越界/空）抛 `AssetFormatException`（路径+原因）。**但管线边界异常会变形**：C# OnProduce 吞异常存 LastError 返 null，原生代理同次 Produce setError(1012) 抛 ReverseBindingError；一个坏文件使整轮 ScanSourceDirectory 中止、SerializeAll 跳过。故**红态观测走 C# 读数通道**（照 ResourceImporterProbe 读数文件模式）：导入器 Produce 内捕获 AssetFormatException 把类型+消息+路径写读数文件（env CASCADED_SOURCE_IMPORTERS_READINGS），C++ 测试读读数断言红态点名（不依赖 C++ 侧直接 catch AssetFormatException，那调不到）。好坏夹具**分目录/分阶段**（修订首轮同目录与「遇坏即抛」冲突）：正控阶段全好夹具，红态阶段单独坏夹具。

### D6 验收（修订：C++ 裸字节读回 + 重导入断言 + 临时根 + 视觉写死）

- tester 新用例 `TestSourceImporters`（**独立进程，进 --list 不进全量同进程顺序跑**，照 TestEditorAssetBrowser；全量是 Main.cpp 硬编码 22 条）：C++ 设临时资源根 + 临时源目录（预置已知 PNG 五滤波夹具 + 已知 OBJ 含四边面）→ C# 门控开造导入器句柄写读数 → C++ ResolveImpl→AddImporter→ScanSourceDirectory → 断言 ② 落盘文件存在 + **C++ 直接读 ② 文件裸字节**与写死预期序列化字节（CATX 头 28+像素 / CAMH 头 32+顶点索引）逐字一致（**修订：Texture2D.Parse/Mesh.Parse 是 internal C++ 调不到，且 resource-importer:63 禁验加载回读；改 C++ 读裸字节对拍 Serialize 输出，非加载回读**）+ HasResource 真 + 资产浏览器 list-assets 含**写死 path/kind=texture/mesh/registered=true/sizeBytes** 条目（防探针顶替）。
- **重导入**：第二轮扫描断言新对象替换旧（旧句柄 ReleaseImpl==1001）+ ② 落盘字节==预期序列化字节（**修订：首轮「与源一致」对 CATX/CAMH 头字面不可能**）。
- 红态：坏 PNG（CRC 错/截断/不支持）/坏 OBJ（索引越界/空）→ C# 读数 AssetFormatException 点名（D5）；门控关零行为（**读数文件无导入器活动 + 临时根无新文件**，修订首轮 name+status 零差异证不了零行为）。
- 截图视觉（编辑器类）：临时根导入后资产浏览器 dock 显示**写死条目**（路径/kind/registered/数量），CapturePreviewAsync 不前置（AGENTS §2）。
- 回归：现有 22 用例 + CoreTests 全绿；绑定产物逐字节不变；**check_csharp_host_readme.py 0 问题**（新门控 CASCADED_SOURCE_IMPORTERS* 登记 CSharpHost/README，修订首轮缺）。

### D7 源文件与 ② 资产同进工程、路径镜像，从源重建（2026-11-25 用户裁定）

**源文件（① PNG/OBJ）与 ② 资产文件（.tex/.mesh）都保留在工程里**，不取「源外部」的纯 Unreal 式。理由：资源变动时能从源文件重建资产。
- **源根**：工程内 `CAResources/`（① 源，Images/Models 等，提交进版本库）；**② 资产**：资源根 CAAssets（提交进版本库，共享真相）。两者都进工程。
- **路径镜像**（用户裁定）：② 资产路径 = 源相对路径镜像 + 扩展名替换（`CAResources/Images/A/Test.png`→`CAAssets/Images/A/Test.tex`）。映射可预测、保留目录组织、避免同名碰撞。
- **从源重建**：导入器扫源目录→重导替换 ② 产物（D4 重导入路径）；源改动后重扫即重建。源→② 映射按镜像路径（无需额外映射表）。
- **与共享真相不冲突**：② 产物仍是共享真相（提交、同步、不在每机盲目重导，否 Unity 式）；源同进工程是**供重建的原点**，不是每机重导的理由。代码型（shader/C#）源即资产本就在工程（CAResources/Shaders），数据型源也进工程对齐此原则。
- **溯源**：镜像路径本身即源↔资产映射；② 产物内嵌源路径元数据供浏览器分组是身份重设计批（v2），本批按镜像路径重建。
- **测试隔离**：测试用临时源目录+临时资源根（D1/D6），不污染真实 CAResources/CAAssets；真实约定（源+资产同进工程、路径镜像）由本决策定，测试夹具验证重建行为。

## Risks / Trade-offs

- [纯 C# PNG 解码正确性] → 已知夹具写死像素期望核 + 五种滤波各覆盖；不支持的格式明确拒绝（不静默）。
- [OBJ 无法线/UV/材质] → 本批只几何（位置+默认色），Mesh 现布局如此；法线/UV/材质留后续（明示 Non-goal）。
- [源目录约定] → env 可配 + 默认 SourceAssets；测试用临时目录预置夹具，不动用户真实源。
- [导入器与现有探针冲突] → 独立门控 CASCADED_SOURCE_IMPORTERS（区别于 CASCADED_CSHARP_IMPORTER_PROBE），默认关。

## Migration Plan

纯增量：新增导入器 C# + 门控 + 测试夹具 + 用例。回滚=撤增量；现有导入器机制/资产格式/绑定产物不受影响（绑定逐字节不变硬判据）。导入产出的 ② 资产喂资产浏览器与后续场景编辑。

## Open Questions

1. PNG 颜色类型支持范围（本批 8 位 RGB/RGBA，灰度/调色板/16 位/隔行拒绝）——是否需灰度留后续。
2. OBJ 默认顶点色白 (1,1,1) 写死——后续材质批替换。
3. 源目录由 C++ 测试传 env 绝对路径（临时夹具目录）；真实生产源目录约定留后续导入 UI 批。