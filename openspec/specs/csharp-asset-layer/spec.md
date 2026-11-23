# csharp-asset-layer Specification

## Purpose

C# 资产层：在托管侧定义继承反向基类的贴图与网格资产类型、带魔数与版本的自定义字节格式、自读加载的替换语义与工厂对象队列，并经验证过的正向绑定把资产字节上传渲染后做两后端像素检查。

## Requirements

### Requirement: C# 资产类型必须继承反向基类并持字节数据

贴图资源 `Texture2D` 与网格资源 `Mesh` MUST 定义在 `Scripting/Managed/Assets/`，继承生成的 `ResourceBase`，持字节数据与标量元数据（贴图：宽 / 高 / 格式 / 像素字节；网格：顶点字节 / 索引字节 / 顶点数 / 索引数）。网格顶点布局 MUST 为位置 float3 偏移 0 + 颜色 float3 偏移 12、步长 24（与 `SceneRenderer.cs` 的 `VertexStride` / `PositionOffset` / `ColorOffset` 常量逐字一致）。`override byte[] Serialize()` MUST 返回自定义格式字节；抽象方法数 MUST 与被绑定虚方法数对齐（本批只有 Serialize）。对应工厂 MUST 继承 `ResourceFactoryBase`。

#### Scenario: 写盘字节被原生侧钉死对照

- **WHEN** C# 造 Texture2D 与 Mesh 填写死数据，经门面 SerializeAll 落盘，C++ 侧读文件
- **THEN** 两文件 MUST 以各自魔数（`CATX` / `CAMH`）开头、formatVersion=1，元数据与载荷字节 MUST 与写死期望逐字节相同；期望值 MUST 写死在 C++ 侧，不从被测物推导

#### Scenario: 反向基类抽象数对齐

- **WHEN** 检查生成的 `ResourceBase` 与两个资产类型的 override
- **THEN** `ResourceBase` 抽象方法 MUST 只有 `Serialize`（Deserialize 排除保持）；两资产类型 MUST 各实现它；`IResource.h` 的 `CA_BIND_SKIP(Deserialize, …)` MUST 仍在（逐字 grep 命中）

### Requirement: 资产格式必须带版本且损坏显式失败

C# 资产格式 MUST 含魔数与 formatVersion（本批 = 1），小端逐字段顺序写。解析 MUST 校验魔数、版本、长度边界；魔数不符 / 版本不是 1 / 长度越界 / 文件缺失 MUST 抛 `AssetFormatException`，消息 MUST 点名路径与原因，MUST NOT 静默返回空对象或把垃圾灌进资产。

#### Scenario: 版本字节被改时加载点名失败

- **WHEN** 把合法资产文件的版本字节改成 2 后调 `AssetDatabase.Load`
- **THEN** MUST 抛 `AssetFormatException`，文本 MUST 含路径与版本不符原因；注册表**旧条目 MUST 仍在**（解析失败发生在释放之前，旧资产 MUST NOT 被销毁）

#### Scenario: 截短文件被边界拒绝

- **WHEN** 把资产文件截短到载荷长度字段声明的一半后加载
- **THEN** MUST 抛异常点名越界；MUST NOT 读出部分数据当成功

#### Scenario: 缺失文件不静默

- **WHEN** 对磁盘上不存在的路径调 `Load`
- **THEN** MUST 抛 `AssetFormatException` 点名路径（包装底层 IO 异常，MUST NOT 裸抛 `FileNotFoundException`——异常类型本身是判据）；MUST NOT 注册空资源；该路径若有旧条目 MUST 仍在

### Requirement: 自读加载必须走新建填数据且替换语义与导入器同构

`AssetDatabase.Load(path)` MUST：**先读文件并解析**（经 `GetResourceFullPath`，C# IO），失败 MUST 抛异常且**旧条目保持不动**（MUST NOT 先释放再读——损坏 / 缺失文件不得销毁旧资产）；解析成功后才释放旧条目（若有）并经门面 `CreateResource` 新建、填数据——与导入器管线「先产出、产出失败旧条目不动、成功后才释放注册」同构。MUST NOT 依赖 get_or_create 静默留旧。加载 MUST NOT 走 `GetOrLoadResource` / `Deserialize` 路径。替换成功后旧对象 MUST 退役（注册表唯一强引用已放）。

