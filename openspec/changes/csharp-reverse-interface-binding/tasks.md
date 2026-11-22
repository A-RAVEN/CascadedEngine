# Tasks: csharp-reverse-interface-binding

> 分工：构建（python build.py --config Debug）、CoreTests、GPUBackendTester（含交互/可见窗口）、截图视觉验收由主循环代跑；执行者做代码/测试/文档编辑与只读检查，需要读数时请求主循环。红绿纪律：缺陷态读数先于修复存档；构造不出的如实登记。**对称工厂架构 + 整条绑定管线反向集成。本批扩绑定面（反向进宿主登记表发 9/10）+ 改 ResourceManagingSystem.h/ResourceImportingSystem.cpp（Impl 增 RegisterResource）+ 四检查反向感知 + CSharpHost kBindingPolicies；既有 8 接口正向产物逐字节不变、登记产物新号合法变化、TResource 字节逻辑不变；走换锚程序。** C# 行为断言经 tester（hostfxr）；CoreTests 跑反向 C++ 结构夹具。审计表全仓唯一（在归档），编辑那份不新建。生成基类落构建目录（不写回 Scripting/Managed/）。

## 0. 开工清点

- [ ] 0.1 七项清点（命令+输出存档 evidence/0.1-清点.txt）：① bindemit::run 反向分支落点；② 回调注册表/GCHandle 机制引文（生成物 cawindow__IWindowSystem.Bindings.cpp/.cs）；③ IResource/WBatch/IOBatch 形状（IResource.h 2 纯虚、WBatch 含 void*/string、空批 SubmitAndWait 直接 return 不开文件 Write(0) 不入队——失败判据不能靠文件字节）；④ 检查管线口径（check_surface/check_alignment/check_stubs/check_shared_value_types 各自判据 + main 合并退出码无单检查开关；check_bindable_audit INTERFACES 硬编码 9 接口 + 全仓唯一审计表在 archive/2026-10-18-csharp-bindable-surface/evidence/）；⑤ 宿主通道（Host.cs GetInterfaceApi+EngineApiBindings.Tables 无 DllImport；build.py CODEGEN_BINDINGS 循环写登记分片 + CODEGEN_REGISTRIES；CSharpHost/private/CSharpHost.cpp kBindingPolicies"新增进主登记表接口这里加一行"；interface-ids.lock 宿主 high water 8）；⑥ 基线锚（Tools/CodeGen/check_binding_baseline.py --baseline/--product-dir，现锚 203 条）；⑦ ResourceManagingSystem 实现（ResourceManagingSystem_Impl + m_PathToResource 在 GeneralResources/private/ResourceImportingSystem.cpp，无 .cpp；.h 公开模板 + 两 protected 纯虚）。验收：七项输出齐备；与 design 不符即修订并记录。
- [ ] 0.2 封存旧基线锚（主循环）：当前基线清单整份封存 evidence/旧锚封存件.txt。验收：含全部既有产物哈希；新锚以此为旧锚。

## 1. 反向投影生成器（第五类形状 + 三件产物 + 所有权）

