# csharp-render-texture-sampling 设计

## Context

读码事实（逐字可 grep）：

- 原生纹理链两段式：`Test/GPUBackendTester/Main.cpp` 的 `TestTriangleWithImageBuffer` —— 第一段上传：逐字 `submitGraph->ScheduleData(testTexture, data, textureSize);`、`submitGraph->Finalize(testTexture, ETextureAccessType::eSampled);` 后独立 `ExecuteGraph`（该图**不含任何渲染 pass**，先例成立）；第二段渲染：`imageStruct->SetImage(CANAME("testTexture"), testTexture);` + `imageStruct->SetSampler(CANAME("testSampler"), TextureSamplerDescriptor::LinearClamp());`，`SetParam(CANAME("textureData"), imageStruct)` 绑进 pass。
- 纹理发行：`Interface/RenderInterface/header/CRenderBackend.h` 逐字 `virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType) = 0;`；原生测试取 `ETextureAccessType::eTransferDst | ETextureAccessType::eSampled`。`GPUTextureDescriptor::Create(width, height, ETextureFormat::E_R8G8B8A8_UNORM)`（默认 `e2D`、layers 1、mips 1、samples e1）。
- `GPUTexture` 基类只有三个纯虚：`GetDescriptor()`/`SetName()`/`GetName()`（假后端**已有** `FakeTexture` 桩与 `CreateGPUTexture` 覆写：`Test/CoreTests/ScriptRenderBridgeTest.cpp` 逐字 `castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& descriptor, ETextureAccessTypeFlags) override`，已记 `createTextureCalls` 并保存描述符——本批只需补"最近创建纹理"读取口，不新建桩）。
- `ImageHandle` 与纹理的接法：`Interface/RenderInterface/header/ShaderResourceHandle.h` 有 `ImageHandle(castl::shared_ptr<GPUTexture> const& texture)` 构造（`ImageType::External`），原生测试正是把 `shared_ptr<GPUTexture>` 直接传入 `ScheduleData(ImageHandle…)` 靠隐式转换；适配器内部照此写 `ScheduleData(ImageHandle(nativeTexture), …)`，**MUST NOT 给 `GPUTexture` 加句柄 getter**（会动公开接口与审计面）。
- `GPUGraph::ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, uint64_t offset = 0, bool copy = true)` 函数体只写图内部表（`m_DataTransfers`），不触后端；`Finalize(ImageHandle const&, ETextureAccessType)` 同（写 `FinalizePass`）。**CoreTests 假后端能全程走通适配器内部上传路**（假 `ExecuteGraph` 只计执行数，不消费表）。
- 元数据：`Interface/ShaderCompiler/header/Compiler.h` 逐字 `struct ShaderTextureData { cacore::NameHash m_Name; EShaderResourceAccess m_RWType; uint32_t m_ElementCount; }`；`ShaderStructData` 含 `castl::vector<ShaderTextureData> m_Textures;`、`castl::vector<cacore::NameHash> m_TextureSamplers;`。
- 两后端写入口与缓冲同形：`SetImageInternal`/`SetSamplerInternal`（D3D12 与 Vulkan 的 `*ShaderStruct.cpp`）对未知字段名静默跳过、对越界下标 `CA_ASSERT(...)`；构造期同样按 `m_Textures`/`m_TextureSamplers` 建表。
- 采样器：`Interface/RenderInterface/header/TextureSampler.h` 的 `TextureSamplerDescriptor` 为 8 字段公开值记录（`magFilterMode`/`minFilterMode`/`mipmapFilterMode` 三枚举、`addressModeU/V/W` 三枚举、`boarderColor` 枚举、`bool integerFormat`），三个枚举都是简单 `enum class`——`CA_BIND` 记录面已有枚举+bool 先例。工厂 `LinearClamp()` 等存在。
- **本批必须新建着色器**：原生 `TestTriangleWithTextureSampling.slang` 的片元逐字 `return float4(input.uv, textureData.Sample(input.uv).a, 1.0f);` —— 输出的是 UV 插值而非采样色，证明不了纹理内容进像素；且其顶点测试用 `eR32G32B32_SFloat` 声明 `float2` 的 TEXCOORD（12 字节格式读 8 字节数据，越界靠运气）。本批新着色器输出采样色本身、TEXCOORD 用 `eR32G32_SFloat` 正规形状。
- 既有窄面：三探针互不消耗模式、图侧参数登记表（`(passIndex, blockName) → 窄结构`）、提交前必需性检查（参数块 + 缓冲字段 + `SetStruct` 拒含缓冲子结构）、`CreateShaderStruct` 空句柄拒绝契约、基线+增量计数断言模式、run-all 插入位置约束（回调测试会关运行时）。

