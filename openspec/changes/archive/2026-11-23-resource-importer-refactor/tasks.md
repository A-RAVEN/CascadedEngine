# Tasks: resource-importer-refactor（v2）

> 分工：构建/CoreTests/GPUBackendTester/截图视觉验收主循环代跑；执行者代码/测试/文档编辑+只读检查，需读数请求主循环。红绿纪律缺陷态先存档。复用上一 change 反向能力 + CreateResource 所有权。**接口用可绑形状（string+裸指针，不扩生成器）；所有权复用 CreateResource；重导入替换；早退修；退役判据限 C++ 源码；C# 不验回读；基线当前锚 213。**

## 0. 开工清点
- [x] 0.1 清点 evidence/0.1：Base/Free/模板现状、ScanSourceDirectory 双循环+早退分支、ShaderImporter_Vulkan/D3D12ShaderResourceImporter 的 GetOrNewResource 与清集合差异、上一 change CreateResource/ResolveImpl/kBindingPolicies、当前基线锚（213 新锚.txt）+既有 20。
- [x] 0.2 封存当前基线锚（主循环实测）：上一 change 新锚 213 整份封存 evidence/旧锚封存件.txt。evidence/0.2-5.1-5.2-封存回归换锚.txt。

## 1. 退役 Base + 接口可绑形状
- [x] 1.1 删 Base/模板/AddImporter(Base*)/后缀循环/四成员；Free→ResourceImporter 拉取式（GetTags/GetSourceDirectory/GetDestDirectory string 返 + Produce()→ResourceImporterProductions*）；ResourceImporterProductions（HasNext bool/NextPath string/NextResource IResource*）。无 IImportOutput。验收：C++ 源码范围退役零命中（折行感知）；双接口全现有形状生成 rejected=0。
- [x] 1.2 ResourceImportingSystem.h AddImporter(ResourceImporter*)。验收：只收 ResourceImporter*。

## 2. 管线注册 + 重导入替换 + 早退修
- [x] 2.1 ScanSourceDirectory 改：对每导入器 Produce()→while HasNext 拉 NextPath/NextResource→包 shared_ptr(ptr,delete)（同 CreateResource）→先 ReleaseResource 再 RegisterResource（替换）→SerializeAll 无条件（移出早退）；源目录 GetSourceDirectory 空用传入。验收：导入器五写注册表入口 grep 0；重导入消费者取新库；源不存在回退仍落盘。
- [x] 2.2 着色器导入器重构：ShaderImporter_Vulkan/D3D12ShaderResourceImporter 改继承 ResourceImporter，Produce 返回单元素迭代器（new ShaderLibrary 填充→NextResource 交出），删 GetOrNewResource（限两文件）；清集合保持各自现状。验收：两文件 GetOrNewResource 0；导入器级字节对拍两后端一致（新增 Vulkan 字节判据）。

## 3. 反向绑定
- [x] 3.1 ResourceImporter + ResourceImporterProductions 双反向（C# ResourceImporterBase/ResourceImporterProductionsBase）；NextResource 返回反向裸指针走 CreateProxy（新对象）；管线包 shared_ptr(ptr,delete)。无 IImportOutput。验收：check_reverse_base 命中含两新基类≥5 且 0 问题；塞 void* 红。
- [x] 3.2 宿主路由 CODEGEN_BINDINGS/REGISTRIES+--update-ids 新号+kBindingPolicies。验收：C# GetInterfaceApi 取表。
- [x] 3.3 所有权：ReleaseResource 后 ReverseKeepAliveCount 1→0、重复释放幂等 1001。验收：读数。

## 4. C# 导入器示例 + 用例
- [x] 4.1 C# 示例导入器（Produce 返回迭代器 yield 2 资源，同一实例，非 1:1）+ CoreTests 退役/解耦断言。验收：同一导入器两路径 HasResource 真。
- [x] 4.2 tester TestCSharpImporter（第 21，两后端）：C# 写+注册+GetResource 同一实例（不验回读）+非1:1+重导入+C++ 对照往返。验收：两后端全断言过；缺陷态红。

## 5. 回归 + 换锚
- [x] 5.1 全量回归（主循环实测，红态前绿构建）：构建绿 CoreTests 26 两后端 21/21；新 21 去 TestCSharpImporter=20 与基线 20 逐条 name+status 差异 0。evidence/5.1-全量-*.json + 0.2-5.1-5.2 文件。
- [x] 5.2 换锚（主循环实测，红态前绿产物）：旧锚 213 对拍缺失0/不一致3（宿主登记新号11/12）/多余5（导入器反向产物）；既有正向逐字节不变；新锚 218 自洽0/0/0。evidence/新锚.txt + 0.2-5.1-5.2 文件。