- [ ] 1.1 反向标注与识别：CodeGenMain.cpp 增反向标注（CA_BIND_REVERSE）+ CA_BIND_SKIP 支持（IResource.Deserialize 用）+ bindemit::run 反向分支。验收：反向夹具识别为反向；CA_BIND_SKIP 方法不生成槽；未标注接口不处理；带编号标注仍拒。
- [ ] 1.2 C++ 代理类生成：`<I>Proxy : public <I>`，每被绑定纯虚方法调投影函数指针槽（一虚方法可多槽）。验收：代理被绑定虚方法数与接口对齐（跳过 CA_BIND_SKIP）；C ABI 签名无容器/模板/智能指针；非空体用行为判据（3.1）。
- [ ] 1.3 C# 抽象基类生成：去 I 前缀加 Base，每被绑定虚方法 abstract 托管方法；**生成物落构建目录 codegen/bindings**（不写回 Scripting/Managed/）。验收：abstract 数=被绑定虚方法数；公开基类干净（check_reverse_base 绿）；产物在构建目录经 CascadedBindingDir 编入。
- [ ] 1.4 glue + releaseUser + 单一所有者借用句柄：CreateImpl(user, 每被绑定虚方法 fnptr..., releaseUser)→强句柄 + ReleaseImpl；代理析构调 releaseUser→托管桥 GCHandle.Free（唯一 Free 托管侧）；**代理强引用只在注册表，句柄表对代理是借用项（kind 4，不另持强引用）**；ReverseKeepAliveCount 每实例 + 原生 getter。验收：注册表持代理计数 1；C# Dispose 借用句柄报 1001 + 按路径仍同一实例 + 计数不归零（两观测钉死，纯泄漏红）；注册表释放计数 1→0（原生 getter 精确）；重复 Dispose 幂等。
- [ ] 1.5 线程 + 失败哨兵 + 异常契约：桥检查注册线程（1005 落槽 + 非 void 槽失败哨兵 len UINT64_MAX/Create nullptr）；工厂 Create 失败抛异常（C++ 原生直接抛、C# 经桥转码，不用 nullptr+错误槽）；C# 异常转反向错误码不跨界（错误表+ERROR_CODE_TABLE+README 同步）。验收：非注册线程 Serialize→len 哨兵+中止+覆写计数 0+下次上报 1005；Create 抛异常进程不崩+后续公开调用抛对应异常含反向标识。

## 2. 反向产物与检查管线集成（替代判据非放松）

- [ ] 2.1 反向类别标记 + 四检查反向感知：生成器给反向产物打类别标记；check_surface 反向产物改判 check_reverse_base（正向仍 IDisposable 不变）、check_alignment 反向对齐 abstract 基类、check_stubs 反向行为判据、check_shared_value_types 反向按 .cs/.cpp 成对接口类。验收：反向产物在 main 全检查下干净态绿（四检查不误红）；正向产物判据不变；改前/改后对照入档（替代非放松）。
- [ ] 2.2 check_reverse_base + --only + 命中数：check_bindings.py 增 check_reverse_base（递归 glob 扫反向抽象基类公开签名 void*/delegate*/原生句柄判红）+ --only 单检查入口 + 报扫描命中数≥1。验收：--only check_reverse_base 干净绿（命中≥1）、塞 public void* 红点名（红绿存档，子目录扫到，空扫不恒真）。
- [ ] 2.3 审计 INTERFACES + 谁实现列：check_bindable_audit.py INTERFACES 增反向接口；编辑归档审计表（archive/2026-10-18-csharp-bindable-surface/evidence/bindable-audit.md）增"谁实现"列 + 反向行。验收：反向接口加被绑定虚方法不补行脚本非零退出点名；全仓仍恰好一份审计表（不新建）。
- [ ] 2.4 build.py 期望产物闸门认反向两件套：build.py 期望产物检查认反向产物（基类.cs + 代理.cpp）。验收：反向产物缺失闸门点名；齐全绿。

## 3. 反向投影夹具红绿

- [ ] 3.1 反向 conformance 夹具（结构 CoreTests / 行为 tester）：投影矩阵增反向行（名字标识）；夹具进 interface-ids.lock 夹具段（不进宿主路由）。结构断言 CoreTests；行为六条（C# 经代理被调收返回值/替换 supersede/释放失效/非注册线程拒/异常转码/double-free 守卫）tester。验收：六条全过；代理空体/注销保留保活项/无守卫对应断言非零退出（红绿存档）。

## 4. IResourceFactory 对称（C++/C# 双基类 + 工厂注册表）