## Goals / Non-Goals

**Goals:**
- C# 能创建 RGBA8 纹理（字节即数据，一次调用完成创建+上传+转采样态）、按元数据把纹理与采样器绑进参数块、用带 `TEXCOORD` 的顶点流提交，双后端真实像素证明采样色进输出。
- 全部无效组合（未知字段、非只读、数组、空纹理、漏绑、错名、尺寸不符、重名）在适配层点名拒绝，两后端的静默跳过与越界断言路径不可达；"释放后使用"由托管表面对象的 `ObjectDisposedException` 防线兜住（适配层不判已释放）。
- ABI 升 v8（编号 7 = 纹理），两侧一致性判据、宿主失败检查、README、关闭顺序全部同步且能失败。

**Non-Goals:**
- 多 mip/数组/3D 纹理、非 RGBA8 格式、渲染目标当纹理采样（多阶段批）、`RWTexture` 写（计算批）、带索引绘制（几何批）、`stbi_load` 文件导入（非引擎接口）、纹理显式销毁（本批纹理由适配器保活到进程末，显式释放留后续批——写进 README 已知限制）。

## Decisions

### 一、窄接口形状：新接口 2 方法 + 三接口各扩

```cpp
CA_BIND() class IScriptTexture
{
public:
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
};

// IScriptRenderBackend（8 方法）追加：
// 一次调用完成创建+上传+Finalize(eSampled)+WaitIdle；失败返回空句柄，后端 GetLastError() 点名。
virtual castl::shared_ptr<IScriptTexture> CreateTexture(cacore::NameHash const& name,
    uint32_t width, uint32_t height, castl::vector<uint8_t> const& rgba) = 0;

// IScriptShaderStruct（现状 4 方法：SetFloat3/SetStruct/SetBuffer/GetLastError；追加后 6）：
virtual int32_t SetImage(cacore::NameHash const& field,
    castl::shared_ptr<IScriptTexture> const& texture) = 0;
virtual int32_t SetSampler(cacore::NameHash const& field,
    TextureSamplerDescriptor const& sampler) = 0;

// IScriptRenderGraph（9 方法）追加：单 POSITION float3 + TEXCOORD float2（eR32G32_SFloat，8 字节）
virtual int32_t SetPositionTextureStream(cacore::NameHash const& streamName, uint32_t stride,
    uint32_t positionOffset, uint32_t texcoordOffset) = 0;
```

审计 +6 行 → **12 接口 90 方法**（`IScriptTexture` 2 + `IScriptRenderBackend` 1 + `IScriptRenderGraph` 1 + `IScriptShaderStruct` 2 = 6）。`TextureSamplerDescriptor` 与其三个枚举、`GPUTextureDescriptor` 不必全进公开面：脚本只暴露记录整体（8 字段 = **7 个枚举**（mag/min/mipmap 三过滤、U/V/W 三地址、`boarderColor` 边界色）**+ 1 个 bool**（`integerFormat`），逐字见 `TextureSampler.h` 结构体）；形状验证点 = 1.1 的构建红态（`CA_BIND` 发射器若拒绝该记录即生成期点名、停工上报，不预设"已确认可用"），尺寸/格式参数由 `CreateTexture` 签名固定住（RGBA8 隐含）。

### 二、`CreateTexture` 单次调用，内部四步

