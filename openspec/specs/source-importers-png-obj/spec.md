# source-importers-png-obj

## Purpose

PNG/OBJ 源导入器：纯 C# 解码与解析（多 IDAT 拼一条 zlib 流 + adler32/CRC + 五种滤波；v/f 行 + 负索引 + 扇形三角化），拉取式导入直接 new Texture2D/Mesh 产出到 ② 镜像路径（源相对路径 + 扩展名替换），导入与注册分离（管线 CreateProxy 单一强所有者），坏文件 AssetFormatException 点名失败，门控默认关、测试用临时根隔离，C++ 读 ② 裸字节对拍写死序列化字节、资产浏览器联动截图与回归零差异且绑定不变。

## Requirements

### Requirement: PNG 导入器必须把源 PNG 导成 registered 的 Texture2D ② 资产

MUST 有 C# `PngTextureImporter : ResourceImporterBase`，门控 `CASCADED_SOURCE_IMPORTERS=1`（默认关零行为，登记 CSharpHost/README），扫源目录 `.png`，**纯 C# 解码**（**全部 IDAT 拼接成一条 zlib 流** + DeflateStream raw deflate 跳 2 字节头 + **adler32 尾与逐块 CRC32 校验** + 五种滤波重建 None/Sub/Up/Average/Paeth；8 位颜色类型 2 RGB/6 RGBA→RGBA8，RGB 补 alpha=255；**0 灰度/4 灰度+alpha/3 调色板/16 位/Adam7 隔行明确拒绝**）产出 `Texture2D`（直接 new，Format=E_R8G8B8A8_UNORM，Pixels 长=宽×高×4），经管线 CreateProxy 注册到 ② **镜像路径**（源相对路径+扩展名替换，如 `Images/A/Test.tex`）落盘 CATX。导入器 MUST NOT 调注册表（直接 new，不经工厂/AssetDatabase）。**C++ 测试读 ② 文件裸字节** MUST 与写死预期序列化字节（CATX 头 28+像素）逐字一致（Texture2D.Parse 是 internal C++ 调不到，且 resource-importer 禁验加载回读，故读裸字节对拍 Serialize 输出）。

#### Scenario: 五滤波 PNG 夹具导入像素逐字一致

- **WHEN** 预置 stbi_write_force_png_filter=0..4 生成的五张各含一种滤波的已知 PNG 到临时源目录，门控开测试驱动导入
- **THEN** ② 镜像路径 `Images/A/Test.tex`（源相对路径+扩展名替换）MUST 落盘且 HasResource 真；C++ 读裸字节 MUST 等于写死预期（CATX 头+解码像素，五种滤波各覆盖）；资产浏览器 list-assets MUST 含写死 path/kind=texture/registered=true/sizeBytes 条目

#### Scenario: 坏 PNG 显式失败（含 CRC）

- **WHEN** 源 PNG 签名错/截断/CRC 错/adler32 错/不支持位深隔行调色板灰度
- **THEN** C# 读数 MUST 含 AssetFormatException 点名路径与原因；MUST NOT 落盘垃圾资产；损坏但可 inflate 的 PNG（CRC 错）MUST 拒（不静默导错像素）

### Requirement: OBJ 导入器必须把源 OBJ 导成 registered 的 Mesh ② 资产

MUST 有 C# `ObjMeshImporter : ResourceImporterBase`，扫源目录 `.obj`，**纯 C# 解析**（v 位置 float3 / f 面**索引 1 基、支持负索引**、**n 边面扇形三角化 n-2 三角**、uint32 索引、未引用顶点保留）产出 `Mesh`（直接 new，VertexData 位置 float3 偏移 0+**写死默认白色 (1,1,1)** float3 偏移 12 步长 24，与 Mesh.VertexStride/PositionOffset/ColorOffset 逐字一致），经管线 CreateProxy 注册到 ② **镜像路径**（源相对路径+扩展名替换，如 `Models/B.mesh`）落盘 CAMH。**C++ 读 ② 文件裸字节** MUST 与写死预期序列化字节（CAMH 头 32+顶点索引）逐字一致。缺顶点/索引越界/空网格 MUST 抛 AssetFormatException 点名（C# 读数观测）。

#### Scenario: 含四边面 OBJ 导入三角化逐字一致

- **WHEN** 预置写死顶点/面的已知 OBJ（**立方体 6 个四边面→12 三角**，强制扇形三角化真实现）到临时源目录，门控开测试驱动导入
- **THEN** ② 镜像路径 `Models/B.mesh`（源相对路径+扩展名替换）MUST 落盘且 HasResource 真；C++ 读裸字节 MUST 等于写死预期（CAMH 头+VertexCount/IndexCount/VertexData/IndexData，四边面三角化结果）；资产浏览器 list-assets MUST 含写死 path/kind=mesh/registered=true 条目

#### Scenario: 坏 OBJ 显式失败

