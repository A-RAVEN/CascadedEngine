# Design: csharp-reverse-interface-binding

> 取证：绑定生成器架构调查（子 agent def04a22，2026-11-22；这是调查 agent 标识非 git 锚）+ 两轮五面对抗验证（round1/round2，攻击 agent 实证，含直接调 check_bindings.py 函数）。本版按用户裁定**对称工厂架构** + round2 全部修复：反向产物与整条绑定管线的集成、所有权闭环、失败正确前提、Deserialize 排除、判据可观测。

## D1 反向投影机制：第五类形状"函数表 + 不透明 user"

C ABI 面现有四类（blittable/视图/句柄/单回调函数指针+user）。反向增**第五类**（独立类别，**不是第 4 类的扩展**——第 4 类仍是单回调）：一个 `void* user` + **一张函数表**（每虚方法投影出的函数指针，**一虚方法可多槽**如两段式 Len/Copy；对齐按"每**被绑定**虚方法的投影槽集合"逐条对，不按总数）。每条函数指针参数仍在前四类内。

**三件产物（往 bindemit::run 加反向分支）**：
- C++ 代理 `<I>Proxy : public <I>`：每**被绑定**纯虚方法体调对应函数指针槽（传 user + 投影参数）。普通 C++ 对象，shared_ptr 持有。
- C# 公开抽象基类：接口名去 I 前缀加 Base（IResource→ResourceBase、IResourceFactory→ResourceFactoryBase）。每**被绑定**虚方法对应 abstract 托管方法，干净托管形状，原生细节藏 internal 桥。**生成物落构建目录 codegen/bindings（经 CascadedBindingDir 编进程序集），MUST NOT 写回 Scripting/Managed/（该 csproj 明文不放生成绑定）**。
- 注册 glue：`<I>_CreateImpl(void* user, <每被绑定虚方法投影 fnptr>..., void (*releaseUser)(void*)) → 强句柄` + `<I>_ReleaseImpl(handle)`。

**复用**：回调注册表/GCHandle/句柄表/per-self 错误通道。**新增 ReverseKeepAliveCount：每实例一个 + 原生 getter**（C++ 可读，tester 经 hostfxr 调；MUST NOT 用全局静态——同进程其它反向夹具会拖漂；MUST NOT 复用窗口 LiveKeepAliveCount）。

## D2 所有权闭环：单一强所有者 + C# 借用句柄（round2 重设计）

**round2 否定的旧设计**：CreateImpl 返强句柄（句柄表持 shared_ptr）+ RegisterFactory 收裸指针（句柄丢）+ 资源又 shared_ptr(ptr,deleter) 独立所有 → 双重所有，计数永不归零或 double-free。