## 6. 文档
- [x] 6.1 README×2+testing.md（导入器双反向/拉取式迭代器/新号/C# 示例/第21用例；README 四形状清单不扩）。验收：两检查绿。

## 修订记录
（逐条改前/改后/依据/谁裁定。）

### 2026-11-22 round1 五面对抗验证处置（v1→v2 大改）
1. 退役零命中口径（验收001/工件001/无中01/真实001）：改前"全仓零命中"（工件自身+生成C#同名恒红、模板折行 ResourceImporter< 0命中不可失败）→改后限 C++ 源码范围+折行感知；C# 反向基类 ResourceImporterBase 生成落构建目录不在退役范围。
2. 主规范 MUST 冲突（工件002）：改前 proposal"无 MODIFIED"但改 ShaderImporter 违反 csharp-resource-factory"模板保留/ShaderImporter 不动"→改后补 MODIFIED 增量（模板保留其它调用方、ShaderImporter 改产出收集）。
3. 接口形状不可绑（工件004/无中03/影响001）：改前 Import(cafs::path,cafs::path,IImportOutput&)+Add(shared_ptr)（生成器拒）→改后 Import(string,string,IImportOutput*)+Add(string,IResource*)（现有支持，不扩生成器）。
4. 所有权发明 thunk（工件003/无中02/影响002）：改前"ResolveImpl借用+释放回调作deleter"（不存在、违反借用句柄作shared_ptr入参MUST失败）→改后复用 CreateResource（sink 包 shared_ptr(ptr,delete)，注册表单一强所有者，C#代理析构releaseUser）。
5. 重导入静默丢（影响003）：改前 RegisterResource get_or_create 留旧（着色器重扫失效）→改后管线先 ReleaseResource 再 RegisterResource 替换+判据。
6. 早退跳 SerializeAll（影响004）：改前源不存在早退跳过落盘（Free回退不注册不落盘）→改后 SerializeAll 无条件+判据。
7. check_reverse_base 恒绿（验收002）：改前 hits≥1 现存3填满→改后命中含 ResourceImporterBase≥4。
8. 着色器字节红线错配（验收003/真实002）：改前 ShaderLibraryBytesTest(只D3D12)/ResourceBatchTest(测NewRes)→改后新增导入器级字节对拍两后端（Vulkan 无字节测试补）。
9. HasResource/非1:1 判不出（验收004）：改后钉同一导入器产2资源+导入器五写注册表入口grep0+重导入取新库。
10. C# 回读不可能（真实003）：改前验"Serialize/加载往返"（Deserialize CA_BIND_SKIP 抛1013）→改后 C# 只验写+注册+GetResource，回读只 C++ 对照。
11. 默认激活前提假（无中04）：改前"默认不激活"→改后着色器默认激活，"既有20零差异"指重构后行为保持。
12. 基线当前锚（影响非claim）：改前 tasks 未点名→改后当前锚=上一change新锚213（非2026-11-11的203）。
13. 命名/观察（真实附）：D12 类名 D3D12ShaderResourceImporter（勿写_D12）；D12 清1集合（v1"D3D12同"错）；ImportResource→Import 改名明确。

### 2026-11-22 round2 apply 停工修订（v2→v3 拉取式，主循环裁定 B）
14. **v2 形状仍不可绑**（执行者停工实证）：反向形参只支持标量（string 形参被拒 CodeGenMain.cpp:7819-7838）；正向无"形参是反向接口裸指针"映射（IResource*→IResourceC 不存在 :4029-4046）。v2 的 Import(string,string,IImportOutput*)/Add(string,IResource*) 生成不出。裁定 **B 不扩生成器、改拉取式**：ResourceImporter.Produce()→ResourceImporterProductions 迭代器（HasNext 标量返/NextPath string 返/NextResource 返回反向裸指针），全落现有四形状。删 push 式 IImportOutput。design D2/D3/D4/D6/D8、spec R2/R3/R4/R5/R7、tasks 1.1/2.1/2.2/3.1/4.1/6.1 同步。
15. **CreateProxy 非 ResolveImpl**（执行者核）：新 C# 资源对象尚无代理/借用句柄，NextResource 返回反向裸指针的现有投影走 CreateProxy（与 IResourceFactory::OnCreate 同构）；ReverseKeepAliveCount==1 在 CreateProxy 后。spec R3 改。
16. **正向命名不成立**（执行者核）：正向产物表面类沿用 C++ 名（不加 Base），ImportOutputBase 名不成立；v3 删 IImportOutput 后双接口皆反向（加 Base），命名问题消失。
17. **1.1 验收恒红随形状改**：rejected=0 判据改拉取式后可达。