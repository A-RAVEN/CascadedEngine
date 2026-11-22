# Design: resource-importer-refactor（v3，经五面对抗+apply 停工修订）

> 复用 csharp-reverse-interface-binding 反向能力与 CreateResource 所有权模式。v3 修（apply 停工发现 v2 形状仍不可绑）：**改拉取式接口**（ResourceImporter.Produce→ResourceImporterProductions 迭代器 HasNext/NextPath/NextResource，全落现有四形状，不扩生成器；删 push 式 IImportOutput）；新 C# 资源走 **CreateProxy**（非 ResolveImpl）；C# 基类双反向加 Base（命名问题消失）。v2 已修：所有权复用 CreateResource、重导入替换、早退修、退役判据口径、C# 往返侧别、基线当前锚。

## D1 退役旧 Base（判据口径修正）

删 ResourceImporterBase、ResourceImporter<TRes> 模板、AddImporter(Base*)、后缀扫描循环、m_Importers/m_PostfixToImporterIndex/m_ReservedSpace/m_ImportingResources。ResourceImporterFree→ResourceImporter（唯一接口）。
**退役判据口径**：零命中范围限定 **C++ 源码**（GeneralResources/header+private、VulkanRenderBackendNew/D3D12RenderBackend 的 ShaderLibrary 目录），MUST NOT 用"全仓"（工件正文与生成 C# 基类同名会恒红）。模板判据用折行感知（`class ResourceImporter` 后跟 `: public ResourceImporterBase` 或 template 行），不用 `ResourceImporter<`（折行 0 命中不可失败）。C# 反向基类名 ResourceImporterBase 由生成器产出（C++ Base 退役后无 C++ 同名冲突），落在构建目录，不在退役判据范围。

## D2 导入器接口用拉取式（全落现有形状，不扩生成器）

**v2 仍错**（执行者实证）：反向形参只支持**标量**（string 形参被拒，CodeGenMain.cpp:7819-7838）；正向无"形参是反向接口裸指针"映射（IResource*→IResourceC 不存在）。故 push 式 Import(string,string,IImportOutput*)/Add(string,IResource*) 都生成不出。**v3 改拉取式**，全部落在现有四种形状（标量参/标量返/string 返/返回反向接口裸指针/写路径）：
```cpp
CA_BIND_REVERSE() class ResourceImporterProductions { public: virtual ~...{}
  virtual bool HasNext() = 0;                  // 标量返
  virtual castl::string NextPath() = 0;        // string 返、无参
  virtual IResource* NextResource() = 0; };    // 返回反向接口裸指针（现有支持）
CA_BIND_REVERSE() class ResourceImporter { public: virtual ~...{}
  virtual castl::string GetTags() const = 0;
  virtual castl::string GetSourceDirectory() const = 0;  // 空=用 ScanSourceDirectory 传入
  virtual castl::string GetDestDirectory() const = 0;
  virtual ResourceImporterProductions* Produce() = 0; }; // 返回反向接口裸指针
```
管线"拉一条→包 shared_ptr→注册"循环（D4）。**无 IImportOutput**（push 收集器删除），C# 基类为 ResourceImporterBase/ResourceImporterProductionsBase（反向加 Base，命名问题消失）。着色器导入器 Produce 返回单元素迭代器（一个 ShaderLibrary）；C# 示例返回两条（非1:1）。ImportResource→Produce 改名明确。

## D3 所有权复用 CreateResource + CreateProxy（不发明借用+release）

管线拉取 NextResource() 得 IResource*：C++ 导入器返回原生对象；C# 导入器返回 C# 资源——**返回反向接口裸指针的现有投影对 new 对象走 CreateProxy**（与 IResourceFactory::OnCreate 同构，生成物实证；v2 误写 ResolveImpl，新对象尚无代理/借用句柄）。管线包 `shared_ptr<IResource>(ptr,[](IResource*p){delete p;})`→RegisterResource（与 CreateResource 同构，注册表单一强所有者；C# 代理析构 releaseUser→GCHandle.Free，ReverseKeepAliveCount 在 CreateProxy 后==1）。**不违反借用句柄作 shared_ptr 入参 MUST 失败**（管线是 C++ 代码直接包 shared_ptr，同 CreateResource）。

## D4 管线注册 + 重导入替换 + 早退修