**修订——单一强所有者，C# 拿借用句柄**：
- **释放回调**：glue releaseUser(void* user) 指向托管桥释放方法（GCHandle.FromIntPtr(user).Free()）。代理析构调 releaseUser → 托管桥 Free（唯一 Free 在托管侧，C++ 不直接调 GCHandle.Free）。
- **跨模块约束（apply 期间发现，round3 修正）**：BindingHandleTable.h 是生成在头里的 inline 函数 + 函数级 static ⇒ **EXE 与 DLL 各有一份表**。反向代理由 CSharpHost.dll 的 __CreateImpl 创建、句柄登记在 CSharpHost 的表；tester.exe include 同一份产物解析到自己那份表，跨模块"反查重建 shared_ptr"走不通（shared_ptr 也不能跨模块传）。故注册 MUST 用**句柄 + 借用指针 + 释放回调**，不用 shared_ptr 重建。
- **工厂代理**：**单一强所有者是创建模块（CSharpHost）的句柄表**。反向 Api_v2 追加 ResolveImpl 槽（`void* (*ResolveImpl)(uint64_t handle)`，在创建模块里按句柄解析借用指针）。RegisterFactory(typeName, **handle, IResourceFactory* borrowed, int32_t (*release)(uint64_t)**)：注册表**保留句柄**（不丢）+ 持借用指针（生命周期随注册表）；注销/替换调 release(handle)（CSharpHost __ReleaseImpl 返 int32_t：0 成功/1001 已失效，与 C# Dispose 同口径，保可观测 → 掉强引用 → 代理析构 → releaseUser → Free）。GetFactory 返回借用裸指针。C# 工厂句柄是借用（kind 4）。**工厂代理由 CreateImpl 创建为强句柄（kind 1，句柄表持强引用）**，故跨模块注册用借用+释放回调（注册表在 tester.exe 不能持 CSharpHost.dll 代理的 shared_ptr）。
- **资源代理（与工厂不同）**：资源代理由 **CreateProxy 创建为借用句柄（kind 4，句柄表不持强引用）**，故**资源注册表是资源对象的单一强所有者**——RegisterResource(path, **shared_ptr<IResource>**)（注册表持 shared_ptr(ptr, delete)，**不用** handle+borrowed+release，那会造出第二所有者：kind 4 的 ReleaseImpl 只清表项不 delete → 泄漏）。代理析构时自己退役借用句柄。C# Dispose 释放借用句柄（报 1001），注册表仍持（ReverseKeepAliveCount 不归零、按路径仍取回同一实例）；ReleaseResource → 注册表 shared_ptr 释放 → 代理析构 → releaseUser → Free → 计数归零 + 退役借用句柄。
- **单一所有权不破**：工厂强引用在创建模块句柄表（注册表借用+释放回调）；资源强引用在资源注册表 shared_ptr（代理借用句柄）。各自单一所有者，无双重所有。
- **寿命判据可观测**（round2 修单向断言）：ReverseKeepAliveCount 每实例 + 原生 getter，断言精确差值；"Dispose 只掉借用一份"用**两个观测**钉死：Dispose 后同一句柄解析 MUST 报 1001（借用已释放）+ 按路径 MUST 仍取回同一实例（注册表仍持）。纯泄漏（Dispose 什么都不做）则句柄不报 1001 → 红。
- double-free 守卫：releaseUser Free 后置空；重复释放按代际拒；C# 重复 Dispose 幂等。

## D3 规格冲突：RENAMED + "不得解引用"限定原生层 + 五类措辞统一

csharp-bindable-surface「用户令牌不得解引用」主语限定**原生生成代码**；托管桥 GCHandle.FromIntPtr/.Free 是第 4 类既有语义。**RENAMED**：四类形状→五类形状、回调投影同时定义注册与保活→定义完整生命周期。**五类措辞统一**：反向是独立第五类，删除"第 4 类扩成函数表"表述；主规范其它引用"四类"处（如 :110）归档同步改五类。csharp-bindings 删"每虚方法一条函数指针"残留，统一"每被绑定虚方法投影出的函数指针（可多条）"。

## D4 失败处理：正确前提 + 可观测错误（round2 重设计）

**round2 否定的旧前提**："Serialize 失败 Write(0) 静默覆盖资源文件成空"——**实测不成立**：WriteBatch 不打开文件（IOManager.cpp），文件只在 SubmitAndWait 打开，空批 SubmitAndWait 直接 return，Write(0) 不入队。故 Write(0) 不会覆盖文件，"失败不空写文件字节"判据空真（缺陷态文件也不变）。

**修订——失败的可观测是错误上报，不是文件字节**：
- **线程契约**：桥检查注册线程不符 → 1005 落槽 + 非 void 槽返失败哨兵（Serialize len UINT64_MAX、Create nullptr）。
- **Serialize 失败**：C# 抛异常/非注册线程 → len 哨兵 → 代理中止（不 Write）+ **落错误槽**；可观测判据 = **错误经错误槽下次注册线程调用上报**（对应接口异常含反向标识 + 原始文本），MUST NOT 静默继续。文件字节不变是自然结果（空批不写），不作判据。
- **Create 失败用异常契约**（round2 修：C++ 原生工厂无 self/错误槽）：工厂 Create() 失败 MUST **抛异常**（C++ 原生工厂直接抛；C# 工厂异常经桥转码），MUST NOT 用"nullptr + 错误槽"（C++ 原生路径无错误槽对象）。系统在包 shared_ptr 与 RegisterResource 前 MUST 检查 nullptr/捕获异常，MUST NOT 注册空资源、MUST NOT 对 nullptr 解引用（现有加载路径 shared_ptr(newCallback()) 后直接 Deserialize 无 nullptr 检查会崩，本批补检查）。
- **"不注册空资源"可观测**：Create 失败后 HasResource(path)==false（或等价公开查询）；"第二次 Load 重新调 Create、计数 2"证明失败未缓存空对象。拆分"抛异常"与"返回 null"两场景。

## D5 IResourceFactory 对称可绑 + 工厂注册表

