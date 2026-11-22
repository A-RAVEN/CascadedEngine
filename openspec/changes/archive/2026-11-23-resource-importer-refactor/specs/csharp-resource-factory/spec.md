# csharp-resource-factory 增量

## MODIFIED Requirements

### Requirement: 工厂注册表必须按类型多态构造且经句柄注册单一所有
工厂注册表 MUST 支持 RegisterFactory(typeName, **handle, IResourceFactory* borrowed, int32_t (*release)(uint64_t)**)（**保留句柄不丢** + 持借用指针；release 返 int32_t：0 成功/1001 已失效，与 C# Dispose 同口径保可观测；跨模块 MUST NOT 用 shared_ptr 重建——句柄表是头里 inline+函数级 static，EXE/DLL 各一份）/ GetFactory(typeName)→借用裸指针（生命周期随注册表）。**工厂代理由 CreateImpl 创建为强句柄（kind 1），单一强所有者是创建模块（CSharpHost）的句柄表**，注册表持借用指针不另持强引用，注销/替换调 release(handle)（→创建模块掉强引用→代理析构→releaseUser）。**资源代理不同**：由 CreateProxy 创建为借用句柄（kind 4），RegisterResource(path, shared_ptr<IResource>) 资源注册表是单一强所有者（MUST NOT 用 handle+borrowed+release，kind 4 的 ReleaseImpl 只清表项不 delete 会泄漏），代理析构退役借用句柄。反向 Api_v2 MUST 含 ResolveImpl 槽（创建模块里按句柄解析借用指针）。tester MUST 经 GetInterfaceApiForTest(9/10) 取 CSharpHost 的表（resolve/release/keepalive 走表里函数指针，不调 EXE 自己那份）。构造 MUST 走 GetFactory(type)→Create()→注册（运行时多态），MUST NOT 要求编译期模板。既有 GetOrNewResource<TRes> 模板 MUST 保留（**其它调用方不动**）；**ShaderImporter_Vulkan/D3D12ShaderResourceImporter 由 resource-importer-refactor 改走导入器产出收集（IImportOutput.Add），不再 GetOrNewResource**（导入器纯转换、管线注册分离）。

#### Scenario: 按类型名运行时构造
- **WHEN** 注册工厂后按类型名（运行时字符串）构造
- **THEN** MUST 查到工厂调 Create() 造资源（不需编译期模板）；未注册类型 MUST 明确报错不静默返回空

#### Scenario: C++/C# 工厂对照钉死期望
- **WHEN** 对 C++ 原生工厂与 C# 工厂跑对照
- **THEN** MUST 按写死期望表逐项核：GetResourceType 各自 ID（写死）、Create 返回非空、Serialize 字节（各自写死期望）、失败都抛异常；MUST NOT 用散文"行为一致"（四处不同仍绿即缺陷）

#### Scenario: 着色器导入器改走产出收集后模板仍保留
- **WHEN** resource-importer-refactor 完成后 grep GetOrNewResource
- **THEN** 模板定义与其它调用方 MUST 仍在；ShaderImporter_Vulkan/D3D12ShaderResourceImporter 两文件 GetOrNewResource 命中 MUST 0（改 output.Add）