适配器内部顺序（读码依据见 Context）：参数校验 → `m_Backend->CreateGPUTexture(GPUTextureDescriptor::Create(w, h, ETextureFormat::E_R8G8B8A8_UNORM), ETextureAccessType::eTransferDst | ETextureAccessType::eSampled)` → 新建内部上传 `GPUGraph`（不建窄对象、不进登记表）`ScheduleData(ImageHandle(nativeTexture), rgba.data(), rgba.size())` + `Finalize(同一 ImageHandle, eSampled)` → `NewScheduler` + `m_Backend->ExecuteGraph` + `m_Backend->WaitIdle`（同步上传，脚本面无异步语义；**不经适配器的 `SubmitGraph`/`WaitIdle`**——避免抬高 `GetExecuteGraphCountForTest`/`GetWaitIdleCountForTest` 或提前清 `m_Pending`）。当场拒绝清单（全部空句柄 + 后端 `GetLastError()` 点名，与 `CreateShaderStruct` 契约同形）：`width/height == 0`；`GetImageByteSize(w, h, E_R8G8B8A8_UNORM) != static_cast<uint64_t>(rgba.size())`（`Interface/RenderInterface/header/Common.h` 逐字 `inline uint64_t GetImageByteSize(uint32_t width, uint32_t height, ETextureFormat format)`，`CRenderBackend.h` 规定它是唯一公开字节数来源——禁止 `width*height*4` 32 位自算，`65536×16384×4` 回绕成 0 会骗过自算）；`name` 在适配器纹理表已存在；`CreateGPUTexture` 返回空（文本点名失败环节）。成功：适配器持 `shared_ptr<GPUTexture>` 强引用表（name→条目），窄 `CScriptTexture` 持副本 + 登记尺寸，纹理在适配器销毁前不失效。计数语义定死：上传执行单列 `m_TextureUploadExecuteCount` + `GetTextureUploadExecuteCountForTest`，`GetExecuteGraphCountForTest` 只计 `SubmitGraph` 进入后端的执行（`private/ScriptRenderBridge.h` 的注释同步这句），一切"提交执行次数 0"判据指后者。

### 三、`SetImage`/`SetSampler` 当场校验 + 提交完备性（沿用缓冲批的骨架）

- `SetImage(field, texture)`：`field` ∈ `m_Textures`（否则非零点名）；条目 `m_RWType != eReadOnly` 或 `m_ElementCount != 1` → 非零（数组/写纹理留计算批）；`texture` 为空 → 非零点名；通过则窄结构记 `field → shared_ptr<IScriptTexture>` 并立即写真实 `ShaderStruct::SetImage`。"释放后使用"不设适配层条款（本批无显式销毁、适配层不可观测），防线在托管表面对象：`Dispose()` 后再调用 MUST 在进入原生实现前抛 `ObjectDisposedException`、原生调用次数 0（生成表面的既有 disposed 防线，探针末尾实测一步）。`SetSampler(field, desc)`：`field` ∈ `m_TextureSamplers`（名字表，仅存在性校验）；通过记 `field → desc` 并写真实 `SetSampler`。重复绑定同字段以最后一次为准（覆盖语义与缓冲一致）。
- `SubmitGraph` 预执行检查扩展（同一预执行段、数据通道沿用图侧登记表——登记表存的窄结构现在同时携带缓冲名、纹理引用与采样器记录）：每个绑定结构的 `m_Textures`/`m_TextureSamplers` 每条目必须已写，缺失非零点名并零执行（本次提交引起的执行增量 0）。`SetStruct` 拒绝条件扩为：子结构类型的 `m_Buffers`、`m_Textures`、`m_TextureSamplers` 任一非空即拒（点名字段与原因）——三张资源表的顶层限定同一防线。只读核对面随 D5 补：`CScriptShaderStruct::HasBoundImage(field)`/`GetBoundImage(field)`、`CScriptRenderGraph::GetBoundImageForTest(passIndex, blockName, field)`（4.1 据此断言"解析到该次 `CreateTexture` 返回的同一对象"，计数替代不了对象同一性）。
- 保活：登记表强引用窄结构 → 结构强引用窄纹理 → 适配器强引用原生纹理；释放表面对象后提交仍可读（CoreTests 断言）。

### 三·五、`SetPositionTextureStream` 的实现形状（本批必须实现，否则窄接口有声明无实现即构建红）