ScanSourceDirectory：对每个 ResourceImporter：productions=importer->Produce()；while productions->HasNext()：path=NextPath()、ptr=NextResource()→包 shared_ptr(ptr,delete)→**重导入替换**先 ReleaseResource(path) 再 RegisterResource(path,shared_ptr)（不 get_or_create 静默留旧）→**SerializeAll 无条件执行**（移出"源不存在早退"分支，Free 回退也落盘）。源/目标目录：importer->GetSourceDirectory() 空则用 ScanSourceDirectory 传入。导入器不碰注册表（判据：导入器代码 grep 五写注册表入口 RegisterResource/ReleaseResource/CreateResource/GetOrLoadResource/GetOrNewResource 全 0）。

## D5 着色器导入器重构

ShaderImporter_Vulkan（类名 ShaderImporter_Vulkan）/D3D12（类名 **D3D12ShaderResourceImporter**，工件勿写 _D12）改继承 ResourceImporter；**Produce 返回单条迭代器（SingleResourceProductions），NextResource 交出 new ShaderLibrary 填充结果**。清集合行为保持各自现状（Vulkan 清 4 集合、D3D12 清 m_ShaderPrograms，v1"D3D12 同"错）。重导入经 D4 替换语义复用消费者键。注册键/落盘路径不变（VulkanShaderLibrary.shLib/D3D12ShaderLibrary.shLib）。

## D6 反向绑定（双反向接口，无正向收集器）

ResourceImporter 与 ResourceImporterProductions **都反向**（C# ResourceImporterBase 抽象 GetTags/GetSourceDirectory/GetDestDirectory/Produce()→ResourceImporterProductionsBase；ResourceImporterProductionsBase 抽象 HasNext/NextPath/NextResource()→ResourceBase）。**无 IImportOutput/ImportOutputBase**（push 删除，正向命名问题消失）。NextResource 返回反向接口裸指针→现有投影 CreateProxy。进 CODEGEN_BINDINGS/REGISTRIES + --update-ids 新号 + CSharpHost kBindingPolicies。check_reverse_base 命中 MUST 含 ResourceImporterBase 与 ResourceImporterProductionsBase（不只现存 3 个）。

## D7 MODIFIED csharp-resource-factory

主规范该能力 MUST"GetOrNewResource 模板保留（ShaderImporter 不动）"与本批冲突→**MODIFIED**：模板保留（其它调用方不动）但 ShaderImporter_Vulkan/D3D12ShaderResourceImporter 改走导入器产出收集（不再 GetOrNewResource）。proposal 声明 MODIFIED（v1"无 MODIFIED"错）。

## D8 判据（可失败、钉侧别）

- 退役：C++ 源码范围零命中（D1 口径）。
- check_reverse_base：命中含 ResourceImporterBase 与 ResourceImporterProductionsBase（≥5）+ 塞 void* 红。
- 形状可绑：ResourceImporter/ResourceImporterProductions 全标量参/string 返/返回反向裸指针，生成 rejected=0（拉取式落现有四形状）。
- 着色器字节：ShaderLibraryBytesTest 只绑 D3D12；**Vulkan 无字节判据→新增导入器级测试**（跑 ScanSourceDirectory 产 ShaderLibrary 序列化对拍改前字节，两后端），不依赖只测内存构造的 ShaderLibraryBytesTest/ResourceBatchTest（后者测 NewRes 不测 ShaderLibrary）。
- 管线注册/非1:1：钉**同一导入器实例** Produce 迭代器 yield 2 资源（HasNext 两次真）→HasResource 两路径真 + 导入器写注册表入口 grep 0；重导入替换（第二次扫描消费者取到新库，非旧库）。
- C# 往返侧别：IResource Deserialize 被 CA_BIND_SKIP（代理抛 1013），**C# 资源只验 Serialize 写+注册+GetResource 同一实例，MUST NOT 验加载回读**；加载回读只对 C++ 对照资源（TResource 走 Deserialize）。
- 早退：源不存在时 Free 回退导入仍注册+SerializeAll 落盘（判据）。
- 基线：当前锚=上一 change 新锚.txt（213 条，非 2026-11-11 的 203）；换锚保留旧锚对账，既有正向产物逐字节不变。
- 默认激活前提：着色器导入器默认激活（backend init AddImporter + tester ScanSourceDirectory），"既有 20 零差异"指重构后行为保持（字节/键不变），非"不激活"。

## D9 线程/文档

导入单线程初始化；反向注册线程契约 1005。文档 README×2 + testing.md（新用例 TestCSharpImporter 第 21）。