**C++ 基类**（新增 GeneralResources/header/CAResource/IResourceFactory.h）：GetResourceType()→string / Create()→IResource*（**无 ResourceManagingSystem 参数**，绕开不可绑协作方）/ 可选 GetSourceFilePostfix/GetDestFilePostfix。Create() 失败抛异常（D4）。IResource* 返回投影成目标句柄。

**C# 反向基类 ResourceFactoryBase**：abstract GetResourceType / abstract ResourceBase Create()。C# 工厂继承它；IResourceFactoryProxy 包 C# 工厂。代理 Create() 调 C# 工厂.Create() 得 ResourceBase → 包 IResourceProxy → 返回 IResource*。

**两种语言工厂**：C++ 原生继承 IResourceFactory；C# 继承 ResourceFactoryBase（反向代理）。

**工厂注册表**（新增）：RegisterFactory(typeName, **handle, borrowed 指针, release 回调**)（保留句柄 + 持借用指针，跨模块不用 shared_ptr 重建，见 D2）/ GetFactory(typeName)→借用裸指针 / 按类型构造。注册式多态取代编译期模板。tester 经 IScriptHost::GetInterfaceApiForTest(9/10) 取 **CSharpHost 的**表，resolve/release/keepalive 都走表里函数指针（不调 EXE 自己那份）。

**C++/C# 工厂对照判据钉死期望**（round2 修"行为一致"无读数）：两列写死期望对照表——GetResourceType 各自 ID（写死）、Create 返回有效（非空）、Serialize 字节（各自写死期望）、失败行为（都抛异常）。MUST NOT 用散文"行为一致"。

## D6 IResource 对称可绑（写路径）+ Deserialize 排除

C++ 基类 IResource + C# 反向基类 ResourceBase。**IResource.h 加 CA_BIND_SKIP(Deserialize, "本批不支持反向读，另立批")**——只绑定 Serialize 一个虚方法。故 ResourceBase 只 1 个 abstract（Serialize），对齐判据按"**被绑定**虚方法数"（IResource=1），不是全部纯虚数（2）。IResourceProxy::Deserialize 仍存在但抛"不支持反向读"（非绑定槽）。

**写路径字节同构 TResource**：两段式 serializeLen(user)→uint64（失败 UINT64_MAX）+ serializeCopy(user,dest,cap)→uint64 实际写入数。代理 Serialize(WBatch*)：len→失败中止落槽→分配→copy→Write(len,ptr)→SubmitAndWait。WBatch/IOBatch 不跨界。**字节判据钉死期望** {0xDE,0xAD,0xBE,0xEF}：读回==期望 且 长度==4 且 copy==4。

## D7 ResourceManagingSystem 集成（实现文件纠正）

**实现文件纠正**（round2）：ResourceManagingSystem 是抽象接口（.h），**实现类 ResourceManagingSystem_Impl 与私有 m_PathToResource（shared_dic<PathHash,shared_ptr<IResource>>）在 GeneralResources/private/ResourceImportingSystem.cpp**（无 ResourceManagingSystem.cpp）。本批改 ResourceManagingSystem.h（增 RegisterResource 声明）+ ResourceImportingSystem.cpp（ResourceManagingSystem_Impl 增 RegisterResource 实现）。

- 增 RegisterResource(path, **shared_ptr<IResource>**)：资源注册表是资源对象单一强所有者（m_PathToResource 持 shared_ptr）。资源代理由 CreateProxy 创建为借用句柄（kind 4），注册表 shared_ptr(ptr, delete) 持强引用（**不用** handle+borrowed+release——kind 4 的 ReleaseImpl 只清表项不 delete 会泄漏，见 D2）。代理析构退役借用句柄。
- **构造路径区分新建/加载**（round2 修 Deserialize 矛盾）：工厂新建 = 查工厂→Create()→shared_ptr（虚析构 deleter，**单一所有者**）→RegisterResource（**MUST NOT Deserialize**，新建对象无盘数据）；加载 = GetOrLoad→Deserialize（现有路径）。删构造路径末尾 Deserialize。
- GetOrNewResource<TRes> 模板保留（既有 ShaderImporter_Vulkan/ShaderImporter_D12 不动）。
- TResource 字节逻辑不变（ResourceBatchTest 红线）；IResource.h 加反向标注 + CA_BIND_SKIP 必然变。
- 头文件现还有两个非模板 protected 纯虚（接构造回调），"现只有模板"表述纠正为"公开面只有模板 + 两个 protected 纯虚，无外部对象注册路径"。

