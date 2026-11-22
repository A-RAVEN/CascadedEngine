# Proposal: resource-importer-refactor

## Why

资源系统已有"自定义资源类型与工厂"（csharp-reverse-interface-binding），但**导入器**还是旧形态，且 C# 碰不到：

- **旧 Base 是死抽象**：`ResourceImporterBase`（源后缀→单资源 1:1）零具体子类；真实导入器（ShaderImporter_Vulkan/_D12）全用 `ResourceImporterFree`。Base 及其模板 `ResourceImporter<TRes>`、`AddImporter(Base*)`、后缀扫描循环是遗留冗余。
- **Free 是演进方向但耦合注册**：Free 给导入器整个源/输出目录、自主搜索、不要求 1:1（一个 fbx 出网格+材质+贴图、着色器拉 include）——方向对。但 `ImportResource` 收 `ResourceManagingSystem*` 并内部调 `GetOrNewResource<TRes>` 模板（C# 调不到），导入器既"解析源"又"创建+注册"，职责混合，C# 绑不动、难单测。

用户裁定（2026-11-22）：把导入器理顺成"**目录级纯转换产出一组资源 + 管线注册分离**"，退役旧 Base，再反向绑定到 C#。import/load 分离保留，进一步把"导入"和"注册"分开。

## What Changes

- **退役旧 Base**：删 `ResourceImporterBase`、`ResourceImporter<TRes>` 模板、`AddImporter(ResourceImporterBase*)`、后缀扫描循环与 `m_Importers/m_PostfixToImporterIndex/m_ReservedSpace/m_ImportingResources`。`ResourceImporterFree` 演进为唯一导入器接口，改名 `ResourceImporter`。
- **解耦导入/注册（拉取式，全落现有绑定形状，不扩生成器）**：`ResourceImporter`（GetTags/GetSourceDirectory/GetDestDirectory string 返 + `Produce()→ResourceImporterProductions*`）+ `ResourceImporterProductions` 迭代器（HasNext/NextPath/NextResource）。导入器只产资源不注册；管线 `ScanSourceDirectory` 拉取→包 shared_ptr（同 CreateResource）→重导入替换注册→`SerializeAll` 无条件。无 push 式 IImportOutput（v3，因反向形参只支持标量、正向无反向裸指针形参映射）。
- **着色器导入器重构**：ShaderImporter_Vulkan/D3D12ShaderResourceImporter 改 Produce 返回单条迭代器、NextResource 交出 new ShaderLibrary，不再 `GetOrNewResource`（模板死结解除）。
- **反向绑定导入器到 C#**：`ResourceImporter` + `ResourceImporterProductions` 双反向（C# ResourceImporterBase/ResourceImporterProductionsBase）；NextResource 返回反向裸指针走 CreateProxy。复用 csharp-reverse-interface-binding 反向能力/跨模块所有权（CreateResource 同构）。C# 自定义导入器产 C# 资源交管线拉取注册。
- **规格**：新能力 `resource-importer`（导入器接口/产出收集/管线注册/退役 Base/反向绑定）。

## Capabilities

### New Capabilities
- `resource-importer`: 目录级导入器（非 1:1）+ 产出收集器 + 管线注册分离 + 旧 Base 退役 + 导入器反向绑定 C#。

### Modified Capabilities
- `csharp-resource-factory`: 其 MUST"GetOrNewResource 模板保留（ShaderImporter 不动）"与本批冲突→MODIFIED 为"模板保留（其它调用方不动）；ShaderImporter_Vulkan/D3D12ShaderResourceImporter 改走导入器产出收集"。其余复用 csharp-reverse-interface 能力不改条文。

## Impact

- **资源接口**：GeneralResources/header/CAResource/ResourceImporter.h（删 Base/模板，Free→ResourceImporter + ResourceImporterProductions 双反向拉取式）；ResourceImportingSystem.h（删 AddImporter(Base*)，AddImporter(ResourceImporter*)）；ResourceImportingSystem.cpp（Impl 删后缀循环，ScanSourceDirectory 改拉取→注册）。
- **着色器导入器**：VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h/.cpp、D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.h/.cpp（类名 D3D12ShaderResourceImporter；改继承 ResourceImporter + Produce 单条迭代器）。
- **绑定生成器/宿主**：ResourceImporter + ResourceImporterProductions 双反向进 CODEGEN_BINDINGS/REGISTRIES + interface-ids.lock 新号 11/12 + CSharpHost kBindingPolicies（复用上一 change 管线）。**接口全落现有四形状（标量参/string 返/返回反向裸指针），不扩 CodeGen 形状**；所有权复用 CreateResource（管线包 shared_ptr(ptr,delete)），不发明借用+release thunk。
- **行为修正**：管线重导入替换（先 ReleaseResource 再 RegisterResource，不 get_or_create 静默丢）；ScanSourceDirectory 早退不跳过 SerializeAll（Free 回退也落盘）。
- **C# 框架**：ResourceImporterBase/ResourceImporterProductionsBase 双反向基类（构建目录）+ C# 导入器示例（Produce 迭代器）。
- **测试**：CoreTests（导入器解耦/退役断言）+ GPUBackendTester 新用例（C# 导入器产资源→管线注册→加载往返）。
- **对外文档**：Tools/CodeGen/README.md + CSharpHost/README.md + docs/testing.md。
- **基线**：既有正向产物逐字节不变（反向码 scoped 沿用）；登记产物新号合法变化；换锚。

## Non-Goals

- **异步导入/工作线程**：导入仍单线程初始化（ShaderLibrary 非线程安全，现有契约），异步另立批。
- **真实资产 breadth（贴图/网格导入器）**：本批给导入器机制 + C# 示例，不新增贴图/网格导入器。
- **增量导入/依赖追踪/热重载**：资源身份层另立批。