- [ ] 4.1 C++ 基类 IResourceFactory：新增 GeneralResources/header/CAResource/IResourceFactory.h（GetResourceType/Create()→IResource* 无 manager 参数/可选后缀，Create 失败抛异常）。验收：头存在签名符合；Create 槽无 manager 参数。
- [ ] 4.2 C# 反向基类 ResourceFactoryBase + 代理：生成反向产物（ResourceFactoryBase abstract GetResourceType/Create()→ResourceBase + IResourceFactoryProxy + glue）；代理 Create() 调 C# 工厂.Create()→ResourceBase→包 IResourceProxy→返回 IResource*。验收：C# 工厂继承注册后 Create 造 C# 资源代理；ResourceFactoryBase 公开干净。
- [ ] 4.3 工厂注册表（句柄注册单一所有）+ C++/C# 对照：RegisterFactory(typeName, 句柄)（反查重建 shared_ptr，不收裸指针）/GetFactory；C++ 原生工厂与 C# 工厂都注册。验收：按类型名运行时构造（不需模板）；未注册类型明确报错；**C++/C# 对照按写死期望表逐项核**（GetResourceType 各自 ID/Create 非空/Serialize 字节各自期望/失败都抛异常），四处不同即红。

## 5. IResource 对称（写路径 + Deserialize 排除）

- [ ] 5.1 ResourceBase + IResourceProxy + Deserialize 排除：生成反向产物（ResourceBase abstract byte[] Serialize() + IResourceProxy + glue）；IResource.h 加 CA_BIND_REVERSE + CA_BIND_SKIP(Deserialize,"本批不支持反向读")。验收：ResourceBase 只 1 个 abstract Serialize（被绑定虚方法数 1）；IResourceProxy::Deserialize 抛"不支持反向读"；ResourceBase 公开干净。
- [ ] 5.2 写路径两段式 + 钉死期望：serializeLen(user)→uint64（失败 UINT64_MAX）+ serializeCopy(user,dest,cap)→uint64 实际写入；代理 Serialize 取 len→失败中止落槽→分配→copy→Write→SubmitAndWait；批不跨界。验收：C# 返回 {0xDE,0xAD,0xBE,0xEF}，读回==期望 且 长度==4 且 copy==4（截短喂不绿）；C ABI 槽无 WBatch*/IOBatch*。
- [ ] 5.3 Serialize 失败错误上报（非文件字节）：C# Serialize 异常/非注册线程→len 哨兵→代理中止+落槽。验收：下次注册线程公开调用抛对应接口异常含反向标识+原始文本（错误槽上报）；MUST NOT 用文件字节判据（空批本就不写文件，空真）。

## 6. ResourceManagingSystem 集成（实现文件纠正）

- [ ] 6.1 RegisterResource（.h 声明 + ResourceImportingSystem.cpp Impl 实现）：ResourceManagingSystem.h 增 RegisterResource(path, shared_ptr<IResource>) 声明；GeneralResources/private/ResourceImportingSystem.cpp 的 ResourceManagingSystem_Impl 增实现（注册进 m_PathToResource）。验收：工厂对象经 RegisterResource 注册，C++ 按路径取回同一实例；**改的是 ResourceImportingSystem.cpp（无 ResourceManagingSystem.cpp）**。
- [ ] 6.2 构造区分新建加载 + 寿命两档 + TResource 不变：工厂新建=查工厂→Create→shared_ptr（单一所有者）→RegisterResource（不 Deserialize）；加载=GetOrLoad→Deserialize；GetOrNewResource 模板保留（ShaderImporter_Vulkan/_D12 不动）；资源代理强引用只在资源注册表，C# 资源句柄借用。验收：新建路径不调 Deserialize；C# Dispose 借用报 1001+按路径同一实例+计数不归零、注册表释放归零；CoreTests ResourceBatchTest 全绿（TResource 未改）。

## 7. 宿主路由通道（C# 可达）

- [ ] 7.1 build.py CODEGEN_BINDINGS + CODEGEN_REGISTRIES + --update-ids：build.py CODEGEN_BINDINGS 增反向头（IResourceFactory/IResource，否则不产产物+登记分片报错）；CODEGEN_REGISTRIES 宿主段增反向；--update-ids 发 interface-ids.lock 第 9/10 号（既有行不动）。验收：反向产物生成；interface-ids.lock 含 9/10 号；登记分片产出。
- [ ] 7.2 CSharpHost kBindingPolicies + 登记产物：CSharpHost/private/CSharpHost.cpp kBindingPolicies 增反向接口行（否则 ApplyPolicies 判 Unregistered、GetInterfaceApi 返 0）；BindingRegistry/EngineApi 产物因新号变化。验收：C# 经 GetInterfaceApi(#9/#10) 真实取到反向函数表调 CreateImpl（非返 0）；登记产物含新号路由。