- **WHEN** 源 OBJ 索引越界/缺顶点/空网格
- **THEN** C# 读数 MUST 含 AssetFormatException 点名路径与原因；MUST NOT 落盘垃圾或静默

### Requirement: 导入必须与注册分离、测试驱动接入、临时根隔离且门控默认关

导入器 MUST 纯转换（**直接 new 资产**交迭代器，MUST NOT 经工厂/AssetDatabase——AssetDatabase.Create 调 CreateResource 会在 Produce 内注册），MUST NOT 调注册表（RegisterResource/ReleaseResource/CreateResource/GetOrLoadResource/GetOrNewResource 导入器文件 grep 0）；注册/落盘/重导入替换 MUST 由管线 CreateProxy 做（单一强所有者）。**C# 无 AddImporter 绑定，接入 MUST 测试驱动**（照 TestCSharpImporter：C# 绑反向表+造导入器句柄写读数→C++ ResolveImpl→ResourceImportingSystem.AddImporter→ScanSourceDirectory）。门控 `CASCADED_SOURCE_IMPORTERS` 默认关 MUST 零行为（**读数文件无导入器活动 + 临时根无新文件**可核，非仅 name+status），新门控 MUST 登记 CSharpHost/README（check_csharp_host_readme 0 问题）。**测试 MUST 用临时资源根 + 临时源目录隔离**（ScanSourceDirectory 对所有导入器 Produce + SerializeAll 按资源根写全部，临时根防着色器空库覆盖真实 shLib/污染用户 CAAssets）；源目录 env 绝对路径（测试传临时路径）。**真实约定（用户裁定）：① 源文件（工程内 CAResources/）与 ② 资产（CAAssets）同进工程**，**② 路径镜像源相对路径+扩展名替换**（Images/A/Test.png→Images/A/Test.tex）；源是重建原点，资源变动重扫源目录重建 ② 产物；② 仍是共享真相（非每机重导）。

#### Scenario: 门控关零行为

- **WHEN** 未设 CASCADED_SOURCE_IMPORTERS=1 跑现有用例
- **THEN** 导入器 MUST 零行为（读数无活动 + 无新落盘文件）；现有用例 name+status MUST 与基线零差异

#### Scenario: 重导入替换为新对象

- **WHEN** 同一源文件第二轮扫描导入
- **THEN** 管线 MUST 注册新对象替换旧（旧句柄 ReleaseImpl==1001）；② 落盘字节 MUST 等于写死预期序列化字节（CATX/CAMH 头+数据，确定性导入；**非「与源文件一致」**）

#### Scenario: 源变动从源重建

- **WHEN** 修改源 PNG/OBJ 内容后重扫源目录
- **THEN** ② 产物 MUST 重建为新源对应的序列化字节（与改前不同）；源文件与 ② 产物 MUST 都在工程内（源 CAResources/、② 资源根），路径镜像

#### Scenario: 临时根隔离不污染用户资产

- **WHEN** 测试用临时资源根 + 临时源目录导入
- **THEN** 真实 CAAssets 的 shLib/探针残留 MUST 字节不变；新 .tex/.mesh MUST 只落临时根

### Requirement: 导入器必须有验收且回归零差异绑定不变

MUST 新增 tester 用例 `TestSourceImporters`（**独立进程，进 --list 不进全量同进程顺序跑**，照 TestEditorAssetBrowser；全量是 Main.cpp 硬编码 22 条）：五滤波 PNG/含四边面 OBJ 夹具导入→② 落盘 + **C++ 读裸字节**对拍写死预期序列化字节 + registered + 资产浏览器 list-assets 写死 path/kind/sizeBytes 联动 + 坏文件红态（C# 读数 AssetFormatException）。MUST 有导入后资产浏览器 dock 显示**写死条目**（路径/kind/registered/数量）的截图（CapturePreviewAsync 不前置，AGENTS §2）。现有用例 + CoreTests MUST 全绿；绑定产物 MUST 逐字节不变（纯 C# 无绑定改动）；check_csharp_host_readme.py MUST 0 问题。

#### Scenario: 资产浏览器联动视觉验收

- **WHEN** 临时根导入 PNG/OBJ 后交互打开资产浏览器
- **THEN** 底部 dock MUST 显示写死的 kind=texture/mesh registered 条目（路径/数量核对，防旧 CAAssets 条目冒充）；CapturePreviewAsync 抓窗经 read_image 核对，MUST NOT 置前/最小化/移动用户窗口

#### Scenario: 回归与绑定零差异

- **WHEN** 完成后跑现有 22 用例 + CoreTests + 绑定基线 + check_csharp_host_readme
- **THEN** 现有用例 name+status MUST 与基线零差异、CoreTests MUST exit 0 + PASSED 26、绑定产物 MUST 逐字节不变（0/0/0）、check_csharp_host_readme MUST 0 问题、--list MUST 含 TestSourceImporters 且全量仍 22