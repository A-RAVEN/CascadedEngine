# Design: csharp-resource-asset-foundation

## Context

机制已就绪（前两批归档）：`IResource` / `IResourceFactory` / `ResourceImporter` / `ResourceImporterProductions` 四个反向接口 + 代理 + 基类；注册表 `ResourceManagingSystem_Impl`（`GeneralResources/private/ResourceImportingSystem.cpp`）持两张表——`m_PathToResource`（路径→资源，单一强所有者）与 `m_TypeToFactory`（类型名→工厂记录：借用指针 + 强所有物 + 句柄 + 释放回调）。C# 探针（`ResourceProbe.cs` / `ResourceImporterProbe.cs`）已验证「C# 造对象 → 注册 → 写盘」，但登记动作全由 C++ tester 侧驱动：`ResourceManagingSystem` 不在 `build.py` 的 `CODEGEN_BINDINGS` 里，C# 没有自己的入口。

正向生成器已实现的形状（`Tools/CodeGen/README.md` 形状表，L1004 起）：标量、`castl::string const&` 入参与 `castl::string` 返回（`char const* + uint64 len`）、`PathHash const&`、`vector<uint8_t> const&`、`shared_ptr` 返回→强句柄 + 配对释放、异常→错误码 + 文本。本批只用前者，不碰生成器。

跨模块约束（反向批确立）：句柄表是头内 inline + 函数级 static，EXE/DLL 各一份；跨模块解析只能经**创建模块**的 `ResolveImpl`/`ReleaseImpl`（生成反向函数表里的槽）。宿主 CSharpHost 已持有 `ca_bind_registry::GetInterfaceApi`（`CSharpHost.cpp` L237）与模块管理器——解析能力天然在宿主层。

## Goals / Non-Goals

**Goals:**

- C# 脚本能自助：注册自己的工厂 → 新建资产 → 填数据 → 落盘 → 自读加载 → 替换 → 渲染消费，全程不需要 C++ 侧逐个驱动。
- GeneralResources **零改动**；生成器改动仅限新增门面专属错误码 1014（沿用 1012/1013 作用域先例，只发射进门面产物）；既有**各接口**绑定产物逐字节不变（三份聚合登记产物只变门面登记行，两段式判据见 D8）；既有 21 个 tester 用例与 CoreTests 全绿。
- 所有权规矩不破：注册表仍是资源对象单一强所有者；门面不引入第二份强引用。

**Non-Goals:**

- 反向 Deserialize（`IResource.h` 的 `CA_BIND_SKIP` 保持，理由不变：IOBatch 问不到剩余长度）。
- C++ 贴图/网格类型、PNG/OBJ 源文件导入器、内容哈希身份、异步加载、热重载、驱逐（见 proposal）。
- 注册表全量 API 绑定（模板 / 共享指针入参 / 裸指针返回不进窄面）。

## Decisions

### D1 门面坐宿主层，不绑 ResourceManagingSystem 本体

新接口 `cs_script::IResourceRegistry`，头 `Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`（与 `IScriptHost` 同目录同命名空间），实现在 `CSharpHost.cpp`，经模块管理器取 `ResourceManagingSystem` 委托。

- **为什么不直接绑本体**：① 工厂注册的句柄入口收 `(string, uint64, IResourceFactory*, ResourceFactoryRelease)`——裸指针 + 裸函数指针参数，形状不收；C# 自助注册必须有人把句柄解析成借用指针 + 释放回调，而解析要 `ca_bind_registry`，让 GeneralResources 拿它等于新增「核心模块 → 绑定层」依赖方向。② 本体的模板 `GetOrLoadResource<T>`、`shared_ptr` 入参的 `RegisterResource`、裸指针返回的 `GetFactory` 要么形状不收要么语义越界，绑本体要挂一串 `CA_BIND_SKIP`，审计面反而更脏。
- **为什么不把解析放 C# 侧**：`ResolveImpl` 返回的借用指针不能跨边界交给 C# 再传回来（裸指针过界违反五类形状）；解析必须留在原生。
- 宿主层做门面与既有事实一致：tester 今天就在宿主侧做同一套解析（`Main.cpp` L6549–6555），本批只是把它从测试代码搬进产品代码并开给 C#。

