# Proposal: csharp-resource-asset-foundation

## Why

前两批（`csharp-reverse-interface-binding`、`resource-importer-refactor`）把资源机制做完：C# 能实现资源/工厂/导入器（反向基类 + 代理），注册式构造与导入管线跑通。但真实资产仍只有 ShaderLibrary——贴图/网格进不来，C# 脚本拿不到「登记/新建/取用/落盘」的入口（`ResourceManagingSystem` 不在绑定清单里，探针的登记动作全靠 C++ tester 侧驱动），场景只能画三角形。这是路线图第一梯队「资源资产层」最挡路的一段，机制现成，正好接上（用户 2026-11-23 本会话对话原话「按你建议的来，更新Roadmap并且出一个change」；记录在 `docs/ROADMAP.md` 阶段 5 立项段，随本批提交入库）。

## What Changes

- **宿主层新增可绑定门面接口 `cs_script::IResourceRegistry`**（头文件 `Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`，与 `IScriptHost` 同目录；实现在 CSharpHost）：把注册表窄面暴露给 C#——工厂注册（收反向工厂强句柄，宿主经生成登记表的 `ResolveImpl`/`ReleaseImpl` 解析后委托既有句柄入口）、资源新建/查询/释放、全量落盘、根路径与全路径查询。全部方法只用正向生成器已实现的形状（标量 / `castl::string` 入参与返回 / void），**不改 `ResourceManagingSystem` 与 GeneralResources 任何代码**。
- **C# 资产框架**（`Scripting/Managed/Assets/`）：贴图资源 `Texture2D` 与网格资源 `Mesh`（继承生成的 `ResourceBase`，持字节数据 + 标量元数据，`override byte[] Serialize()` 走自定义带版本格式）；对应工厂（继承 `ResourceFactoryBase`，跟踪所造对象供 C# 填数据）；资产库助手（新建/自读加载/替换/落盘）。
- **C# 自读加载**：读文件（C# IO）→ 解析自定义格式 → 经门面 `CreateResource` 造对象 → 填数据；同路径替换语义与导入器管线同构（先释放旧条目再新建）。**不走引擎 Deserialize 路径**（反向读仍是显式排除）。
- **渲染消费**：C# 拿资产字节走现有正向绑定上传渲染——Texture2D 经 `CreateGPUTexture`，Mesh 经**图内缓冲绑定**（`AllocBuffer` + `ScheduleData`，绘制 pass 按图内登记名解析顶点/索引缓冲，后端直建缓冲进不了 pass 绑定表，执行侧实测），两后端真实像素检查。
- 配套：`build.py` 绑定/登记清单、`kBindingPolicies`、接口编号锁（新高水位 13）、**生成器两处受控扩展：门面专属错误码 1014（作用域发射，照 1012/1013 先例）+ 字符串两段式返回带参数（零参数字节同构）**（既有产物逐字节不变）、审计表新行、tester 第 22 用例 + C# 探针、CSharpHost README 编号表、CodeGen README 错误码表、`docs/testing.md` 清单。

## Capabilities

### New Capabilities

- `csharp-resource-registry`: C# 可调的注册表窄面——门面方法集、句柄解析与失败通道、所有权委托（注册表仍是单一强所有者）、路径查询；GeneralResources 核心零改动。
- `csharp-asset-layer`: C# 资产类型与自读加载——Texture2D/Mesh 资源与工厂、自定义序列化格式（带版本、损坏显式失败）、新建/加载/替换语义、经现有正向绑定的渲染消费。

### Modified Capabilities

（无。`csharp-resource-factory` 的注册/所有权/构造规矩不变——门面委托同一组方法；`csharp-host` 的生成登记表要求不变——新接口按既有规矩加行。）

## Impact

- **代码**：`Interface/ScriptHost/header/ScriptHost/IResourceRegistry.h`（门面头，新）；CSharpHost（实现 + 登记行 + 实例注册）；`CSharpHost/CMakeLists.txt`（include 目录加 ScriptHost 头目录）；`Scripting/Managed/Assets/`（新）；`Test/GPUBackendTester/Main.cpp`（第 22 用例）；`Scripting/Managed/ResourceAssetProbe.cs`（新探针）；`build.py`（清单）；`Tools/CodeGen/interface-ids.lock`（新编号）；`Tools/CodeGen/check_bindable_audit.py`（INTERFACES 加门面一项）；`Tools/CodeGen/CodeGenMain.cpp` + `check_bindings.py` + `Tools/CodeGen/README.md`（错误码 1014 三处同步 + 形状表「会抛异常」行改准 + 字符串两段式返回带参数）；审计表（归档内单一文件：主表九行 + 汇总一行 + 合计 114）；`Tools/CodeGen/README.md`（审计输出段 14/114）；`docs/testing.md`（清单 + 四处计数 22）。
- **不动**：GeneralResources 全部、IOManager、反向生成器；正向生成器仅限两处受控扩展（错误码 1014 作用域发射 + 字符串两段式返回带参数，既有产物逐字节不变）；既有**各接口**绑定产物（逐字节不变；三份聚合登记产物只变门面登记行）、既有 21 个 tester 用例与 CoreTests。
- **依赖方向**：门面坐宿主层，复用其已持有的 `ca_bind_registry::GetInterfaceApi` 与模块管理器；不新增 GeneralResources → 绑定层的依赖。

## Non-goals

- **反向 Deserialize**：IOBatch 问不到剩余长度，`IResource.Deserialize` 的 `CA_BIND_SKIP` 保持；引擎标准加载路径对 C# 资源仍不可用，本批用 C# 自读替代。
- **C++ 贴图/网格资源类型**：资产类型完全在 C# 定义；C++ 的 `TResource` 读不了 C# 自定义格式（加载方在 C#，闭环自洽）。
- **PNG/OBJ 等源文件导入器**：导入器机制已就绪，真实解码另立批；本批数据来自 C# 自定义格式文件与程序化生成。
- **内容哈希身份 / 依赖记录 / 异步加载 / 热重载 / 驱逐**：路线图第三梯队与后续方向。
- **注册表 API 全量绑定**：`GetOrLoadResource` 模板、共享指针入参的 `RegisterResource`、裸指针返回的 `GetFactory` 等不进窄面（形状不收或语义越界），C++ 侧照旧可用。