## D8 通道进宿主登记表 + 整条管线集成（round2 核心补全）

反向接口进宿主登记表 C# 才可达（现有 C# 经 EngineApiBindings.Tables+GetInterfaceApi 取表，无 DllImport）。**完整管线集成（round2 抓出多处遗漏）**：
- **build.py CODEGEN_BINDINGS** 增反向头（IResourceFactory/IResource）——登记分片只在 CODEGEN_BINDINGS 循环写，缺它报"登记输入没有产出分片"且反向基类不生成。
- **build.py CODEGEN_REGISTRIES** 宿主段增反向接口；**--update-ids** 发 interface-ids.lock 第 9/10 号（现宿主段 high water 8）。**tasks MUST 有宿主路由创建任务**（round2 抓出零命中）。
- **CSharpHost/private/CSharpHost.cpp kBindingPolicies** 增反向接口行（该文件明文"新增进主登记表的接口时这里加一行"）——缺它 ApplyPolicies 判 Unregistered、GetInterfaceApi(#9/#10) 返 0。
- **登记产物** BindingRegistry/EngineApi 因新号合法变化。
- **基线口径**：既有 8 接口正向产物逐字节不变；登记产物新号合法变化；换锚差异逐文件指认。

## D9 反向产物与检查管线集成（round2 核心补全）

反向产物（abstract 基类 + 代理）不符合正向产物形状（IDisposable 表面类 + Api_v2 函数表），现有四检查 + build.py 闸门会判红。**MUST 给反向产物显式类别 + 四检查反向感知（替代判据，非放松）**：
- **反向类别标记**：生成器给反向产物打类别标记（如产物内反向标志或独立反向清单），检查脚本据此区分正向/反向。
- **check_surface**：正向产物仍要 public sealed IDisposable 表面类（不变）；**反向产物改判 check_reverse_base**（public abstract 基类、干净托管形状、abstract 数=被绑定虚方法数）。改前/改后对照入档（反向产物从"IDisposable 表面"换成"abstract 基类干净"，是替代不是放松；正向判据不动）。
- **check_alignment**：反向产物对齐 abstract 基类（非表面类），按被绑定虚方法数。
- **check_stubs**：反向代理非空体用**行为判据**（函数指针槽真调托管桥，conformance 触发 C# 覆写），不用 object-> 结构判据。
- **check_shared_value_types**：反向产物是 .cs/.cpp 成对（基类.cs + 代理.cpp），按接口类处理（非共享值类型）。
- **check_bindings.py 增 --only 单检查入口**（round2：main 合并退出码无单检查开关，check_reverse_base 红绿无法独立跑）+ **check_reverse_base 报扫描命中数≥1**（避免空扫恒真）。
- **check_bindable_audit.py INTERFACES** 增反向接口（现硬编码 9 真实接口，夹具/反向不在内红态构造不出）+ 审计表（全仓唯一，archive/2026-10-18-csharp-bindable-surface/evidence/）增"谁实现"列 + 反向行。
- **build.py 期望产物闸门**认反向产物两件套（基类.cs + 代理.cpp）。

## D10 测试与文档

- **夹具分两层**：C++ 结构（虚方法对齐/签名/句柄 kind）CoreTests；C#→C++ 行为 tester（hostfxr；CoreTests 不加载 C#）。夹具进 interface-ids.lock 夹具段（不进宿主路由）。
- **tester TestCSharpResourceFactory**（第 20 用例，两后端）：C# 工厂注册→Create 造 C# 资源→写路径往返钉死期望 + C++ 原生工厂对照（钉死期望表）+ Create 失败抛异常不注册空（HasResource 读数）+ Serialize 失败错误上报 + Deserialize 拒 + 寿命两档（Dispose 借用句柄报 1001 + 按路径同一实例 + 计数不归零；注册表释放归零）。
- **文档**：Tools/CodeGen/README.md（反向投影 + 第五类形状 + releaseUser + 所有权单一所有者 + 失败异常契约 + 管线集成 + 示例，投影路径用名字）；CSharpHost/README.md（ResourceFactoryBase/ResourceBase + C++/C# 工厂示例 + 借用句柄 Dispose 语义 + Deserialize 不支持）。