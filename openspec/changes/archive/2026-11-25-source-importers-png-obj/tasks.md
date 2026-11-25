# Tasks: source-importers-png-obj

> 验收口径：每条给「命令 / 产物 + 期望读数」，期望写死、不从被测物推导；红态存 evidence/。
> 构建唯一路径 python build.py --config Debug。编辑器类 MUST 截图视觉验收（CapturePreviewAsync 不前置，AGENTS §2）。
> 纯 C# 解码/解析，不动绑定面（绑定产物逐字节不变硬判据）。临时文件 %TEMP% 用完即清，证据只进 change evidence/。

## 1. 解码与解析（纯 C#）

- [x] 1.1 PNG 解码器 `Scripting/Managed/Assets/Importers/PngDecoder.cs`：签名+IHDR+**全部 IDAT 拼接一条 zlib 流**+IEND；DeflateStream raw deflate 跳 2 字节头+**adler32 尾校验**+**逐块 CRC32 校验**；五种滤波重建（None/Sub/Up/Average/Paeth）；8 位颜色类型 2 RGB（补 alpha=255）/6 RGBA→RGBA8；**0 灰度/4 灰度+alpha/3 调色板/16 位/Adam7 隔行抛 AssetFormatException 点名**。验收：构建过；**stbi_write_force_png_filter=0..4 五张夹具**逐张解码像素==写死期望（五滤波各覆盖）；坏 PNG（错签名/截断/CRC 错/adler32 错/不支持类型）抛 AssetFormatException（evidence/1.1-*.txt）。
- [x] 1.2 OBJ 解析器 `Scripting/Managed/Assets/Importers/ObjParser.cs`：v 位置 float3 / f 面**索引 1 基+负索引**+**n 边面扇形三角化 n-2 三角**+uint32 索引+未引用顶点保留；顶点布局位置 float3 偏移 0+**写死白色 (1,1,1)** float3 偏移 12 步长 24（与 Mesh 三常量逐字一致）；缺顶点/索引越界/空网格抛 AssetFormatException。验收：构建过；**含四边面 OBJ（立方体 6 quad→12 三角）**解析顶点/索引==写死期望（强制三角化真实现）；坏 OBJ 红态（evidence/1.2-*.txt）。

## 2. 导入器与门控

- [x] 2.1 `PngTextureImporter.cs : ResourceImporterBase`：门控 CASCADED_SOURCE_IMPORTERS=1 默认关；GetSourceDirectory（env CASCADED_SOURCE_IMPORTERS_DIR，C++ 传绝对路径，非空）/GetDestDirectory（标识串，不参与落盘）/GetTags；Produce 扫 .png 逐个 PngDecoder 解码→**直接 new Texture2D**（RGBA8）入迭代器（**NextPath=源相对路径镜像+扩展名替换**，Images/A/Test.png→Images/A/Test.tex）；**不调注册表、不经工厂/AssetDatabase**（导入器文件五写入口 grep 0）。验收：构建过；门控开 Produce 产 Texture2D；grep 五写入口 0。
- [x] 2.2 `ObjMeshImporter.cs : ResourceImporterBase`：同上扫 .obj→ObjParser→**直接 new Mesh**（VertexData/IndexData/counts）入迭代器（**NextPath=源相对路径镜像+扩展名替换**，Models/B.obj→Models/B.mesh）；不调注册表。验收：构建过；Produce 产 Mesh；grep 五写入口 0。
- [x] 2.3 测试驱动接入管线（照 TestCSharpImporter，C# 无 AddImporter 绑定）：C# 门控下绑三张反向表（照 ResourceImporterProbe.BindReverseTables）+ ResourceImporterBindings.Create 造导入器强句柄写读数文件（env CASCADED_SOURCE_IMPORTERS_READINGS）；**C++ 测试侧**读句柄→ResolveImpl→`pImportingSystem->AddImporter`→`pImportingSystem->ScanSourceDirectory`（ResourceImportingSystem）；**测试 SetResourceRootPath 临时资源根 + 临时源目录隔离**（防着色器空库覆盖真实 shLib/污染 CAAssets）；**新门控 CASCADED_SOURCE_IMPORTERS* 登记 CSharpHost/README**。验收：构建过；门控关零行为（读数无活动+无新文件）；门控开测试驱动导入→临时根**镜像路径** Images/A/Test.tex+Models/B.mesh 落盘+HasResource 真；真实 CAAssets 字节不变；check_csharp_host_readme 0 问题。