适配器给图的流登记结构（`CScriptRenderGraph` 的 `StreamRecord`）追加 `texcoordOffset` 字段；校验：`stride == 0`、`positionOffset + 12 > stride`、`texcoordOffset + 8 > stride`（TEXCOORD 是 `float2` = 8 字节，区别于 `SetPositionColorStream` 的两个 12 字节属性），流名与两张既有流共用同一张表、重名非零点名 `streamName`。通过后构造 `VertexInputsDescriptor`：`POSITION` 用 `eR32G32B32_SFloat`（offset=`positionOffset`）、`TEXCOORD` 用 `eR32G32_SFloat`（offset=`texcoordOffset`）——**不是**原生测试那种用 `eR32G32B32_SFloat` 声明 `float2`（12 字节格式读 8 字节数据）的越界写法。`AddTrianglePass` 据 `streamName` 选该描述符（沿用现有按名取流）。CoreTests 正例经 `GetGraphForTest` 读回该 pass 顶点描述符、断言 TEXCOORD 的 offset/格式（四纹素全同色使像素判据看不出 texcoordOffset 写错，必须读描述符这一层）。

### 四、新着色器与像素判据

`CAResources/Shaders/Test/TestCSharpTextureSampling.slang`：`VertexInput { float3 position : POSITION; float2 uv : TEXCOORD; }`；参数块 `ParameterBlock<TextureSampleBlock> textureBlock`，块类型含 `Texture2D<float4> testTexture; SamplerState testSampler;`；片元 **返回 `testTexture.Sample(testSampler, input.uv)` 原值（premultiplied：`rgb *= a`，与原生风格一致，测试 alpha=1 不影响）**。像素判据（全部来自着色器与 C# 数据的算术事实，不手算插值）：清除色 `(0, 0, 1, 1)`；纹理 `2×2` 四纹素全 `(255, 255, 0, 255)`（16 字节全同色 → 任何过滤/寻址/UV 都返回该色，判据对驱动差异与 texcoord 值都不敏感，故不设"UV 覆盖某区间"这种单三角形做不到的几何前提）；三角形三顶点位置沿用常量色批 `(-0.25,-0.25,0.25)`、`(0.25,-0.25,0.25)`、`(0,0.5,0.5)`（中心落在三角形内，故中心取采样色；四角在三角形外，取清除色），UV 三值写 `(0,0)`、`(1,0)`、`(0.5,1)` 仅作流数据实例（不参与判据）；中心像素精确 `(255, 255, 0, 255)`、背景精确 `(0, 0, 255, 255)`。负向 `bad-texture-color`：四纹素全 `(0, 255, 255, 255)` → 中心精确 `(0, 255, 255, 255)`（证明"字节→纹理→采样→像素"整链参与，而非仅存在性）。

### 五、探针、错误码与报告

第 16 项 `TestCSharpTextureSampling`（`--list` 追加在第 15 项后；run-all 调用插在 `TestCSharpWindowCallbacks` 之前，位置红绿沿用 B5 判据同构）；`OnTextureProbe` + `CASCADED_CSHARP_TEX_PROBE` + `CASCADED_CSHARP_TEX_SELFTEST` ∈ {`bad-texture-color`, `unbound-image`, `unbound-sampler`}，与既有三探针互不消耗。错误码族：`E_TEX_PIXEL`（像素）、`E_TEX_BIND`（提交前拒绝，两种漏绑各点名 `testTexture`/`testSampler`）、`E_TEX_CREATE`（创建拒绝负例）、`E_TEX_HOST`/`E_TEX_PROBE_MISSING`（宿主挡与探针缺席，同构既有）。新钩子 `GetCreateTextureCountForTest`/`GetSetImageCountForTest`/`GetSetSamplerCountForTest`/`GetTextureUploadExecuteCountForTest` + 只读核对口 `HasBoundImage`/`GetBoundImage`/`GetBoundImageForTest`（决策三），断言一律基线+增量：正常探针 `CreateTexture`/`SetImage`/`SetSampler` 各 +1、`SetPassParam` +2、上传执行 +1、适配器 `WaitIdle` 与提交执行增量 +1/+0；`unbound-image`/`unbound-sampler` 形态对应绑定计数增量 0、**提交执行增量 0**（上传执行已发生的 1 次不计入该判据）。托管释放防线实测：探针末尾 `tex.Dispose()` 后再 `SetImage`，捕获 `ObjectDisposedException` 一行写进摘要（3.2 记摘要、4.1 核对摘要行）。真实后端负例（两后端各一次，进程不崩）：尺寸不符创建 → 空句柄 + 点名；表外字段 `SetImage` → 非零点名。`Tools/check_csharp_texture_sampling.py` 对账 8 份（2 后端 × normal/bad-texture-color/unbound-image/unbound-sampler），缺目录退出 2 的两态判据沿用。

