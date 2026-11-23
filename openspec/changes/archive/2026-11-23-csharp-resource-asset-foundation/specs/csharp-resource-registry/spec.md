## ADDED Requirements

### Requirement: 注册表窄面必须是宿主层门面且核心零改动

C# 可调的注册表入口 MUST 是宿主层门面接口 `cs_script::IResourceRegistry`（头文件 `Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`，实现在 CSharpHost），经模块管理器取 `ResourceManagingSystem` 委托。门面方法 MUST 只使用正向生成器已实现的形状（标量 / `castl::string` 入参与返回 / void）；`ResourceManagingSystem` 与 GeneralResources 模块 MUST 零改动；生成器改动 MUST 仅限两处受控扩展：① 门面专属错误码 1014（沿用 1012/1013 作用域先例，只发射进门面产物）；② 字符串两段式返回带参数（零参数方法生成字节与旧实现逐字相同，不支持的参数形状生成期点名拒绝）；既有**各接口**绑定产物 MUST 逐字节不变；三份聚合登记产物（`BindingRegistry.Bindings.h/.cpp`、`EngineApi.Bindings.cs`）允许变化，但每处差异 MUST 能指认到门面编号 13 的登记行。门面 MUST NOT 持有资源对象的第二份强引用——注册表仍是单一强所有者。

#### Scenario: 既有各接口产物逐字节不变且聚合差异可指认

- **WHEN** 本批接线完成后重跑生成，用封存的旧锚（218 条，即导入器批归档时的新锚）逐字节比对
- **THEN** 旧锚覆盖的**各接口**产物 MUST 逐字节相同；三份聚合产物的每处差异 MUST 逐处指认到门面编号 13 的登记行，指认不出即红；新锚 MUST 为 221 条（218 + 3 份新增：门面 `.Bindings.cpp` / `.Bindings.cs` / 登记分片），份数或文件集不符即红；比对脚本 MUST 能因任何一份未指认差异而非零退出

#### Scenario: 门面方法全落在已实现形状

- **WHEN** 对 `IResourceRegistry.h` 跑绑定生成与 `check_bindable_audit.py`
- **THEN** 生成 MUST exit 0 且无 `CA_BIND_SKIP`；审计表 MUST 有九行（每方法一行），投影形状列只出现已实现形状，错误通道列逐行写明

### Requirement: 工厂注册必须经句柄解析且单一所有权不破

门面 `RegisterFactory(typeName, factoryHandle)` MUST 经生成登记表取 `IResourceFactory` 反向函数表，用 `ResolveImpl(handle)` 拿借用指针后委托注册表既有句柄入口 `RegisterFactory(typeName, handle, ptr, ReleaseImpl)`。句柄 MUST NOT 丢；注销 / 替换 MUST 经 `release(handle)` 放掉创建模块的强引用。反向函数表取不到 MUST 经错误通道 1014 点名「反向工厂接口未登记」，句柄解析为空 MUST 经错误通道 1001 点名句柄无效，MUST NOT 登记空工厂。**注册成功后强句柄的处置权归注册表**：C# MUST NOT 在注册期间对同一句柄再调 `Release`（那会让注册表持悬空借用指针）；放掉强引用只经注销 / 替换路径。

#### Scenario: C# 自助注册工厂并构造

- **WHEN** C# 经 `IResourceFactoryBindings.Create` 造工厂拿强句柄，调门面 `RegisterFactory`，再调 `CreateResource`
- **THEN** `HasFactory` MUST 为 true；工厂 `Create` 计数 MUST +1；造出的对象 MUST 是 C# 资源（代理包装，写路径走反向 Serialize）

#### Scenario: 坏句柄被点名拒绝

- **WHEN** 用 0 句柄、已释放句柄或代际不符句柄调门面 `RegisterFactory`
- **THEN** MUST 经错误通道 1001 上报且文本点名句柄无效，C# 表面 MUST 抛含该文本的 `IResourceRegistryException`；`HasFactory` MUST 仍为 false；注册表 MUST 无空记录

#### Scenario: 替换工厂旧代理退役

