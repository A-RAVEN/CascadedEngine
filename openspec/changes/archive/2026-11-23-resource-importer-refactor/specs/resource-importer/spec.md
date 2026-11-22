# resource-importer 增量

## ADDED Requirements

### Requirement: 旧 1:1 导入器 Base 必须退役（C++ 源码范围零命中）
MUST 删 ResourceImporterBase、ResourceImporter<TRes> 模板、AddImporter(Base*)、后缀扫描循环与 m_Importers/m_PostfixToImporterIndex/m_ReservedSpace/m_ImportingResources。ResourceImporterFree→ResourceImporter。退役判据零命中范围 MUST 限 C++ 源码（GeneralResources/header+private、两后端 ShaderLibrary 目录），MUST NOT 用全仓（工件/生成 C# 同名恒红）；模板判据 MUST 折行感知（不用 ResourceImporter<）。

#### Scenario: C++ 源码退役零命中
- **WHEN** 在 C++ 源码范围 grep ResourceImporterBase 与折行感知的模板声明
- **THEN** 命中 MUST 0；ResourceImporter 接口存在

### Requirement: 导入器接口必须拉取式全落现有形状且纯转换不注册
ResourceImporter MUST 为反向接口：GetTags/GetSourceDirectory/GetDestDirectory（string 返、无参）+ Produce()→ResourceImporterProductions*（返回反向接口裸指针）。ResourceImporterProductions MUST 为反向接口：HasNext()→bool（标量返）+ NextPath()→string + NextResource()→IResource*（返回反向接口裸指针）。**MUST NOT 用 string 形参/cafs::path/引用/shared_ptr 形参/push 式 IImportOutput**（反向形参只支持标量，现有四形状内）。导入器 MUST NOT 调注册表（grep 五写入口 RegisterResource/ReleaseResource/CreateResource/GetOrLoadResource/GetOrNewResource MUST 0）。

#### Scenario: 拉取式形状可绑且导入器不注册
- **WHEN** 生成 ResourceImporter/ResourceImporterProductions 反向产物 + grep 导入器写注册表入口
- **THEN** 生成 MUST rejected=0（全标量参/string 返/返回反向裸指针）；导入器五入口命中 MUST 0

### Requirement: 产出所有权必须复用 CreateResource 单一强所有者且新对象走 CreateProxy
管线拉取 NextResource() 得 IResource* 后 MUST 包 shared_ptr<IResource>(ptr,delete)→RegisterResource（与 CreateResource 同构，注册表单一强所有者；C# 反向代理析构 releaseUser→GCHandle.Free）。C# 导入器 NextResource 返回新 C# 资源 MUST 经**返回反向接口裸指针的现有投影 CreateProxy**（新对象尚无代理/借用句柄，MUST NOT 用 ResolveImpl）。MUST NOT 发明"借用+释放回调作 deleter"thunk。

#### Scenario: 释放后计数归零无泄漏
- **WHEN** C# 导入器 NextResource 产资源经 CreateProxy 注册（ReverseKeepAliveCount 先==1）后 ReleaseResource
- **THEN** 计数 MUST 1→0；重复释放幂等（ReleaseImpl 返 1001）

### Requirement: 管线必须拉取注册产出且重导入替换不静默丢
ScanSourceDirectory MUST 对每导入器 Produce()→while HasNext() 拉 NextPath/NextResource→包 shared_ptr→先 ReleaseResource(path) 再 RegisterResource(path,shared_ptr)（重导入替换，MUST NOT get_or_create 静默留旧）→SerializeAll 无条件执行（源不存在早退 MUST NOT 跳过 SerializeAll，Free 回退导入也落盘）。源目录 importer->GetSourceDirectory() 空则用传入。

#### Scenario: 重导入消费者取到新库
- **WHEN** 同一导入器第二次扫描产新资源
- **THEN** 消费者 GetResource MUST 取到新实例（非旧库）；HasResource 真

#### Scenario: 源不存在回退仍落盘
- **WHEN** 源目录不存在但 Free 导入器有回退目录产出
- **THEN** 产出 MUST 注册 + SerializeAll 落盘（不早退跳过）

### Requirement: 非 1:1 必须钉同一导入器产多资源
验收 MUST 钉同一 ResourceImporter 实例 Produce 迭代器 yield≥2 资源（HasNext 两次真）→两路径 HasResource 真（MUST NOT 用两个 1:1 导入器各产一个冒充非 1:1）。

#### Scenario: 同一导入器产两资源
- **WHEN** 一个 C# 导入器 Produce 返回的迭代器 HasNext 两次真、NextResource 各产一资源
- **THEN** 两路径 MUST 都注册（HasResource 真），且出自同一导入器实例

### Requirement: 着色器导入器必须改产出收集且字节行为保持
ShaderImporter_Vulkan 与 D3D12ShaderResourceImporter MUST 改继承 ResourceImporter，Produce 返回单条迭代器、NextResource 交出 new ShaderLibrary 填充结果，MUST NOT GetOrNewResource（限两导入器文件范围）。清集合保持各自现状（Vulkan 4 集合/D3D12 m_ShaderPrograms）。注册键/落盘路径不变。**字节判据 MUST 用导入器级测试**（跑 ScanSourceDirectory 产 ShaderLibrary 序列化对拍改前字节，两后端；Vulkan 现无字节测试 MUST 新增），MUST NOT 只依赖 ShaderLibraryBytesTest（只绑 D3D12）/ResourceBatchTest（测 NewRes）。

#### Scenario: 导入器级字节对拍两后端
- **WHEN** 跑导入器级测试（ScanSourceDirectory 产 ShaderLibrary 序列化）对拍改前字节
- **THEN** 两后端字节 MUST 一致；导入器漏填/漏清/写错 Add MUST 变红

### Requirement: 导入器双接口必须反向绑定 C# 且 check_reverse_base 含新产物
ResourceImporter 与 ResourceImporterProductions MUST 都反向（C# ResourceImporterBase/ResourceImporterProductionsBase，反向加 Base；**无 IImportOutput/ImportOutputBase 正向收集器**）。NextResource 返回反向裸指针走 CreateProxy。进宿主登记表新号 + kBindingPolicies。check_reverse_base 命中 MUST 含 ResourceImporterBase 与 ResourceImporterProductionsBase（≥5，不只现存 3）。

#### Scenario: 新反向产物生成且干净
- **WHEN** 生成后跑 check_reverse_base
- **THEN** 命中 MUST 含 ResourceImporterBase 与 ResourceImporterProductionsBase（≥5）且 0 问题；塞 void* MUST 红

### Requirement: C# 导入器验收必须钉侧别（不验加载回读）
IResource Deserialize 被 CA_BIND_SKIP（代理抛 1013），C# 资源 MUST 只验 Serialize 写+注册+GetResource 同一实例，MUST NOT 验加载回读；加载回读只对 C++ 对照资源（TResource）。TestCSharpImporter（第 21 用例两后端）验 C# 写+注册+非1:1+重导入+ C++ 对照往返。默认激活的着色器导入器重构后既有 20 用例 MUST 与基线零差异（行为保持，非不激活）。

#### Scenario: C# 写注册不验回读 + 既有零差异
- **WHEN** 两后端跑 TestCSharpImporter + 全量
- **THEN** C# 写+注册+GetResource 同一实例过（不验回读）；既有 20 与基线（当前锚 213 条对应 result）零差异