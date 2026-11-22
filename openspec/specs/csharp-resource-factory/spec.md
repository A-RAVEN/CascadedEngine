# csharp-resource-factory Specification

## Purpose

对称工厂与资源：IResourceFactory / IResource 各配 C++ 与 C# 双基类，工厂注册表支持按类型名多态构造，ResourceManagingSystem.RegisterResource 打通写路径，失败以异常契约可观测。

## Requirements

### Requirement: 资源工厂接口必须 C++ 与 C# 双基类对称
生成器 MUST 为新增 C++ 基类 IResourceFactory（GetResourceType()→string / Create()→IResource* / 可选后缀）生成反向产物：C# 公开抽象基类 ResourceFactoryBase（abstract GetResourceType / abstract ResourceBase Create()）+ IResourceFactoryProxy + glue（含 releaseUser）。**Create() MUST NOT 收 ResourceManagingSystem 参数**（绕开不可绑协作方），失败 MUST 抛异常。C++ 类型 MUST 能原生继承 IResourceFactory 写工厂；C# 类型 MUST 能继承 ResourceFactoryBase 写工厂（反向代理包装）。生成基类落构建目录（MUST NOT 写回 Scripting/Managed/）。

#### Scenario: C++ 原生工厂与 C# 反向工厂都能注册并构造
- **WHEN** 分别注册 C++ 原生工厂与 C# 工厂（经句柄），各调 Create()
- **THEN** 两者 MUST 都返回有效资源（C++ 原生对象 / C# 资源代理）；ResourceFactoryBase 公开签名 MUST 干净（check_reverse_base 绿）

#### Scenario: Create 无 manager 参数
- **WHEN** 扫描 IResourceFactory 的 C ABI 槽签名
- **THEN** Create 槽 MUST NOT 出现 ResourceManagingSystem* 参数

### Requirement: 工厂注册表必须按类型多态构造且经句柄注册单一所有
工厂注册表 MUST 支持 RegisterFactory(typeName, **handle, IResourceFactory* borrowed, int32_t (*release)(uint64_t)**)（**保留句柄不丢** + 持借用指针；release 返 int32_t：0 成功/1001 已失效，与 C# Dispose 同口径保可观测；跨模块 MUST NOT 用 shared_ptr 重建——句柄表是头里 inline+函数级 static，EXE/DLL 各一份）/ GetFactory(typeName)→借用裸指针（生命周期随注册表）。**工厂代理由 CreateImpl 创建为强句柄（kind 1），单一强所有者是创建模块（CSharpHost）的句柄表**，注册表持借用指针不另持强引用，注销/替换调 release(handle)（→创建模块掉强引用→代理析构→releaseUser）。**资源代理不同**：由 CreateProxy 创建为借用句柄（kind 4），RegisterResource(path, shared_ptr<IResource>) 资源注册表是单一强所有者（MUST NOT 用 handle+borrowed+release，kind 4 的 ReleaseImpl 只清表项不 delete 会泄漏），代理析构退役借用句柄。反向 Api_v2 MUST 含 ResolveImpl 槽（创建模块里按句柄解析借用指针）。tester MUST 经 GetInterfaceApiForTest(9/10) 取 CSharpHost 的表（resolve/release/keepalive 走表里函数指针，不调 EXE 自己那份）。构造 MUST 走 GetFactory(type)→Create()→注册（运行时多态），MUST NOT 要求编译期模板。既有 GetOrNewResource<TRes> 模板 MUST 保留（ShaderImporter_Vulkan/ShaderImporter_D12 不动）。

#### Scenario: 按类型名运行时构造
- **WHEN** 注册工厂后按类型名（运行时字符串）构造
- **THEN** MUST 查到工厂调 Create() 造资源（不需编译期模板）；未注册类型 MUST 明确报错不静默返回空

#### Scenario: C++/C# 工厂对照钉死期望
- **WHEN** 对 C++ 原生工厂与 C# 工厂跑对照
- **THEN** MUST 按写死期望表逐项核：GetResourceType 各自 ID（写死）、Create 返回非空、Serialize 字节（各自写死期望）、失败都抛异常；MUST NOT 用散文"行为一致"（四处不同仍绿即缺陷）

### Requirement: 资源接口必须 C++ 与 C# 双基类对称且 Deserialize 显式排除
生成器 MUST 为 IResource 生成 C# 反向基类 ResourceBase + IResourceProxy + glue。**IResource.h MUST 加 CA_BIND_SKIP(Deserialize, "本批不支持反向读，另立批")**——只绑定 Serialize。ResourceBase MUST 只 1 个 abstract（byte[] Serialize()），对齐按**被绑定虚方法数**（IResource=1，MUST NOT 按全部纯虚数 2）。IResourceProxy::Deserialize 仍存在但抛"不支持反向读"（非绑定槽）。写路径 MUST 走两段式 serializeLen(user)→uint64（失败 UINT64_MAX）+ serializeCopy(user,dest,cap)→uint64 实际写入数；代理 Serialize(WBatch*) 取 len→失败中止落槽→分配→copy→Write→SubmitAndWait（与 TResource 同构）。WBatch/IOBatch MUST NOT 跨界。字节判据 MUST 钉死期望 {0xDE,0xAD,0xBE,0xEF}：读回==期望 且 长度==4 且 copy==4。