- **WHEN** 同类型名注册第二个工厂后，对第一个工厂的旧句柄调反向表 `ReleaseImpl`
- **THEN** MUST 返回代际失效码 1001（旧强引用已在替换时放掉）；新工厂 MUST 仍可构造

#### Scenario: 注销工厂放掉强引用

- **WHEN** 调门面 `UnregisterFactory(typeName)` 后读 `HasFactory`，并对该工厂旧句柄调反向表 `ReleaseImpl`
- **THEN** `HasFactory` MUST 为 false；`ReleaseImpl` MUST 返回 1001（强引用已经注销路径放掉，代理已析构）

### Requirement: 资源新建查询释放必须委托注册表所有权

门面 `CreateResource(typeName, path)` MUST 委托 `ResourceManagingSystem::CreateResource`（查工厂 → Create → shared_ptr(ptr, delete) → 注册），MUST NOT 在门面另持强引用或另建对象表。**同路径已有条目时门面 MUST 先拒绝**（经错误通道 1014 点名「路径已存在，先释放再新建」）——注册表 get_or_create 留旧弃新，门面 void 返回若静默放行，新对象析构而 C# 队列拿到孤儿，落盘写旧字节还不报错。`HasResource` / `ReleaseResource` MUST 与注册表读数一致；释放 MUST 放掉唯一强引用（反向代理析构 → releaseUser → GCHandle.Free）。新建路径 MUST NOT 调 Deserialize。

#### Scenario: 新建释放读数一致

- **WHEN** C# 调 `CreateResource` 后读 `HasResource`，再调 `ReleaseResource` 后再读
- **THEN** 两次读数 MUST 分别为 true 与 false；释放后对旧资源借用句柄调 `ReleaseImpl` MUST 返回 1001

#### Scenario: 构造失败不注册空

- **WHEN** 用未注册类型名调 `CreateResource`，或工厂 Create 抛异常 / 返回 null
- **THEN** MUST 经错误通道 1014 上报且文本点名类型名或原因，C# 表面 MUST 抛含该文本的异常；`HasResource` MUST 为 false；注册表 MUST 无空条目

#### Scenario: 同路径直接新建被显式拒绝

- **WHEN** 路径已有注册条目时再调门面 `CreateResource`（不经释放）
- **THEN** MUST 经错误通道 1014 上报且文本点名「路径已存在」；注册表 MUST 仍是旧条目（旧对象不被析构、字节不变）；MUST NOT 出现「C# 拿到新对象但注册表是旧对象」的静默错位；先 `ReleaseResource` 再新建 MUST 成功（替换正路）

### Requirement: 门面失败必须可观测且异常不跨边界

门面每个方法的失败 MUST 经错误码 + 文本可观测：正向普通方法的生成 thunk 无 try/catch（实测），故门面**实现 MUST 自行捕获**委托抛出的异常（`resource_creation_error` / `ca_io::io_error` / `ReverseBindingError`），写生成错误通道（句柄类失败用既有 1001，其余操作失败用门面专属 1014）；C# 包装经既有 `ThrowIfFailed` MUST 抛含错误码与原始文本的 `IResourceRegistryException`。C++ 异常 MUST NOT 穿过 C ABI 边界。无效输入 MUST 在边界被拒，MUST NOT 进入实现后崩溃或静默成功。

#### Scenario: 未注册类型新建被点名

- **WHEN** C# 用不存在的类型名调 `CreateResource`
- **THEN** C# MUST 收到 `IResourceRegistryException`，Code MUST 为 1014，文本 MUST 含该类型名；进程 MUST 不崩溃；后续合法调用 MUST 仍可用

#### Scenario: 1014 只发射进门面产物

- **WHEN** 生成后检查各正向产物的错误码常量与全局对账
- **THEN** 门面产物 MUST 含 `ERROR_RESOURCE_OPERATION_REJECTED = 1014`；既有正向产物 MUST 不含 1014 且与旧锚逐字节相同；`check_bindings.py` 的 `ERROR_CODE_TABLE` 与 `Tools/CodeGen/README.md` 错误码表 MUST 都含 1014（三处同步，缺一即对账红）

