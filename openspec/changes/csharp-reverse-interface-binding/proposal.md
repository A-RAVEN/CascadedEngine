# Proposal: csharp-reverse-interface-binding

## Why

资源系统是通往可用引擎的第一缺口（ROADMAP 阶段 5 第一梯队）：框架骨架在（注册表 + CodeGen 序列化 + 导入器 + I/O 错误通道），但只喂了着色器一种资产，且**没有暴露给 C#**——C# 不能自定义资源类型与工厂。

用户裁定的设计（2026-11-22 讨论收敛）：**对称工厂架构**——工厂接口与资源接口都给 **C++ 基类 + C# 基类**，两种语言的类型都能有自己的工厂类与资源类，构造析构各类型自己管，资源系统按类型注册工厂、运行时多态构造。这取代编译期模板 `GetOrNewResource<TRes>`（C# 调不到①），是成熟引擎的注册式工厂做法（Unreal UClass 构造器 / Godot ClassDB creator / Unity 导入器）。

**地基缺口（调查取证）**：现有绑定**严格单向**——C# 调 C++（正向包装 + 句柄）或 C++ 调一个 C# 静态桥（第 4 类回调）。生成器 `bindemit::run` 现有投影路径（快速镜像/序列化块/句柄/回调/值类/两段式 Len-Copy 等）**没有一条**生成"C++ 代理转发虚方法到 C# 实现"。对称工厂的 C# 侧（C# 工厂/资源实现 C++ 接口）正需要这个**反向接口投影能力**。

## What Changes

- **反向接口投影能力（地基）**：对标注的反向接口生成 ① C++ 代理 `<I>Proxy : public <I>`（每虚方法调函数指针槽）② C# 公开抽象基类（去 I 前缀加 Base）③ 注册 glue `<I>_CreateImpl(user, 每虚方法投影 fnptr..., releaseUser) → 强句柄` + `<I>_ReleaseImpl`。第五类形状"函数表 + 不透明 user"（一虚方法可多槽）。所有权：代理析构调 releaseUser → 托管桥 GCHandle.Free（Free 唯一在托管侧）；寿命随持有方。复用回调注册表/GCHandle/句柄表/错误通道；新增 ReverseKeepAliveCount。
- **IResourceFactory 对称可绑**：C++ 基类 `IResourceFactory`（GetResourceType / Create()→IResource* / 可选导入后缀）+ C# 反向基类 `ResourceFactoryBase`。C++ 类型原生继承 IResourceFactory 写工厂；C# 类型继承 ResourceFactoryBase（反向代理包装）。**Create() 无 ResourceManagingSystem 参数**（绕开不可绑协作方），只构造资源对象，注册由系统负责。
- **IResource 对称可绑（写路径）**：C++ 基类 IResource + C# 反向基类 `ResourceBase`（abstract byte[] Serialize()）。Serialize 走两段式 Len/Copy（len 失败哨兵 UINT64_MAX，copy 返实际写入数），代理 Write+SubmitAndWait 与 TResource 同构，批不跨界。Deserialize 本批明确拒（IOBatch 无剩余长度查询）。
- **注册式工厂构造**：新增工厂注册表（按类型名注册 IResourceFactory*，C++ 原生或 C# 代理）；ResourceManagingSystem 增 `RegisterResource(path, shared_ptr<IResource>)` 接收工厂造的对象（**ResourceManagingSystem 会改**，不再要求零改动）；构造路径 = 查工厂→Create()→shared_ptr→注册→Deserialize，取代模板 new。
- **规格修订**：csharp-bindable-surface（四类→五类 + "不得解引用"限定原生层，含 RENAMED）、csharp-bindings（反向产物 + check_reverse_base）MODIFIED；新 csharp-reverse-interface / csharp-resource-factory。

## Capabilities

### New Capabilities
- `csharp-reverse-interface`: 托管实现原生接口投影契约——第五类形状、代理/基类/glue/releaseUser 生成、所有权与寿命、注册线程 + 失败哨兵、错误通道、审计"谁实现"列 + check_reverse_base。
- `csharp-resource-factory`: 对称工厂与资源——IResourceFactory（C++/C# 双基类）+ IResource（C++/C# 双基类，写路径）+ 工厂注册表多态构造 + ResourceManagingSystem.RegisterResource + 失败不空写。