## 3. 用例与视觉验收

- [x] 3.1 tester 用例 `TestSourceImporters`（Main.cpp + --list 登记，**独立进程，进 --list 不进全量同进程顺序跑**）：C++ 设临时资源根+临时源目录（预置五滤波 PNG + 含四边面 OBJ）→C# 门控开造句柄写读数→C++ ResolveImpl→AddImporter→ScanSourceDirectory→断言 ② 落盘存在 + **C++ 读 ② 文件裸字节**==写死预期序列化字节（CATX 头 28+像素 / CAMH 头 32+顶点索引，**非 Parse 读回——internal 调不到**）+ HasResource 真 + 资产浏览器 list-assets 含**写死 path/kind=texture/mesh/registered=true/sizeBytes**；**重导入**第二轮新对象（旧句柄 ReleaseImpl==1001）+落盘字节==预期；**源变动重建**：改源 PNG/OBJ 内容重扫→② 产物重建为新源字节（与改前不同），源（SourceAssets/）与 ②（资源根）同进工程；红态（好坏夹具分阶段）：坏 PNG（CRC 错/截断/不支持）/坏 OBJ（索引越界/空）→**C# 读数 AssetFormatException 点名**（管线边界变形 1012，走读数通道观测）；门控关零行为（读数无活动+临时根无新文件）。验收：--list 含 TestSourceImporters、全量仍 22；独立进程过；红态各能红（evidence/3.1-*.txt 含开关+命令+失败码）。
- [x] 3.2 截图视觉验收（主循环）：临时根导入 PNG/OBJ 后交互打开资产浏览器，CapturePreviewAsync 抓窗（不前置，AGENTS §2）+ read_image 核对底部 dock 显示**写死条目**（路径/kind=texture/mesh/registered/数量，防旧 CAAssets 冒充）。验收：evidence/3.2-截图.png + read_image 核对记录；抓不到/不符判不通过。

## 4. 回归与编译

- [x] 4.1 回归零差异（主循环跑）：现有 22 用例同进程两后端 name+status 与基线（归档 2026-11-24-editor-web-render-spike/evidence/4.1-全量-*.json）零差异；TestSourceImporters 独立进程过；CoreTests exit 0 + PASSED 26（evidence/4.1-CoreTests.txt）；绑定基线 check_binding_baseline.py 缺失0/不一致0/多余0（evidence/4.1-基线.txt，纯 C# 无绑定改动）；**check_csharp_host_readme.py 0 问题**（新门控登记）；--list/docs/testing.md 同步。验收：零差异 + CoreTests 26 + 基线 0/0/0 + README 0 问题。
- [x] 4.2 编译验证（最后）：python build.py --config Debug 失败修到成功；成功后复跑 4.1 读数不变。验收：BUILD SUCCESSFUL + 复跑一致（evidence/4.2-构建.txt）。

## 修订记录（对抗验证首轮，五面 49 条主张，主循环复核按九根因归并修订）

> 五面：验收 12（6B）/一致 12（9B）/溯源 17（5B）/影响 4（3B）/存在 4（3B）。按根因归并：

1. **管线集成无通道**（存在-002/影响-001/验收-008/一致-011/012）：C# 无 AddImporter/ScanSourceDirectory 绑定，Host.Tick 无法交管线；ScanSourceDirectory 属 ResourceImportingSystem 非 ResourceManagingSystem（存在-001）。
   - 改后：D1/tasks 2.3 改**测试驱动**（照 TestCSharpImporter：C# 绑反向表+造导入器句柄写读数→C++ ResolveImpl→AddImporter→ScanSourceDirectory）；类名改 ResourceImportingSystem。