#### Scenario: 字符串两段式返回带参数

- **WHEN** 对「返回 `castl::string` 且带参数」的门面方法（`GetResourceFullPath(path)`）生成绑定
- **THEN** Len/Copy 伴生 MUST 带方法参数（C++ 签名 / 表槽 / C# delegate / 调用壳一致），thunk MUST 重建实参传给实现，编译 MUST 过；不支持的参数形状 MUST 生成期点名拒绝；零参数方法的生成字节 MUST 与旧实现逐字相同（既有产物逐字节不变）

#### Scenario: 落盘时反向 Serialize 抛异常两段式可上报

- **WHEN** C# 资源的 `Serialize()` 故意抛异常，调门面 `SerializeAll`（首次），再触发一次经同一资源代理的调用（第二次 `SerializeAll`）
- **THEN** 首次：该资源落盘 MUST 中止（文件字节保持写前状态，其余资源照常写），错误 MUST 落代理槽（既有反向错误语义，本批不改机制）；第二次：代理 MUST 按既有反向错误语义抛 `ReverseBindingError`（1012 标识 + 原始异常文本），门面捕获后转错误通道 1014，文本 MUST 保留 1012 标识与原始异常文本，C# 侧 MUST 收到含该文本的 `IResourceRegistryException`；用例 MUST 两段都断言（只断言第二段而放任首次落盘缺席不核 = 判据不合格）

### Requirement: 路径查询与全量落盘必须可用

门面 `GetResourceRootPath` / `GetResourceFullPath` MUST 返回注册表当前根路径与拼接结果（与 C++ 直调读数一致）；`SerializeAll` MUST 无条件全量落盘（与导入器批确立的规矩一致），落盘字节 MUST 等于 C# `Serialize()` 返回的字节。

#### Scenario: 全路径与根路径一致

- **WHEN** C++ 侧设定根路径后，C# 调 `GetResourceRootPath` 与 `GetResourceFullPath("a/b.tex")`
- **THEN** 两读数 MUST 与 C++ 直调 `ResourceManagingSystem` 的结果逐字相同

#### Scenario: 落盘字节等于 Serialize 返回

- **WHEN** C# 资源填好数据后调 `SerializeAll`，C++ 侧读对应文件
- **THEN** 文件字节 MUST 与该资源 `Serialize()` 返回的字节逐字节相同（期望值写死对照，不从被测物推导）

### Requirement: 门面接线必须完整且缺件能红

门面 MUST 完成全部接线：`build.py` 绑定/登记清单行、`kBindingPolicies` 行（要求默认实例）、`interface-ids.lock` host 段新编号 13（既有编号不变）、审计表主表九行 + 汇总表一行 + 合计 114、`check_bindable_audit.py` 的 `INTERFACES` 表加门面一项、`CSharpHost/CMakeLists.txt` include 目录加 ScriptHost 头目录（产物按基名找头）、宿主实例注册（注册点不在渲染后端缺失 early-return 之后）。任一缺件 MUST 有可观测失败，MUST NOT 静默降级。

#### Scenario: 审计脚本接口表未同步时僵尸行红

- **WHEN** 审计表追加了门面九行，但 `check_bindable_audit.py` 的 `INTERFACES` 未加 `cs_script::IResourceRegistry`
- **THEN** 脚本 MUST 非零退出并把九行判为僵尸行点名；同步后 MUST exit 0（接口数 13→14、方法对账数 105→114，实测基线）

#### Scenario: 缺登记行时取表失败点名

- **WHEN** 去掉 `kBindingPolicies` 的门面行后启动，C# 取门面函数表
- **THEN** `GetInterfaceApi` MUST 返 0；C# 绑表 MUST 失败且错误文本点名编号未登记（与既有反向接口缺行同款判据）

#### Scenario: 编号锁高水位与既有编号

- **WHEN** 跑 `--update-ids` 后读 `interface-ids.lock`
- **THEN** host 段高水位 MUST 为 13，门面 MUST 占 13；1–12 的既有映射 MUST 逐行不变；fixture 段 MUST 不动