#### Scenario: 往返逐字段一致

- **WHEN** 造资产填数据 → SaveAll → ReleaseResource → Load 同路径
- **THEN** 加载回的资产 MUST 与原数据逐字段一致（宽高 / 格式 / 像素字节 / 顶点索引字节 / 计数）

#### Scenario: 重加载替换旧对象

- **WHEN** 同路径加载两次（第二次数据不同），对第一次对象的旧借用句柄调资源反向表 `ReleaseImpl`
- **THEN** MUST 返回代际失效码 1001（旧代理已析构）；消费者 MUST 拿到第二次的数据；MUST NOT 拿到旧对象

### Requirement: 工厂必须跟踪所造对象且队列可观测

C# 工厂助手 MUST 在 `Create()` 里把新对象入队；`AssetDatabase` MUST 在 `CreateResource` 返回后立即出队填数据。全部操作 MUST 在主循环（反向代理拒绝非注册线程调用，既有 1005）。阶段末队列 MUST 为空——造了没取 MUST 能被断言抓红。

#### Scenario: 连造两类型按序取回

- **WHEN** 连续 CreateResource 造 Texture2D 与 Mesh 各一
- **THEN** 两次出队 MUST 按 FIFO 拿到对应类型的对象（类型断言）；阶段末队列计数 MUST 为 0

#### Scenario: 造了没取能红

- **WHEN** 构造一条「CreateResource 后不出队」的反例路径
- **THEN** 阶段末队列空断言 MUST 失败（判据可失败证明）

### Requirement: 渲染消费必须走现有正向绑定且两后端像素检查

C# MUST 能拿资产字节经既有正向绑定上传渲染：Texture2D → `CreateGPUTexture` → 纹理三角形；Mesh → 图内缓冲绑定（`AllocBuffer` + `ScheduleData`，绘制 pass 按图内登记名解析顶点/索引缓冲）→ 索引绘制。本批 MUST NOT 改既有正向绑定（各接口产物逐字节不变）；两后端 MUST 通过真实像素检查，期望颜色写死。

#### Scenario: 纹理三角形两后端像素过

- **WHEN** C# 用 Texture2D 资产字节建纹理、提交纹理三角形，Vulkan 与 D3D12 各跑一遍读回
- **THEN** 采样像素 MUST 等于写死期望色（容差口径与既有纹理采样批一致）；两后端都 MUST 过

#### Scenario: 网格绘制两后端像素过

- **WHEN** C# 用 Mesh 资产字节建顶点/索引缓冲、提交绘制，两后端各读回
- **THEN** 目标像素 MUST 等于写死期望色；既有 21 个 tester 用例与 CoreTests MUST 全绿（回归零差异）

### Requirement: 资产层验收必须有可失败用例与干净回归

本批 MUST 新增 tester 第 22 用例 `TestCSharpAssets`（`--list` 21 → 22）与 C# 探针 `ResourceAssetProbe.cs`（环境变量门控 + 阶段机 + 读数文件，与既有探针同一套做法）。失败面（坏句柄 / 未注册类型 / 损坏版本 / 缺失文件）MUST 各自能独立变红；回归 MUST 按两段式口径与基线零差异（各接口产物逐字节不变；聚合产物差异逐处指认到门面登记行；既有 21 用例去掉新增后逐条 name+status 零差异）。

#### Scenario: 用例清单与门控

- **WHEN** 跑 `GPUBackendTester.exe --list`
- **THEN** MUST 列出 22 个用例含 `TestCSharpAssets`；不设门控环境变量时探针 MUST 零行为（既有用件读数不变）

#### Scenario: 失败面各自独立红

- **WHEN** 分别构造坏句柄注册、未注册类型新建、版本字节损坏、缺失文件加载四种反例
- **THEN** 四条断言 MUST 各自失败且文本点名对应原因；任何一条恒真即判据不合格