### D2 窄面方法集：九个方法，全已实现形状，无共享指针过界

```cpp
CA_BIND()
class IResourceRegistry
{
public:
    virtual ~IResourceRegistry() {}
    virtual void RegisterFactory(castl::string const& typeName, uint64_t factoryHandle) = 0;
    virtual bool HasFactory(castl::string const& typeName) const = 0;
    virtual bool UnregisterFactory(castl::string const& typeName) = 0;
    virtual void CreateResource(castl::string const& typeName, castl::string const& path) = 0;
    virtual bool HasResource(castl::string const& path) const = 0;
    virtual bool ReleaseResource(castl::string const& path) = 0;
    virtual void SerializeAll() = 0;
    virtual castl::string GetResourceRootPath() const = 0;
    virtual castl::string GetResourceFullPath(castl::string const& path) const = 0;
};
```

- 形状核对：string 入参/返回（README L1004 已实现）、bool→int32、uint64 标量、void——全在已实现清单，无需 `CA_BIND_SKIP`，无需生成器改动。
- **CreateResource 返回 void 而不是 shared_ptr**：C# 侧对象经工厂跟踪拿（D4），不需要资源句柄；而 `shared_ptr<IResource>` 返回要求目标接口解析入口，`IResource` 是纯反向接口、没有正向表，那条投影路径未经验证——不趟。C++ 调用方要对象直接用 `ResourceManagingSystem::CreateResource`（返回 shared_ptr，现状不变）。
- **不收 RegisterResource**：它的 `shared_ptr<IResource>` 入参投影要求强句柄反查；C# 直接 new 的资源只有反向借用句柄，按规范借用句柄作 shared_ptr 入参必须失败——收了也是死路。C# 资产一律走「工厂 + CreateResource」，与 `CreateResource` 既有所有权路径逐字同构。
- **CreateResource 同路径守卫**：门面先查 `HasResource(path)`，已有条目 MUST 经错误通道 1014 上报（点名「路径已存在，先释放再新建」）。原因：注册表 `RegisterResource` 是 get_or_create——同路径留旧弃新；核心 `CreateResource` 返回的 shared_ptr 在 C++ 调用方手里不会丢，门面返回 void 一旦静默放行，新对象当场析构（代理 releaseUser → GCHandle.Free），C# 工厂队列却把它当成功结果交出去（HasResource 真、SerializeAll 写旧字节、不抛异常）。替换语义统一走 Load（先 ReleaseResource 再新建，D6）。
- 失败通道（**实测修正**：正向普通方法的生成 thunk **没有** try/catch——try 只在回调分支，GPUBuffer thunk_SetName 实测直调；README 形状表「会抛异常的方法→生成器 try/catch」行与实现不符，本批顺手改准）：门面**实现自己 catch**（注册表抛的 `resource_creation_error` / `ca_io::io_error` / 反向代理的 `ReverseBindingError` 都在门面内捕获），调同翻译单元可见的生成 `cs_script__IResourceRegistry_setError` 写错误码 + 文本；C# 包装每次调用后自动 `ThrowIfFailed`（GPUBuffer.Bindings.cs 实测），抛 `IResourceRegistryException(code, 原文)`。**用码**：句柄无效 / 已释放 / 代际不符 → 既有 1001（STALE_HANDLE，语义贴合）；其余操作失败（表缺失 / 未注册类型 / Create 失败或返空 / 同路径 / 落盘 IO 错误）→ 新增门面专属 **1014 RESOURCE_OPERATION_REJECTED**，文本点名原因。1014 沿用 1012/1013 的作用域机制只发射进门面产物（CodeGenMain.cpp 错误码表 + 作用域过滤 + check_bindings.py ERROR_CODE_TABLE + README 错误码表四处同步），既有产物逐字节不变。异常不跨 C ABI 边界。

### D3 工厂句柄解析：复用 tester 已验证的流程

`RegisterFactory(typeName, handle)` 实现：