## 8. tester 用例 + 回归 + 基线换锚

- [ ] 8.1 tester TestCSharpResourceFactory（第 20 用例，两后端）：C# 工厂注册→Create 造 C# 资源→写路径往返钉死期望 + C++ 原生工厂对照（钉死期望表）+ Create 失败抛异常不注册空（HasResource）+ Serialize 失败错误上报 + Deserialize 拒 + 寿命两档。注册进 tester 清单（--list 20 行）。验收：主循环两后端跑该用例各组断言全过，日志存档；缺陷态对应变红。
- [ ] 8.2 全量回归：构建绿（含四检查反向感知 + 闸门）；CoreTests 全绿（反向结构夹具 + ResourceBatchTest）；两后端 20/20；默认不激活既有 19 用例 name/status 与改前基线（19 条）一致、无反向副作用。验收：改前基线 + 改后 result.json 与日志存档。
- [ ] 8.3 基线换锚（主循环）：新产物全集重取哈希入新锚，Tools/CodeGen/check_binding_baseline.py --baseline 新锚 --product-dir 产物目录；差异逐文件指认。验收：旧锚封存件 + 新锚；既有 8 接口正向产物逐份哈希相同；BindingRegistry/EngineApi 差异指认到新号；新增反向产物指认到反向投影；指认不了的差异收口红。

## 9. 文档

- [ ] 9.1 Tools/CodeGen/README.md：反向投影（第五类形状/代理/基类/glue/releaseUser/单一所有者借用句柄/失败异常契约/管线集成/退出码/示例+真实输出）+ 投影矩阵增反向行（名字）+ 错误表增反向标识。验收：verify_readme.py 绿；示例可复制运行。
- [ ] 9.2 CSharpHost/README.md：ResourceFactoryBase/ResourceBase + C++/C# 工厂示例 + 工厂注册 + 借用句柄 Dispose 语义 + Deserialize 不支持。验收：check_csharp_host_readme.py 问题 0 处；示例可复制运行。

## 修订记录

（执行期间对 proposal/design/specs/tasks 的任何修订，逐条记在此处：改前原文 / 改后原文 / 依据 / 谁裁定。）

### 2026-11-22 round2 对抗验证裁决处置（五并行攻击面 + 主循环复核；管线集成/所有权/失败前提重设计）

round2 五面（真实存在性4/无中生有4/验收可失败性4+2旁证/工件一致性4/影响面4），攻击 agent 实证（直接调 check_bindings.py 函数）。去重处置：

**管线集成（round2 核心，新增 design D9 + tasks 2/7）**
1. 反向产物与四检查+build.py 闸门不兼容（验收001/影响002/真实003）：改前——只说"新增 check_reverse_base 不复用 check_surface"，无豁免任务，照做 build.py 必红（check_surface 要 IDisposable/check_alignment 要 1 表面类/check_stubs 要 Api_v2/check_shared_value_types 要成对，反向 abstract 基类全触发）；改后——反向类别标记 + 四检查反向感知替代判据（正向不动，改前/改后对照）+ --only 单检查 + 命中数≥1 + build.py 闸门认反向两件套。依据：攻击 agent 实测四检查全红、main 合并退出码无单检查开关。
2. 宿主路由无任务 + CODEGEN_BINDINGS 缺 + CSharpHost 策略漏（工件004/影响001/验收旁证②）：改前——Impact 只写 CODEGEN_REGISTRIES，tasks 无 update-ids/路由任务；改后——tasks 7.1/7.2 补 CODEGEN_BINDINGS 增反向头（否则不产产物+登记分片报错）+ CODEGEN_REGISTRIES + --update-ids 9/10 + CSharpHost.cpp kBindingPolicies 增行（否则 GetInterfaceApi 返 0）。依据：build.py 登记分片只在 CODEGEN_BINDINGS 循环写；CSharpHost.cpp 明文"新增进主登记表接口这里加一行"。
3. 审计 INTERFACES 硬编码（验收旁证①）：改前——夹具加虚方法红态构造不出（INTERFACES 硬编码 9 真实接口）；改后——INTERFACES 增反向接口 + 审计表增谁实现列（tasks 2.3）。