#### Scenario: 写字节往返钉死期望
- **WHEN** C# Serialize 返回 {0xDE,0xAD,0xBE,0xEF}，经代理写进 WBatch，C++ 读回
- **THEN** 读回 MUST 逐字节==期望 且 长度==4 且 copy==4；截短 MUST 喂不绿

#### Scenario: Deserialize 排除且 abstract 数对齐被绑定虚方法
- **WHEN** 检查 IResource.h 与 ResourceBase
- **THEN** IResource.h MUST 含 CA_BIND_SKIP(Deserialize,...)；ResourceBase MUST 只 1 个 abstract Serialize；IResourceProxy::Deserialize 调之 MUST 抛"不支持反向读"

### Requirement: ResourceManagingSystem 必须能注册工厂对象且构造路径区分新建加载
**实现文件**：ResourceManagingSystem 是抽象接口（.h），实现类 ResourceManagingSystem_Impl 与私有 m_PathToResource 在 **GeneralResources/private/ResourceImportingSystem.cpp**（无 ResourceManagingSystem.cpp）。本批 MUST 改 ResourceManagingSystem.h（增 RegisterResource 声明）+ ResourceImportingSystem.cpp（Impl 增 RegisterResource(path, shared_ptr<IResource>) 实现）。**构造路径区分**：工厂新建 = 查工厂→Create()→shared_ptr（虚析构 deleter，单一所有者）→RegisterResource（**MUST NOT Deserialize**，新建无盘数据）；加载 = GetOrLoad→Deserialize（现有）。资源代理强引用只在资源注册表，C# 资源句柄是借用。TResource 字节逻辑 MUST 不变（ResourceBatchTest 红线）；IResource.h 加标注+CA_BIND_SKIP 必然变。

#### Scenario: 工厂对象经 RegisterResource 注册取回同一实例
- **WHEN** 工厂 Create() 造资源经 RegisterResource(path,ptr) 注册，C++ 按路径取回
- **THEN** 取回 MUST 同一实例；Serialize 触发对应实现；新建路径 MUST NOT 调 Deserialize

#### Scenario: TResource 字节契约不变
- **WHEN** 跑 CoreTests ResourceBatchTest
- **THEN** 既有断言 MUST 全绿

### Requirement: 构造与写路径失败必须可观测不静默
工厂 Create() 失败 MUST 抛异常；系统 MUST 在包 shared_ptr 与 RegisterResource 前检查 nullptr/捕获异常，MUST NOT 注册空资源、MUST NOT 对 nullptr 解引用（现有加载路径无 nullptr 检查会崩，本批补）。"不注册空"可观测：HasResource(path)==false + 第二次 Load 重调 Create 计数 2。Serialize 失败（C# 异常/非注册线程）→ len 哨兵 → 代理中止 + 落错误槽；**可观测判据 = 错误经错误槽下次注册线程调用上报**（对应接口异常含反向标识+原始文本），MUST NOT 静默继续。**MUST NOT 用"文件字节不变"作失败判据**（实测空批 SubmitAndWait 直接 return 不开文件、Write(0) 不入队，文件本就不变，该判据空真）。

#### Scenario: Create 失败不注册空（HasResource 读数）
- **WHEN** 工厂 Create() 抛异常
- **THEN** HasResource(path) MUST ==false，第二次 Load MUST 重调 Create（计数 2）；系统 MUST NOT 崩（nullptr 检查）

#### Scenario: Serialize 失败错误上报
- **WHEN** C# Serialize 抛异常，下次注册线程公开调用
- **THEN** MUST 抛对应接口异常含反向标识+原始文本（错误槽上报）；MUST NOT 静默继续

### Requirement: 对称工厂消费方必须有可失败验收
GPUBackendTester 新用例 TestCSharpResourceFactory（第 20 用例，两后端）MUST 验：C# 工厂注册→Create 造 C# 资源→写路径往返钉死期望 + C++ 原生工厂对照（钉死期望表）+ Create 失败抛异常不注册空（HasResource）+ Serialize 失败错误上报 + Deserialize 拒 + 寿命两档（Dispose 借用句柄报 1001 + 按路径同一实例 + 计数不归零；注册表释放归零）。每条缺陷态变红。默认不激活既有 19 用例 MUST 与基线零差异。

#### Scenario: 消费方用例两后端全断言过
- **WHEN** 主循环两后端跑 TestCSharpResourceFactory
- **THEN** 各组断言 MUST 全过，日志存档；缺陷态（代理空体/泄漏/注册空/失败静默）对应变红