1. `ca_bind_registry::GetInterfaceApi(kBindingInterfaceIdresource_management__IResourceFactory, nullptr, TableVersion, …)` 取反向函数表；取不到（尺寸 0）→ 错误通道 1014（点名「反向工厂接口未登记」）。
2. `table->ResolveImpl(handle)` → 借用 `IResourceFactory*`；空 → 错误通道 1001（点名「句柄查不到代理」，坏句柄 / 已释放 / 代际不符都走这里）。
3. 委托 `registry->RegisterFactory(typeName, handle, ptr, table->ReleaseImpl)`——既有句柄入口，替换 / 注销语义不变（注销时 `release(handle)` 放创建模块的强引用）。

句柄 MUST NOT 丢：注册表记录里保留 handle + release，与反向批 D8 逐字一致。门面自己不持工厂强引用。**注册成功后强句柄的处置权归注册表**（注销 / 替换经 release 放掉）；C# MUST NOT 在注册期间对同一句柄再调 `IResourceFactoryBindings.Release`——那会让注册表持悬空借用指针（所有权移交契约，写进 spec）。

### D4 C# 拿对象：工厂跟踪队列

`Create()` 无参数（既有契约），不知道路径；C# 要填数据必须拿到刚造的对象。做法：C# 工厂基类助手 `AssetFactoryBase`（继承 `ResourceFactoryBase`）在 `Create()` 里把新对象入队；`AssetDatabase` 在 `CreateResource` 返回后立即出队。

- 为什么队列不是 LastCreated：一次阶段里可能连造多个（贴图 + 网格），LastCreated 会丢前一个。
- 为什么不用路径键：Create 时路径未知（注册在 Create 之后）。
- 线程边界：反向代理拒绝非注册线程调用（错误码 1005，既有），本批全部操作在主循环；队列无需并发容器。
- 可观测：探针阶段末断言队列为空（造了没取 = C# 框架 bug，必须红）。

### D5 C# 资产类型与自定义格式

`Scripting/Managed/Assets/`：

- `Texture2D : ResourceBase`：`Width`/`Height`（int）、`Format`（枚举，本批只 `R8G8B8A8_UNORM`）、`byte[] Pixels`。`override byte[] Serialize()` 产出：魔数 `CATX`（4 字节）+ formatVersion=1（uint32）+ width/height/format（各 int32）+ 像素长度（uint64）+ 像素字节。
- `Mesh : ResourceBase`：`byte[] VertexData`（位置 float3 偏移 0 + 颜色 **float3** 偏移 12，步长 **24**——与 `SceneRenderer.cs` 的 `VertexStride=24` / `PositionOffset=0` / `ColorOffset=12` 逐字一致；颜色是 float3 不是 float4）、`byte[] IndexData`（uint32 索引）、`VertexCount`/`IndexCount`（int）。Serialize：魔数 `CAMH` + formatVersion=1 + 两个计数 + 两段长度 + 两段字节。
- 格式是 **C# 自定义**（写读同程序集），不是 C++ CodeGen 格式——C++ `TResource` 读不了它，加载方在 C#，闭环自洽（proposal 已声明）。小端、无对齐填充（逐字段顺序写）。
- 解析失败（魔数不符 / 版本不是 1 / 长度越界 / 文件缺失）→ 抛 `AssetFormatException`，消息点名路径与原因（与 io-error-channel「失败必须可观测」同 policy）。**MUST NOT 静默返回空对象**。

### D6 自读加载与替换语义

`AssetDatabase` 助手（C#）：