### 六、宿主与 ABI v8

`EngineApi` 追加 `script_texture_interface_id = 7`（手写 ABI 末尾 uint32），版本 7 → 8；**`sizeof(EngineApi)` v8 = 80 字节**——现 72 已被 v7 用满 8 的整倍数（`CSharpHost.cpp` 的 `static_assert(sizeof(EngineApi) == 72u, …)` 是布局读数），追加一个 `uint32` 后正文 76、结构按最大成员 8 字节对齐补到 80；两侧 `static_assert`（C++）与 `ExpectedAbiVersion`/`Marshal.SizeOf`（C#）强制该值，写错即编译/自检失败。字面量真正所在处逐一点名同步：`CSharpHost/private/CSharpHost.cpp`（`s_engineApiVersion`、`static_assert` 与其注释、自检打印 `s_engineApiVersion + 1` 的"引擎 9"输出）、`Scripting/Managed/Host.cs`（`ExpectedAbiVersion = 8`）、`CSharpHost/README.md`（第 5 行「当前是 v7/72」→ v8/80、第 17 行「不是 80」这句要改写、失败语义表与实测输出节的 ABI/字节数示例、第 139 行「编号 4 / 5」→「编号 4 / 5 / 6 / 7」）、`Tools/check_csharp_host_failures.py`（「托管侧期望 7」→ 8）。`check_csharp_host_readme.py` 保持只做对拍不改它，但判据须扩到 README **全文**的「函数表 N 字节」「ABI vN」「不是 80」类读数都与 static_assert/版本一致（红态：把第 17 行改回「不是 80」必须非零——见 tasks 3.1）。纹理接口属目标类型表（无默认实例，编号 7 空实例取表）。宿主接线点名 `Host.cs` 的 `BindRenderInterfaces`：加第五个 `BindTargetTable(s_api->ScriptTextureInterfaceId, "IScriptTexture", IScriptTextureBindings.Bind)`，绑定门 `s_renderTablesBound` 后的日志「四个函数表已绑定」→「五个函数表已绑定」，`ProbeTexture` 的失败文本改「后端 + 四个目标类型表」；`EnsureBound` 保持复位语义、不承载新表。`Shutdown` 释放顺序：参数结构 → 纹理 → 图 → 离屏目标 → 后端表面（纹理事务对象在结构之后释放——结构持纹理引用，先放持有者）。`CSharpHost/README.md`：编号表 +1 行、v8/80、`CASCADED_CSHARP_TEX_*` 开关、纹理无显式销毁的已知限制。

## Risks / Trade-offs

- [上传图 `Finalize`+零渲染 pass 在真实后端首跑] → 原生测试同型先例存在（上传段就是这个形状）；4.1 normal 跑通即证据，跑不通按报错阻断并上报主 agent 修工件，不绕。
- [`TextureSamplerDescriptor` 6 枚举 + bool 的生成形状未实测] → 1.1 的构建红态同时是形状验证点：`CA_BIND` 发射失败会在生成期点名，若确属不可发射形状，停工上报（不许改成手 extern）。
- [假后端 `GPUTexture` 桩] → **既有** `FakeTexture` 与 `FakeRenderBackend::CreateGPUTexture`（已记调用、存描述符）沿用；本批只补"最近创建纹理/描述符"读取口（现只存 lastGraph），"桩成本与 `ShaderStruct` 桩同量级"的说法作废（不是待建，是已存在）。纹理表重名/尺寸校验在适配器层，假环境可全量断言。
- [纹理无显式销毁，测试进程外的长跑会持续占用] → 已知限制如实写 README；显式 `ReleaseTexture` 的语义（谁最后持引用、飞行中提交）留给下一批与参数结构释放语义一并设计。
- [`m_TextureSamplers` 只有名字表，采样器字段无读写/数组维度可校验] → 存在性 + 必绑检查已封顶误绑面；数组采样器留后续批。
- [run-all 位置/增量计数/登记表这些上批钉过的机制] → 工件直接沿用其原文判据，不重开。

## Open Questions

（无——`sizeof` v8=80 由布局算术与 `static_assert` 强制，其余数值与形状均有出处或列为验证点。）