**所有权/失败重设计**
4. 所有权双重所有/句柄丢（影响004）：改前——CreateImpl 返强句柄+RegisterFactory 收裸指针丢句柄+资源 shared_ptr 独立所有→计数永不归零或 double-free；改后——单一强所有者（注册表持代理 shared_ptr），句柄表对代理借用项（kind 4 不另持强引用），RegisterFactory 收句柄反查重建，C# 句柄借用（design D2）。
5. Serialize 失败前提错（影响003）：改前——"Write(0) 静默覆盖文件成空"+文件字节判据；改后——实测空批 SubmitAndWait 直接 return 不开文件、Write(0) 不入队，文件本就不变（判据空真）；失败可观测改为错误槽上报（design D4/tasks 5.3）。依据：IOManager.cpp 空批 return/Write(0) 不入队实证。
6. Create 失败无错误槽对象（验收004）：改前——nullptr+错误槽（C++ 原生工厂无 self/错误槽）+现有路径 nullptr 直接解引用崩；改后——Create 失败抛异常契约 + 包 shared_ptr/RegisterResource 前 nullptr 检查 + HasResource 可观测 + 拆抛异常/返回 null 两场景（design D4/tasks 1.5/4.3）。
7. 寿命单向断言+计数语义未定义（验收002）：改前——"Dispose 不归零"纯泄漏也绿、计数全局/每实例未定义、无原生读入口；改后——ReverseKeepAliveCount 每实例+原生 getter，"Dispose 掉借用一份"用句柄报 1001+按路径同一实例两观测钉死（design D2/tasks 1.4）。

**判据/排除/一致性**
8. Deserialize 三判据互斥（无中003/工件003/真实002）：改前——每虚方法调槽×Deserialize 必抛×check_surface 表面成员数=纯虚数，无排除机制；构造路径末尾 Deserialize 与"反向 Deserialize 必抛"矛盾；改后——IResource.h CA_BIND_SKIP(Deserialize) + 判据按被绑定虚方法数（ResourceBase 1 abstract）+ 构造路径区分新建（不 Deserialize）/加载（Deserialize）（design D6/D7）。
9. 工厂"行为一致"无读数（验收003）：改前——散文"行为一致"四处不同仍绿；改后——C++/C# 对照写死期望表逐项核（design D5/tasks 4.3）。
10. 第五类措辞矛盾（工件001/002）：改前——"第4类扩成函数表"与"独立第五类"互斥、csharp-bindings 残留"每虚方法一条"；改后——统一独立第五类（第4类仍单回调）+ "每被绑定虚方法投影出的函数指针（可多条）"。
11. 文件引用错（真实001/无中002/影响001）：改前——ResourceManagingSystem.cpp（不存在）；改后——ResourceManagingSystem.h + GeneralResources/private/ResourceImportingSystem.cpp（ResourceManagingSystem_Impl + m_PathToResource）；"只有模板"纠正为"公开模板+两 protected 纯虚"。
12. 生成基类落点矛盾（无中004）：改前——Scripting/Managed/（csproj 明文不放生成绑定）；改后——构建目录 codegen/bindings 经 CascadedBindingDir 编入。
13. ShaderImporter 命名（真实004）：改前——ShaderImporter（0 命中）；改后——ShaderImporter_Vulkan/ShaderImporter_D12。
14. def04a22 锚点（无中001）：澄清是调查子 agent 标识非 git 锚。
15. 主规范 :110"四类"未同步（工件附加）：归档时同步改五类（design D3 注明）。

**核过未成立（不改）**：RENAMED 两对 FROM 主规范逐字命中、csharp-bindings MODIFIED 标题对上、能力目录正确（旧 managed-resource 已删）、新能力每需求≥1 Scenario、命名一致、无零改动残留；1005/1003/1004 在错误表、--list 19、宿主 high water 8、ReverseKeepAliveCount 新增、句柄表 shared_ptr<void> strong 真实。