- `Create(typeName, path)`：门面 `CreateResource` → 出队 → 返回对象（调用方填数据）。
- `Load(path)`：**先读文件与解析**（`GetResourceFullPath(path)` → `File.ReadAllBytes` → 按魔数分派解析）——失败 MUST 抛 `AssetFormatException` 且**旧条目不动**（旧资产不被销毁）；全部成功后才 `ReleaseResource(path)`（若已有条目）→ `CreateResource` → 出队 → 填数据。这才与导入器管线 `ScanSourceDirectory` 真同构：**先产出、产出失败旧条目不动、成功后才释放注册**（ResourceImportingSystem.cpp L80-92）。MUST NOT 先释放再读文件——那会让损坏 / 缺失文件静默毁掉旧资产。剩余的 Release→Create 窗口若 Create 失败，错误显式上报（错误码 + 文本，路径空，可重试），不是静默丢失。文件缺失 / IO 错误 MUST 包装成 `AssetFormatException` 点名路径（MUST NOT 裸抛 `FileNotFoundException`——类型判据见 tasks 3.4）。
- **已知约束（实测）**：IOManager_FS 的 `GetOStream` 把写流缓存在 `m_OStreamCache`、提交后不关（核心行为，非目标不改）——资产文件在 manager 存活期间一直被原生写句柄占用。C# 读写 MUST 用 `FileShare.ReadWrite`（`File.ReadAllBytes` 默认 FileShare.Read 会撞共享冲突）；「缺失文件」用例用「条目已建但从未落盘」构造（缓存写流使已落盘文件删不掉），语义等价满足 spec 场景。
- `SaveAll()`：门面 `SerializeAll()`——引擎经反向 `Serialize()` 写盘，无条件全量（导入器批确立的规矩）。
- 加载不走 `GetOrLoadResource` / `Deserialize`（反向读排除保持）。

### D7 接线（编号 / 登记 / 审计）

- `build.py`：`CODEGEN_BINDINGS` 加行（`Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`，期望前缀 `cs_script__IResourceRegistry`）；`CODEGEN_REGISTRIES` 同步。
- `interface-ids.lock`：`--update-ids`（用 Debug 档 CodeGen.exe，ensure_codegen_tool）→ host 段新编号 **13**（现高水位 12，锁文件实测），fixture 段不动（高水位 14）。
- `kBindingPolicies` 加 `{ kBindingInterfaceIdcs_script__IResourceRegistry, true }`（要求默认实例）；取 `ResourceManagingSystem` 实例 → new 门面实现 → 经 `SetInterfaceInstance` 注册（CRenderBackend 先例）。**注册点 MUST 在 `CreateRenderAdapter` 的渲染后端缺失 early-return 之前**（或无条件初始化段）——无渲染后端的宿主也要能取门面；门面对象生命周期随宿主（宿主持有，与实例表同口径）。
- `CSharpHost/CMakeLists.txt`：`target_include_directories PRIVATE` 加 `Interface/ScriptHost/header/ScriptHost`——生成产物按基名 `#include "IResourceRegistry.h"` 找头（与 CAResource 行同类先例），缺行编译报 C1083。
- 生成器两处受控改动（既有产物逐字节不变是硬判据）：① 错误码——`CodeGenMain.cpp` 错误码表加 `{ "RESOURCE_OPERATION_REJECTED", 1014 }` + 作用域过滤（只发射进 `cs_script__IResourceRegistry` 产物，机制照 `isReverseOnlyErrorCode` 先例）；`check_bindings.py` 的 `ERROR_CODE_TABLE` 与 `Tools/CodeGen/README.md` 错误码表同步加 1014；README 形状表「会抛异常的方法」行改准（普通方法 thunk 无 try/catch，错误由实现写通道）。② 字符串两段式返回带参数——旧 stringTwoStep 分支无条件 `object->Method()` 丢参（实测门面 `GetResourceFullPath(path)` 生成 C2660）；改为 Len/Copy 伴生带方法参数（形状限注入/文本/标量/枚举，其它生成期点名拒绝），零参数方法生成字节与旧实现逐字相同，故既有产物不受影响。
- 审计表：`check_bindable_audit.py` 要求**全仓唯一一份** `bindable-audit.md`（实测 glob 只命中归档 `2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md`）——主表九行追加进那一份，逐行写投影形状 / 分配归属 / 错误通道，**顶部汇总表加门面一行、合计 105→114**（脚本 summary_rows 逐接口核汇总行并与实算对账）；**同时**给该脚本的 `INTERFACES` 表加 `cs_script::IResourceRegistry` 一项（脚本按 INTERFACES 生成期望集，表外多出的行判「僵尸行」非零退出——只追加审计行不改脚本必红）。改后脚本绿（接口数 13→14、方法数 105→114，实测基线）。
- C# 命名空间按既有镜像规则：`Scripting.Generated.Bindings.CsScript`（线形 `cs_script__IResourceRegistry`）。

### D8 验收载体