### Modified Capabilities
- `csharp-bindable-surface`: C ABI 面四类→五类（增函数表 + user）；"用户令牌不得解引用"限定原生生成代码（托管桥 GCHandle 解回是既有语义）。含 RENAMED。
- `csharp-bindings`: 生成器增反向产物（代理/基类/glue/releaseUser）；新增 check_reverse_base 公开基类干净形状检查。

## Impact

- **绑定生成器**：Tools/CodeGen/CodeGenMain.cpp（反向分支 + CA_BIND_SKIP + 代理/基类/glue/releaseUser + 反向类别标记）；夹具头。
- **检查管线反向集成**：check_bindings.py（check_surface/check_alignment/check_stubs/check_shared_value_types 反向感知替代判据 + 新增 check_reverse_base + --only 单检查 + 命中数）；check_bindable_audit.py（INTERFACES 增反向 + 谁实现列）；build.py 期望产物闸门认反向两件套。
- **宿主登记通道**：build.py（**CODEGEN_BINDINGS 增反向头**——否则不产产物 + 登记分片报错；CODEGEN_REGISTRIES 宿主段增反向）；interface-ids.lock（--update-ids 发 9/10）；**CSharpHost/private/CSharpHost.cpp kBindingPolicies 增反向行**（否则 GetInterfaceApi 返 0）；BindingRegistry.Bindings.h/.cpp + EngineApi.Bindings.cs（新号路由，合法变化）；Tools/CodeGen/check_binding_baseline.py 新锚。
- **资源系统**：GeneralResources/header/CAResource/IResource.h（加反向标注 + CA_BIND_SKIP(Deserialize)，必然变；TResource 字节逻辑不变）；新增 IResourceFactory.h（C++ 基类）；**ResourceManagingSystem.h（增 RegisterResource 声明）+ GeneralResources/private/ResourceImportingSystem.cpp（ResourceManagingSystem_Impl 增 RegisterResource 实现；无 ResourceManagingSystem.cpp）**；新增工厂注册表（句柄注册单一所有）。
- **C# 框架**：生成反向基类 ResourceFactoryBase/ResourceBase 落**构建目录 codegen/bindings**（经 CascadedBindingDir 编入，**不写回 Scripting/Managed/**——csproj 明文不放生成绑定）；Scripting/Managed/ 仅手写包装（如需）。
- **规格**：csharp-bindable-surface（含 RENAMED）/csharp-bindings MODIFIED；新 csharp-reverse-interface/csharp-resource-factory。
- **测试**：CoreTests（反向 C++ 结构夹具 + ResourceBatchTest）+ GPUBackendTester 新用例 TestCSharpResourceFactory（C# 工厂/资源行为 + 写路径往返钉死期望 + Create 失败异常不注册空 HasResource + Serialize 失败错误上报 + C++ 原生工厂对照钉死期望表 + 寿命两档）+ Main.cpp 注册；审计表编辑归档那份（全仓唯一）。
- **对外文档**：Tools/CodeGen/README.md + CSharpHost/README.md。
- **基线口径**：既有 8 接口正向产物逐字节不变；登记产物因新号合法变化；换锚差异逐文件指认。

## Non-Goals（明确不做，各自另立批）

- **反向 Deserialize（读路径）**：IOBatch 无剩余长度查询（IResource.h 逐字"问不到剩余长度"），需先加长度查询（改接口 + 两实现 + 两测试桩）。本批读回仍走 TResource，不碰 ResourceBatchTest 红线。
- **工作线程导入 / marshaling**：Host 无队列/Dispatcher；本批沿用注册线程契约，异步加载 + 主线程泵是独立批（资源系统内存管理迭代方向）。
- **资产驻留驱逐 / 流式 / 内容哈希依赖图**：资源系统内存/身份层迭代，另立批。
- **完整 ECS / 场景纳入资源框架**：阶段 5 后续。