2. **资产创建路径调注册表**（存在-004/影响-003/溯源-015/一致-004/005/006）：AssetDatabase.Create 调 CreateResource（Produce 内注册），工厂只在探针注册，第二轮撞 1014 双份所有权。
   - 改后：D4/tasks 2.1/2.2/spec 要求3 **钉死直接 new 资产+管线 CreateProxy**，删工厂/AssetDatabase 分支。
3. **读回不可执行**（存在-003/验收-002/溯源-017/一致-001/002）：Texture2D.Parse/Mesh.Parse internal C++ 调不到，且与 resource-importer:63「禁验加载回读」冲突。
   - 改后：D6/tasks 3.1/spec 要求1/2/4 改 **C++ 读 ② 文件裸字节对拍写死预期序列化字节**（CATX/CAMH 头+数据，验 Serialize 输出非加载回读）。
4. **资源根污染**（影响-002）：ScanSourceDirectory 对所有导入器 Produce+SerializeAll 按资源根写全部，临时夹具扫描致着色器空库覆盖真实 shLib、新资产污染 CAAssets。
   - 改后：D1/D6/tasks 2.3/3.1/spec 要求3 **测试用临时资源根+临时源目录隔离**，真实 CAAssets 字节不变断言。
5. **PNG 规格不全**（溯源-003/006/011/013/验收-001/005）：多 IDAT 未拼接、4×4 与五滤波算术互斥、灰度 0/4 未定义、忽略 adler32 无 CRC。
   - 改后：D2/tasks 1.1/spec 要求1 补**多 IDAT 拼接+adler32/CRC 校验+灰度/调色板/16 位/隔行拒绝+stbi_write_force_png_filter 五滤波夹具**。
6. **OBJ 三角化可免实现**（验收-003/一致-003）：立方体 12 三角面不要求四边面；索引基数/负索引未定。
   - 改后：D3/tasks 1.2/spec 要求2 **含四边面夹具（6 quad→12 三角）+索引 1 基/负索引/未引用顶点保留+默认白色钉死**。
7. **红态不可观测**（验收-004/影响-004）：AssetFormatException 管线边界变 1012，C++ 调不到；好坏同目录冲突。
   - 改后：D5/tasks 3.1/spec 要求1/2 **红态走 C# 读数通道**（捕获 AssetFormatException 写读数文件），好坏夹具分阶段。
8. **重导入字节断言不可能**（验收-010）：「② 与源一致」对 CATX/CAMH 头字面不可能。
   - 改后：D6/tasks 3.1/spec 要求3 改**② 落盘==写死预期序列化字节**+第二轮新对象（旧句柄 1001）。
9. **源目录/README/全量/视觉**（一致-007/008/009/010/影响次级/验收-009/011/012）：SourceAssets 不存在、门控未登记 README、TestSourceImporters 须不进全量、list-assets/截图无写死期望。
   - 改后：D1/D6/tasks 2.3/3.1/3.2/4.1/spec 要求3/4 **源目录 C++ 传 env 绝对路径+门控登记 README+check_csharp_host_readme+不进全量+list-assets/截图写死 path/kind/sizeBytes/数量**。
10. **用户裁定：源+② 资产同进工程，从源重建（2026-11-25，原话「目前还是保持要求源文件和assets文件都要存在比较好吧？便于资源发生变动时从源文件重建资源」）**：不取纯 Unreal「源外部」式。
   - 改前：design 源目录默认仓库根/SourceAssets（首轮）后改 env 绝对路径（测试），未定源是否进工程；proposal 无源进工程原则。
   - 改后：design 新增 **D7 源（工程内 CAResources/）+② 资产（CAAssets）同进工程、② 路径镜像源相对路径+扩展名替换（Images/A/Test.png→Images/A/Test.tex）、源是重建原点、资源变动重扫源重建**；② 仍共享真相（否 Unity 式每机重导），源同进工程供重建；代码型源即资产本就在工程，数据型对齐。proposal 加该原则；spec 要求1/2/3 改镜像路径+「源变动从源重建」场景；tasks 2.1/2.2/2.3/3.1 改镜像 NextPath+源变动重建断言。溯源完整链（② 内嵌源路径元数据）留身份重设计批（v2），本批重建按镜像路径。依据：用户当场裁定（源+资产同进工程、路径镜像）。