- **tester 第 22 用例 `TestCSharpAssets`**（`--list` 从 21 → 22）：C# 探针 `ResourceAssetProbe.cs`（环境变量门控 + 阶段机 + 读数文件，与 `ResourceProbe` 同一套做法）；C++ 侧读同一文件断言 + 原生直检。阶段覆盖：绑门面表 → 注册两工厂 → 新建 + 填数据 → SerializeAll → **C++ 读盘断言魔数/版本/字节**（期望值写死，不从被测物推导）→ 释放 + 自读加载 → 往返一致 → 替换（旧对象退役：资源反向表 `ReleaseImpl(旧借用句柄) == 1001` 代际失效判据，与导入器批同口径——槽位会被后来者复用，不许拿保活计数当判据）→ 渲染消费（纹理三角形 + 网格，两后端真实像素检查）→ 失败面（坏句柄 / 未注册类型 / 同路径直接新建 / 损坏版本 / 缺失文件 / Serialize 异常两段式，各断言错误文本点名）。Serialize 异常按既有反向错误语义两段断言：首次 SerializeAll 该资源落盘缺席（文件字节保持写前状态、其余资源照写）、错误落代理槽；下一次经该代理的调用 MUST 抛 1012 + 原始文本（README 反向错误通道既有语义，本批不改机制）。
- **C++ 直检门面**（不经 C#）：坏句柄注册抛、未注册类型新建抛、HasResource 空表读 false。
- **回归**：既有 21 用例（去掉新增用例）逐条 name+status 与上批基线零差异 + CoreTests 全绿；绑定基线**两段式**（与上批换锚实测同口径）：既有**各接口**产物（含 codegen 产物）逐字节不变；三份**聚合**产物（`BindingRegistry.Bindings.h/.cpp`、`EngineApi.Bindings.cs`）允许变，但每处差异 MUST 逐处指认到门面编号 13 的登记行，指认不出即红；新锚**期望 221 条** = 218 + 3 份新增（门面 `.Bindings.cpp` / `.Bindings.cs` / 登记分片——正向接口每接口两份 + 一分片，GPUBuffer 先例实测；218 = 导入器批归档时的新锚，勿与那批封存的 213 旧锚混读），份数或文件集不符即红。
- 对外文档：`CSharpHost/README.md` 编号表加 13；`docs/testing.md` 清单加第 22 用例且四处「21」计数改 22；`Tools/CodeGen/README.md` 审计输出段改 14 接口 / 114 方法并加门面名（`verify_readme.py` 只跑 CodeGen.exe 开头的命令，覆盖不到审计段——用 grep 判据钉死，见 tasks 4.4）。

## Risks / Trade-offs

- [工厂队列错位（造了没取 / 取错对象）] → 主循环单线程 + 阶段末队列空断言；探针连造两类型验证 FIFO 顺序。
- [C# 格式写读漂移] → 写读同程序集同源；C++ 侧期望字节写死对照（参照物不从被测物推导）。
- [门面实例未登记（脚本关闭 / 初始化顺序）] → `requiresInstance=true`，`GetInterfaceApi` 返 0 时 C# 绑表失败点名（既有模式，Host.cs 渲染表同款）。
- [跨模块引用计数] → 无共享指针过界（D2）；注册表内部 shared_ptr 与反向批同口径，不新增跨模块控制块。
- [C# 资产 C++ 读不了] → 明示的取舍（Non-Goal）：将来 C++ 要消费时另立批对齐格式或建 C++ 类型；本批渲染消费全在 C# 侧经正向绑定，不触发该问题。
- [审计表在归档里被改] → 既有惯例（脚本要求唯一一份，前批同样追加）；追加主表九行 + 汇总一行并改合计 114，不动旧行。

## Migration Plan

纯增量：加头 / 加实现 / 加清单行 / 加 C# 文件 / 加用例。回滚 = 撤这些增量；无数据迁移（新格式资产文件只在用例临时目录产生）。基线两段式（D8）是硬判据，未指认差异即停。

## Open Questions

（无阻塞项。网格顶点布局本批钉死为场景着色器现用布局；多纹理格式 / 任意顶点布局随后续资产批扩。）